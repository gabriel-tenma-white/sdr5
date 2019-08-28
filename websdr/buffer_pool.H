#include <vector>
#include <assert.h>

class BufferPool {
public:
	std::vector<int> availableBuffers;
	volatile uint8_t* reservedMem = nullptr;
	volatile uint8_t* reservedMemEnd = nullptr;
	int bufSize = 0;
	int nBuffers = 0;

	void init(volatile void* reservedMem, int reservedMemBytes, int bufSize) {
		this->reservedMem = (volatile uint8_t*)reservedMem;
		this->reservedMemEnd = this->reservedMem + reservedMemBytes;
		this->bufSize = bufSize;
		this->nBuffers = reservedMemBytes/bufSize;

		availableBuffers.resize(nBuffers);
		for(int i=0; i<nBuffers; i++)
			availableBuffers[i] = (nBuffers-i-1);
	}

	volatile uint8_t* get() {
		if(availableBuffers.empty())
			throw runtime_error("could not allocate buffer: no more free buffers");

		int tmp = availableBuffers.back();
		availableBuffers.pop_back();
		return reservedMem + tmp*bufSize;
	}
	void put(volatile void* buf_) {
		volatile uint8_t* buf = (uint8_t*) buf_;
		assert(buf >= reservedMem);
		assert(buf < reservedMemEnd);
		
		int tmp = (buf-reservedMem)/bufSize;
		assert(buf == (reservedMem + tmp*bufSize));
		availableBuffers.push_back(tmp);
	}
};

// a collection of buffer pools for allocating several buffer sizes
class MultiBufferPool {
public:
	vector<BufferPool> pools;
	volatile uint8_t* reservedMem = nullptr;
	int reservedMemBytes = 0;

	void init(volatile void* reservedMem, int reservedMemBytes) {
		this->reservedMem = (volatile uint8_t*) reservedMem;
		this->reservedMemBytes = reservedMemBytes;
	}
	// always add larger buffer pools first to keep buffer addresses aligned to its size
	void addPool(int bufSize, int nBuffers) {
		int totalSize = bufSize * nBuffers;
		if(totalSize > reservedMemBytes)
			throw length_error("MultiBufferPool::addPool(): not enough unpooled memory remaining");
		pools.push_back({});
		pools.back().init(reservedMem, totalSize, bufSize);
		reservedMem += totalSize;
		reservedMemBytes -= totalSize;
	}

	volatile uint8_t* get(int size) {
		for(auto& pool: pools) {
			if(pool.bufSize == size) {
				return pool.get();
			}
		}
		throw logic_error("no BufferPool for bufSize " + std::to_string(size));
	}
	void put(volatile void* buf) {
		for(auto& pool: pools) {
			if(buf >= pool.reservedMem && buf < pool.reservedMemEnd) {
				pool.put(buf);
				return;
			}
		}
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "MultiBufferPool::put(): address %p does not belong to any pool", buf);
		throw runtime_error(tmp);
	}
};
