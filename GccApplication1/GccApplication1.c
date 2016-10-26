#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LCD_DIR     DDRA
#define LCD_PORT    PORTA
#define LCD_E       PORTA2
#define LCD_RS      PORTA1

#define lineOne     0x00
#define lineTwo     0x40
#define clear       0b00000001
#define home        0b00000010
#define setCursor   0b10000000

void LCD_send_upper_nibble(uint8_t);
void LCD_command(uint8_t);
void LCD_char(uint8_t);
void LCD_string(char[]);
void LCD_init(void);

void LCD_init(void) {
    LCD_DIR |= 0x7E; // Data: PORTD6..PORTD3, E: PORTD2, RS: PORTD1
    LCD_command(0x33);
    LCD_command(0x32);
    LCD_command(0x2C);
    LCD_command(0x0C);
    LCD_command(0x01);
}

void LCD_string(char string[]) {
    for (int i = 0; string[i] != 0; i++) {
        LCD_char(string[i]);
    }
}

void LCD_char(uint8_t data) {
    LCD_PORT |= (1 << LCD_RS);
    LCD_PORT &= ~(1 << LCD_E);
    LCD_send_upper_nibble(data);
    _delay_us(10);
    LCD_send_upper_nibble(data << 4);
    _delay_us(10);
}

void LCD_command(uint8_t cmd) {
    LCD_PORT &= ~(1 << LCD_RS); 
    LCD_PORT &= ~(1 << LCD_E); 
    LCD_send_upper_nibble(cmd); 
    _delay_us(10);
    LCD_send_upper_nibble(cmd << 4);
    _delay_ms(10);
}

void LCD_send_upper_nibble(uint8_t byte) {
    LCD_PORT &= ~0x78; // Save the data of the LCD port (& set nibble to 0)
    LCD_PORT |= byte >> 1 & 0x78; // set the nibble (requires shifting)
    LCD_PORT |= (1 << LCD_E);
    LCD_PORT &= ~(1 << LCD_E);
}

#define BAUDRATE 25 // baud rate: 2400 (see pg. 168)
void UART_init(void) {
    PORTD |= 0x01; // Enable transmitter
    UBRRH = (BAUDRATE>>8);
    UBRRL = BAUDRATE;
    UCSRB = (1<<TXEN)|(1<<RXEN);
    UCSRC = (1<<URSEL)|(1<<UCSZ0)|(1<<UCSZ1);   // 8bit data format 1 parity
    UCSRB |= (1 << RXCIE); 
}

void UART_send(unsigned char data) {
    while (!( UCSRA & (1<<UDRE)));
    UDR = data;
}

unsigned char UART_get_char(void) {
    while(!(UCSRA) & (1<<RXC));
    return UDR;
}

#define CREADER_BUFF_SIZE 15		// card reader buffer size
struct {
    volatile char ID_str[CREADER_BUFF_SIZE + 1]; //extra char for null terminator
    volatile uint8_t index; // ptr to an unoccupied 
    volatile bool locked; // is the buffer ready to be consumed?
} creader_buff;


inline void waitfor_creader_buff(void) {
    while (!creader_buff.locked); // wait until buffer is locked
}

inline void release_creader_buff(void) {
    creader_buff.locked = false;
}

ISR(USART_RXC_vect)
{
    char c = UART_get_char();
    if (!creader_buff.locked) { // modify buffer if not locked
        creader_buff.ID_str[creader_buff.index++] = c;
        if (creader_buff.index >= 12) {
            creader_buff.index = 0;
            creader_buff.locked = true; // lock the buffer so it won't be modified until consumed
        }
    }
    UART_send(c);
}

struct card {
    char id[CREADER_BUFF_SIZE];
} cards[2];

void scan_cards(void) {
    for (int i = 0; i < 2; i++) {
        LCD_command(home);
        LCD_string("Scan card ");
        LCD_char(i + '1');
        LCD_string(":");
        waitfor_creader_buff();
        strcpy(cards[i].id,(char *)creader_buff.ID_str);
        LCD_command(setCursor | lineTwo);
        LCD_string(cards[i].id);
        release_creader_buff();
    }
}
int find_card(char * str) {
    for (int i = 0; i < 2; i++) {
        if (strcmp(cards[i].id, str) == 0) {
            return i;
        }
    }
    return -1;
}

int main(void) {
    LCD_init();
    UART_init();
    sei();
    scan_cards();
    LCD_command(clear);
    while(1) {
        waitfor_creader_buff();
        LCD_command(clear);
        LCD_command(home);
        int card_index = find_card((char *)creader_buff.ID_str);
        if (card_index >= 0) {
            LCD_string("card ");
            LCD_char(card_index + '1');
            LCD_string(" detected!");
        } else {
            LCD_string("invalid card?");
        }
        release_creader_buff();
    }
    return 0;
}
