#define F_CPU 8000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>
#include <string.h>

#define CARD_COUNT              2       // the amount of different RFID cards the system supports
#define CREADER_BUFF_SIZE       12      // card reader buffer size
#define CREADER_BAUD_REG_VAL    207     // card reader baud rate: 2400 (see pg. 242)
#define ESP8266_BAUD_REG_VAL    51      // WiFi chip baud rate: 9600 (see pg. 242)
#define LCD_DIR                 DDRA
#define LCD_PORT                PORTA
#define LCD_E                   PORTA2
#define LCD_RS                  PORTA1

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
    _delay_us(50);
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
/* UART card reader Functions                                           */
/************************************************************************/
void UART_creader_init(void) {
    UBRR0H = (CREADER_BAUD_REG_VAL>>8);
    UBRR0L = CREADER_BAUD_REG_VAL;
    UCSR0B = (1<<TXEN0) | (1<<RXEN0);
    UCSR0C = (3<<UCSZ00);
    UCSR0B |= (1 << RXCIE0); // enable interrupt on receive
}
void UART_creader_send(unsigned char data) {
    while (!(UCSR0A & (1<<UDRE0)));
    UDR0 = data;
}
unsigned char UART_creader_receive(void) {
    while(~(UCSR0A) & (1<<RXC0));
    return UDR0;
}
inline bool isready_creader_buff(void) {
    return creader_buff.locked;
}
inline void release_creader_buff(void) {
    creader_buff.locked = false;
}
ISR(USART0_RX_vect) {
    char c = UART_creader_receive();
    UART_creader_send(c); // debug:: echo
    if (creader_buff.locked) return; 
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
/* UART ESP8266 Functions                                               */
/************************************************************************/
#define ESP8266_ROW_SIZE 10
#define ESP8266_COL_SIZE 52
struct ESP8266_buff {
    volatile char buffer[ESP8266_ROW_SIZE][ESP8266_COL_SIZE];
    volatile uint8_t row_index;
    volatile uint8_t col_index;
} ESP8266;


void UART_ESP8266_send(unsigned char data) {
    while (!( UCSR1A & (1<<UDRE1)));
    UDR1 = data;
}
void UART_ESP8266_cmd(char string[]) {
    for (int i = 0; string[i] != 0; i++) {
        UART_ESP8266_send(string[i]);
    }
    UART_ESP8266_send(0x0D);
    UART_ESP8266_send(0x0A);
}
unsigned char UART_ESP8266_receive(void) {
    while(~(UCSR1A) & (1<<RXC1));
    return UDR1;
}
ISR(USART1_RX_vect) {
    char c = UART_ESP8266_receive();
    int row = ESP8266.row_index, col = ESP8266.col_index;
    ESP8266.buffer[row][col] = c;

    if ((col > 0 && ESP8266.buffer[row][col - 1] == 0x0D && ESP8266.buffer[row][col] == 0x0A)
        || (col == ESP8266_COL_SIZE - 1)) {
        ESP8266.buffer[row][col - 1] = 0; // insert null terminator
        if (col > 1) { // advance to the next row if we have a non empty line (size is bigger than 2)
            ESP8266.row_index = (row == ESP8266_ROW_SIZE - 1)? 0: row + 1; // LF
        }        
        ESP8266.col_index = 0;  // CR
        return;
    }
    ESP8266.col_index++;
}
char * ESP8266_read_next_line() {
    static int row = 0;
    char * line = (char *)ESP8266.buffer[row];
    row = (row == ESP8266_ROW_SIZE - 1)? 0: row + 1;
    return line;
}
int ESP8266_search_for_str(char string[]) {
    for (int i = 0; i < ESP8266_ROW_SIZE - 1; i++) {
        if(strcmp((char *)ESP8266.buffer[i], string) == 0) {
            return i;
        }
    }
    return -1;
}
void UART_ESP8266_init(void) {
    UBRR1H = (ESP8266_BAUD_REG_VAL>>8);
    UBRR1L = ESP8266_BAUD_REG_VAL;
    UCSR1B = (1<<TXEN1) | (1<<RXEN1);
    UCSR1C = (3<<UCSZ10);
    UCSR1B |= (1 << RXCIE1); // enable interrupt on receive
    for (;;) {
        memset(&ESP8266, 0, sizeof(struct ESP8266_buff));
        UART_ESP8266_cmd("AT+RST");
        LCD_command(clear);
        LCD_string("Resetting WIFI..");
        _delay_ms(3000);
        for (;;) {
            if (ESP8266_search_for_str("ready") != -1 && ESP8266_search_for_str("WIFI CONNECTED") != -1
            && ESP8266_search_for_str("WIFI GOT IP") != -1) {
                LCD_string("hey");
                break;
            }
        }
        UART_ESP8266_cmd("ATE0");
        LCD_command(setCursor | lineTwo);
        LCD_string("disabling echo..");
        _delay_ms(5000);
        
        LCD_command(clear);
        LCD_string("UART:");
        UART_ESP8266_cmd("AT");
        _delay_ms(3000);
//         if (strcmp(ESP8266_read_next_line(), "OK") != 0) {
//             LCD_string("NO");
//             LCD_command(setCursor | lineTwo);
//             LCD_string("restarting WIFI..");
//             _delay_ms(2000);
//             continue;
//         }
        LCD_string("OK");
        LCD_command(setCursor | lineTwo);
        LCD_string("Connection:");
        UART_ESP8266_cmd("AT+CIPSTATUS");
        _delay_ms(1000);
        if (strcmp(ESP8266_read_next_line(), "STATUS:2") != 0
            && strcmp(ESP8266_read_next_line(), "OK") != 0) {
            LCD_string("NO");
            LCD_command(setCursor | lineTwo);
            LCD_string("restarting WIFI..");
            _delay_ms(2000);
            continue;
        }
        LCD_string("OK");
        _delay_ms(1000);
        UART_ESP8266_cmd("AT+CIPSTART=\"TCP\",\"35.162.70.152\",80");
        _delay_ms(1000);
        UART_ESP8266_cmd("AT+CIPSEND=34");
        _delay_ms(1000);
        UART_ESP8266_cmd("GET /add/0F02D777CF/o HTTP/1.0");
        UART_ESP8266_cmd(0);
        _delay_ms(1000);
        break;
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
    TCCR1B = (1 << CS12  | 1 << WGM12); // prescaler is 256 (see pg. 177)
    OCR1A = 31249; // TOP value:  required_time/(1/(F_CPU/prescaler))-1
}
ISR(TIMER1_COMPA_vect) {
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].time_left > 0 && cards[i].checked_out) {
            cards[i].time_left--;
        }
    }
}
inline void enable_T1SEC(void) {
    TIMSK1 |= 1 << OCIE1A;
}
inline void disable_T1SEC(void) {
    TIMSK1 &= ~(1 << OCIE1A);
}

/************************************************************************/
/* 1 KHz Timer Functions                                             */
/************************************************************************/
void buzzer_init(void) {
    DDRB |= (1 << PB5);
    TCCR0A |= (1 << WGM01);
    TCCR0B = (1 << CS02);   // prescalar is 256 (see pg. 177)
    OCR0A = 15;             // TOP value:  0.0005/(1/(F_CPU/prescaler))-1
    PORTB &= ~(1 << PB5);
}
ISR(TIMER0_COMPA_vect) {
    PORTB  ^= (1 << PB5);
}
inline void enable_buzzer(void) {
    TIMSK0 |= 1 << OCF0A;
}
inline void disable_buzzer(void) {
    TIMSK0 &= ~(1 << OCF0A);
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
    if (card_index >= 0) {                      // check if card is found
        if (!cards[card_index].checked_out) {   // reset time if card is not checked out
            cards[card_index].time_left = cards[card_index].max_time;
        }
        cards[card_index].checked_out ^= 1;     // toggle card status
        LCD_string("Card ");
        LCD_char(card_index + '1');
        LCD_string(" detected!");
        LCD_command(setCursor | lineTwo);
        LCD_substring(cards[card_index].id, 1, CREADER_BUFF_SIZE - 1);
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
    enable_buzzer();
    for(;;) {
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            disable_buzzer();
            return screen_index - 1;
        } else if (pressed == RIGHT) {
            disable_buzzer();
            return screen_index + 1;
        } else if (pressed == OK) {
            disable_buzzer();
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
    sei();
    LCD_init();
    T1SEC_init();
    UART_creader_init();
    //UART_ESP8266_init();
    LCD_command(clear);
    LCD_string(" PharmaTracker 9");
    _delay_ms(2000);
    enable_T1SEC();
    T1SEC_init();
    enable_T1SEC();
    buzzer_init();
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
