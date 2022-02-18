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
#include "gen.h"
#include "lcd.h"

/** Some generic identifiers **/
// Mode strings
#define HMI_SQR			0
#define HMI_TRI			1
#define HMI_SAW			2
#define HMI_SIN			3
#define HMI_PUL			4
#define HMI_NMODE		5
uint8_t *hmi_chmode[HMI_NMODE]  = { LCD_SQR24X12, LCD_TRI24X12, LCD_SAW24X12, LCD_SIN24X12, LCD_PUL24X12};

// Unit strings
#define HMI_USEC		0
#define HMI_MSEC		1
#define HMI_NUNIT		2
uint8_t *hmi_chdur[HMI_NUNIT] = {"usec", "msec"};

// Pulse parameters
#define HMI_DUTY		0
#define HMI_RISE		1
#define HMI_FALL		2
#define HMI_NPUL		3													// Additional Pulse parameters
uint8_t *hmi_chpul[HMI_NPUL] = {"Duty", "Rise", "Fall"};


/** Channel A/B wave definition structures **/
extern uint8_t sine[GEN_MAXBUFLEN];											// Full sine waveform
typedef struct
{
	int 	mode;															// Waveform type
	float 	time;															// Duration, in seconds
	int 	duty;															// Duty cycle, percentage of duration
	int		rise;															// Rise time, percentage of duration
	int 	fall;															// Fall time, percentage of duration
} ch_t;
ch_t hmi_chdef[2];
uint8_t hmi_wave[GEN_MAXBUFLEN];											// Scratch buffer for waveform samples

// Generate waveform samples in scratch buffer
// Division factor should end-up above 4 to get a <0.1% deviation
// so: fsample < fsys/4, implying a bufferlength of maximum time*fsys/4.
// This is about 50 samples per usec, but this length should be minimized too
// which means that for times smaller than a few usec the frequency will be less accurate.
// Simple waveforms can be calculated, for Sine wave there is a lookup-table
void hmi_genwave(int ch)
{
	int i;
	uint32_t d, r, f;
	float step;
	wfg_t wf;
	
	// Calculate optimum nr of samples
	wf.len = (uint32_t)((_fsys/1) * hmi_chdef[ch].time);					// Calculate required nr of samples
	wf.len &= ~3;															// Multiple of 4 bytes
	if (wf.len<20) wf.len = 20;												// Minimum size
	if (wf.len>GEN_MAXBUFLEN) wf.len = GEN_MAXBUFLEN;						// Maximum size
	
	// Fill array

	switch (hmi_chdef[ch].mode)
	{
	case HMI_SQR:
		memset(&hmi_wave[0],0xff, wf.len/2); 								// High half samples
		memset(&hmi_wave[wf.len/2],0x00, wf.len/2);							// Low half samples
		break;
	case HMI_TRI:
		step = 255.0/(wf.len/2);											// Calculate slope (step per sample)
		for (i=0; i<wf.len/2; i++)
		{
			hmi_wave[i] = (uint8_t)(i*step);								// Samples way up
			hmi_wave[i+wf.len/2] = 255 - hmi_wave[i];						// Samples way down
		}
		break;
	case HMI_SAW:
		step = 255.0/(wf.len);												// Calculate slope (step per sample)
		for (i=0; i<wf.len; i++)
			hmi_wave[i] = (uint8_t)(i*step);								// Samples rising side
		break;
	case HMI_SIN:
		step = (float)GEN_MAXBUFLEN/wf.len;									// Step to next sine index
		for (i=0; i<wf.len; i++)
			hmi_wave[i] = sine[(int)(i*step)];								// Truncate i*step to get index
		break;
	case HMI_PUL:
		d = hmi_chdef[ch].duty * wf.len / 100;								// Fraction of duty cycle samples
		r = hmi_chdef[ch].rise * wf.len / 100;								// Fraction of rising flank samples
		f = hmi_chdef[ch].fall * wf.len / 100;								// Fraction of falling flank samples
		step = 255.0/r;														// Calculate rise slope (step per sample)
		for (i=0; i<r; i++)
			hmi_wave[i] = (uint8_t)(i*step);								// Samples way up
		for (i=r; i<d; i++)
			hmi_wave[i] = 0xff;												// High samples
		step = 255.0/f;														// Calculate fall slope (step per sample)
		for (i=0; i<f; i++)
			hmi_wave[i+d] = 0xff-i*step;									// Samples way down
		for (i=d+f; i<wf.len; i++)
			hmi_wave[i] = 0x00;												// Low samples
		break;
	}
	
	// Play waveform
	wf.buf = hmi_wave;
	wf.dur = hmi_chdef[ch].time;
	gen_play(ch, &wf);
}

/** Channel setting menu **/
#define HMI_NPAR		8													// Normal channel parameters
#define HMI_NPPAR		6													// Additional Pulse parameters

// Channel parameter scratch and write function
typedef struct
{
	uint8_t	val;															// Parameter value
	uint8_t max;															// Parameter range (0,max-1)
	uint8_t	**str;															// String representation (NULL if false)
	uint8_t	x;																// Field upperleft x
	uint8_t	y;																// Field upperleft y
} par_t;
par_t hmi_chpar[HMI_NPAR+HMI_NPPAR] =
{
	{0, HMI_NMODE-1, hmi_chmode,  8, 52}, 									// Mode string/graph
	{0,           9, NULL,       46, 54}, 									// Time digits (000.000 - 999.999)
	{0,           9, NULL,       54, 54}, 
	{0,           9, NULL,       60, 54}, 
	{0,           9, NULL,       72, 54}, 
	{0,           9, NULL,       78, 54}, 
	{0,           9, NULL,       84, 54}, 
	{0, HMI_NUNIT-1, hmi_chdur,  92, 54},									// Time unit string
	{0,           9, NULL,       24, 76}, 									// Duty cycle % digits (00-99)
	{0,           9, NULL,       30, 76}, 
	{0,           9, NULL,       66, 76},  									// Rise time % digits (00-99)
	{0,           9, NULL,       72, 76}, 
	{0,           9, NULL,      108, 76},  									// Fall time % digits (00-99)
	{0,           9, NULL,      114, 76}
};


// Write a certain parameter according to par_t struct
void hmi_writepar(int i, bool invert)
{
	if (i>=(HMI_NPAR+HMI_NPUL*2)) return;									// Range check
	if (hmi_chpar[i].str == NULL)											// Character?
		lcd_putc(hmi_chpar[i].x,hmi_chpar[i].y,'0'+hmi_chpar[i].val,LCD_6X8,invert);
	else if (i==0)															// or Graphic?
		lcd_putg(hmi_chpar[i].x,hmi_chpar[i].y,hmi_chpar[i].str[hmi_chpar[i].val],invert);
	else																	// else String
		lcd_puts(hmi_chpar[i].x,hmi_chpar[i].y,hmi_chpar[i].str[hmi_chpar[i].val],LCD_6X8,invert);
}

// Write channel setting at display bottom
void hmi_writech(int ch)
{
	int y = (ch?112:98);
	lcd_clrscr(0,y,128,12);
	lcd_puts(  4, y  , (ch?"B:":"A:"), LCD_8X12, false);	
	lcd_putg( 20, y  , hmi_chmode[hmi_chpar[0].val], false);
	lcd_putc( 54, y+2, '0'+hmi_chpar[1].val, LCD_6X8, false);
	lcd_putc( 60, y+2, '0'+hmi_chpar[2].val, LCD_6X8, false);
	lcd_putc( 66, y+2, '0'+hmi_chpar[3].val, LCD_6X8, false);
	lcd_putc( 72, y+2, '.', LCD_6X8, false);
	lcd_putc( 78, y+2, '0'+hmi_chpar[4].val, LCD_6X8, false);
	lcd_putc( 84, y+2, '0'+hmi_chpar[5].val, LCD_6X8, false);
	lcd_putc( 90, y+2, '0'+hmi_chpar[6].val, LCD_6X8, false);
	lcd_puts( 98, y+2, hmi_chdur[hmi_chpar[7].val], LCD_6X8, false);
}

// Init scratch from channel def
void hmi_initch(int ch, int par)
{
	ch_t *chdef;
	uint32_t dur;
	char s[16];
	int i;

	chdef = &hmi_chdef[ch&1];
	
	if (chdef->time < 1.0e-3)												// Range is either 999.999 msec or usec
	{
		hmi_chpar[7].val = HMI_USEC;
		dur = (uint32_t)(chdef->time*1e9);
	}
	else if (chdef->time < 1.0)
	{
		hmi_chpar[7].val = HMI_MSEC;
		dur = (uint32_t)(chdef->time*1e6);
	}
	hmi_chpar[6].val = dur%10; dur = dur/10;								// so only 6 digits are relevant
	hmi_chpar[5].val = dur%10; dur = dur/10;
	hmi_chpar[4].val = dur%10; dur = dur/10;
	hmi_chpar[3].val = dur%10; dur = dur/10;
	hmi_chpar[2].val = dur%10; dur = dur/10;
	hmi_chpar[1].val = dur%10;
	hmi_chpar[0].val = chdef->mode;
	
	hmi_chpar[13].val = chdef->fall%10;										// For some waveforms this is meaningless
	hmi_chpar[12].val = chdef->fall/10;
	hmi_chpar[11].val = chdef->rise%10;
	hmi_chpar[10].val = chdef->rise/10;
	hmi_chpar[ 9].val = chdef->duty%10;
	hmi_chpar[ 8].val = chdef->duty/10;

	if ((par<0)||(par>=HMI_NPAR+HMI_NPPAR)) return;
	
	// Initialize display layout of scratch area
	lcd_clrscr(32,0,96,32);													// Clean title area
	lcd_puts(44,8,ch?"Chan.B":"Chan.A",LCD_12X16,false);					// Write title
	lcd_hruler(44,24,72);
	lcd_clrscr(0,32,128,62);												// Clean scratch area
	lcd_puts(  4, 36, "Mode",LCD_8X12,false);								// Channel parameter captions
	lcd_puts( 48, 36, "Time",LCD_8X12,false);
	lcd_putc( 66, 54, '.', LCD_6X8, false);
	lcd_puts(  2, 76, "Dut", LCD_6X8, false);
	lcd_puts( 44, 76, "Ris", LCD_6X8, false);
	lcd_puts( 86, 76, "Fal", LCD_6X8, false);
	for (i=0;i<HMI_NPAR+HMI_NPPAR;i++) 
			hmi_writepar( i, (i==par));										// Channel parameter values

	return;
}

// Save scratch to channel definition, after range check
void hmi_exitch(int ch)
{
	ch_t *chdef;
	uint32_t val;
	char s[16];

	chdef = &hmi_chdef[ch&1];
	
	chdef->mode = hmi_chpar[0].val;
	val = hmi_chpar[1].val;
	val = val*10 + hmi_chpar[2].val;
	val = val*10 + hmi_chpar[3].val;
	val = val*10 + hmi_chpar[4].val;
	val = val*10 + hmi_chpar[5].val;
	val = val*10 + hmi_chpar[6].val;
	if (hmi_chpar[7].val == HMI_USEC)
		chdef->time = (val+1) * 1.0e-9;
	else if (hmi_chpar[7].val == HMI_MSEC)
		chdef->time = (val+1) * 1.0e-6;
	
	// TBD Check range
	
	// Store shape scratch values
	switch (hmi_chdef[ch].mode)
	{
	case HMI_SQR:
		hmi_chdef[ch].duty = 50; hmi_chdef[ch].rise =  1; hmi_chdef[ch].fall =  1;
		break;
	case HMI_TRI:
		hmi_chdef[ch].duty = 50; hmi_chdef[ch].rise = 50; hmi_chdef[ch].fall = 50;
		break;
	case HMI_SAW:
		hmi_chdef[ch].duty = 99; hmi_chdef[ch].rise = 99; hmi_chdef[ch].fall = 1;
		break;
	case HMI_SIN:
		hmi_chdef[ch].duty = 50; hmi_chdef[ch].rise =  0; hmi_chdef[ch].fall = 0;
		break;
	case HMI_PUL:
		hmi_chdef[ch].duty = 10*hmi_chpar[ 8].val + hmi_chpar[ 9].val;
		hmi_chdef[ch].rise = 10*hmi_chpar[10].val + hmi_chpar[11].val;
		if (hmi_chdef[ch].rise>hmi_chdef[ch].duty) hmi_chdef[ch].rise=hmi_chdef[ch].duty;
		hmi_chdef[ch].fall = 10*hmi_chpar[12].val + hmi_chpar[13].val;
		if (hmi_chdef[ch].fall>100-hmi_chdef[ch].duty) hmi_chdef[ch].rise=100-hmi_chdef[ch].duty;
		break;
	}
	return;
}


/** Menu state machine **/
// The three menu selection keys (TOP, MID, BOT) choose the menu to enter.
// Pressing the menu selection key again re-enters the menu, rejecting all changes.
// Changes have to be committed by the CENTER key.
// The active menu context determines how the navigation keys are interpreted.

// Key definitions
#define HMI_NOKEY		0x00
#define HMI_BOT			0x01
#define HMI_MID			0x02
#define HMI_TOP			0x04
#define HMI_CENTER		0x08
#define HMI_DOWN		0x10
#define HMI_RIGHT		0x20
#define HMI_UP			0x40
#define HMI_LEFT		0x80
uint8_t	keystat;

// Menu parameters
#define HMI_M_LCR		0x01
#define HMI_M_CHA		0x02
#define HMI_M_CHB		0x03
int hmi_menu = HMI_M_LCR;													// Current active menu
int par;																	// Current active menu parameter
int channel;																// Current active channel

void hmi_lcrmenu(int key)
{
}

void hmi_chmenu(int key)
{
	switch(key)
	{
	case HMI_UP:
		hmi_chpar[par].val = (hmi_chpar[par].val<hmi_chpar[par].max)?hmi_chpar[par].val+1:0;
		hmi_writepar( par, true);
		break;
	case HMI_DOWN:
		hmi_chpar[par].val = (hmi_chpar[par].val>0)?hmi_chpar[par].val-1:hmi_chpar[par].max;
		hmi_writepar( par, true);
		break;
	case HMI_LEFT:
		hmi_writepar( par, false);
		par = (par>0)?par-1:par;
		hmi_writepar( par, true);		
		break;
	case HMI_RIGHT:
		hmi_writepar( par, false);
		if (hmi_chpar[0].val == HMI_PUL)									// Only change shape when Pulse WF
			par = (par<HMI_NPAR+HMI_NPPAR-1)?par+1:par;
		else
			par = (par<HMI_NPAR-1)?par+1:par;
		hmi_writepar( par, true);		
		break;
	case HMI_CENTER:
		hmi_exitch(channel);												// Save scratch, after range check
		hmi_initch(channel, par);											// Re-initialize scratch
		hmi_genwave(channel);												// Generate and start waveform
		hmi_writech(channel);												// Write current channel setting
		break;																// Initialize scratch area
	}
}

/** Keypad interface **/
#define I2C_PCF8574		0x27
void hmi_handler(int key)
{
	switch(key)																// Process menu selections
	{
	case HMI_TOP:															// Top button => LCR measurement
		hmi_menu = HMI_M_LCR;
		par = 0;															// Initialize parameter nr
		lcd_clrscr(32,0,96,32);												// Clean title area
		lcd_putg(55,8,LCD_CIR16X16,false);									// Write title, 3 circles
		lcd_putg(75,8,LCD_CIR16X16,false);
		lcd_putg(95,8,LCD_CIR16X16,false);
		lcd_clrscr(0,32,128,62);											// Clean scratch area
		break;
	case HMI_MID:															// Middle button => channel A
		hmi_menu = HMI_M_CHA;
		par = 0;															// Initialize parameter nr
		channel = 0;
		hmi_initch(channel, par);											// Init scratch
		break;
	case HMI_BOT:															// Bottom button => channel B
		hmi_menu = HMI_M_CHB;
		par = 0;															// Initialize parameter nr
		channel = 1;
		hmi_initch(channel, par);											// Init scratch
		break;
	}
	if (hmi_menu == HMI_M_LCR)
		hmi_lcrmenu(key);
	else
		hmi_chmenu(key);
}


/** Called from main loop at regular intervals **/
void hmi_evaluate()
{
	uint8_t rxdata[4];
	uint8_t key;
	static bool firsttime = true;

	// Get key status
	i2c_read_blocking(i2c0, I2C_PCF8574, rxdata, 1, false);					// Poll PCF8574
	key = rxdata[0] ^ 0xff;													// Take XOR for selection
	if (key == keystat) return;												// No change: so bail out
	if (firsttime) 															// Initialize screen
	{ 
		lcd_clrscr(0,0,128,128);
		lcd_putg(0, 0, LCD_UDJAT32, false);
		lcd_hruler(0,  94, 128);
		hmi_initch(0, -1); hmi_writech(0);
		hmi_initch(1, -1); hmi_writech(1);
		lcd_hruler(0, 126, 128);
		firsttime = false; 
	}
	hmi_handler(key);
	keystat = key;															// Remember this key event
}

void hmi_init()
{
	uint8_t rxdata[4];
	
	// Get key status
	i2c_read_blocking(i2c0, I2C_PCF8574, rxdata, 1, false);					// Get PCF8574 byte
	keystat = rxdata[0] ^ 0xff;												// Initialize keystat
	hmi_chdef[0].mode = HMI_SQR;											// Waveform type
	hmi_chdef[0].time = 1.001e-6f;											// Duration, in seconds
	hmi_chdef[0].duty = 50;													// Duty cycle, percentage of duration
	hmi_chdef[0].rise = 1;													// Rise time, percentage of duration
	hmi_chdef[0].fall = 1;													// Fall time, percentage of duration
	hmi_chdef[1].mode = HMI_TRI;											// Waveform type
	hmi_chdef[1].time = 1.001e-6f;											// Duration, in seconds
	hmi_chdef[1].duty = 50;													// Duty cycle, percentage of duration
	hmi_chdef[1].rise = 50;													// Rise time, percentage of duration
	hmi_chdef[1].fall = 50;													// Fall time, percentage of duration
}