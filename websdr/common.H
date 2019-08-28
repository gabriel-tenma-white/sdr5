#pragma once
#include <math.h>

static inline uint32_t expandBits(uint32_t val) {
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

static inline double spectrumValue(int32_t re, int32_t im) {
	double tmp = double(re)*re + double(im)*im;
	double db;
	if(tmp < 1) db = -10;
	else db = log10(tmp)*10;
	return round((db - 60));
}

static inline double clamp(double v, double lower, double upper) {
	if(v <= lower) v = lower;
	if(v >= upper) v = upper;
	return v;
}
