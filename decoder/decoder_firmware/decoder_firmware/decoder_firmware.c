
#define F_CPU 9600000UL
#define PWM_VAL 38

#define CIRCUIT_STIM    PB0
#define SIGNAL_IN       PB1
#define TX_PIN          PB4
#define BAUDRATE        2400
#define ONE_BIT_DELAY   (1000000/BAUDRATE)
#define TOLERANCE       4

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>

struct {
    volatile int8_t data_in;
    volatile bool new_data;
    int8_t buff[10];
} RFID;

void transmit(unsigned char data) {
    PORTB &= ~(1 << TX_PIN);   // start bit
    _delay_us(416);
    for(int8_t i = 0; i < 8; i++) {
        if (data & 1) {
            PORTB |= (1 << TX_PIN);
        } else {
            PORTB &= ~(1 << TX_PIN);
        }
        _delay_us(416);
        data >>= 1;
    }
    PORTB |= (1 << TX_PIN); // stop bit
    _delay_us(416);
}

ISR(TIM0_OVF_vect) {
    volatile static int8_t counter = 3;
    if (counter > 0) {
        counter--;
        return;
    }
    counter = 3;
/*    PORTB ^= (1 << TX_PIN);*/
    RFID.data_in = bit_is_set(PINB,SIGNAL_IN)? 1 : 0;
    RFID.new_data = true;
}
volatile uint8_t judge = 0;
int8_t get_next_sample(void) {
    while (!RFID.new_data) {
        judge++;
    }
    RFID.new_data = false;
    return RFID.data_in;
}

/************************************************************************/
/* data_stream is used by the Manchester decoding functions,            */
/* to keep track of the state of the information stream                 */
/************************************************************************/
struct  {
    int8_t current_logic;      // current logic value (1 or 0)
    int8_t prev_logic_count;   // previous logic count
} data_stream;

inline void detect_change(void) {
    int8_t count = 1, sample;
    while (true) { // Keep counting until there is a change detected
        sample = get_next_sample();
        if (sample != data_stream.current_logic) break;
        count++;
    }
    data_stream.current_logic = sample;    // store the logic value after the change occurred
    data_stream.prev_logic_count = count;  // store the # of consecutive previous logic values
}
int8_t get_first_manchester(void) {
    data_stream.current_logic = get_next_sample();
    while (true) {
        detect_change();
        if (data_stream.prev_logic_count > TOLERANCE) 
            break;
    }
    return data_stream.current_logic;
}

int8_t get_next_manchester(void) {
    detect_change();
    if (data_stream.prev_logic_count <= TOLERANCE) {
        detect_change();
        if (data_stream.prev_logic_count > TOLERANCE) {
            asm("nop");
        }
        return data_stream.current_logic;
    } else {
        return (data_stream.current_logic) ^ 1; // return the opposite of "current"
    }
}
char formatHex(int8_t i) {
    if ( 0 <= i && i <= 9){
        return i + '0';
    } else {
        return (i - 10) + 'A';
    }    
}

volatile uint8_t errors = 0;
bool decodeRFID(void) {
    data_stream.current_logic = 0;
    data_stream.prev_logic_count = 0;
    int8_t one_count = get_first_manchester();
    while (one_count < 9) { // wait until we get 9 consecutive 1's
        one_count = (get_next_manchester() == 1) ? one_count + 1 : 0;
    }
    int8_t col_parity[4] = {0};
    for (int8_t i = 0; i < 10; i++) { // scan all 10 rfid characters
        int8_t rfid_char = 0, row_parity = 0;
        for (int8_t j = 3; j >= 0; j--) { //build 4-bit hex number bit by bit
            int8_t decoded_bit = get_next_manchester();
            rfid_char += decoded_bit << j;
            row_parity += decoded_bit;
            col_parity[j] += decoded_bit;
        }
        row_parity += get_next_manchester();
        if ((row_parity & 1) != 0) return false; // assert row parity is even
        RFID.buff[i] = rfid_char;
    }
    for (int8_t i = 3; i >= 0; i--) { // now scan all the column parities
        col_parity[i] += get_next_manchester();
        if((col_parity[i] & 1) != 0) return false; // assert they are all even
    }
    int8_t stop_bit = get_next_manchester();
    if (stop_bit != 0) return false;
    return true;
}

void init_PWM (void) {
    TCCR0A |= (1<<WGM00 | 1<<WGM01 | 1<<COM0A0);
    TCCR0B |= (1<<WGM02 | 1<<CS00 | 1<<FOC0A);
    TIMSK0 |= (1<<TOIE0);
    /*  PCMSK  |= (1<<PCINT1);*/
    /*  GIMSK  |= (1<<PCIE);*/
    OCR0A   = PWM_VAL;
}
int8_t manchester[40] = {0};
void output_manchester(void) {
    int8_t first = get_first_manchester();
    manchester[0] = first;
    for (int i = 1; i < 40; i++) {
        manchester[i] = get_next_manchester();
    }
    
}

int main (void) {
    DDRB |= (1<<CIRCUIT_STIM) | (1<<TX_PIN);
    init_PWM();
    sei();
//     get_first_manchester();
//     output_manchester();

    while (true) {
            while(RFID.data_in == 0x01);
            while(RFID.data_in == 0x00);
         if(decodeRFID()) {
             asm("nop");
         }
         //for (volatile int i = 0; i< 100; i++);
//         if (!success) continue;    
//             transmit(0x0A);
//             for (int i = 0; i < 10; i++) {
//                 transmit(formatHex(RFID.buff[i]));
//             }
//             transmit(0x0D);  
    }
}