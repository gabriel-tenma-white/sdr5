#pragma once

// sdr5 websocket protocol data structures

namespace sdr5proto {
	// server => client data chunk header
	struct dataChunkHeader {
		// the total length of the waveform in hw samples
		uint32_t waveSizeSamples;

		// the start of the subview in hw samples
		uint32_t startSamples;

		// how many hw samples each mipmap sample covers
		uint32_t compressionFactor;

		// the original Y value corresponding to the lowest possible received number (0)
		float yLower;

		// the original Y value corresponding to the highest possible received number (255)
		float yUpper;

		// which display this chunk is for
		uint8_t displayIndex;

		uint8_t flags;
		enum:uint8_t {
			// if set, each sample consists of two bytes, lower and upper
			FLAG_IS_MIPMAP = 1,

			// if set, there is only one channel; if unset, there
			// are two interleaved channels (real & imaginary)
			FLAG_IS_SPECTRUM = 2
		};
	} __attribute__ ((packed));
}
