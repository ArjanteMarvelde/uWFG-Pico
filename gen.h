#ifndef __GEN_H__
#define __GEN_H__
/* 
 * gen.h
 *
 * Created: Dec 2021
 * Author: Arjan te Marvelde
 *
 * See gen.c for more information 
 */
 
#define OUTA	0															// Channel A indicator
#define PINA	0															// LSB pin Channel A
#define OUTB	1															// Channel B indicator
#define PINB	8															// LSB pin Channel B


typedef struct wfg
{
	uint32_t *buf;															// Points to waveform buffer
	uint32_t len;															// Buffer length in 32 bit words
	float    freq;															// Buffer repetition rate
} wfg_t;

/* Initialize channel indicated by output */
void wfg_init(void);

/* Play a waveform on the channel indicate by output */
void wfg_play(int output, wfg_t *wave);


#endif