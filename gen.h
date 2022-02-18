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
#define OUTB	1															// Channel B indicator

#define GEN_MINBUFLEN		  20											// Minimum nr of byte samples
#define GEN_MAXBUFLEN		2000											// Maximum buffer size (byte samples)


extern float _fsys;

/*
 * The structure that defines the waveform input to wfg_play.
 * Note that the buffer needs to contain 4N bytes, i.e. needs to be 32bit aligned.
 * For high frequencies, the minimum buffer length is in the order of 32 bytes (buflen=8).
 */
typedef struct wfg
{
	uint8_t  *buf;															// Points to waveform buffer
	uint32_t  len;															// Buffer length in byte samples
	float     dur;															// Duration of buffer, in seconds
} wfg_t;

/* Initialize both channels */
void gen_init(void);

/* Play a waveform on the channel indicated by output */
void gen_play(int output, wfg_t *wave);


#endif