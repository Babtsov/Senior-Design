#define F_CPU 1000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>

#define CARD_COUNT          2       // the amount of different RFID cards the system supports
#define CREADER_BUFF_SIZE   12      // card reader buffer size
#define BAUDRATE            25      // baud rate: 2400 (see pg. 168)
#define LCD_DIR             DDRA
#define LCD_PORT            PORTA
#define LCD_E               PORTA2
#define LCD_RS              PORTA1

struct {
    char id[CREADER_BUFF_SIZE + 1]; // the RFID tag of the card attached to the medicine
    uint16_t max_time;              // maximum amount of time a medicine can be checked out
    volatile uint16_t time_left;    // how much time remaining until medicine goes bad
    bool checked_out;               // is this medicine currently checked out
} cards[CARD_COUNT] = {             // default initializations mainly used for debugging
    [0].id = {0x0a, 0x30, 0x46, 0x30, 0x32, 0x44, 0x37, 0x37, 0x37, 0x43, 0x36, 0x0d, 0x00},
    [0].max_time = 2000, [0].time_left = 2000, [0].checked_out = false,
    [1].id = {0x0a, 0x30, 0x46, 0x30, 0x32, 0x44, 0x37, 0x37, 0x37, 0x43, 0x46, 0x0d, 0x00},
    [1].max_time = 68, [1].time_left = 68, [1].checked_out = false
};

struct {
    volatile char ID_str[CREADER_BUFF_SIZE + 1];    //extra char for null terminator
    volatile uint8_t index;                         // pointer to an unoccupied slot
    volatile bool locked;                           // buffer is locked from modifications by the ISR
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
void LCD_substring(char[], int, int);
void LCD_uint(uint16_t);

void LCD_init(void) {
    LCD_DIR |= 0x7E; // Data: PORTD6..PORTD3, E: PORTD2, RS: PORTD1
    _delay_ms(40); // wait until LCD's voltage is high enough
    uint8_t init_commands[] = {0x30, 0x30, 0x30, 0x20, 0x20, 0xC0, 0x00, 0xC0};
    for (int i = 0, n = sizeof(init_commands)/sizeof(uint8_t); i < n; i++) {
        LCD_send_upper_nibble(init_commands[i]);
        _delay_ms(10);
    }
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

button_t probe_buttons(void) {
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
void T1SEC_init(void) {
    TCCR1B = (1 << CS11 | 1 << CS10  | 1 << WGM12); // divide by 64 (see pg. 113)
    OCR1A = 15624; // TOP value
}
ISR(TIMER1_COMPA_vect) {
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].time_left > 0 && cards[i].checked_out) {
            cards[i].time_left--;
        }
    }
}
inline void enable_T1SEC(void) {
    TIMSK |= 1 << OCIE1A;
}
inline void disable_T1SEC(void) {
    TIMSK &= ~(1 << OCIE1A);
}

/************************************************************************/
/* Helper Functions                                                     */
/************************************************************************/
char * format_time(uint16_t time) {
    static char time_str[6]= {'0', '0', ':', '0', '0', 0};
    uint8_t minutes = time / 60;
    uint8_t seconds = time % 60;
    time_str[0] = (minutes / 10) + '0';
    time_str[1] = (minutes % 10) + '0';
    time_str[3] = (seconds / 10) + '0';
    time_str[4] = (seconds % 10) + '0';
    return time_str;
}

bool set_card_timeout(int index) {
    LCD_command(clear);
    LCD_command(cursorOn);
    LCD_string("Time for card ");
    LCD_uint(index + 1);
    LCD_char(':');
    LCD_command(setCursor | lineTwo);
    LCD_string(format_time(cards[index].max_time));
    LCD_string(" (MM/SS)");
    LCD_command(setCursor | lineTwo);
    uint8_t cursor_index = 0;
    uint8_t min = cards[index].max_time / 60;
    uint8_t sec = cards[index].max_time % 60;
    int time[5] = {min/10, min%10, 0, sec/10, sec%10}; // time[2] is a placeholder (corresponds to ':')
    bool setup_completed = false;
    for(;;) {
        button_t button = probe_buttons();
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
    cards[index].max_time = cards[index].time_left = 60 * (10 * time[0] + time[1]) + 10 * time[3] + time[4];
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
        button_t pressed = probe_buttons();
        if (isready_creader_buff()) { // if something is available, show it to the screen
            LCD_command(setCursor | lineTwo);
            LCD_substring((char *)creader_buff.ID_str, 1, CREADER_BUFF_SIZE - 1);
            release_creader_buff();
        } else if (pressed == OK || pressed == RIGHT) {
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
int find_card(char * str) {
    for (int i = 0; i < CARD_COUNT; i++) {
        if (strcmp(cards[i].id, str) == 0) {
            return i;
        }
    }
    return -1;
}
void probe_card_reader(void) {
    if (!isready_creader_buff()) { // no card is near the RFID scanner
        return;
    }
    LCD_command(clear);
    int card_index = find_card((char *)creader_buff.ID_str);
    if (card_index >= 0) {
        LCD_string("Card ");
        LCD_char(card_index + '1');
        LCD_string(" detected!");
        LCD_command(setCursor | lineTwo);
        LCD_substring(cards[card_index].id, 1, CREADER_BUFF_SIZE - 1);
        cards[card_index].checked_out ^= 1; // toggle card status
        if (!cards[card_index].checked_out) { // reset time if card is not checked out
            cards[card_index].time_left = cards[card_index].max_time;
        }
    } else {
        LCD_string("This card is");
        LCD_command(setCursor | lineTwo);
        LCD_string("not registered.");
    }
    _delay_ms(3000);
    LCD_command(clear);
    release_creader_buff();
}

/************************************************************************/
/* UI screen functions                                                  */
/* Each UI screen function returns the code of the next screen          */
/* to transition to.                                                    */ 
/* Screen codes:                                                        */
/* 0    - clocks screen     - show the remaining time of each tag       */
/* 1    - confirm config    - confirm the config screen                 */
/* 2    - tags ID screen    - show the ID of each tag                   */
/* 100  - setup screen      - configure the system with time & tag ID   */ 
/************************************************************************/
int setup_screen(void) {
    disable_T1SEC(); // stop timer while we are at the setup
    LCD_command(clear);
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
        if (counter < 0) break; // exit the setup screen
    }
    enable_T1SEC(); // let the time start ticking...
    return 0; // go back to the clock screen
}
int clocks_screen(int screen_index) {
    LCD_command(clear);
    for(;;) {
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return screen_index - 1;
        } else if (pressed == RIGHT) {
            return screen_index + 1;
        } else if (pressed == UP) {
            enable_T1SEC();
        } else if (pressed == DOWN) {
            disable_T1SEC();
        }
        LCD_command(home); 
        LCD_string("1: ");
        LCD_string(format_time(cards[0].time_left));
        if (cards[0].checked_out) LCD_string(" OUT");
        else LCD_string(" IN");
        LCD_command(setCursor | lineTwo);
        LCD_string("2: ");
        LCD_string(format_time(cards[1].time_left));
        if (cards[1].checked_out) LCD_string(" OUT"); 
        else LCD_string(" IN");
    }
    return 0; // execution shouldn't reach this point
}
int tagsID_screen(int screen_index) {
    LCD_command(clear);
    for(;;) {
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return screen_index - 1;
        } else if (pressed == RIGHT) {
            return screen_index + 1;
        }
        LCD_command(home);
        LCD_string("1: ");
        LCD_substring(cards[0].id, 1, CREADER_BUFF_SIZE - 1);
        LCD_command(setCursor | lineTwo);
        LCD_string("2: ");
        LCD_substring(cards[1].id, 1, CREADER_BUFF_SIZE - 1);
    }
    return 0; // execution shouldn't reach this point
}
int confirm_configuration_screen(int screen_index) {
    LCD_command(clear);
    for(;;) {
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return screen_index - 1;
        } else if (pressed == RIGHT) {
            return screen_index + 1;
        } else if (pressed == OK) {
            return 100; // go to setup screen
        }
        LCD_command(home);
        LCD_string("Press OK to");
        LCD_command(setCursor | lineTwo);
        LCD_string("configure system");
    }
    return 0; // execution shouldn't reach this point
}

int main(void) {
    LCD_init();
    T1SEC_init();
    UART_init();
    LCD_command(clear);
    LCD_string(" PharmaTracker");
    _delay_ms(2000);
    enable_T1SEC();
    sei();
    int current_screen = 0, next_screen;
    for(;;) {
        if (current_screen < 0) next_screen = 2; // loop back to the ID's screen
        else if (current_screen == 0) next_screen = clocks_screen(current_screen);
        else if (current_screen == 1) next_screen = confirm_configuration_screen(current_screen);
        else if (current_screen == 2) next_screen = tagsID_screen(current_screen);
        else if (current_screen == 100) next_screen = setup_screen();
        else next_screen = 0;
        current_screen = next_screen;
    }
    return 0; // execution shouldn't reach this point
}
