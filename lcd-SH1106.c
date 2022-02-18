/*
 * lcd.c
 *
 * Created: Jan 2022
 * Author: Arjan te Marvelde
 * 
 * 
 * Driver for SSD1306 or SSD1309 or SH1106 OLED LCD module
 * It is an output-only MMI, so only write is supported
 * For now only 5x7 ASCII font, character mode, i.e. grid of 6x8 sized fields
 * Screen is 128x64 pixels, so that makes 21x8 character positions
 * Character position is the display location (X,Y) coordinate
 *
 * Functions are:
 * lcd_write()		Write ASCII to current position. (truncate at right border or next line?)
 * lcd_ctrl()		LCD settings, e.g. current cursor position, clear screen, ...
 * lcd_init()		Initializes interface, sets cursor to (0,0), etc
 * lcd_evaluate()	Can be called regularly to update LCD with canvas changes
 *
 * buffer contents:
 * control byte + following bytes
 * control byte = 0bxy000000, 
 *   x: single (1) or multiple (0) 
 *   y: data transfer(1) or control(0)
 *   0x00: burst command
 *   0x80: single command
 *   0x40: burst data
 *   0xc0: single data
 *
 * Display is 128 columns wide by 8 pages (=64 pixels) high. Each (column, page) is a segment byte. LSB is top and MSB is bottom.
 * So one 6x8 character box has 6 bytes:
 * +--+--+--+--+--+--+
 * |b0|b0|b0|b0|b0|b0|
 * +--+--+--+--+--+--+
 * |b1|b1|b1|b1|b1|b1|
 * +--+--+--+--+--+--+
 * |b2|b2|b2|b2|b2|b2|
 * +--+--+--+--+--+--+
 * |b3|b3|b3|b3|b3|b3|
 * +--+--+--+--+--+--+
 * |b4|b4|b4|b4|b4|b4|
 * +--+--+--+--+--+--+
 * |b5|b5|b5|b5|b5|b5|
 * +--+--+--+--+--+--+
 * |b6|b6|b6|b6|b6|b6|
 * +--+--+--+--+--+--+
 * |b7|b7|b7|b7|b7|b7|
 * +--+--+--+--+--+--+
 */
 
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "lcd.h"

//		Command definition		   DataBits	RST		Function
//		SH1106/SSD1306	           76543210  val 	
// ----------------------------------------------------------------------------------------------------------------------------
#define LCD_SETCOL_LO	0x00	// 0000XXXX	0x00	Sets 4 lower bits of column address of display RAM in register.
#define LCD_SETCOL_HI	0x10	// 0001XXXX	0x10	Sets 4 upper bits of column address of display RAM in register.
#define LCD_SETADDMODE	0x20	// 001000XX 0x22	Page address mode (SSD1306), 0x02 is page address mode
#define LCD_SETVREF		0x30	// 001100XX	0x32	Control the DC-DC voltage output value.
#define LCD_SETOFFSET	0x40	// 01XXXXXX	0x40	Specifies RAM display line for COM0.
#define LCD_SETCONTRAST	0x81	// 10000001			Sets Contrast  of the display.
								// XXXXXXXX	0x80	The chip has 256 contrast steps from 00 to FF.
#define LCD_SETADC		0xa0	// 1010000X	0xa0	The right (0) or left (1) rotation.
#define LCD_ALLWHITE	0xa4	// 1010010X	0x00	Selects normal display (0) or Entire Display ON (1).
#define LCD_SETREVERSE	0xa6	// 1010011X	0x00	Selects Normal (0) or Reverse (1) display.
#define LCD_SETMPXRATIO	0xa8	// 10101000			Sets multiplex mode to any multiplex ratio from 16 to 64.
								// **XXXXXX	0x3f	Ratio value (=X+1) (X<15 is invalid)
#define LCD_SETDCDC		0xad	// 10101101			Controls the DC-DC voltage (only use when display off)
#define LCD_SETDCDC_V	0x8a	// 1000101X	0x8b	DC-DC converter turned on when display ON (1) or DC-DC OFF (0).
#define LCD_CHARGEPUMP	0x8d	// 10001101			Control charge pump (Enable before display on, Disable after Display off)
								// 00010X00 0x10	Enable (1) or Disable (0)
#define LCD_SETDISPLAY	0xae	// 1010111X	0xae	Set Display ON (1) or OFF (0).
#define LCD_SETPAGE		0xb0	// 1011XXXX	0xb0	Specifies current RAM page address (0-15).
#define LCD_SETSCANDIR	0xc0	// 1100X***	0xc0	Scan direction COM[0 .. N-1] (0) or COM [N-1 .. 0] (1).
#define LCD_SETSTART	0xd3	// 11010011			Sets display start line to one of COM0-63.
								// **XXXXXX	0x00	[0 .. 63]
#define LCD_SETFREQ		0xd5	// 11010101			Sets the frequency of the internal display clocks.
								// XXXXYYYY	0x50	Frequency shift (=(X-5)*5%) | Divide ratio (=Y+1)
#define LCD_SETCHARGE	0xd9	// 11011001			Sets the duration of the dis-charge and pre-charge period.
								// XXXXYYYY	0x22	Discharge period [1..15] | Precharge period [1..15]
#define LCD_SETLAYOUT	0xda	// 11011010			Select the common signals pad layout (=display i/f)
#define LCD_SETALTSEQ	0x02	// 000X0010	0x12	Sequential (0) or Alternating (1)
#define LCD_SETVLEVEL	0xdd	// 11011011			Sets the common pad output voltage level at deselect stage.
								// XXXXXXXX	0x35	Vcom = 0.430 + X * 0.006415 * Vref
#define LCD_SETRMW_STA	0xe0	// 11100000			Read-Modify-Write start (see datasheet)
#define LCD_SETRMW_END	0xee	// 11101110			Read-Modify-Write end (see datasheet)
#define LCD_NOP			0xe3	// 11100011			No-operation command
#define LCD_GETSTATE	0x00	// XY***000			Busy | On/Off



#define I2C_SH1106			0x3C											// I2C address (0x3C)

#define LCD_DELAY			1000											// Screen refresh time
		
#define LCD_WIDTH			0x80											// Pixels or Columns
#define LCD_HEIGHT			0x40											// Pixels
#define LCD_PAGES			(LCD_HEIGHT/0x08)								// Character rows
#define LCD_ROWLEN			(LCD_WIDTH/0x06)								// Character cols

#define LCD_CTRLCMD			0x00											// Control byte for burst commands
#define LCD_CTRLDATA		0x40											// Control byte for burst data
#define LCD_CTRLSINGLE		0x80											// OR in case of single command or data


uint8_t lcd_canvas[LCD_HEIGHT/8][LCD_WIDTH+1];

uint8_t lcd_xch, lcd_ych;													// Location of display (0-20, 0-7)

	


// Write to canvas starting at (lcd_xch, lcd_ych)
void lcd_puts(char *buf, int font)
{
	uint8_t *sptr;
	char *bptr;
	int i,j;
	
	bptr = buf;
	while ((lcd_xch < LCD_ROWLEN) && (*bptr != 0))
	{
		switch(font)
		{
			case LCD_FONTSMALL:												// Arial, 6x8
				if ((*bptr<0x00) || (*bptr>0x7f)) break;
				sptr = &lcd_canvas[lcd_ych][1+lcd_xch*6];
				for (i=0; i<6; i++)
					*sptr++ = ASCII6x8[6*(*bptr)+i];
				bptr++;														// next char
				lcd_xch++;													// move cursor
				break;
			case LCD_FONTLARGE:												// Comic sans, 24x32
				if ((*bptr<0x20) || (*bptr>0x3f)) break;
				for (j=0; j<4; j++)
				{
					sptr = &lcd_canvas[lcd_ych+j][1+lcd_xch*6];
					for (i=0; i<24; i++)
						*sptr++ = ASCII24x32[96*((*bptr) - 0x20) + i + 24*j];
				}
				bptr++;														// next char
				lcd_xch+=4;													// move cursor
				break;
			case LCD_FONTUDJAT:												// Udjat logo, 32x32
				for (j=0; j<4; j++)
				{
					sptr = &lcd_canvas[lcd_ych+j][1+lcd_xch*6];
					for (i=0; i<32; i++)
						*sptr++ = UDJAT32x32[i + 32*j];
				}
				bptr++;														// next char
				lcd_xch+=4;													// move cursor
				break;
				
		}
	}
}

// Control functions
void lcd_ctrl(uint8_t cmd, uint8_t x, uint8_t y)
{
	int page;
	uint8_t txdata[16], *iptr;
	
	switch (cmd)
	{
		case LCD_GOTO:														// Set cursor position for next write
			lcd_xch = MIN(x, LCD_ROWLEN-1);									// Cursor X location (0..20)
			lcd_ych = MIN(y, LCD_PAGES-1);									// Cursor Y location (0..7)
			break;
			
		case LCD_UPDATE:													// Flush scratch RAM to display RAM
			txdata[0] = LCD_CTRLCMD;										// 0x00, control byte
			txdata[1] = LCD_SETCOL_LO | 0x00;								// Column 0
			txdata[2] = LCD_SETCOL_HI | 0x00;								//
			for (page=0; page<LCD_PAGES; page++)
			{
				txdata[3] = LCD_SETPAGE | page;								// Set row
				i2c_write_blocking(i2c0, I2C_SH1106, txdata, 4, false);		// Set position to (0,page) and write line
				i2c_write_blocking(i2c0, I2C_SH1106, &lcd_canvas[page][0], (uint16_t)(LCD_WIDTH+1), false);	
			}
			break;
			
		case LCD_CLRSCR:
			for (page=0; page<LCD_PAGES; page++)
				memset(&lcd_canvas[page][1], 0x00, LCD_WIDTH);
			lcd_xch = x; 
			lcd_ych = y;
			break;
	}
}

/** Initialize LCD and clear canvas **/
void lcd_init()
{
	int page;
	uint8_t txdata[16], *iptr;
	
	iptr = txdata;
	txdata[0] = LCD_CTRLCMD;												// 0x00, control byte
	txdata[1] = LCD_SETDISPLAY | 0x00;										// DISPLAY OFF
	txdata[2] = LCD_SETADC | 0x01;											// Left rotation
	txdata[3] = LCD_SETSCANDIR | 0x08;										// COM 63-0 scan direction
	txdata[4] = LCD_SETCONTRAST;											// Set contrast
	txdata[5] = 0xf0;														// 
	txdata[6] = LCD_SETREVERSE | 0x00;										// Normal video
	txdata[7] = LCD_CHARGEPUMP;												// Charge pump control
	txdata[8] = 0x14;														// Enable
	txdata[9] = LCD_SETDISPLAY | 0x01;										// DISPLAY ON
	
	i2c_write_blocking(i2c0, I2C_SH1106, txdata, 10, false);
	
	for (page=0; page<LCD_PAGES; page++)
		lcd_canvas[page][0] = LCD_CTRLDATA;									// First byte indicates data xfer
		
	lcd_ctrl(LCD_CLRSCR, 0, 0);												// Clear screen, go to (0,0)

	// Udjat logo in left upper corner
	txdata[0] = 0x20;														// Construct one char dummy string
	txdata[1] = 0x00;
	lcd_ctrl(LCD_GOTO, 0, 0);
	lcd_puts(txdata, 2);
	lcd_ctrl(LCD_UPDATE, 0, 0);	
}

/** Transfer canvas to LCD **/
void lcd_evaluate()
{
	lcd_ctrl(LCD_UPDATE, 0, 0);
}