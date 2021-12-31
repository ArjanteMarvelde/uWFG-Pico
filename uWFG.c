/*
 * uWFG.c
 *
 * Created: Dec 2021
 * Author: Arjan te Marvelde
 * 
 * The main loop of the application.
 * 
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"


#include "uWFG.h"
#include "gen.h"
#include "monitor.h"

/** Some predefined waveforms **/
// Pico is little endian, so with proper alignment word and byte addressing overlap nicely

uint8_t sine16[16] __attribute__((aligned(4))) = 						 		 \
{                                                                                \
 128, 176, 218, 245, 255, 245, 218, 176, 128,  79,  37,  10,   0,  10,  37,  79  \
};

uint8_t sine64[64] __attribute__((aligned(4))) = 								 \
{																				 \
 128, 140, 152, 165, 176, 188, 198, 208, 218, 226, 234, 240, 245, 250, 253, 254, \
 255, 254, 253, 250, 245, 240, 234, 226, 218, 208, 198, 188, 176, 165, 152, 140, \
 128, 115, 103,  90,  79,  67,  57,  47,  37,  29,  21,  15,  10,   5,   2,   1, \
   0,   1,   2,   5,  10,  15,  21,  29,  37,  47,  57,  67,  79,  90, 103, 115  \
};

uint8_t sine256[256] __attribute__((aligned(4))) = 								 \
{																				 \
 128, 131, 134, 137, 140, 143, 146, 149, 152, 155, 158, 162, 165, 167, 170, 173, \
 176, 179, 182, 185, 188, 190, 193, 196, 198, 201, 203, 206, 208, 211, 213, 215, \
 218, 220, 222, 224, 226, 228, 230, 232, 234, 235, 237, 238, 240, 241, 243, 244, \
 245, 246, 248, 249, 250, 250, 251, 252, 253, 253, 254, 254, 254, 255, 255, 255, \
 255, 255, 255, 255, 254, 254, 254, 253, 253, 252, 251, 250, 250, 249, 248, 246, \
 245, 244, 243, 241, 240, 238, 237, 235, 234, 232, 230, 228, 226, 224, 222, 220, \
 218, 215, 213, 211, 208, 206, 203, 201, 198, 196, 193, 190, 188, 185, 182, 179, \
 176, 173, 170, 167, 165, 162, 158, 155, 152, 149, 146, 143, 140, 137, 134, 131, \
 128, 124, 121, 118, 115, 112, 109, 106, 103, 100,  97,  93,  90,  88,  85,  82, \
  79,  76,  73,  70,  67,  65,  62,  59,  57,  54,  52,  49,  47,  44,  42,  40, \
  37,  35,  33,  31,  29,  27,  25,  23,  21,  20,  18,  17,  15,  14,  12,  11, \
  10,   9,   7,   6,   5,   5,   4,   3,   2,   2,   1,   1,   1,   0,   0,   0, \
   0,   0,   0,   0,   1,   1,   1,   2,   2,   3,   4,   5,   5,   6,   7,   9, \
  10,  11,  12,  14,  15,  17,  18,  20,  21,  23,  25,  27,  29,  31,  33,  35, \
  37,  40,  42,  44,  47,  49,  52,  54,  57,  59,  62,  65,  67,  70,  73,  76, \
  79,  82,  85,  88,  90,  93,  97, 100, 103, 106, 109, 112, 115, 118, 121, 124  \
};

uint8_t saw256[256] __attribute__((aligned(4)));

uint8_t block16[16] __attribute__((aligned(4))) = 								 \
{                                                                                \
   0,   0,   0,   0,   0,   0,   0,   0, 255, 255, 255, 255, 255, 255, 255, 255  \
};


/* 
 * LED TIMER definition and callback routine
 */
#define LED_MS		1000
struct repeating_timer led_timer;
bool led_callback(struct repeating_timer *t) 
{
	static bool led_state;
	
	gpio_put(PICO_DEFAULT_LED_PIN, led_state);
	led_state = (led_state?false:true);
	return true;
}

/*
 * Scheduler timer callback function.
 * This executes every LOOP_MS msec.
 */
#define LOOP_MS		100
semaphore_t loop_sem;
struct repeating_timer loop_timer;
bool loop_callback(struct repeating_timer *t)
{
	sem_release(&loop_sem);
	return(true);
}


int main()
{
	/* Initialize LED pin output */
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_put(PICO_DEFAULT_LED_PIN, true);									// Set LED on
	add_repeating_timer_ms(-LED_MS, led_callback, NULL, &led_timer);

	/* Initialize Pico power supply */
	gpio_init(23);
	gpio_set_dir(23, GPIO_OUT);
	gpio_put(23, true);														// Set PWM mode for less ripple

	for (int i=0; i<256; i++) 
		saw256[i] = (uint8_t)i;

	wfg_init();
	mon_init();																// Monitor shell on stdio
		
	/* A simple round-robin scheduler */
	sem_init(&loop_sem, 1, 1) ;	
	add_repeating_timer_ms(-LOOP_MS, loop_callback, NULL, &loop_timer);
	while (1) 										
	{
		sem_acquire_blocking(&loop_sem);
		mon_evaluate();														// Check monitor input
	}

    return 0;
}
