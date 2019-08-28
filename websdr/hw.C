#include "hw.H"
#include "simple_epoll.H"
#include "buffer_pool.H"
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <owocomm/axi_pipe.H>
#include <owocomm/axi_fft.H>

#include <complex>

using namespace OwOComm;
using namespace std;

typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t u64;
typedef complex<double> complexd;


/*****************************
 * SHARED VARIABLES
 *****************************/

int hw_mipmapSteps[4];	// the compression factor of each mipmap step
vector<hw_streamView> hw_streamViews;
vector<hw_streamViewChunk> hw_streamViewsCurrentChunk;

/*****************************
 * HARDWARE PARAMETERS
 *****************************/

#define MIPMAP_FLAG_BTRANSPOSE (1<<2)
#define MIPMAP_FLAG_TRANSPOSE0 (1<<3)
#define MIPMAP_FLAG_RTRANSPOSE0 (1<<4)
#define MIPMAP_FLAG_WSPLIT (1<<5)
#define MIPMAP_FLAG_TRANSPOSE1 (1<<6)

#define PIN_TRANSPOSE (1 << 14)
#define PIN_BURSTTRANSPOSE (1 << 13)


static const long reservedMemAddr = 0x20000000;
static const long reservedMemSize = 0x10000000;
volatile uint8_t* reservedMem = NULL;
volatile uint8_t* reservedMemEnd = NULL;

// we don't map the entire h2f1 and h2f2 address range because it is too large
// (1GiB each), so only h2fMapSize bytes are mapped.
static const long h2fMapSize = 1024*1024*128;
static const long h2f1Begin = 0x40000000, h2f1End = 0x80000000;
static const long h2f2Begin = 0x80000000, h2f2End = 0xC0000000;
volatile uint8_t* h2f1 = NULL;
volatile uint8_t* h2f2 = NULL;

static const long slcrBegin = 0xf8000000, slcrEnd = 0xf8010000;
volatile uint8_t* slcr = NULL;

static const long h2f1gpioBegin = 0x41200000, h2f1gpioEnd = 0x41210000;
static const long gpioBegin = 0xE0000000, gpioSize=0xB000;

volatile uint8_t* h2f1gpio = NULL;
volatile uint32_t* gpio_axi0data = NULL;


// fft parameters
int W = 512, H = 512;
int w = 2, h = 2;

// main pipe buffer size
int sz = 1024*1024*4;

AXIPipe* mainPipe = nullptr;
AXIPipe* mipmapPipe = nullptr;
AXIFFT* fftPipe = nullptr;

SimpleEPoll epoll;
MultiBufferPool bufPool;

volatile uint64_t* testBuffer;

void copyArrayToMemHalfWidth(const complexd* src, volatile void* dst, int W, int H);

int mapH2FBridge() {
	int memfd;
	if((memfd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		perror("open");
		printf( "ERROR: could not open /dev/mem\n" );
		return -1;
	}
	h2f1 = (volatile uint8_t*) mmap(NULL, h2fMapSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, h2f1Begin);
	if(h2f1 == NULL) {
		perror("mmap");
		printf( "ERROR: could not map h2f1\n" );
		return -1;
	}
	h2f2 = (volatile uint8_t*) mmap(NULL, h2fMapSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, h2f2Begin);
	if(h2f2 == NULL) {
		perror("mmap");
		printf( "ERROR: could not map h2f2\n" );
		return -1;
	}
	reservedMem = (volatile uint8_t*) mmap(NULL, reservedMemSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, reservedMemAddr);
	if(reservedMem == NULL) {
		perror("mmap");
		printf( "ERROR: could not map reservedMem\n" );
		return -1;
	}
	reservedMemEnd = reservedMem + reservedMemSize;
	slcr = (volatile uint8_t*) mmap(NULL, (slcrEnd-slcrBegin), ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, slcrBegin);
	if(slcr == NULL) {
		perror("mmap");
		printf( "ERROR: could not map slcr registers\n" );
		return -1;
	}
	h2f1gpio = (volatile uint8_t*) mmap(NULL, (h2f1gpioEnd - h2f1gpioBegin), ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, h2f1gpioBegin);
	if(h2f1gpio == NULL) {
		perror("mmap");
		printf( "ERROR: could not map h2f1gpio\n" );
		return -1;
	}
	gpio_axi0data = (volatile uint32_t*)(h2f1gpio + 0x0);
	close(memfd);
	return 0;
}

void _maskWrite(uint32_t location, uint32_t mask, uint32_t data) {
	assert(location >= slcrBegin);
	assert(location < slcrEnd);
	location = location - slcrBegin;
	volatile uint32_t* ptr = (volatile uint32_t*)(slcr + location);
	uint32_t tmp = (*ptr) & (~mask);
	tmp |= (data & mask);
	*ptr = tmp;
}


int writeAll(int fd,void* buf, int len) {
	u8* buf1=(u8*)buf;
	int off=0;
	int r;
	while(off<len) {
		if((r=write(fd,buf1+off,len-off))<=0) break;
		off+=r;
	}
	return off;
}


int mipmapElements(int inputElements) {
	return inputElements/4 + inputElements/16 + inputElements/64 + inputElements/256;
}


class AXIPipeRecv {
public:
	// user parameters
	AXIPipe* axiPipe = nullptr;
	MultiBufferPool* bufPool = nullptr;
	uint32_t hwFlags = AXIPIPE_FLAG_INTERRUPT | AXIPIPE_FLAG_TRANSPOSE | AXIPIPE_FLAG_INTERLEAVE;
	int bufSize = 0;
	int nTargetPending = 4;

	// this callback is called for every completed buffer;
	// if the function returns false we don't free the buffer.
	function<bool(volatile uint8_t*)> cb;

	// internal state
	int nPending = 0;
	void start() {
		while(nPending < nTargetPending) {
			nPending++;
			volatile uint8_t* buf = bufPool->get(bufSize);
			uint32_t marker = axiPipe->submitWrite(buf, bufSize, hwFlags);
			//printf("submit write; acceptance %d\n", axiPipe->write🅱ufferAcceptance());
			axiPipe->waitWriteAsync(marker, [this, buf]() {
				//printf("write complete\n");
				if(cb(buf))
					bufPool->put(buf);
				nPending--;
				start();
			});
		}
	}
};

int roundUp(int i) {
	int ret = 1;
	while(ret < i) ret *= 2;
	return ret;
}

void computeMipmap(volatile void* src, int length, bool halfWidth, const function<void(volatile uint64_t* res)>& cb) {
	uint32_t MYFLAG_HALFWIDTH = (1<<1);
	int mipmapFlags = 0
				| (halfWidth ? MYFLAG_HALFWIDTH : 0)
				| AXIPIPE_FLAG_TRANSPOSE
				| MIPMAP_FLAG_BTRANSPOSE
				| MIPMAP_FLAG_TRANSPOSE0
				| (halfWidth ? MIPMAP_FLAG_WSPLIT : 0)
				| MIPMAP_FLAG_TRANSPOSE1
				;

	int srcBytes = halfWidth ? (length * 4) : (length * 8);
	int dstBytes = roundUp(mipmapElements(length) * 2 * 8);
	volatile void* dst = bufPool.get(dstBytes);

	//printf("submit mipmap %d => %d\n", srcBytes, dstBytes);
	auto marker = mipmapPipe->submitRW(src, dst, srcBytes, dstBytes, mipmapFlags, 0);
	mipmapPipe->waitWriteAsync(marker, [cb, dst]() {
		//printf("complete mipmap\n");
		cb((volatile uint64_t*) dst);
	});
}

void computeFFTMipmap(volatile void* src, int length, const function<void(volatile uint64_t* res)>& cb) {
	int mipmapFlags = 0
				//| AXIPIPE_FLAG_INTERLEAVE
				//| AXIPIPE_FLAG_TRANSPOSE
				| MIPMAP_FLAG_BTRANSPOSE
				| MIPMAP_FLAG_TRANSPOSE0;
				//| MIPMAP_FLAG_RTRANSPOSE0;

	int srcBytes = (length * 8);
	int dstBytes = roundUp(mipmapElements(length) * 2 * 8);
	volatile void* dst = bufPool.get(dstBytes);

	//printf("submit mipmap %d => %d\n", srcBytes, dstBytes);
	auto marker = mipmapPipe->submitRW(src, dst, srcBytes, dstBytes, mipmapFlags, 0);
	mipmapPipe->waitWriteAsync(marker, [cb, dst]() {
		//printf("complete mipmap\n");
		cb((volatile uint64_t*) dst);
	});
}


void freeChunk(hw_streamViewChunk& chunk) {
	if(chunk.original != nullptr) bufPool.put(chunk.original);
	if(chunk.mipmap != nullptr) bufPool.put(chunk.mipmap);
	if(chunk.spectrum != nullptr) bufPool.put(chunk.spectrum);
	if(chunk.spectrumMipmap != nullptr) bufPool.put(chunk.spectrumMipmap);
	chunk = {};
}

struct chunkProcessor {
	hw_streamView& sv;
	hw_streamViewChunk chunk;
	volatile void* fftScratch = nullptr;

	chunkProcessor(hw_streamView& sv) :sv(sv) {}

	void start(volatile uint8_t* original) {
		chunk.original = original;
		chunk.spectrum = (volatile uint64_t*) bufPool.get(sv.length * 8);
		fftScratch = bufPool.get(sv.length * 8);
		fftPipe->performLargeFFTAsync(chunk.original, chunk.spectrum, fftScratch, [this]() {
			bufPool.put(fftScratch);
			fftDone();
		});
		computeMipmap(chunk.original, sv.length, sv.halfWidth, [this](volatile uint64_t* res) {
			chunk.mipmap = res;
			checkDone();
		});
	}
	void fftDone() {
		computeFFTMipmap(chunk.spectrum, sv.length, [this](volatile uint64_t* res) {
			chunk.spectrumMipmap = res;
			checkDone();
		});
	}
	void checkDone() {
		if(chunk.mipmap == nullptr) return;
		if(chunk.spectrumMipmap == nullptr) return;
		int index = (sv.currChunk+1) % sv.chunks.size();
		if(!sv.chunks[index].noFree)
			freeChunk(sv.chunks[index]);
		sv.chunks[index] = chunk;
		__sync_synchronize();
		sv.currChunk = index;
		delete this;
	}
};
void addChunk(hw_streamView& sv, volatile uint8_t* buf) {
	sv.totalChunksCounter++;
	if(sv.totalChunksCounter % 6 == 0) {
		chunkProcessor* cp = new chunkProcessor(sv);
		cp->start(buf);
	} else bufPool.put(buf);
}


void addPipeToEPoll(AXIPipe& p) {
	epoll.add(p.irqfd, [&p](uint32_t events) {
		//printf("epoll return\n");
		if(events & EPOLLIN)
			p.dispatchInterrupt();
	});
}
void hw_doLoop() {
	addPipeToEPoll(*mainPipe);
	addPipeToEPoll(*mipmapPipe);
	addPipeToEPoll(*fftPipe);

	AXIPipeRecv pipeRecv;
	pipeRecv.axiPipe = mainPipe;
	pipeRecv.bufPool = &bufPool;
	pipeRecv.bufSize = sz;
	pipeRecv.cb = [](volatile uint8_t* buf) {
		addChunk(hw_streamViews[0], buf);
		return false;
	};
	pipeRecv.start();

	mainPipe->dispatchInterrupt();
	epoll.loop();
}
hw_streamViewChunk hw_reserveChunk(hw_streamView& sv) {
	auto& chunk = sv.chunks[sv.currChunk];

	// TODO: fix race condition here
	if(chunk.noFree)
		return hw_streamViewChunk();
	chunk.noFree = true;
	return chunk;
}
void hw_freeChunk(hw_streamViewChunk& chunk) {
	freeChunk(chunk);
}



void setReservedMem(AXIPipe& p) {
	p.reservedMem = reservedMem;
	p.reservedMemEnd = reservedMemEnd;
	p.reservedMemAddr = reservedMemAddr;
}
void hw_init() {
	assert(mapH2FBridge() == 0);

	mainPipe = new OwOComm::AXIPipe(0x43C00000, "/dev/uio0");
	fftPipe = new OwOComm::AXIFFT(0x43C10000, "/dev/uio1", W,H,w,h);
	mipmapPipe = new OwOComm::AXIPipe(0x43C20000, "/dev/uio2");
	setReservedMem(*mainPipe);
	setReservedMem(*fftPipe);
	setReservedMem(*mipmapPipe);

	uint32_t MYFLAG_HALFWIDTH = (1<<5) | (1<<1);

	// we used a custom address permutation module (for the read side only)
	// so we need to use our custom flags.
	fftPipe->pass1InSize = fftPipe->pass1InSize/2;
	fftPipe->pass1InFlags = AXIFFT_FLAG_INPUT_BURST_TRANSPOSE | MYFLAG_HALFWIDTH;
	fftPipe->pass1OutFlags = AXIPIPE_FLAG_INTERLEAVE | AXIPIPE_FLAG_TRANSPOSE;
	fftPipe->pass2InFlags = AXIFFT_FLAG_INPUT_BURST_TRANSPOSE | AXIFFT_FLAG_TWIDDLE_MULTIPLY;
	fftPipe->pass2OutFlags = AXIPIPE_FLAG_INTERLEAVE | AXIPIPE_FLAG_TRANSPOSE;

	printf("gpio value: %x\n", *gpio_axi0data);

	*gpio_axi0data |= PIN_TRANSPOSE | PIN_BURSTTRANSPOSE;
	//*gpio_axi0data &= ~PIN_TRANSPOSE;
	//*gpio_axi0data &= ~PIN_BURSTTRANSPOSE;
	//*gpio_axi0data = 0;

	printf("gpio value: %x\n", *gpio_axi0data);

	bufPool.init(reservedMem, reservedMemSize);
	bufPool.addPool(sz*2, 20);
	bufPool.addPool(sz, 20);
	//bufPool.addPool(sz/2, 12);

	hw_mipmapSteps[0] = 4;
	hw_mipmapSteps[1] = 4;
	hw_mipmapSteps[2] = 4;
	hw_mipmapSteps[3] = 256;

	hw_streamViews.push_back({});
	hw_streamViews[0].centerFreqHz = 100.1e6;
	hw_streamViews[0].bandwidthHz = 20.48e6;
	hw_streamViews[0].length = sz/4;
	hw_streamViews[0].halfWidth = true;
	hw_streamViews[0].chunks.resize(2);		// keep 2 chunks in memory

	testBuffer = (volatile uint64_t*) bufPool.get(1024*1024*4);
	complexd* tmp = new complexd[1024*1024];
	/*for(int i=0; i<1024*1024; i++) {
		tmp[i] = cos(i*M_PI*2*100000/(1024*1024))*30
				+ cos(i*M_PI*2*100001/(1024*1024))*30
				+ cos(i*M_PI*2*100002/(1024*1024))*30
				+ cos(i*M_PI*2*100003/(1024*1024))*30
				+ cos(i*M_PI*2*100004/(1024*1024))*30
				+ cos(i*M_PI*2*100005/(1024*1024))*30
				+ cos(i*M_PI*2*100006/(1024*1024))*30
				+ cos(i*M_PI*2*100007/(1024*1024))*30
				+ cos(i*M_PI*2*100008/(1024*1024))*30
				+ cos(i*M_PI*2*100009/(1024*1024))*30
				+ cos(i*M_PI*2*100010/(1024*1024))*30;
	}*/
	copyArrayToMemHalfWidth(tmp, testBuffer, W/2, H);
	delete[] tmp;
}



inline uint32_t expandBits(uint32_t val) {
	uint32_t tmp = (val & 0b1);
	tmp |= (val & 0b10) << 1;
	tmp |= (val & 0b100) << 2;
	tmp |= (val & 0b1000) << 3;
	tmp |= (val & 0b10000) << 4;
	tmp |= (val & 0b100000) << 5;
	tmp |= (val & 0b1000000) << 6;
	tmp |= (val & 0b10000000) << 7;
	tmp |= (val & 0b100000000) << 8;
	tmp |= (val & 0b1000000000) << 9;
	return tmp;
}
#define COMPLEX_TO_U32(val) (uint32_t(uint16_t(int16_t((val).real()))) \
						| (uint32_t)(int32_t((val).imag()) << 16))
void copyArrayToMemHalfWidth(const complexd* src, volatile void* dst, int W, int H) {
	int w=4, h=2;
	volatile uint32_t* dstMatrix = (volatile uint32_t*)dst;
	int burstLength = w*h;
	int Imask = (W>H) ? (H-1) : (W-1);
	int Ibits = ((W>H) ? myLog2(H) : myLog2(W)) - 1;

	for(int X=0; X<W; X++) {
		uint32_t X1 = (expandBits(X&Imask) | ((X & (~Imask)) << Ibits));
		for(int Y=0;Y<H;Y++) {
			// interleave row and col address
			uint32_t Y1 = (expandBits(Y&Imask) | ((Y & (~Imask)) << Ibits)) << 1;
			uint32_t addr = (X1 | Y1) * burstLength;

			dstMatrix[addr + 0] = COMPLEX_TO_U32(src[Y*h+0 + (X*w+0)*1024]);
			dstMatrix[addr + 1] = COMPLEX_TO_U32(src[Y*h+0 + (X*w+1)*1024]);
			dstMatrix[addr + 2] = COMPLEX_TO_U32(src[Y*h+1 + (X*w+0)*1024]);
			dstMatrix[addr + 3] = COMPLEX_TO_U32(src[Y*h+1 + (X*w+1)*1024]);
			dstMatrix[addr + 4] = COMPLEX_TO_U32(src[Y*h+0 + (X*w+2)*1024]);
			dstMatrix[addr + 5] = COMPLEX_TO_U32(src[Y*h+0 + (X*w+3)*1024]);
			dstMatrix[addr + 6] = COMPLEX_TO_U32(src[Y*h+1 + (X*w+2)*1024]);
			dstMatrix[addr + 7] = COMPLEX_TO_U32(src[Y*h+1 + (X*w+3)*1024]);
		}
	}
}
