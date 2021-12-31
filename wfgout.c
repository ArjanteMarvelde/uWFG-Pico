#include <hardware/pio.h>
#include <wfgout.pio.h>


void wfgout_init()
{
	PIO		pio = pio0;
	uint	offset = pio_add_program(pio, &wfgout_program);
	uint	sm = 0;
	
	// Set up DMA channels
	
	// Initialize state machine
	wfgout_program_init(pio, sm, offset, (uint)8, (uint)8));
}