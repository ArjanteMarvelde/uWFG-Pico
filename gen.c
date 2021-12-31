/*
 * gen.c
 *
 * Created: Dec 2021
 * Author: Arjan te Marvelde
 * 
 * The generation of the output.
 *
 * The output is generated by simply taking an array of samples which is repeatedly output on a set of GPIO pins.
 * To accomplish highest speed, the generator utilizes the Pico PIO feature, which enables outputting new data
 * every system clock cycle.
 * A PIO consists of common instruction memory, that may contain several PIO programs at different offsets. These 
 * programs can then be executed by each of 4 statemachines contained in the PIO. Each SM has its own I/O FIFOs to 
 * the system, its own GPIO interfacing and a set of internal registers to let the program do its work.
 * The SM clock rate is derived from the system clock by means of a divider, [1.0 - 65536.0], in 1/256 steps.
 *
 * For the waveform generator, a simple PIO program is defined that moves a word from the SM TX fifo into the SM output 
 * shift register (OSR), and then clocks out one byte on the designated pins on every SM clock tick. 
 * A separate SM is allocated for each output channel, thus providing two independent outputs.
 * 
 * Two DMA channels are used for each output, chained in a loop to keep the flow going.
 * The dma_data channel transfers from *buffer to the PIO TX FIFO (paced by the PIO DREQ signal), chained to dma_ctrl channel.
 * The dma_ctrl channel transfers the buffer address back into the dma_data channel read_addr, and chains back to dma_data.

   From RP2040 datasheet, DMA Control / Status word layout:
 
   0x80000000 [31]    : AHB_ERROR (0): Logical OR of the READ_ERROR and WRITE_ERROR flags
   0x40000000 [30]    : READ_ERROR (0): If 1, the channel received a read bus error
   0x20000000 [29]    : WRITE_ERROR (0): If 1, the channel received a write bus error
   0x01000000 [24]    : BUSY (0): This flag goes high when the channel starts a new transfer sequence, and low when the...
   0x00800000 [23]    : SNIFF_EN (0): If 1, this channel's data transfers are visible to the sniff hardware, and each...
   0x00400000 [22]    : BSWAP (0): Apply byte-swap transformation to DMA data
   0x00200000 [21]    : IRQ_QUIET (0): In QUIET mode, the channel does not generate IRQs at the end of every transfer block
   0x001f8000 [20:15] : TREQ_SEL (0): Select a Transfer Request signal
   0x00007800 [14:11] : CHAIN_TO (0): When this channel completes, it will trigger the channel indicated by CHAIN_TO
   0x00000400 [10]    : RING_SEL (0): Select whether RING_SIZE applies to read or write addresses
   0x000003c0 [9:6]   : RING_SIZE (0): Size of address wrap region
   0x00000020 [5]     : INCR_WRITE (0): If 1, the write address increments with each transfer
   0x00000010 [4]     : INCR_READ (0): If 1, the read address increments with each transfer
   0x0000000c [3:2]   : DATA_SIZE (0): Set the size of each bus transfer (byte/halfword/word)
   0x00000002 [1]     : HIGH_PRIORITY (0): HIGH_PRIORITY gives a channel preferential treatment in issue scheduling: in...
   0x00000001 [0]     : EN (0): DMA Channel Enable
   
 * DMA channel CTRL words, assuming DMA CH0..3 and PIO0 fifo TX0 and TX1:
 *  CH0: 0x0020081f (IRQ_QUIET=0x1, TREQ_SEL=0x00, CHAIN_TO=1, INCR_WRITE=0, INCR_READ=1, DATA_SIZE=2, HIGH_PRIORITY=1, EN=1)	
 *  CH1: 0x003f800f (IRQ_QUIET=0x1, TREQ_SEL=0x3f, CHAIN_TO=0, INCR_WRITE=0, INCR_READ=0, DATA_SIZE=2, HIGH_PRIORITY=1, EN=1)
 *  CH2: 0x0020981f (IRQ_QUIET=0x1, TREQ_SEL=0x01, CHAIN_TO=3, INCR_WRITE=0, INCR_READ=1, DATA_SIZE=2, HIGH_PRIORITY=1, EN=1)	
 *  CH3: 0x003f900f (IRQ_QUIET=0x1, TREQ_SEL=0x3f, CHAIN_TO=2, INCR_WRITE=0, INCR_READ=0, DATA_SIZE=2, HIGH_PRIORITY=1, EN=1)
*/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "wfgout.pio.h"
#include "gen.h"

#define FSYS		1.25e8f

/*
 * Global variables that hold the active parameters for both channels
 */
uint		a_sm = 0, b_sm = 1;												// PIO0 state machine numbers
uint32_t 	*a_buffer, *b_buffer;											// Buffer address
uint32_t	a_buflen, b_buflen;												// Buffer length (in 32bit words)
float		a_freq, b_freq;													// Buffer frequency

/*
 * DMA channel definitions
 */
#define DMA_ADATA		0
#define DMA_ACTRL		1
#define DMA_BDATA		2
#define DMA_BCTRL		3

#define DMA_ADATA_C		0x0020081f
#define DMA_ACTRL_C		0x003f800f
#define DMA_BDATA_C		0x0020981f
#define DMA_BCTRL_C		0x003f900f

/* 
 * Define initial waveforms 
 */
#define A_FREQ			1.0e6f
#define A_BUFLEN			4
uint32_t a_buf[A_BUFLEN] = {0x00000000, 0x00000000, 0xffffffff, 0xffffffff};
#define B_FREQ			1.0e6f
#define B_BUFLEN			4
uint32_t b_buf[B_BUFLEN] = {0x00000000, 0x00000000, 0xffffffff, 0xffffffff};

/*
 * Unit initialization, only call this once!
 * This function initializes the WFG parameters, the PIO statemachines and teh DMA channels.
 */
void wfg_init()
{
	float fsam;
	float div;
	uint i;
	uint offset;
	
	for (i=0; i<8; i++)														// Initialize the used pins
	{
		gpio_set_function(PINA+i, GPIO_FUNC_PIO0);							// Function: PIO
		gpio_set_slew_rate(PINA+i, GPIO_SLEW_RATE_FAST);					// No slewrate limiting
		gpio_set_drive_strength(PINA+i, GPIO_DRIVE_STRENGTH_8MA);			// Drive 8mA (might increase to 12)
		gpio_set_function(PINB+i, GPIO_FUNC_PIO0);
		gpio_set_slew_rate(PINB+i, GPIO_SLEW_RATE_FAST);
		gpio_set_drive_strength(PINB+i, GPIO_DRIVE_STRENGTH_8MA);
	}
	
	a_buffer = &a_buf[0];													// Initialize active parameters
	a_buflen = A_BUFLEN;
	a_freq   = A_FREQ;
	b_buffer = &b_buf[0];
	b_buflen = B_BUFLEN;
	b_freq   = B_FREQ;

	offset = pio_add_program(pio0, &wfgout_program);						// Move program to PIO space and obtain offset

	fsam = a_freq * 4.0 * (float)a_buflen;									// Required sample rate = samples in buffer * freq
	div = FSYS / fsam;														// Ratio FSYS and sampleclock
	if (div < 1.0) div=1.0; 												// Cannot get higher than FSYS
	wfgout_program_init(pio0, a_sm, offset, (uint)PINA, (uint)8, div);		// Invoke PIO initializer (see wfgout.pio.h)
	
	fsam = b_freq * 4.0 * (float)b_buflen;									// Required sample rate = samples in buffer * freq
	div = FSYS / fsam;														// Ratio FSYS and sampleclock
	if (div < 1.0) div=1.0; 												// Cannot get higher than FSYS
	wfgout_program_init(pio0, b_sm, offset, (uint)PINB, (uint)8, div);		// Invoke PIO initializer (see wfgout.pio.h)

	dma_hw->ch[DMA_ADATA].read_addr = (io_rw_32)a_buffer;					// Read from waveform buffer
	dma_hw->ch[DMA_ADATA].write_addr = (io_rw_32)&pio0->txf[a_sm];			// Write to PIO TX fifo
	dma_hw->ch[DMA_ADATA].transfer_count = a_buflen;						// Nr of 32 bit words to transfer
	dma_hw->ch[DMA_ADATA].al1_ctrl = DMA_ADATA_C;							// Write ctrl word without starting the DMA

	dma_hw->ch[DMA_ACTRL].read_addr = (io_rw_32)&a_buffer;					// Read from waveform buffer address reference
	dma_hw->ch[DMA_ACTRL].write_addr = (io_rw_32)&dma_hw->ch[DMA_ADATA].read_addr;	// Write to data channel read address
	dma_hw->ch[DMA_ACTRL].transfer_count = 1;								// One word to transfer
	dma_hw->ch[DMA_ACTRL].ctrl_trig = DMA_ACTRL_C;							// Write ctrl word and start DMA
	
	dma_hw->ch[DMA_BDATA].read_addr = (io_rw_32)b_buffer;					// Read from waveform buffer
	dma_hw->ch[DMA_BDATA].write_addr = (io_rw_32)&pio0->txf[b_sm];			// Write to PIO TX fifo
	dma_hw->ch[DMA_BDATA].transfer_count = b_buflen;						// Nr of 32 bit words to transfer
	dma_hw->ch[DMA_BDATA].al1_ctrl = DMA_BDATA_C;							// Write ctrl word without starting the DMA

	dma_hw->ch[DMA_BCTRL].read_addr = (io_rw_32)&b_buffer;					// Read from waveform buffer address reference
	dma_hw->ch[DMA_BCTRL].write_addr = (io_rw_32)&dma_hw->ch[DMA_BDATA].read_addr;	// Write to data channel read address
	dma_hw->ch[DMA_BCTRL].transfer_count = 1;								// One word to transfer
	dma_hw->ch[DMA_BCTRL].ctrl_trig = DMA_BCTRL_C;							// Write ctrl word and start DMA
}

/*
 * This function is the main API of the generator.
 * Parameters are a waveform samples buffer, its length and a desired frequency.
 * It is assumed that the buffer contains one wave, and the frequency will be maximized at Fsys/buflen
 */
void wfg_play(int output, wfg_t *wave)
{
	uint32_t clkdiv;														// 31:16 int part, 15:8 frac part (in 1/256)
	float div;
	
	div = FSYS/((wave->freq)*(wave->len)*4.0);								// Calculate divider
	if (div < 1.0) div=1.0; 
	clkdiv = (uint32_t)div;													// Extract integer part
	div = (div - clkdiv)*256;												// Fraction x 256
	clkdiv = (clkdiv << 8) + (uint32_t)div;									// Add 8bit integer part of fraction
	clkdiv = clkdiv << 8;													// Final shift to match required format

	if (output)
	{
		b_buffer = wave->buf;
		b_buflen = wave->len;
		b_freq   = wave->freq;
		dma_hw->ch[DMA_BDATA].read_addr = (io_rw_32)b_buffer;				// Read from waveform buffer
		dma_hw->ch[DMA_BDATA].write_addr = (io_rw_32)&pio0->txf[b_sm];		// Write to PIO TX fifo
		dma_hw->ch[DMA_BDATA].transfer_count = b_buflen;					// Nr of 32 bit words to transfer
		dma_hw->ch[DMA_BDATA].al1_ctrl = DMA_BDATA_C;						// Write ctrl word without starting the DMA
		dma_hw->ch[DMA_BCTRL].read_addr = (io_rw_32)&b_buffer;				// Read from waveform buffer address reference
		dma_hw->ch[DMA_BCTRL].write_addr = (io_rw_32)&dma_hw->ch[DMA_BDATA].read_addr;	// Write to data channel read address
		dma_hw->ch[DMA_BCTRL].transfer_count = 1;							// One word to transfer
		dma_hw->ch[DMA_BCTRL].ctrl_trig = DMA_BCTRL_C;						// Write ctrl word and start DMA
		pio0_hw->sm[b_sm].clkdiv = (io_rw_32)clkdiv;						// Set new value
		pio_sm_clkdiv_restart(pio0, b_sm);									// Restart clock
	}
	else
	{
		a_buffer = wave->buf;
		a_buflen = wave->len;
		a_freq   = wave->freq;
		dma_hw->ch[DMA_ADATA].read_addr = (io_rw_32)a_buffer;				// Read from waveform buffer
		dma_hw->ch[DMA_ADATA].write_addr = (io_rw_32)&pio0->txf[a_sm];		// Write to PIO TX fifo
		dma_hw->ch[DMA_ADATA].transfer_count = a_buflen;					// Nr of 32 bit words to transfer
		dma_hw->ch[DMA_ADATA].al1_ctrl = DMA_ADATA_C;						// Write ctrl word without starting the DMA
		dma_hw->ch[DMA_ACTRL].read_addr = (io_rw_32)&a_buffer;				// Read from waveform buffer address reference
		dma_hw->ch[DMA_ACTRL].write_addr = (io_rw_32)&dma_hw->ch[DMA_ADATA].read_addr;	// Write to data channel read address
		dma_hw->ch[DMA_ACTRL].transfer_count = 1;							// One word to transfer
		dma_hw->ch[DMA_ACTRL].ctrl_trig = DMA_ACTRL_C;						// Write ctrl word and start DMA
		pio0_hw->sm[a_sm].clkdiv = (io_rw_32)clkdiv;						// Set new value
		pio_sm_clkdiv_restart(pio0, a_sm);									// Restart clock
	}
}
