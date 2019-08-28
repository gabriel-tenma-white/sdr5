#include <sys/epoll.h>
#include <string.h>
#include <stdexcept>
#include <string>
#include <functional>
#include <unistd.h>
#include <errno.h>

using namespace std;

class SimpleEPoll {
public:
	typedef function<void(uint32_t evts)> CB;

	int epfd = -1;
	SimpleEPoll() {
		epfd = epoll_create1(EPOLL_CLOEXEC);
		if(epfd < 0)
			throw runtime_error(strerror(errno));
	}
	~SimpleEPoll() {
		close(epfd);
	}
	void add(int fd, const CB& cb) {
		auto* cbCopy = new CB(cb);
		epoll_event evt = {};
		evt.events = EPOLLIN | EPOLLOUT | EPOLLET;
		evt.data.u64 = 0;
		evt.data.ptr = cbCopy;
		int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &evt);
		if(ret < 0)
			throw runtime_error(strerror(errno));
	}
	void remove(int fd) {
		int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
		if(ret < 0)
			throw runtime_error(strerror(errno));
	}
	void loop() {
		static constexpr int MAXEVENTS = 32;
		epoll_event events[MAXEVENTS];
		while(true) {
			int nEvents = epoll_wait(epfd, events, MAXEVENTS, -1);
			if(nEvents < 0)
				throw runtime_error(strerror(errno));
			if(nEvents == 0) break;

			for(int i=0; i<nEvents; i++) {
				auto* cb = (CB*) events[i].data.ptr;
				(*cb)(events[i].events);
			}
		}
	}
};
