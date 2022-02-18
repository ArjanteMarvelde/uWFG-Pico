#ifndef _LCD_H_
#define _LCD_H_
/*
 * lcd.h
 *
 * Created: Jan 2022
 * Author: Arjan te Marvelde
 * 
 */

/* 
 * Font and Bitmap definitions 
 *    see lcdfont.c and lcdlogo.c
 */

#define LCD_6X8			ASCII6x8
#define LCD_8X12		ASCII8x12
#define LCD_12X16		ASCII12x16
#define LCD_SQR24X12	SQR24x12
#define LCD_SAW24X12	SAW24x12
#define LCD_TRI24X12	TRI24x12
#define LCD_SIN24X12	SIN24x12
#define LCD_PUL24X12	PUL24x12
#define LCD_UDJAT32		UDJAT32x32
#define LCD_UDJAT128	UDJAT128x128
#define LCD_CIR16X16	CIRCLE16x16
extern uint8_t 			ASCII6x8[];
extern uint8_t 			ASCII8x12[];
extern uint8_t 			ASCII12x16[];
extern uint8_t 			UDJAT32x32[];
extern uint8_t 			SQR24x12[];
extern uint8_t 			SAW24x12[];
extern uint8_t 			TRI24x12[];
extern uint8_t 			SIN24x12[];
extern uint8_t 			PUL24x12[];
extern uint8_t 			UDJAT128x128[];
extern uint8_t			CIRCLE16x16[];

/* API */
void lcd_putc(uint8_t x, uint8_t y, uint8_t c, uint8_t *font, bool invert);
void lcd_puts(uint8_t x, uint8_t y, char *buf, uint8_t *font, bool invert);
void lcd_putg(uint8_t x, uint8_t y, uint8_t *bitmap, bool invert);
void lcd_hruler(uint8_t x, uint8_t y, uint8_t w);
void lcd_vruler(uint8_t x, uint8_t y, uint8_t h);
void lcd_clrscr(uint8_t x, uint8_t y, uint8_t w, uint8_t h);
void lcd_init(void);

#endif