/*
 * hmi.c
 *
 * Created: Dec 2021
 * Author: Arjan te Marvelde
 * 
 * 
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

#include "hmi.h"
#include "wfgout.pio.h"
#include "gen.h"

