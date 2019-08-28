/*
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * */
#include <pthread.h>
#include <assert.h>
#include <cpoll-ng/cpoll.H>
#include <cppsp-ng/cppsp.H>
#include <cppsp-ng/websocket.H>
#include <cppsp-ng/static_handler.H>
#include "hw.H"
#include "hw_data_format.H"
#include "mipmap_reader.H"
#include "protocol.H"
using namespace CP;
using namespace cppsp;

/*
 * Websocket server for websdr; all communication with hardware is done through
 * the API defined in hw.H.
 * */

Worker worker;
Socket srvsock;

// per-request state machine
class MyHandler {
public:
	ConnectionHandler& ch;
	WebSocketParser wsp;
	FrameWriter wsw;
	Timer timer;

	MyHandler(ConnectionHandler& ch): ch(ch) {}

	void handle100() {
		ch.response.write("XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
		finish(true);
	}

	// handler for /points
	void handlePoints() {
		if(ws_iswebsocket(ch.request)) {
			// switch to websocket mode
			ws_init(ch, [this](int r) {
				if(r <= 0) {
					abort();
					return;
				}
				wsStart();
			});
		} else {
			ch.response.status = "400 Bad Request";
			ch.response.write("only websocket requests supported on this endpoint");
			finish(true);
		}
	}
	static constexpr int displays = 2;
	mipmapReader<4, 2> mReader;

	// the client x view extents
	array<mipmapReaderView, displays> mView;

	// the client y view extents
	array<pair<double,double>, displays> yRange;

	// if the client waveform display is paused, the chunk is pinned in memory
	hw_streamViewChunk reservedChunk;

	void wsStart() {
		mipmapReaderView mViewReq = {0, 131072, 1024};
		mReader.length = hw_streamViews.at(0).length;
		mReader.init(hw_mipmapSteps);
		for(int d=0; d<displays; d++)
			mReader.requestView(mViewReq, mView.at(d));

		yRange.at(0) = {-32768., 32768.};
		yRange.at(1) = {-20., 50.};

		wsw.streamWriteAll = [this](const void* buf, int len, const Callback& cb) {
			ch.socket.writeAll(buf, len, cb);
		};
		timer.setInterval(150);
		timer.setCallback([this](int r) { timerCB(r); });
		worker.epoll.add(timer);
		wsRead();
		wsSendSpectrumParams();
	}
	void wsSendSpectrumParams() {
		auto& sv = hw_streamViews[0];
		string s = "spectrumParams 1 ";
		s += to_string(sv.centerFreqHz);
		s += ' ';
		s += to_string(sv.bandwidthHz);
		wsw.append(s, 1);
		wsw.flush();
	}
	void wsRead() {
		auto buf = wsp.beginAddData();
		ch.socket.read(get<0>(buf), get<1>(buf), [this](int r) {
			if(r <= 0) {
				wsEnd();
				return;
			}
			wsp.endAddData(r);
			WebSocketParser::WSFrame f;
			while(wsp.process(f)) {
				handleFrame(f);
			}
			wsRead();
		});
	}
	void timerCB(int i) {
		// skip a frame if there are still data waiting to be sent
		if(wsw.writing) {
			return;
		}
		auto& sv = hw_streamViews[0];
		volatile void* original;
		hw_streamViewChunk chunk;
		if(reservedChunk) {
			chunk = reservedChunk;
		} else {
			auto chunks = sv.snapshot();
			chunk = chunks.back();
		}
		for(int d=0; d<displays; d++) {
			bool isSpectrum = (d == 1);
			mReader.mipmap = isSpectrum ? chunk.spectrumMipmap : chunk.mipmap;
			original = isSpectrum ? (volatile void*) chunk.spectrum : (volatile void*) chunk.original;
			
			auto& mView = this->mView[d];
			bool useOriginal = (mView.compression() == 1);
			double yLower = get<0>(yRange.at(d));
			double yUpper = get<1>(yRange.at(d));
			int sampleGroups = mView.resolution;
			int channels = isSpectrum ? 1 : 2;
			int wordBytes = 1;
			int bytes = useOriginal ? (sampleGroups*channels*wordBytes) : (sampleGroups*channels*wordBytes*2);

			int headerBytes = sizeof(sdr5proto::dataChunkHeader);
			uint8_t* s = wsw.beginAppend(headerBytes + bytes);
			auto* header = (sdr5proto::dataChunkHeader*) s;
			
			header->waveSizeSamples = mReader.length;
			header->startSamples = mView.startSamples;
			header->compressionFactor = mView.compression();
			header->yLower = yLower;
			header->yUpper = yUpper;
			header->displayIndex = d;
			header->flags = 0;
			if(!useOriginal)
				header->flags |= sdr5proto::dataChunkHeader::FLAG_IS_MIPMAP;
			if(isSpectrum)
				header->flags |= sdr5proto::dataChunkHeader::FLAG_IS_SPECTRUM;


			//memcpy(s + headerBytes, (void*)subBuffer, bytes);
			uint8_t* dst = (uint8_t*) (s + headerBytes);
			if(useOriginal) {
				if(isSpectrum)
					copySpectrum(chunk.spectrum, dst, mView.startSamples, mView.endSamples, yLower, yUpper);
				else copyOriginal(original, dst, mView.startSamples, mView.endSamples, yLower, yUpper, sv.halfWidth);
			} else {
				if(isSpectrum)
					mReader.readSpectrum(mView, dst, yLower, yUpper);
				else mReader.read(mView, dst, yLower, yUpper);
			}
			
			//write(3, s.data(), 1024);
			wsw.endAppend(2); // opcode=2
		}
		wsw.flush();
	}

	void handleFrame(WebSocketParser::WSFrame f) {
		auto buf = wsw.beginAppend(f.data.length());
		memcpy(buf, f.data.data(), f.data.length());
		wsw.endAppend(f.opcode);
		wsw.flush();
		
		if(f.opcode == 1) {
			auto s = f.data;
			// TODO: "stop" should be restricted to privileged clients because
			// it pins a buffer in memory.
			if(s == "start") {
				if(reservedChunk) hw_freeChunk(reservedChunk);
				return;
			}
			if(s == "stop") {
				if(reservedChunk) hw_freeChunk(reservedChunk);
				auto tmp = hw_reserveChunk(hw_streamViews[0]);
				if(tmp) reservedChunk = tmp;
				return;
			}
			int i1 = s.find(' ');
			if(i1 == s.npos) return;
			int i2 = s.find(' ', i1 + 1);
			if(i2 == s.npos) return;
			int i3 = s.find(' ', i2 + 1);
			if(i3 == s.npos) return;
			int i4 = s.find(' ', i3 + 1);
			if(i4 == s.npos) return;
			int i5 = s.find(' ', i4 + 1);
			if(i5 == s.npos) return;
			if(s.substr(0, i1) == "setview") {
				int d = stoi(string(s.substr(i1 + 1, i2 - i1 - 1)));
				if(d < 0 || d >= displays) return;
				double start = stod(string(s.substr(i2 + 1, i3 - i2 - 1)));
				double end = stod(string(s.substr(i3 + 1, i4 - i3 - 1)));
				double lower = stod(string(s.substr(i4 + 1, i5 - i4 - 1)));
				double upper = stod(string(s.substr(i5 + 1)));
				start *= mReader.length;
				end *= mReader.length;
				
				// set x extents
				if(start < 0) start = 0;
				if(start > mReader.length - 128) start = mReader.length - 128;
				if(end < start + 64) end = start + 64;
				if(end > mReader.length) end = mReader.length;
				mipmapReaderView mViewReq = {int(start), int(end), 1024};
				mReader.requestView(mViewReq, mView.at(d));
				
				// set y extents
				yRange.at(d) = {(float) lower, (float) upper};
			}
		}
	}
	void wsEnd() {
		worker.epoll.remove(timer);
		abort();
	}
	~MyHandler() {
		if(reservedChunk) hw_freeChunk(reservedChunk);
	}
	void finish(bool flush) {
		this->~MyHandler();
		ch.finish(flush);
	}
	void abort() {
		this->~MyHandler();
		ch.abort();
	}
};

// given a type and a member function, create a handler that
// will instantiate the type and call the member function.
template<class T, void (T::*FUNC)()>
HandleRequestCB createMyHandler() {
	return [](ConnectionHandler& ch) {
		auto* tmp = ch.allocateHandlerState<T>(ch);
		(tmp->*FUNC)();
	};
}

bool ends_with(string_view a, string_view b) {
	if(b.length() > a.length()) return false;
	return a.substr(a.length() - b.length()) == b;
}
void runWorker() {
	StaticFileManager sfm(".");

	// request router; given a http path return a HandleRequestCB
	auto router = [&](string_view host, string_view path) {
		string tmp(path);
		//printf("%s\n", tmp.c_str());
		if(path.compare("/points") == 0)
			return createMyHandler<MyHandler, &MyHandler::handlePoints>();
		if(path.compare("/100") == 0)
			return createMyHandler<MyHandler, &MyHandler::handle100>();

		// serve html and js files from disk
		if(ends_with(path, ".html") || ends_with(path, ".js"))
			return sfm.createHandler(path);

		// unhandled paths
		HandleRequestCB h = [](ConnectionHandler& ch) {
			ch.response.status = "418 I'm a teapot";
			ch.response.write("418");
			ch.finish();
		};
		return h;
	};

	worker.router = router;
	worker.addListenSocket(srvsock);

	Timer timer((uint64_t) 1000);
	timer.setCallback([&](int r) {
		worker.timerCB();
		sfm.timerCB();
	});
	worker.epoll.add(timer);
	worker.loop();
}

void* thread1(void*) {
	hw_doLoop();
	return NULL;
}
int main(int argc, char** argv) {
	if(argc<3) {
		printf("usage: %s bind_host bind_port\n", argv[0]);
		return 1;
	}
	hw_init();
	pthread_t pth;
	assert(pthread_create(&pth, nullptr, &thread1, nullptr) == 0);

	srvsock.bind(argv[1], argv[2]);
	srvsock.listen();

	runWorker();
}
