#include "common.H"

// the data returned by the mipmap hardware is a depth first listing of the
// chunk tree. we need to calculate the chunk number (index into the array) given
// the level in the tree (0 being leaf level) and the index among that level.

// mipmapChunkFinder calculates the absolute chunk array index given
// the mipmap level and the logical chunk index (offset) within that level.
// usage:
// 1. fill out levelSteps with the compression factor of each mipmap level of the hardware
// 2. call init()
// 3. use goToChunk() and/or advanceChunk() as needed, which both set currIndex to the
//    absolute chunk index of the requested chunk.
// 4. repeat (3) as needed
template<int LEVELS>
struct mipmapChunkFinder {
	int levelSteps[LEVELS] = {};
	int levelSizes[LEVELS] = {};
	int levelIndex[LEVELS] = {};
	int totalChunkCount = 0;
	int currLevel = 0;
	int currIndex = 0;

	void init() {
		// calculate the total chunk count of each level's nodes, including of its children
		levelSizes[0] = 1;
		for(int i=1; i<LEVELS; i++) {
			levelSizes[i] = levelSizes[i-1]*levelSteps[i-1] + 1;
		}
		totalChunkCount = levelSizes[LEVELS-1] * levelSteps[LEVELS-1];
	}

	// jump to the specified chunk index at the specified level
	void goToChunk(int level, int index) {
		currLevel = level;

		// calculate the local indexes (index among children of the same parent node)
		// of the ancestors of the chunk node we are after.
		int tmpIndex = index;
		for(int i=level; i<LEVELS; i++) {
			levelIndex[i] = tmpIndex % levelSteps[i];
			tmpIndex /= levelSteps[i];
		}

		// calculate the total number of chunks in the stream before the chunk we are after
		currIndex = 0;
		for(int i=level; i<LEVELS; i++)
			currIndex += levelIndex[i] * levelSizes[i];

		// if we have selected a chunk that is not a leaf, we have to also
		// skip over all our children to get to the correct chunk.
		if(level > 0)
			currIndex += levelSteps[level-1] * levelSizes[level-1];
	}

	// move to the next chunk in the same level
	void advanceChunk() {
		int level = currLevel;
		currIndex += levelSizes[level];
		if(levelIndex[level] == (levelSteps[level]-1)) {
			for(int i=level+1; i<LEVELS; i++) {
				currIndex++;
				if(levelIndex[i] != (levelSteps[i]-1)) {
					levelIndex[i]++;
					break;
				}
				levelIndex[i] = 0;
			}
			levelIndex[level] = 0;
		} else {
			levelIndex[level]++;
		}
		if(currIndex >= totalChunkCount)
			currIndex -= totalChunkCount;
	}
};

// represents a view into array data
struct mipmapReaderView {
	// startSamples is inclusive and endSamples is exclusive
	int startSamples, endSamples;
	// number of samples of resolution
	int resolution;
	int compression() {
		return (endSamples - startSamples) / resolution;
	}
};
template<int LEVELS, int CHANNELS>
class mipmapReader {
public:
	mipmapChunkFinder<LEVELS> finder;
	volatile uint64_t* mipmap;
	int levelCompression[LEVELS];

	// sample groups (samples/channels)
	int length;

	// how many sample groups are in each mipmap chunk
	int chunkSize = 16;

	// the compression factor of the "pre-stage" of the mipmap hw
	int baseLevelStep = 4;

	// whether to allow views to the original data rather than a mipmap
	bool allowOriginal = true;

	void init(int* levelSteps) {
		for(int i=0; i<LEVELS; i++) {
			finder.levelSteps[i] = levelSteps[i];
		}
		levelCompression[0] = baseLevelStep;
		for(int i=1; i<LEVELS; i++) {
			levelCompression[i] = levelCompression[i - 1] * levelSteps[i - 1];
		}
		finder.init();
	}
	void requestView(const mipmapReaderView& requested, mipmapReaderView& returned) {
		assert(requested.startSamples >= 0 && requested.startSamples < length);
		assert(requested.endSamples > requested.startSamples && requested.endSamples <= length);
		int reqViewSpan = requested.endSamples - requested.startSamples;
		double compression = double(reqViewSpan) / requested.resolution;
		// find nearest mipmap level that is at least as detailed as requested
		int i = LEVELS-1;
		for(; i >= 0; i--) {
			if(levelCompression[i] <= compression)
				break;
		}
		if(i < 0) {
			if(allowOriginal) {
				returned = requested;
				returned.resolution = returned.endSamples - returned.startSamples;
				goto ret;
			} else {
				i = 0;
			}
		}
		{
			int c = levelCompression[i];
			int roundTo = c * chunkSize;
			returned.startSamples = requested.startSamples/roundTo;
			returned.startSamples *= roundTo;
			returned.endSamples = (requested.endSamples + roundTo - 1)/roundTo;
			returned.endSamples *= roundTo;
			returned.resolution = (returned.endSamples - returned.startSamples) / c;
		}
	ret:
		assert(returned.endSamples <= length);
		fprintf(stderr, "requested view: %d - %d, %d points; got: %d - %d, %d points (level %d)\n",
				requested.startSamples, requested.endSamples, requested.resolution,
				returned.startSamples, returned.endSamples, returned.resolution, i);
	}

	// only supports reading mipmaps!!! if view.compression() is 1, you need to use your own
	// function for copying the raw data to the dst array.
	// dst should be an array of size view.resolution*CHANNELS*2 (each point has a lower and upper value)
	template<class INTTYPE>
	void read(const mipmapReaderView& view, INTTYPE* dst, double yLower, double yUpper) {
		INTTYPE valMin = numeric_limits<INTTYPE>::min();
		INTTYPE valMax = numeric_limits<INTTYPE>::max();
		double A = (double(valMax) - double(valMin)) / (yUpper - yLower);
		double B = double(valMin);

		int viewSpan = view.endSamples - view.startSamples;
		int compression = viewSpan / view.resolution;
		int i = 0;
		for(; i<LEVELS; i++) {
			if(levelCompression[i] == compression)
				break;
		}
		if(i == LEVELS) throw logic_error("no mipmap level for this resolution");
		int mipmapStart = view.startSamples/compression;
		finder.goToChunk(i, mipmapStart/chunkSize);
		
		int chunkElements = chunkSize*CHANNELS;
		int dstElements = view.resolution*CHANNELS;
		int dstOffs = 0;
		while(true) {
			int offs = finder.currIndex * chunkElements;
			for(int x=0; x<chunkElements; x++) {
				uint64_t element = mipmap[offs + x];
				double lower = (double) int32_t(element & 0xffffffff);
				double upper = (double) int32_t((element >> 32) & 0xffffffff);
				lower = clamp(lower, yLower, yUpper);
				upper = clamp(upper, yLower, yUpper);
				dst[(dstOffs + x) * 2] = INTTYPE(round((lower - yLower)*A + B));
				dst[(dstOffs + x) * 2 + 1] = INTTYPE(round((upper - yLower)*A + B));
			}
			dstOffs += chunkElements;
			if(dstOffs >= dstElements) break;
			finder.advanceChunk();
		}
	}

	// only supports reading mipmaps!!! if view.compression() is 1, you need to use your own
	// function for copying the raw data to the dst array.
	// dst should be an array of size view.resolution*2 (each point has a lower and upper value)
	template<class INTTYPE>
	void readSpectrum(const mipmapReaderView& view, INTTYPE* dst, double yLower, double yUpper) {
		static_assert(CHANNELS == 2);
		INTTYPE valMin = numeric_limits<INTTYPE>::min();
		INTTYPE valMax = numeric_limits<INTTYPE>::max();
		double A = (double(valMax) - double(valMin)) / (yUpper - yLower);
		double B = double(valMin);

		int viewSpan = view.endSamples - view.startSamples;
		int compression = viewSpan / view.resolution;
		int i = 0;
		for(; i<LEVELS; i++) {
			if(levelCompression[i] == compression)
				break;
		}
		if(i == LEVELS) throw logic_error("no mipmap level for this resolution");
		int totalChunks = length / levelCompression[i] / chunkSize;
		int mipmapStart = view.startSamples/compression;
		int chunkIndex = mipmapStart/chunkSize;
		chunkIndex += totalChunks/2;
		if(chunkIndex >= totalChunks)
			chunkIndex -= totalChunks;
		finder.goToChunk(i, chunkIndex);
		
		int chunkElements = chunkSize;
		int dstElements = view.resolution;
		int dstOffs = 0;
		while(true) {
			int offs = finder.currIndex * CHANNELS * chunkElements;
			for(int x=0; x<chunkSize; x++) {
				uint64_t elementRe = mipmap[offs + x*2];
				uint64_t elementIm = mipmap[offs + x*2 + 1];
				int32_t lowerRe = int(elementRe & 0xffffffff);
				int32_t upperRe = int((elementRe >> 32) & 0xffffffff);
				int32_t lowerIm = int(elementIm & 0xffffffff);
				int32_t upperIm = int((elementIm >> 32) & 0xffffffff);
				if((-lowerRe) > upperRe) upperRe = -lowerRe;
				if((-lowerIm) > upperIm) upperIm = -lowerIm;
				double tmp = spectrumValue(upperRe, upperIm);
				tmp = clamp(tmp, yLower, yUpper);
				dst[(dstOffs + x) * 2] = INTTYPE(round((tmp - yLower)*A + B));
				dst[(dstOffs + x) * 2 + 1] = INTTYPE(round((tmp - yLower)*A + B));
			}
			dstOffs += chunkElements;
			if(dstOffs >= dstElements) break;
			finder.advanceChunk();
		}
	}
};
