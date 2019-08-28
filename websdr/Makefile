
CXX := clang++-7
AXI_UTIL_PATH ?= ../../axi-util/sw
FPGA_FFT_PATH ?= ../../fpga-fft/sw
CPPSP_PATH ?= ../../cppsp-ng
CPOLL_PATH ?= ../../cpoll-ng
CFLAGS ?= -g2

INCLUDES ?= -I$(AXI_UTIL_PATH)/include -I$(FPGA_FFT_PATH)/include -I$(CPPSP_PATH)/include -I$(CPOLL_PATH)/include
LIBS ?= -lcryptopp -lpthread

REQUIRED_CXXFLAGS := --std=c++17 -finput-charset=UTF-8 -fextended-identifiers -fwrapv

CXXFLAGS2 := $(REQUIRED_CXXFLAGS) $(INCLUDES) $(CFLAGS) $(CXXFLAGS)


all: server

%.o: %.C
	$(CXX) -c $(CXXFLAGS2) $< -o $@

$(CPPSP_PATH)/libcppsp-ng.a: FORCE
	$(MAKE) -C $(CPPSP_PATH) libcppsp-ng.a

$(FPGA_FFT_PATH)/libaxi_fft.a: FORCE
	$(MAKE) -C $(FPGA_FFT_PATH) libaxi_fft.a

$(AXI_UTIL_PATH)/libaxi_pipe.a: FORCE
	$(MAKE) -C $(AXI_UTIL_PATH) libaxi_pipe.a

FORCE:


server: server.o hw.o $(FPGA_FFT_PATH)/libaxi_fft.a $(AXI_UTIL_PATH)/libaxi_pipe.a $(CPPSP_PATH)/libcppsp-ng.a
	$(CXX) $(LIBS) $^ -o $@

clean:
	rm -f server *.o

clean_all: clean
	$(MAKE) -C $(FPGA_FFT_PATH) clean
	$(MAKE) -C $(AXI_UTIL_PATH) clean


