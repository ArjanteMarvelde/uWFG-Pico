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
#include "hardware/i2c.h"

#include "uWFG.h"
#include "gen.h"
#include "monitor.h"
#include "hmi.h"
#include "lcd.h"

#define I2C0_SDA		16
#define I2C0_SCL		17

/* 
 * LED TIMER definition and callback routine
 */
#define LED_MS		1000
struct repeating_timer led_timer;
volatile bool led_state;
bool led_callback(struct repeating_timer *t) 
{
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
	sem_reset(&loop_sem, 1);
	return(true);
}


int main()
{
	/* Overclock by factor 2 */
	set_sys_clock_khz(125000, false);
	sleep_ms(2);
	
	/* Initialize Pico power supply */
	gpio_init(23);
	gpio_set_dir(23, GPIO_OUT);
	gpio_put(23, true);														// Set PWM mode for less ripple

	/* Initialize LED pin output */
	gpio_init(PICO_DEFAULT_LED_PIN);
	gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
	gpio_put(PICO_DEFAULT_LED_PIN, true);									// Set LED on
	add_repeating_timer_ms(-LED_MS, led_callback, NULL, &led_timer);

	/* i2c0 initialisation at 400Khz. */
	i2c_init(i2c0, 400*1000);
	gpio_set_function(I2C0_SDA, GPIO_FUNC_I2C);
	gpio_set_function(I2C0_SCL, GPIO_FUNC_I2C);
	gpio_pull_up(I2C0_SDA);
	gpio_pull_up(I2C0_SCL);

	gen_init();
	lcd_init();
	hmi_init();
	mon_init();																// Monitor shell on stdio
		
	/* A simple round-robin scheduler */
	sem_init(&loop_sem, 1, 1) ;	
	add_repeating_timer_ms(-LOOP_MS, loop_callback, NULL, &loop_timer);		// Run every LOOP_MS
	while (1) 										
	{
		sem_acquire_blocking(&loop_sem);
		mon_evaluate();														// Check monitor input
		hmi_evaluate();
	}

    return 0;
}
