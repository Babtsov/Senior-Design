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
#define SERVER_IP_ADDRESS       "35.162.70.152"
#define LCD_DIR                 DDRA
#define LCD_PORT                PORTA
#define LCD_E                   PORTA2
#define LCD_RS                  PORTA1

#define ASSERT(condition)       if(!(condition)) {asm("break");}

/************************************************************************/
/* Buttons Functions                                                    */
/************************************************************************/
typedef enum {NONE, LEFT, RIGHT, UP, DOWN, OK, INVALID} button_t;

button_t probe_buttons(void) {
    ASSERT((PINB & 0x1F) == ((PINB & 0x1F) & -(PINB & 0x1F))); // assert only 1 button is pressed at a time
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

void LCD_send_upper_nibble(uint8_t byte) {
    LCD_PORT &= ~0x78; // Save the data of the LCD port (& set nibble to 0)
    LCD_PORT |= byte >> 1 & 0x78; // set the nibble (requires shifting)
    LCD_PORT |= (1 << LCD_E);
    LCD_PORT &= ~(1 << LCD_E);
    _delay_us(50);
}
void LCD_init(void) {
    LCD_DIR |= 0x7E; // Data: PORTD6..PORTD3, E: PORTD2, RS: PORTD1
    _delay_ms(40); // wait until LCD's voltage is high enough
    uint8_t init_commands[] = {0x30, 0x30, 0x30, 0x20, 0x20, 0xC0, 0x00, 0xC0};
    for (int i = 0, n = sizeof(init_commands)/sizeof(uint8_t); i < n; i++) {
        LCD_send_upper_nibble(init_commands[i]);
        _delay_ms(10);
    }
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
void LCD_string(char * string) {
    for (int i = 0; string[i] != 0; i++) {
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
#define CREADER_INDEX -1

typedef enum {CHECKED_OUT, CHECKED_IN, ALARMED} card_status_t;

struct {
    char id[CREADER_BUFF_SIZE + 1]; // the RFID tag of the card attached to the medicine
    uint16_t max_time;              // maximum amount of time a medicine can be checked out
    volatile uint16_t time_left;    // how much time remaining until medicine goes bad
    card_status_t status;           // is this medicine currently checked out, checked it, or overdue?
    bool armed;                     // flag used by check_alarm to upload the alarm triggered event only once
} cards[CARD_COUNT] = {             // default initializations mainly used for debugging
    [0].id = {0x00, 0x33, 0x31, 0x30, 0x30, 0x33, 0x37, 0x44, 0x39, 0x33, 0x44, 0x00, 0x00},
    [0].max_time = 5, [0].time_left = 5, [0].status = CHECKED_IN, [0].armed = true,
    [1].id = {0x00, 0x36, 0x36, 0x30, 0x30, 0x36, 0x43, 0x34, 0x42, 0x37, 0x46, 0x00, 0x00},
    [1].max_time = 10, [1].time_left = 10, [1].status = CHECKED_IN, [1].armed = true
};
struct {
    volatile char ID_str[CREADER_BUFF_SIZE + 1];    //extra char for null terminator
    volatile uint8_t index;                         // pointer to an unoccupied slot
    volatile bool locked;                           // buffer is locked from modifications by the UART rx ISR
} creader_buff;

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
char * get_card_id(int8_t index) {
    char * rfid = (index == CREADER_INDEX)? (char *) creader_buff.ID_str : cards[index].id;
    return  (rfid + 1); // actually return a pointer to index 1 as index 0 is always 0x00
}
int find_card(void) {
    for (int i = 0; i < CARD_COUNT; i++) {
        if (strcmp(cards[i].id + 1, (char *)(creader_buff.ID_str + 1)) == 0) {
            return i;
        }
    }
    return -1;
}
ISR(USART0_RX_vect) {
    char c = UART_creader_receive();
    UART_creader_send(c); // debug:: echo
    if (creader_buff.locked) return; 
    int8_t index = creader_buff.index;
    ASSERT(0 <= index && index < CREADER_BUFF_SIZE);
    if ((index == 0 && c != 0x0A) || (index == CREADER_BUFF_SIZE - 1 && c != 0x0D)) {
        creader_buff.index = 0; // reset buffer since data is not valid
        return;
    }
    creader_buff.ID_str[creader_buff.index++] = c;
    if (creader_buff.index >= CREADER_BUFF_SIZE) { // we successfully scanned a card.
        creader_buff.index = 0;
        creader_buff.ID_str[CREADER_BUFF_SIZE - 1] = creader_buff.ID_str[0] = 0; // insert null at the beginning and at the end
        creader_buff.locked = true; // lock the buffer so it won't be modified until consumed by user
    }
}

/************************************************************************/
/* UART ESP8266 Functions                                               */
/************************************************************************/
#define ESP8266_ROW_SIZE 15
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
int ESP8266_search_for_str(char string[]) {
    for (int i = 0; i < ESP8266_ROW_SIZE - 1; i++) {
        if(strcmp((char *)ESP8266.buffer[i], string) == 0) {
            ESP8266.buffer[i][0] = 0; // clear the string
            return i;
        }
    }
    return -1;
}
bool ESP8266_find(char string[]) {
    for (int i = 0; i < ESP8266_ROW_SIZE - 1; i++) {
        if(strcmp((char *)ESP8266.buffer[i], string) == 0) {
            ESP8266.buffer[i][0] = 0; // clear the string
            return true;
        }
    }
    return false;
}
void ESP8266_clear_buffer(void) {
    for (int i = 0; i < ESP8266_ROW_SIZE - 1; i++) {
        ESP8266.buffer[i][0] = 0;
    }
    ESP8266.row_index = 0;
    ESP8266.col_index = 0;
}
//keeps polling ESP8266 connection status until connected or user pressed "back" to cancel
bool isConnected(void) { 
    LCD_command(clear);
    LCD_string("Connecting...");
    LCD_command(setCursor | lineTwo);
    LCD_string("Connected:");
    for (;;) {
        UART_ESP8266_cmd("AT+CIPSTATUS");
        _delay_ms(500);
        LCD_command(setCursor | lineTwo);
        if (ESP8266_find("STATUS:2")) {
            LCD_string("Connected: YES");
            ESP8266_clear_buffer();
            return true;
        } else if (ESP8266_find("STATUS:5")) {
            LCD_string("Connected: NO ");
            ESP8266_clear_buffer();
        } else {
            LCD_string("No response...");
        }
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return false;
        }
    }
}
void upload_to_server(char * rfid, char action) {
    char HTTP_request_buffer[] = "GET /add/##########/& HTTP/1.0";
    for (int i = 0 ; i < 10; i++) { // copy the RFID to the buffer (starting at first # which is index 9)
        HTTP_request_buffer[9 + i] = rfid[i];
    }
    HTTP_request_buffer[20] = action; // copy the action (index 20 which is &)
    UART_ESP8266_cmd("AT+CIPSTART=\"TCP\",\""SERVER_IP_ADDRESS"\",80");
    _delay_ms(1000);
    UART_ESP8266_cmd("AT+CIPSEND=34");
    _delay_ms(1000);
    UART_ESP8266_cmd(HTTP_request_buffer);
    UART_ESP8266_cmd(0);
    _delay_ms(1000);
}
void UART_ESP8266_init(void) {
    UBRR1H = (ESP8266_BAUD_REG_VAL>>8);
    UBRR1L = ESP8266_BAUD_REG_VAL;
    UCSR1B = (1<<TXEN1) | (1<<RXEN1);
    UCSR1C = (3<<UCSZ10);
    UCSR1B |= (1 << RXCIE1); // enable interrupt on receive
    for (;;) {
        ESP8266_clear_buffer();
        UART_ESP8266_cmd("AT+RST");
        LCD_command(clear);
        LCD_string("Resetting WIFI..");
        _delay_ms(1000);
        if (!ESP8266_find("ready")) { // seems like the ESP8266 didn't respond...
            LCD_command(clear);
            LCD_string("timeout/UART err");
            LCD_command(setCursor | lineTwo);
            LCD_string("restarting...");
            _delay_ms(1000);
            continue;
        }
        UART_ESP8266_cmd("ATE0"); // disable ESP8266 echo functionality
        _delay_ms(500);
        if (!isConnected()) continue;       // if user pressed reset, restart ESP8266
        upload_to_server("----------",'b');  // record the restart of the system
        break;
    }    
}
ISR(USART1_RX_vect) {
    char c = UART_ESP8266_receive();
    int row = ESP8266.row_index, col = ESP8266.col_index;
    ASSERT(0 <= col && col < ESP8266_COL_SIZE)
    ESP8266.buffer[row][col] = c;
    if ((col > 0 && ESP8266.buffer[row][col - 1] == 0x0D && ESP8266.buffer[row][col] == 0x0A)
    || (col == ESP8266_COL_SIZE - 1)) {
        ESP8266.buffer[row][col - 1] = 0; // insert null terminator
        ESP8266.row_index = (row == ESP8266_ROW_SIZE - 1)? 0: row + 1;
        ESP8266.col_index = 0;  // return to the beginning of the line
        return;
    }
    ESP8266.col_index++;
}

/************************************************************************/
/* 1 Second Timer Functions                                             */
/************************************************************************/
void T1SEC_init(void) {
    TCCR1B = (1 << CS12  | 1 << WGM12); // prescaler is 256 (see pg. 177)
    OCR1A = 31249; // TOP value:  required_time/(1/(F_CPU/prescaler))-1
}
inline void enable_T1SEC(void) {
    TIMSK1 |= 1 << OCIE1A;
}
inline void disable_T1SEC(void) {
    TIMSK1 &= ~(1 << OCIE1A);
}
ISR(TIMER1_COMPA_vect) {
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        if (cards[i].time_left > 0 && cards[i].status == CHECKED_OUT) {
            cards[i].time_left--;
        }
    }
}

/************************************************************************/
/* 1 KHz Timer Functions                                                */
/************************************************************************/
void buzzer_init(void) {
    DDRB |= (1 << PB5);
    TCCR0A |= (1 << WGM01);
    TCCR0B = (1 << CS02);   // prescalar is 256 (see pg. 177)
    OCR0A = 15;             // TOP value:  0.0005/(1/(F_CPU/prescaler))-1
    PORTB |= (1 << PB5);
}
inline void enable_buzzer(void) {
    TIMSK0 |= 1 << OCF0A;
}
inline void disable_buzzer(void) {
    TIMSK0 &= ~(1 << OCF0A);
    PORTB |= (1 << PB5);
}
ISR(TIMER0_COMPA_vect) {
    PORTB  ^= (1 << PB5);
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
    int8_t cursor_index = 0;
    uint8_t min = cards[index].max_time / 60;
    uint8_t sec = cards[index].max_time % 60;
    uint8_t time[5] = {min/10, min%10, 0, sec/10, sec%10}; // time[2] is a placeholder (corresponds to ':')
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
    LCD_string(get_card_id(index));
    bool setup_complete = false, new_scanned_card = false;
    for(;;) {
        button_t pressed = probe_buttons();
        if (isready_creader_buff()) { // if something is available, show it to the screen
            LCD_command(setCursor | lineTwo);
            LCD_string(get_card_id(CREADER_INDEX));
            new_scanned_card = true;
            release_creader_buff();
        } else if (pressed == OK || pressed == RIGHT) {
            setup_complete = true;
            if (new_scanned_card) {
                LCD_command(clear);
                LCD_string("Updating...");
                strcpy(get_card_id(index), get_card_id(CREADER_INDEX));
                upload_to_server(get_card_id(index), 'r');
            }                
            break;
        } else if (pressed == LEFT) {
            setup_complete = false;
            break;
        }
    }
    return setup_complete;
}
void probe_card_reader(void) {
    if (!isready_creader_buff()) return; // no card is near the RFID scanner
    LCD_command(clear);
    int card_index = find_card();
    if (card_index < 0) { // card not found
        LCD_string("This card is");
        LCD_command(setCursor | lineTwo);
        LCD_string("not registered.");
        _delay_ms(500);
        LCD_command(clear);
        release_creader_buff();
        return;
    }
    ASSERT(card_index < CARD_COUNT);
    LCD_string("Card ");
    LCD_char(card_index + '1');
    release_creader_buff();
    card_status_t current_status = cards[card_index].status;
    char status_to_upload = '?';
    switch(current_status) {
        case ALARMED:
            disable_buzzer(); // disable buzzer and fall through
        case CHECKED_OUT: 
            cards[card_index].status = CHECKED_IN;
            cards[card_index].armed = true;
            cards[card_index].time_left = cards[card_index].max_time;
            LCD_string(" check in");
            status_to_upload = 'i';
            break;
        case CHECKED_IN:
            cards[card_index].status = CHECKED_OUT;
            LCD_string(" check out");
            status_to_upload = 'o';
            break;
    }
    ASSERT(status_to_upload != '?'); // make sure one of the cases was actually executed.
    LCD_command(setCursor | lineTwo);
    LCD_string("ID: ");
    LCD_string(get_card_id(card_index));
    upload_to_server(get_card_id(card_index), status_to_upload);
    LCD_command(clear);
    release_creader_buff();
}
void check_alarm(void) { //check if the card ran out of time and if we need to trigger the alarm
    for (int i = 0; i < CARD_COUNT; i++) {
        if (cards[i].time_left == 0 && cards[i].armed) {
            enable_buzzer();
            LCD_command(clear);
            LCD_string("Card ");
            LCD_char(i + '1');
            LCD_string(" ran out");
            LCD_command(setCursor | lineTwo);
            LCD_string("of time!!!");
            cards[i].status = ALARMED;
            upload_to_server(get_card_id(i), 'a');
            cards[i].armed = false;
            LCD_command(clear);
        }
    }
}
/************************************************************************/
/* UI screen functions                                                  */
/* Each UI screen function returns the next screen to transition to.    */
/* clocks screen     - show the remaining time of each tag              */
/* confirm setup     - if user pressed OK, goes to setup screen         */
/* tags ID screen    - show the ID of each tag                          */
/* setup screen      - configure the system with time & tag ID          */
/************************************************************************/
typedef enum {CLOCKS_SCREEN, CONFIRM_SETUP_SCREEN, TAGS_SCREEN, SETUP_SCREEN, INVALID_SCREEN} screen_t;

screen_t setup_screen(void) {
    disable_T1SEC(); // stop timer while we are at the setup
    disable_buzzer(); // if currently buzzing, don't buzz while we are configuring the system
    LCD_command(clear);
    int counter = 0;
    for (;;) {
        if (counter == 2 * CARD_COUNT) break; // (# of config stages) * (# of cards) -> done with config
        bool success = ((counter & 1) == 0)? set_card_id(counter >> 1) : set_card_timeout(counter >> 1);
        counter = (success)? counter + 1 : counter - 1;
        if (counter < 0) break; // exit the setup screen
    }
    enable_T1SEC(); // let the time start ticking...
    return CLOCKS_SCREEN; // go back to the clock screen
}
screen_t clocks_screen(void) {
    LCD_command(clear);
    for(;;) {
        check_alarm();
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return TAGS_SCREEN;
        } else if (pressed == RIGHT) {
            return CONFIRM_SETUP_SCREEN;
        }
        LCD_command(home);
        for (int i = 0; i < 2; i++) {
            LCD_char(i + '1');
            LCD_string(": ");
            LCD_string(format_time(cards[i].time_left));
            switch(cards[i].status) {
                case CHECKED_IN: LCD_string(" IN"); break;
                case CHECKED_OUT: LCD_string(" OUT"); break;
                case ALARMED: LCD_string(" ALARMED"); break;
            }
            LCD_command(setCursor | lineTwo);
        }
    }
}
screen_t tagsID_screen(void) {
    LCD_command(clear);
    for(;;) {
        check_alarm();
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return CONFIRM_SETUP_SCREEN;
        } else if (pressed == RIGHT) {
            return CLOCKS_SCREEN;
        }
        LCD_command(home);
        LCD_string("1: ");
        LCD_string(get_card_id(0));
        LCD_command(setCursor | lineTwo);
        LCD_string("2: ");
        LCD_string(get_card_id(1));
    }
}
screen_t confirm_setup_screen(void) {
    LCD_command(clear);
    for(;;) {
        check_alarm();
        probe_card_reader();
        button_t pressed = probe_buttons();
        if (pressed == LEFT) {
            return CLOCKS_SCREEN;
        } else if (pressed == RIGHT) {
            return TAGS_SCREEN;
        } else if (pressed == OK) {
            return SETUP_SCREEN;
        }
        LCD_command(home);
        LCD_string("Press OK to");
        LCD_command(setCursor | lineTwo);
        LCD_string("configure system");
    }
}
int main(void) {
    sei();
    LCD_init();
    T1SEC_init();
    buzzer_init();
    UART_creader_init();
    UART_ESP8266_init();
    LCD_command(clear);
    LCD_string(" PharmaTracker 9");
    _delay_ms(2000);
    enable_T1SEC();
    screen_t current_screen = CLOCKS_SCREEN;
    for(;;) {
        screen_t next_screen;
        switch(current_screen) {
            case CLOCKS_SCREEN:
                next_screen = clocks_screen();
                break;
            case CONFIRM_SETUP_SCREEN:
                next_screen = confirm_setup_screen();
                break;
            case TAGS_SCREEN:
                next_screen = tagsID_screen();
                break;
            case SETUP_SCREEN:
                next_screen = setup_screen();
                break;
            default:
                next_screen = INVALID_SCREEN;
        }
        ASSERT(next_screen != INVALID_SCREEN);
        current_screen = next_screen;
    }
    ASSERT(false); // execution shouldn't reach this point
    return 0;
}