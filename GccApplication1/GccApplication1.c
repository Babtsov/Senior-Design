#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>

#define CARD_COUNT          2
#define CREADER_BUFF_SIZE   12
#define BAUDRATE            25      // baud rate: 2400 (see pg. 168)
#define LCD_DIR             DDRA
#define LCD_PORT            PORTA
#define LCD_E               PORTA2
#define LCD_RS              PORTA1

struct {
    char id[CREADER_BUFF_SIZE + 1];
    volatile uint16_t timeout;
} cards[CARD_COUNT];

struct {
    volatile char ID_str[CREADER_BUFF_SIZE + 1]; //extra char for null terminator
    volatile uint8_t index; // pointer to an unoccupied slot
    volatile bool locked; // buffer is locked from modifications by the ISR
} creader_buff;

/************************************************************************/
/* LCD Functions                                                        */
/* For LCD commands, see: http://www.dinceraydin.com/lcd/commands.htm   */
/************************************************************************/
#define setCursor   0x80
#define lineOne     0x00
#define lineTwo     0x40
#define clear       0x01
#define home        0x02
#define moveLeft    0x10
#define moveRight   0x14
#define cursorOn    0x0E
#define cursorOff   0x0C

void LCD_init(void);
void LCD_send_upper_nibble(uint8_t);
void LCD_command(uint8_t);
void LCD_char(uint8_t);
void LCD_string(char[]);
void LCD_substring(char[],int,int);
void LCD_uint(uint16_t);

void LCD_init(void) {
    LCD_DIR |= 0x7E; // Data: PORTD6..PORTD3, E: PORTD2, RS: PORTD1
    _delay_ms(150); // wait until LCD's voltage is high enough
    LCD_command(0x33);
    LCD_command(0x32);
    LCD_command(0x2C);
    LCD_command(0x0C);
}
void LCD_send_upper_nibble(uint8_t byte) {
    LCD_PORT &= ~0x78; // Save the data of the LCD port (& set nibble to 0)
    LCD_PORT |= byte >> 1 & 0x78; // set the nibble (requires shifting)
    LCD_PORT |= (1 << LCD_E);
    LCD_PORT &= ~(1 << LCD_E);
}
void LCD_command(uint8_t cmd) {
    LCD_PORT &= ~(1 << LCD_RS);
    LCD_PORT &= ~(1 << LCD_E);
    LCD_send_upper_nibble(cmd);
    _delay_us(10);
    LCD_send_upper_nibble(cmd << 4);
    _delay_ms(5);
}
void LCD_char(uint8_t data) {
    LCD_PORT |= (1 << LCD_RS);
    LCD_PORT &= ~(1 << LCD_E);
    LCD_send_upper_nibble(data);
    _delay_us(10);
    LCD_send_upper_nibble(data << 4);
    _delay_us(10);
}
void LCD_string(char string[]) {
    for (int i = 0; string[i] != 0; i++) {
        LCD_char(string[i]);
    }
}
void LCD_substring(char string[], int begin, int end) {
    for (int i = begin; string[i] != 0 && i < end; i++) {
        LCD_char(string[i]);
    }
}
void LCD_uint(uint16_t num) {
    if (num == 0) {
        LCD_char('0');
        return;
    }
    uint16_t reversed = 0, digits = 0;
    for (uint16_t original = num; original > 0; original /= 10) {
        reversed = 10 * reversed + original % 10;
        digits++;
    }
    for (uint8_t i = 0; i < digits; i++) {
        LCD_char(reversed % 10 + '0');
        reversed /= 10;
    }
}

/************************************************************************/
/* UART Functions                                                       */
/************************************************************************/
void UART_init(void) {
    PORTD |= 0x01; // enable transmitter
    UBRRH = (BAUDRATE>>8);
    UBRRL = BAUDRATE;
    UCSRB = (1<<TXEN) | (1<<RXEN);
    UCSRC = (1<<URSEL) | (1<<UCSZ0) | (1<<UCSZ1);   // 8bit data format 1 parity
    UCSRB |= (1 << RXCIE); // enable interrupt on receive
}
void UART_send(unsigned char data) {
    while (!( UCSRA & (1<<UDRE)));
    UDR = data;
}
unsigned char UART_get_char(void) {
    while(~(UCSRA) & (1<<RXC));
    return UDR;
}
inline void waitfor_creader_buff(void) {
    while (!creader_buff.locked); // wait until buffer is unlocked for consumption
}
inline bool isready_creader_buff(void) {
    return creader_buff.locked;
}
inline void release_creader_buff(void) {
    creader_buff.locked = false;
}
ISR(USART_RXC_vect) {
    char c = UART_get_char();
    UART_send(c); // debug:: echo
    if (creader_buff.locked) {
        return;
    }
    uint8_t index = creader_buff.index;
    if ((index == 0 && c != 0x0A) || (index == CREADER_BUFF_SIZE - 1 && c != 0x0D)) { 
        creader_buff.index = 0; // reset buffer since data is not valid
        return;
    }
    creader_buff.ID_str[creader_buff.index++] = c;
    if (creader_buff.index >= CREADER_BUFF_SIZE) {
        creader_buff.index = 0;
        creader_buff.ID_str[CREADER_BUFF_SIZE] = 0; // null terminator for sanity
        creader_buff.locked = true; // lock the buffer so it won't be modified until consumed by user
    }
}

/************************************************************************/
/* Buttons Functions                                                    */
/************************************************************************/
typedef enum {NONE, LEFT, RIGHT, UP, DOWN, OK, INVALID} button_t;

button_t get_button(void) {
    button_t pressed;
    if (!(PINB & 0x1F)) {
        return NONE;
    } else if (PINB & (1<<PB0)) {
        pressed = RIGHT;
    } else if (PINB & (1<<PB1)) {
        pressed = LEFT;
    } else if (PINB & (1<<PB2)) {
        pressed = UP;
    } else if (PINB & (1<<PB3)) {
        pressed = DOWN;
    } else if (PINB & (1<<PB4)) {
        pressed = OK;
    } else {
        pressed = INVALID;
    }
    _delay_ms(200);
    return pressed;
}

/************************************************************************/
/* 1 Second Timer Functions                                             */
/************************************************************************/
void TIMER1SEC_init(void) {
    TCCR1B = (1 << CS11 | 1 << CS10  | 1 << WGM12); // divide by 64 (see pg. 113)
    OCR1A = 15624; // TOP value
    TIMSK = 1 << OCIE1A;
}
ISR(TIMER1_COMPA_vect) {
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].timeout > 0) {
            cards[i].timeout--;
        }
    }
}

/************************************************************************/
/* Main Program Functions                                               */
/************************************************************************/
char * format_card_timeout(int card_index) {
    static char time_str[6]= {'0', '0', ':', '0', '0', 0};
    uint8_t minutes = cards[card_index].timeout / 60;
    uint8_t seconds = cards[card_index].timeout % 60;
    time_str[0] = (minutes / 10) + '0';
    time_str[1] = (minutes % 10) + '0';
    time_str[3] = (seconds / 10) + '0';
    time_str[4] = (seconds % 10) + '0';
    return time_str;
}

bool set_card_timeout(int card_index) {
    LCD_command(clear);
    LCD_command(cursorOn);
    LCD_string("Time for card ");
    LCD_uint(card_index + 1);
    LCD_char(':');
    LCD_command(setCursor | lineTwo);
    LCD_string(format_card_timeout(card_index));
    LCD_string(" (MM/SS)");
    LCD_command(setCursor | lineTwo);
    uint8_t cursor_index = 0;
    uint8_t min = cards[card_index].timeout / 60;
    uint8_t sec = cards[card_index].timeout % 60;
    int time[5] = {min/10, min%10, 0, sec/10, sec%10}; // time[2] is a placeholder (corresponds to ':')
    bool setup_completed = false;
    for(;;) {
        button_t button = get_button();
        if (button == LEFT) {
            if (cursor_index <= 0) { // abort setup
                setup_completed = false;
                break;
            }
            do {
                LCD_command(moveLeft);
                cursor_index--;
            } while(cursor_index == 2); // don't let cursor stand on ':'                
        } else if (button == RIGHT) {
            if (cursor_index >= 4) { // setup is finished
                setup_completed = true;
                break;
            }
            do {
                LCD_command(moveRight);
                cursor_index++;
            } while(cursor_index == 2); // don't let cursor stand on ':'           
        } else if (button == UP || button == DOWN) {
            int digit = time[cursor_index];
            int inc = (button == UP) ? 1 : -1;
            if (cursor_index == 0 || cursor_index == 3) {
                time[cursor_index] = (digit + inc < 0)? 5 : (digit + inc) % 6; // digit 0~5
            } else if (cursor_index == 1 || cursor_index == 4) {
                time[cursor_index] = (digit + inc < 0)? 9 : (digit + inc) % 10; // digit 0~9
            }
            LCD_char(time[cursor_index] + '0');
            LCD_command(moveLeft); // stay on the same digit
        } else if (button == OK) { // setup is finished
            setup_completed = true;
            break;
        }
    }
    cards[card_index].timeout = 60 * (10 * time[0] + time[1]) + 10 * time[3] + time[4];
    LCD_command(cursorOff);
    return setup_completed;
}
bool set_card_id(int index) {
    LCD_command(clear);
    LCD_string("Scan card ");
    LCD_char(index + '1');
    LCD_string(":");
    LCD_command(setCursor | lineTwo);
    LCD_substring(cards[index].id, 1, CREADER_BUFF_SIZE - 1);
    bool setup_complete = false;
    for(;;) {
        button_t pressed = get_button();
        if (isready_creader_buff()) { // if something is available, show it to the screen
            LCD_command(setCursor | lineTwo);
            LCD_substring((char *)creader_buff.ID_str, 1, CREADER_BUFF_SIZE - 1);
            release_creader_buff();
        } else if (pressed == OK || pressed == RIGHT) { // if something got scanned and user confirmed, save it!
            strcpy(cards[index].id, (char *)creader_buff.ID_str);
            setup_complete = true;
            break;
        } else if (pressed == LEFT) {
            setup_complete = false;
            break;
        }
    }
    return setup_complete;
}
void populate_cards_info(void) {
    int counter = 0;
    for (;;) {
        if (counter == 2 * CARD_COUNT) break; // # of config stages times # of cards
        bool success;
        if (counter % 2 == 0) {
            success = set_card_id(counter / 2); 
        } else {
            success = set_card_timeout(counter / 2);
        }
        counter = (success) ? counter + 1 : counter - 1;
        if (counter < 0) counter = 0;
    }
}
int find_card(char * str) {
    for (int i = 0; i < CARD_COUNT; i++) {
        if (strcmp(cards[i].id, str) == 0) {
            return i;
        }
    }
    return -1;
}
void welcome_screen(void) {
    LCD_command(clear);
    LCD_string(" PharmaTracker");
    LCD_command(setCursor | lineTwo);
    LCD_string("Press any key...");
    while(get_button() == NONE);
}

int main(void) {
    LCD_init();
    welcome_screen();
    UART_init();
    sei();
    populate_cards_info();
    TIMER1SEC_init();
    while(1) {
        LCD_command(clear);
        LCD_string("Scan a card:");
        waitfor_creader_buff();
        LCD_command(clear);
        int card_index = find_card((char *)creader_buff.ID_str);
        if (card_index >= 0) {
            LCD_string("card ");
            LCD_char(card_index + '1');
            LCD_string(" detected!");
            LCD_command(setCursor | lineTwo);
            LCD_substring(cards[card_index].id, 1, CREADER_BUFF_SIZE - 1);
            _delay_ms(1000);
            LCD_command(clear);
            LCD_string("time left:");
            LCD_command(setCursor | lineTwo);
            LCD_string(format_card_timeout(card_index));
            LCD_string(" (MM/SS)");
        } else {
            LCD_string("Unknown Card.");
        }
        _delay_ms(2000);
        release_creader_buff();
    }
    return 0;
}
