/***************************************************************************//**
 *   @file   Platform.c
 *   @brief  Implementation of Platform Driver.
 *   @author DBogdan (dragos.bogdan@analog.com)
********************************************************************************
 * Copyright 2013(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include "../util.h"
#include "platform.h"
#include "parameters.h"
#include <stdio.h>

#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <fcntl.h>
#include <termios.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

typedef struct pollfd pollfd;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;
typedef uint64_t u64;


// we don't map the entire h2f1 and h2f2 address range because it is too large
// (1GiB each), so only h2fMapSize bytes are mapped.
static const long h2fMapSize = 1024*1024*8;
static const long h2f1gpioBegin = 0x41200000, h2f1gpioEnd = 0x80000000;
static const long gpioBegin = 0xE0000000, gpioSize=0xB000;
uint8_t* gpioRegs = NULL;
uint8_t* h2f1gpio = NULL;

volatile uint32_t* gpio_emio0out = NULL; // output data; 16 bits; upper 16 bits mask the write access
volatile uint32_t* gpio_emio0in = NULL; // input data
volatile uint32_t* gpio_emio0dir = NULL;
volatile uint32_t* gpio_emio0oe = NULL;
volatile uint32_t* gpio_axi0data = NULL;

uint32_t pin_spi_clk = (1<<0);
uint32_t pin_spi_cs = (1<<1);
uint32_t pin_spi_sdi = (1<<2);
uint32_t pin_spi_sdo = (1<<3);

static uint32_t gpioState;


int mapH2FBridge() {
	int memfd;
	if((memfd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) {
		perror("open");
		printf( "ERROR: could not open /dev/mem\n" );
		return -1;
	}
	gpioRegs = (uint8_t*) mmap(NULL, gpioSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, gpioBegin);
	if(gpioRegs == NULL) {
		perror("mmap");
		printf( "ERROR: could not map gpioRegs\n" );
		return -1;
	}
	h2f1gpio = (uint8_t*) mmap(NULL, h2fMapSize, ( PROT_READ | PROT_WRITE ), MAP_SHARED, memfd, h2f1gpioBegin);
	if(h2f1gpio == NULL) {
		perror("mmap");
		printf( "ERROR: could not map h2f1gpio\n" );
		return -1;
	}
	close(memfd);
	
	/*gpio_emio0out = (volatile uint32_t*)(gpioRegs + 0xA048);
	gpio_emio0in = (volatile uint32_t*)(gpioRegs + 0xA068);
	gpio_emio0dir = (volatile uint32_t*)(gpioRegs + 0xA284);
	gpio_emio0oe = (volatile uint32_t*)(gpioRegs + 0xA288);*/
	
	gpio_axi0data = (volatile uint32_t*)(h2f1gpio + 0x0);
	return 0;
}

inline void gpioWrite(uint32_t pin, int val) {
	//uint32_t mask = (~pin) << 16;
	//*gpio_emio0out = mask | (val ? pin : 0);
	uint32_t tmp = val ? pin : 0;
	gpioState = (gpioState & (~pin)) | tmp;
	*gpio_axi0data = gpioState;
}
void spi_delay() {
	struct timespec tmp,tmp2;
	clock_gettime(CLOCK_MONOTONIC, &tmp);
	while(1) {
		clock_gettime(CLOCK_MONOTONIC, &tmp2);
		u64 elapsed_ns = u64(tmp2.tv_sec - tmp.tv_sec)*1000000000
						+ (s64(tmp2.tv_nsec) - s64(tmp.tv_nsec));
		if(elapsed_ns >= 1000) return;
	}
}

void spiTransaction(u8* din, u8* dout, int bytes) {
	// bit pattern:
	// smp 0 0 0 0 sdi cs clk
	
	// pull down cs pin
	gpioWrite(pin_spi_cs, 1);
	spi_delay(); spi_delay();
	gpioWrite(pin_spi_cs, 0);
	spi_delay(); spi_delay();
	
	int index = 7;
	
	// send data bits and sample dout bits
	for(int b=0;b<bytes;b++) {
		u8 byte = din[b];
		u8 rbyte = 0;
		for(int i=0;i<8;i++) {
			u32 bit = byte>>7;
			
			gpioWrite(pin_spi_clk, 0);
			gpioWrite(pin_spi_sdi, bit);
			spi_delay();
			gpioWrite(pin_spi_clk, 1);
			spi_delay();
			
			u8 rbit = (*gpio_axi0data & pin_spi_sdo)?1:0;
			rbyte = (rbyte<<1) | rbit;
			
			byte <<= 1;
		}
		dout[b] = rbyte;
	}
	
	// release cs pin
	gpioWrite(pin_spi_clk, 0);
	spi_delay(); spi_delay();
	gpioWrite(pin_spi_cs, 1);
}


/***************************************************************************//**
 * @brief spi_init
*******************************************************************************/
int32_t spi_init(uint32_t device_id,
				 uint8_t  clk_pha,
				 uint8_t  clk_pol)
{
	int tmp = mapH2FBridge();
	if(tmp == 0) {
		//*gpio_emio0dir = 0xffffffff;
		//*gpio_emio0oe = 0xffffffff;
		//*gpio_axi0data = 1 << (11+8);
		//gpioState = (0b100000 << 16);
		gpioState = *gpio_axi0data;
		gpioWrite(pin_spi_cs, 1);
		gpioWrite(pin_spi_clk, 0);
		//gpioWrite(1<<3, 1);
		//*gpio_emio0out = 0;
	}
	//exit(0);
	return tmp;
}

void spi_deinit() {
	
}

/***************************************************************************//**
 * @brief spi_read
 * Perform a spi transaction of bytes_number total bytes, with sdi data supplied 
 * in data and sdo data also written back to data
*******************************************************************************/
int32_t spi_read(uint8_t *data,
				 uint8_t bytes_number)
{
	fprintf(stderr, "spi_read %d\n", bytes_number);
	spiTransaction(data, data, bytes_number);
	return 0;
}

/***************************************************************************//**
 * @brief spi_write_then_read
 * Perform a spi transaction of n_tx+n_rx bytes total, with the first n_tx bytes
 * sdi supplied from txbuf, and with the last n_rx bytes sdo written into rxbuf
*******************************************************************************/
int spi_write_then_read(struct spi_device *spi,
		const unsigned char *txbuf, unsigned n_tx,
		unsigned char *rxbuf, unsigned n_rx)
{
	//fprintf(stderr, "spi_write_then_read %d, %d\n", n_tx, n_rx);
	u8 buf[n_tx+n_rx];
	memset(buf,0,sizeof(buf));
	memcpy(buf, txbuf, n_tx);
	spiTransaction(buf, buf, n_tx+n_rx);
	memcpy(rxbuf, buf+n_tx, n_rx);
	return 0;
}

/***************************************************************************//**
 * @brief gpio_init
*******************************************************************************/
void gpio_init(uint32_t device_id)
{

}

/***************************************************************************//**
 * @brief gpio_direction
*******************************************************************************/
void gpio_direction(uint8_t pin, uint8_t direction)
{

}

/***************************************************************************//**
 * @brief gpio_is_valid
*******************************************************************************/
bool gpio_is_valid(int number)
{
	if(number == GPIO_RESET_PIN) return 1;
	return 0;
}

/***************************************************************************//**
 * @brief gpio_data
*******************************************************************************/
void gpio_data(uint8_t pin, uint8_t data)
{

}

/***************************************************************************//**
 * @brief gpio_set_value
*******************************************************************************/
void gpio_set_value(unsigned gpio, int value)
{
	gpioWrite((uint32_t(1)<<gpio), value);
}

/***************************************************************************//**
 * @brief udelay
*******************************************************************************/
void udelay(unsigned long usecs)
{

}

/***************************************************************************//**
 * @brief mdelay
*******************************************************************************/
void mdelay(unsigned long msecs)
{

}

/***************************************************************************//**
 * @brief msleep_interruptible
*******************************************************************************/
unsigned long msleep_interruptible(unsigned int msecs)
{

	return 0;
}

/***************************************************************************//**
 * @brief axiadc_init
*******************************************************************************/
void axiadc_init(struct ad9361_rf_phy *phy)
{

}

/***************************************************************************//**
 * @brief axiadc_post_setup
*******************************************************************************/
int axiadc_post_setup(struct ad9361_rf_phy *phy)
{
	return 0;
}

/***************************************************************************//**
 * @brief axiadc_read
*******************************************************************************/
unsigned int axiadc_read(struct axiadc_state *st, unsigned long reg)
{
	return 0;
}

/***************************************************************************//**
 * @brief axiadc_write
*******************************************************************************/
void axiadc_write(struct axiadc_state *st, unsigned reg, unsigned val)
{

}

/***************************************************************************//**
* @brief axiadc_set_pnsel
*******************************************************************************/
int axiadc_set_pnsel(struct axiadc_state *st, int channel, enum adc_pn_sel sel)
{
	return 0;
}

/***************************************************************************//**
 * @brief axiadc_idelay_set
*******************************************************************************/
void axiadc_idelay_set(struct axiadc_state *st,
				unsigned lane, unsigned val)
{

}
