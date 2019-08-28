#pragma once
#include <vector>
#include <stdint.h>
using namespace std;

/*****************************
 * SHARED VARIABLES
 *****************************/

// a chunk of received data, for display only
struct hw_streamViewChunk {
	volatile uint8_t* original = nullptr;
	volatile uint64_t* mipmap = nullptr;
	volatile uint64_t* spectrum = nullptr;
	volatile uint64_t* spectrumMipmap = nullptr;
	bool noFree = false;
	operator bool() {
		return (original != nullptr) || (mipmap != nullptr);
	}
};

struct hw_streamView {
	// if nonzero, serves as a hint to the user application what the spectrum center frequency is
	double centerFreqHz = 0;

	// if nonzero, serves as a hint to the user application what the spectrum physical bandwidth is
	double bandwidthHz = 0;

	// samples per chunk
	int length;

	// if true, samples are 32 bits each (16 bit real and 16 bit imag) (only applies to .original)
	bool halfWidth;

	// currently resident in memory chunks
	vector<hw_streamViewChunk> chunks;

	// which index in .chunks is the latest one
	volatile int currChunk = 0;

	volatile int totalChunksCounter = 0;

	// gets a snapshot of the current chunks, in order from oldest to most recent.
	// this does not lock the chunks in memory, and all returned chunks may become
	// invalid after one chunk period (except for the oldest chunk which may become
	// invalid right away).
	vector<hw_streamViewChunk> snapshot() {
		vector<hw_streamViewChunk> ret;
		ret.resize(chunks.size());
		int startIndex = currChunk;
		__sync_synchronize();
		startIndex++;
		for(int i=0; i<(int)ret.size(); i++) {
			int index = (i + startIndex) % ret.size();
			ret[i] = chunks[index];
		}
		return ret;
	}
};


// these are automatically populated by the implementation after hw_init()

extern int hw_mipmapSteps[4];	// the compression factor of each mipmap step
extern vector<hw_streamView> hw_streamViews;

void hw_init();
void hw_doLoop();

// locks the latest chunk in memory indefinitely until hw_freeChunk is called
hw_streamViewChunk hw_reserveChunk(hw_streamView& sv);
void hw_freeChunk(hw_streamViewChunk& chunk);

