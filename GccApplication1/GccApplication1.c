#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>
#include <stdio.h>

#define LCD_DIR		DDRD 
#define LCD_PORT	PORTD          
#define LCD_E		PORTD2
#define LCD_RS		PORTD1

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

void LCD_send_upper_nibble(uint8_t);
void LCD_command(uint8_t);
void LCD_char(uint8_t);
void LCD_string(char[]);
void LCD_init(void);

void LCD_init(void)
{
	LCD_DIR |= 0x7E; // Data: PORTD6..PORTD3, E: PORTD2, RS: PORTD1 
    LCD_command(reset);                 
    LCD_command(bit4Mode);
    LCD_command(off);                               
    LCD_command(clear);
    LCD_command(entryMode);
    LCD_command(on);
}

void LCD_string(char string[])
{
    for (int i = 0; string[i] != 0; i++)
    {
        LCD_char(string[i]);
    }
}

void LCD_char(uint8_t data)
{
	LCD_PORT |= (1 << LCD_RS);
	LCD_PORT &= ~(1 << LCD_E);
	LCD_send_upper_nibble(data);
	_delay_us(10);
	LCD_send_upper_nibble(data << 4);
	_delay_us(10);
}

void LCD_command(uint8_t cmd)
{
	LCD_PORT &= ~(1 << LCD_RS); 
	LCD_PORT &= ~(1 << LCD_E); 
	LCD_send_upper_nibble(cmd); 
	_delay_us(10);
	LCD_send_upper_nibble(cmd << 4);
	_delay_ms(10);
}

void LCD_send_upper_nibble(uint8_t byte)
{
	LCD_PORT &= ~0x78; // Save the data of the LCD port (& set nibble to 0)
	LCD_PORT |= byte >> 1 & 0x78; // set the nibble (requires shifting)
	LCD_PORT |= (1 << LCD_E);
	LCD_PORT &= ~(1 << LCD_E);
}

int main(void)
{
	LCD_init();
	LCD_string("Benjamin");
	while(1)
	{
	}
	
	return 0;
}