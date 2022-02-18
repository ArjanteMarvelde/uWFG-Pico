/*
 * lcd.c
 *
 * Created: Jan 2022
 * Author: Arjan te Marvelde
 * 
 * Driver for SSD1327 based 128x128 display.
 * 
 * It is an output-only MMI, so only write is supported
 *
 * The interface functions are:
 * lcd_putc(int x, int y, uint8_t c, uint8_t *font);
 * lcd_puts(int x, int y, char *buf, uint8_t *font);
 * lcd_putg(int x, int y, uint8_t *bitmap);
 * lcd_clrscr(void);
 * lcd_init(void);
 * The parameters (x, y) determine upper left corner of object item, should be even numbers.
 * The font or bitmap object itself contains other parameters like (w, h) and the item data content.
 * (w, h) also should be even numbers.
 *
 * A font or bitmap object definition starts with:
 * - uint8_t item field width
 * - uint8_t item field height
 * - uint8_t first item number (e.g. character code)
 * - uint8_t last item number
 * followed by the item data, a nibble per pixel.
 *
 * Use of graphics functions requires a bitmap, where the same format as for fonts is used but there is only one character. 
 * A predefined bitmap is for example the Udjat logo, but user defined bitmap can also be dumped on display.
 * For filling the bitmap dynamically, several graphics functions are provided:
 *
 * I2C write sequence:
 * control byte + following bytes
 * control byte = 0bxy000000, where 
 *   x: single (1) or multiple (0) 
 *   y: data(1) or control(0)
 *
 * Display is 128 columns wide by 128 rows high. Each (col, row) is a grey value nibble [1-15]. 
 * The nibbles are organized in display RAM as follows:
 * +-----+-----+-----+-----+-----+-----+ ~ +-----+-----+
 * |L   0 H   0|L   1 H   1|L   2 H   2|   |L  63 H  63|
 * +-----+-----+-----+-----+-----+-----+ ~ +-----+-----+
 * |L  64 H  64|L  65 H  65|L  66 H  66|   |L 127 H 127|
 * +-----+-----+-----+-----+-----+-----+ ~ +-----+-----+
 * /     /     /     /     /     /     /   /     /     /
 * \     \     \     \     \     \     \   \     \     \
 * +-----+-----+-----+-----+-----+-----+ ~ +-----+-----+
 * |L8128 H8128|L8129 H8129|L8130 H8130|   |L8191 H8191|
 * +-----+-----+-----+-----+-----+-----+ ~ +-----+-----+
 *
 *
 * For writing Display RAM, first the conditions and start location are set (commands) then followed by a data burst.
 *
 */
 
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "lcd.h"

//		Command definition		   D	Function (reset values)
//		SSD1327 				   #
// ----------------------------------------------------------------------------------------------------------------------------
#define LCD_WINCOLADDR	0x15	// 2	Sets first and last column for window (0x00, 0x3F).
#define LCD_WINROWADDR	0x75	// 2	Sets first and last row for window (0x00, 0x7F).

#define LCD_SCR_RIGHT	0x26	// 7	Setup right-scrolling part of display (see below)
#define LCD_SCR_LEFT	0x27	// 7	Setup left-scrolling part of display
#define LCD_SCR_STOP	0x2e	// 0	Stop scrolling window
#define LCD_SCR_START	0x2f	// 0	Start scrolling window

#define LCD_CONTRAST	0x81	// 1	Sets Contrast of the display (0x7F).
#define LCD_REMAP		0xa0	// 1	Enables/Disables address remapping (0x00).
#define LCD_DSTARTLINE	0xa1	// 1	Sets display start line (0x00).
#define LCD_DOFFSET		0xa2	// 1	Sets display vertical offset (0x00).
#define LCD_MODENORM	0xa4	// 0	Display in normal mode
#define LCD_MODEWHITE	0xa5	// 0	Display all pixels white, greyscale=15
#define LCD_MODEBLACK	0xa6	// 0	Display all pixels black, greyscale=0
#define LCD_MODEINVERS	0xa7	// 0	Display all pixels inverted, greyscale=15-val
#define LCD_MUXRATIO	0xa8	// 1	Set ratio to X+1, X>14, (0x7f)
#define LCD_FASELECT	0xab	// 0	Select internal Vdd regulator when 1, external when 0 (0x01)

#define LCD_INACTIVE	0xae	// 0	Switches display to sleep mode
#define LCD_ACTIVE		0xaf	// 0	Switches display on, normal mode

#define LCD_PHASELEN	0xb1	// 1	High nibble phase2, low nibbel phase1 (0x74)
#define LCD_NOP1		0xb2	// 0	No operation
#define LCD_OSC_D_F		0xb3	// 1	Set oscillator divider (0x00)
#define LCD_GPIO		0xb5	// 1	(0x02)
#define LCD_PCPER2		0xb6	// 1	(0x04)
#define LCD_GS_TABLE	0xb8	// 15	Pulse width for GS levels 1..15, all unequal and value rising
#define LCD_GS_LINEAR	0xb9	// 0	Sets linear GS table (default)
#define LCD_NOP2		0xbb	// 0	No operation
#define LCD_PCLEVEL		0xbc	// 1	(0x05)
#define LCD_CDLEVEL		0xbe	// 1	(0x05)
#define LCD_FBSELECT	0xd5	// 1	(0x00)
#define LCD_CMDLOCK		0xfd	// 1	Lock OLED command interface (0x16) or unlock (0x12)


#define I2C_SSD1327		0x3c												// I2C address (0x3C)
#define LCD_CTRLCMD		0x00												// Control byte for burst commands
#define LCD_CTRLDATA	0x40												// Control byte for burst data
#define LCD_CTRLSINGLE	0x80												// OR in case of single command/data
#define LCD_CTRLMULTI	0x00												// OR in case of multibyte command/data


#define LCD_DELAY		1000												// Screen refresh time
		
#define LCD_WIDTH		0x80												// Pixels
#define LCD_HEIGHT		0x80												// Pixels


uint8_t txdata[1+LCD_WIDTH];												// Maximum transfer size, one line of data


/*
 * Set display active area (cursor) to (x,y) left upper corner and (width x height) size
 */
void lcd_cursor(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	txdata[0] = LCD_CTRLCMD | LCD_CTRLMULTI;								// Multiple command byte
	txdata[1] = LCD_WINCOLADDR;												// Set window columns
	txdata[2] = (x/2)&0x3f;													// left 
	txdata[3] = (((x+w)/2)-1)&0x3f;											// right 
	txdata[4] = LCD_WINROWADDR;												// Set window rows
	txdata[5] = (y)&0x7f;													// top
	txdata[6] = (y+h-1)&0x7f;												// bottom
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 7, false);				// Send commands
}

/*
 * Clear the display
 */
void lcd_clrscr(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	int i;
	
	lcd_cursor(x,y,w,h);													// Set window
	
	txdata[0] = LCD_CTRLDATA | LCD_CTRLMULTI;
	memset(&txdata[1], 0x00, w/2);											// Black line
	
	for (i=0; i<h; i++)
		i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 1+w/2, false);		// Clear one line
}


/* 
 * Output one character to location (x,y)
 * Inverse graphics if invert is true
 */
void lcd_putc(uint8_t x, uint8_t y, uint8_t c, uint8_t *font, bool invert)
{
	int i, w, h;
	uint8_t xorbyte;														// XOR value for memcpy
	uint8_t *srce, *dest;													// Pointers for memcpy
	
	if (!((c>=font[2]) && (c<=font[3]))) 									// Range check character code
		c=0;																// Not good: default on first
	else 
		c-=font[2];															// Good: shift down value

	w=font[0]; h=font[1];													// Retrieve character width and height
	if (((x+w)>LCD_WIDTH)||((y+h)>LCD_HEIGHT)) return;						// Out of range!
	lcd_cursor(x,y,w,h);													// Define window
	
	txdata[0] = LCD_CTRLDATA | LCD_CTRLMULTI ;								// Multiple data byte

	xorbyte = (invert?0xff:0x00);											// Optionally set inversion
	srce = &font[4 + (int)c*w*h/2];											// Initialize pointers
	dest = &txdata[1];
	for (i=0; i<w*h/2; i++)													// Copy data
		*dest++ = *srce++ ^ xorbyte;

	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 1+w*h/2, false);			// Send character 
}

/*
 * Output multiple characters, starting on (x,y)
 */
void lcd_puts(uint8_t x, uint8_t y, char *buf, uint8_t *font, bool invert)
{
	int len, i;
	
	len = strlen(buf);
	for (i=0; i<len; i++)
		lcd_putc(x+font[0]*i, y, (char)(buf[i]), font, invert);
}


/*
 * Dump bitmap graphic on screen
 * This is essentially lcd_putc(x, y, 0, bitmap): bitmap being a single character font...
 */
void lcd_putg(uint8_t x, uint8_t y, uint8_t *bitmap, bool invert)
{
	int w, h, i, j;
	uint8_t xorbyte = 0x00;													// XOR value for memcpy
	uint8_t *srce, *dest;													// Pointers for memcpy
	
	w=bitmap[0]; h=bitmap[1];												// Retrieve bitmap width and height
	if (((x+w)>LCD_WIDTH)||((y+h)>LCD_HEIGHT)) return;						// Out of range!
	lcd_cursor(x,y,w,h);													// Define window for writing
	
	if (invert) xorbyte = 0xff;												// Optionally set inversion
	txdata[0] = LCD_CTRLDATA | LCD_CTRLMULTI ;								// Multiple data byte
	for (i=0; i<h; i++)														// Split up graphic into lines
	{
		srce = &bitmap[4 + i*w/2];											// Initialize pointers
		dest = &txdata[1];
		for (j=0; j<w/2; j++)												// Copy data
			*dest++ = *srce++ ^ xorbyte;
		i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 1+w/2, false);		// Send graphic line 
	}
}

/*
 * Write a horizontal line on row y
 */
void lcd_hruler(uint8_t x, uint8_t y, uint8_t w)
{
	lcd_cursor(x,y,w,2);													// Just the line
	// range x, y and truncate w
	txdata[0] = LCD_CTRLDATA | LCD_CTRLMULTI;
	memset(&txdata[1], 0x88, w);											// White line		
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 1+w, false);				// Dump two lines
}

/*
 * Write a vertical line on col x and x+1
 */
void lcd_vruler(uint8_t x, uint8_t y, uint8_t h)
{
	lcd_cursor(x,y,2,h);													// Just the line
	// range x, y and truncate w
	txdata[0] = LCD_CTRLDATA | LCD_CTRLMULTI;
	memset(&txdata[1], 0x88, h);											// White line		
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 1+h, false);				// Dump two lines
}


/*
 * Initialize and clear display
 */
void lcd_init()
{
	int page;
	
	sleep_ms(1);

	txdata[0] = LCD_CTRLCMD | LCD_CTRLMULTI ;								// Multiple command byte
	txdata[1] = LCD_CMDLOCK;												// Unlock command interface
	txdata[2] = 0x12;
	txdata[3] = LCD_CTRLCMD | LCD_CTRLMULTI ;								// Multiple command byte
	txdata[4] = LCD_REMAP;													// Display upside-down
	txdata[5] = 0x53;														// So change GDRAM mapping
	txdata[6] = LCD_CTRLCMD | LCD_CTRLSINGLE ;								// Multiple command byte	
	txdata[7] = LCD_ACTIVE;													// DISPLAY ON
	txdata[8] = LCD_CTRLCMD | LCD_CTRLSINGLE ;								// Multiple command byte	
	txdata[9] = LCD_MODENORM;												// NORMAL MODE
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 10, false);
	
	// Output init flush screen
	txdata[0] = LCD_CTRLCMD | LCD_CTRLSINGLE;
	txdata[1] = LCD_INACTIVE;
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 2, false);				// Send commands

	lcd_clrscr(0,0,128,128);
	lcd_putg(0, 0, LCD_UDJAT128, true);
	
	txdata[0] = LCD_CTRLCMD | LCD_CTRLSINGLE;
	txdata[1] = LCD_ACTIVE;
	i2c_write_blocking(i2c0, I2C_SSD1327, txdata, 2, false);				// Send commands
}
