#pragma once
#include "hw.H"
#include "common.H"
#include <owocomm/axi_pipe.H>

using namespace OwOComm;


// dst must be an array of size (end-start)*2
template<class INTTYPE>
void copyOriginal(volatile void* src, INTTYPE* dst, int start, int end, double yLower, double yUpper, bool halfWidth) {
	INTTYPE valMin = numeric_limits<INTTYPE>::min();
	INTTYPE valMax = numeric_limits<INTTYPE>::max();
	double A = (double(valMax) - double(valMin)) / (yUpper - yLower);
	double B = double(valMin);

	if(halfWidth) {
		auto srcArray = (volatile uint32_t*) src;
		int w=4, h=2, W=256, H=512;
		int burstLength = w*h;
		int Imask = (W>H) ? (H-1) : (W-1);
		int Ibits = ((W>H) ? myLog2(H) : myLog2(W)) - 1;
		for(int i=start; i<end; i++) {
			uint32_t X = i/1024/w, Y = (i % 1024)/h;
			uint32_t x = (i/1024) % w, y = (i % 1024) % h;
			uint32_t X1 = (expandBits(X&Imask) | ((X & (~Imask)) << Ibits));
			uint32_t Y1 = (expandBits(Y&Imask) | ((Y & (~Imask)) << Ibits)) << 1;
			uint32_t addr = (X1 | Y1) * burstLength;
			addr += (x % 2);
			addr += y * 2;
			addr += (x / 2) * 4;
			uint32_t element = srcArray[addr];
			double lower = (double) int16_t(element & 0xffff);
			double upper = (double) int16_t(element >> 16);
			lower = clamp(lower, yLower, yUpper);
			upper = clamp(upper, yLower, yUpper);
			dst[0] = INTTYPE(round((lower - yLower)*A + B));
			dst[1] = INTTYPE(round((upper - yLower)*A + B));
			dst += 2;
		}
	}
}


// dst must be an array of size (end-start)
template<class INTTYPE>
void copySpectrum(volatile void* src, INTTYPE* dst, int start, int end, double yLower, double yUpper) {
	INTTYPE valMin = numeric_limits<INTTYPE>::min();
	INTTYPE valMax = numeric_limits<INTTYPE>::max();
	double A = (double(valMax) - double(valMin)) / (yUpper - yLower);
	double B = double(valMin);

	auto srcArray = (volatile uint64_t*) src;
	auto dstEnd = dst + (end-start);
	int w=2, h=2, W=512, H=512;
	int len = W*H*w*h;
	int burstLength = w*h;
	start += len/2; end += len/2;
	if(start >= len) start -= len;
	if(end >= len) end -= len;


	for(int i=start; dst != dstEnd; i = (i+1) % len) {
		uint32_t X = (i % 1024) / w, Y = (i / 1024) / h;
		uint32_t x = (i % 1024) % w, y = (i / 1024) % h;
		uint32_t X1 = expandBits(X);
		uint32_t Y1 = expandBits(Y) << 1;
		uint32_t addr = (X1 | Y1) * burstLength;
		addr += x;
		addr += y * 2;
		uint64_t element = srcArray[addr];
		int32_t re = int(element & 0xffffffff);
		int32_t im = int(element >> 32);
		double tmp = spectrumValue(re, im);
		tmp = clamp(tmp, yLower, yUpper);
		*dst = INTTYPE(round((tmp - yLower)*A + B));
		dst++;
	}
}



template<class INTTYPE>
void copyOriginal_(volatile void* src, INTTYPE* dst, int start, int end, bool halfWidth) {
	if(halfWidth) {
		auto srcArray = (volatile uint32_t*) src;
		for(int i=start; i<end; i++) {
			uint32_t element = srcArray[i];
			int16_t lower = int(element & 0xffff);
			int16_t upper = int(element >> 16);
			dst[0] = INTTYPE(lower);
			dst[1] = INTTYPE(upper);
			dst += 2;
		}
	} else {
		auto srcArray = (volatile uint64_t*) src;
		for(int i=start; i<end; i++) {
			uint64_t element = srcArray[i];
			int32_t lower = int(element & 0xffffffff);
			int32_t upper = int(element >> 32);
			dst[0] = INTTYPE(lower);
			dst[1] = INTTYPE(upper);
			dst += 2;
		}
	}
}

