
#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>

/*************** LCD Configuration ****************/

#define lcdDdr		DDRD		//Data direction 
#define lcdPort		PORTD		//PortD
#define lcdD7Bit	PORTD6		//LCD D7 (pin 14) -> PORTD6
#define lcdD6Bit	PORTD5		//LCD D6 (pin 13) -> PORTD5
#define lcdD5Bit	PORTD4		//LCD D5 (pin 12) -> PORTD4
#define lcdD4Bit	PORTD3		//LCD D4 (pin 11) -> PORTD3               
#define lcdEBit		PORTD2		//LCD E (pin 6)   -> PORTD2
#define lcdRSBit	PORTD1		//LCD RS (pin 4)  -> PORTD1

#define lineOne			0x00                // Line 1
#define lineTwo			0x40                // Line 2
#define clear           0b00000001          // clears all characters
#define home            0b00000010          // returns cursor to home
#define entryMode       0b00000110          // moves cursor from left to right
#define off      		0b00001000          // LCD off
#define on       		0b00001100          // LCD on
#define reset   		0b00110000          // reset the LCD
#define bit4Mode 		0b00101000          // we are using 4 bits of data
#define setCursor       0b10000000          //sets the position of cursor

void lcd_write(uint8_t);
void lcd_instruction(uint8_t);
void lcd_char(uint8_t);
void lcd_string(char[]);
void lcd_init(void);
void adc_init(void);
long adc_read(void);


int main(void)
{
    lcd_init(); 
	lcd_string("judge me free");	
	while(1)
	{
	}
	
    return 0;
}

void lcd_init(void)
{
	lcdDdr |= (1 << lcdD7Bit) | (1 << lcdD6Bit) | (1 << lcdD5Bit) | (1 << lcdD4Bit) | (1 << lcdEBit) | (1 << lcdRSBit);
    _delay_ms(100);                                
    lcdPort &= ~(1 << lcdRSBit);
    lcdPort &= ~(1 << lcdEBit);
    lcd_write(reset);                 
    _delay_ms(8);
    lcd_write(reset);                 
    _delay_us(200);
    lcd_write(reset);                 
    _delay_us(200);                                 
    lcd_write(bit4Mode);
    _delay_us(50);
    lcd_instruction(bit4Mode);
    _delay_us(50);
    lcd_instruction(off);
    _delay_us(50);                                  
    lcd_instruction(clear);
    _delay_ms(3);
    lcd_instruction(entryMode);
    _delay_us(40);
    lcd_instruction(on);
    _delay_us(50);
}


void lcd_string(char string[])
{
    for (int i = 0; string[i] != 0; i++)
    {
        lcd_char(string[i]);
        _delay_us(50);
    }
}


void lcd_char(uint8_t data)
{
    lcdPort |= (1 << lcdRSBit);                 // RS high
    lcdPort &= ~(1 << lcdEBit);                // E low
    lcd_write(data);                          // write the upper four bits of data
    lcd_write(data << 4);                    // write the lower 4 bits of data
}


void lcd_instruction(uint8_t instruction)
{
    lcdPort &= ~(1 << lcdRSBit);                // RS low
    lcdPort &= ~(1 << lcdEBit);                // E low
    lcd_write(instruction);                   // write the upper 4 bits of data
    lcd_write(instruction << 4);             // write the lower 4 bits of data
}


void lcd_write(uint8_t byte)
{
    lcdPort &= ~(1 << lcdD7Bit);                        // assume that data is '0'
    if (byte & 1 << 7) lcdPort |= (1 << lcdD7Bit);     // make data = '1' if necessary

    lcdPort &= ~(1 << lcdD6Bit);                        // repeat for each data bit
    if (byte & 1 << 6) lcdPort |= (1 << lcdD6Bit);

    lcdPort &= ~(1 << lcdD5Bit);
    if (byte & 1 << 5) lcdPort |= (1 << lcdD5Bit);

    lcdPort &= ~(1 << lcdD4Bit);
    if (byte & 1 << 4) lcdPort |= (1 << lcdD4Bit);

                               
    lcdPort |= (1 << lcdEBit);                   // E high
    _delay_us(1);                               // data setup 
    lcdPort &= ~(1 << lcdEBit);                // E low
    _delay_us(1);                             // hold data 
}
