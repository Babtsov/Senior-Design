

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
void lcd_string(uint8_t *);
void lcd_init(void);
void adc_init(void);
long adc_read(void);



int main(void)
{

    lcdDdr |= (1 << lcdD7Bit) | (1 << lcdD6Bit) | (1 << lcdD5Bit) | (1 << lcdD4Bit) | (1 << lcdEBit) | (1 << lcdRSBit);                  
                 

    lcd_init(); 
   
	lcd_string("qazza");
	
	while(1)
	{

			
	}
	
    
}

void lcd_init(void)
{

    _delay_ms(100);                                

    lcdPort &= ~(1 << lcdRSBit);                 // RS low
    lcdPort &= ~(1 << lcdEBit);                 // E low

// LCD resets
    lcd_write(reset);                 
    _delay_ms(8);                           // 5 ms delay min

    lcd_write(reset);                 
    _delay_us(200);                       // 100 us delay min

    lcd_write(reset);                 
    _delay_us(200);                                 
 
    lcd_write(bit4Mode);               	//set to 4 bit mode
    _delay_us(50);                     // 40us delay min

    lcd_instruction(bit4Mode);   	 // set 4 bit mode
    _delay_us(50);                  // 40 us delay min

// display off
    lcd_instruction(off);        	// turn off display
    _delay_us(50);                                  

// Clear display
    lcd_instruction(clear);              // clear display 
    _delay_ms(3);                       // 1.64 ms delay min

// entry mode
    lcd_instruction(entryMode);          // this instruction shifts the cursor
    _delay_us(40);                      // 40 us delay min

// Display on
    lcd_instruction(on);          // turn on the display
    _delay_us(50);               // same delay as off
}


void lcd_string(uint8_t* string)
{
    volatile int i = 0;                             //while the string is not empty
    while (string[i] != 0)
    {
        lcd_char(string[i]);
        i++;
        _delay_us(50);                              //40 us delay min
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

// write the data
                                                   
    lcdPort |= (1 << lcdEBit);                   // E high
    _delay_us(1);                               // data setup 
    lcdPort &= ~(1 << lcdEBit);                // E low
    _delay_us(1);                             // hold data 
}
