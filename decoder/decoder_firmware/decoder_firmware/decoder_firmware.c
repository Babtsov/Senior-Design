
#define F_CPU 9600000UL
#define PWM_VAL 38

#define CIRCUIT_STIM    PB0
#define SIGNAL_IN       PB1
#define TX_PIN          PB4
#define BAUDRATE        9600
#define ONE_BIT_DELAY   (1000000/BAUDRATE)
#define TOLERANCE       6

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdbool.h>

struct {
    volatile int8_t data_in;
    volatile bool new_data;
    int8_t buff[10];
} RFID;

// a struct used by detect_change, get_first_manchester, and get_next_manchester to store
// the current logic value and the count of consecutive previous logic values.
struct msg_detect {
    int8_t current_logic;      // current logic value
    int8_t prev_logic_count;   // previous logic count
};

void transmit (unsigned char data) {
	PORTB &= ~(1 << TX_PIN);   // start bit
    _delay_us(ONE_BIT_DELAY);
	for(int8_t i = 0; i < 8; i++) {
		if (bit_is_clear(data, 0)) {
			PORTB &= ~(1 << TX_PIN);
		} else {
			PORTB |= (1 << TX_PIN);
        }            
		_delay_us(ONE_BIT_DELAY);
		data >>= 1;
	}
	PORTB |= (1 << TX_PIN); // stop bit
	_delay_us(ONE_BIT_DELAY);
}

ISR(TIM0_OVF_vect) {
    volatile static int8_t counter = 5;
    if (counter > 0) {
        counter--;
        return;
    }
    counter = 5;
    RFID.data_in = (bit_is_set(PINB,SIGNAL_IN))? 1 : 0;
    RFID.new_data = true;
}

int8_t get_next_sample(void) {
    while (!RFID.new_data);
    RFID.new_data = false;
    return RFID.data_in;
}


void detect_change(struct msg_detect * msg) {
    int8_t count = 1, sample;
    // Keep counting until there is a change detected
    while (true) {
        sample = get_next_sample();
        if (sample != msg->current_logic) break;
        count++;
    }
    msg->current_logic = sample;    // store the logic value after the change occurred
    msg->prev_logic_count = count;  // store the # of consecutive previous logic values
}


int8_t get_first_manchester(struct msg_detect * msg) {
    msg->current_logic = get_next_sample();
    while (true) {
        detect_change(msg);
        if (msg->prev_logic_count > TOLERANCE) break;
    }
    return msg->current_logic;
}

int8_t get_next_manchester(struct msg_detect * msg) {
    detect_change(msg);
    if ( msg->prev_logic_count <= TOLERANCE) {
        detect_change(msg);
        //assert(msg->prev_logic_count <= TOLERANCE);
        return msg->current_logic;
    } else {
        return (msg->current_logic) ^ 1; // return the opposite of "current"
    }
}
char formatHex(int8_t i) {
    if ( 0 <= i && i <= 9){
        return i + '0';
    } else {
        return (i - 10) + 'A';
    }    
}

bool decodeRFID(void) {
    struct msg_detect msg = {0, 0};
    int8_t decoded_bit = get_first_manchester(&msg);
    int8_t one_count = decoded_bit;
    while (one_count < 9) { // wait until we get 9 consecutive 1's
        one_count = (get_next_manchester(&msg) == 1) ? one_count + 1 : 0;
    }
    int8_t col_parity[4] = {0};
    for (int8_t i = 0; i < 10; i++) { // scan all 10 rfid characters
        int8_t rfid_char = 0, row_parity = 0;
        for (int8_t j = 3; j >= 0; j--) { //build 4-bit hex number bit by bit
            decoded_bit = get_next_manchester(&msg);
            rfid_char += decoded_bit << j;
            row_parity += decoded_bit;
            col_parity[j] += decoded_bit;
        }
        row_parity += get_next_manchester(&msg);
        if ((row_parity & 1) != 0) return false; // assert row parity is even
        RFID.buff[i] = rfid_char;
    }
    for (int8_t i = 3; i >= 0; i--) { // now scan all the column parities
        col_parity[i] += get_next_manchester(&msg);
        if((col_parity[i] & 1) == 0) return false; // assert they are all even
    }
    int8_t stop_bit = get_next_manchester(&msg);
    if (stop_bit != 0) return false;
    return true;
}

void init_PWM (void)
{
	TCCR0A |= (1<<WGM00 | 1<<WGM01 | 1<<COM0A0);
	TCCR0B |= (1<<WGM02 | 1<<CS00 | 1<<FOC0A);
	TIMSK0 |= (1<<TOIE0); 
	PCMSK  |= (1<<PCINT1);
	GIMSK  |= (1<<PCIE);
	OCR0A   = PWM_VAL;
} 

int main (void) {
	DDRB |= (1<<CIRCUIT_STIM) | (1<<TX_PIN);

	init_PWM(); //Initialize PWM.
	
	sei();
    while (true) {
        bool success = decodeRFID();
        if (success) {
            cli();
            transmit(0x0A);
            for (int i = 0; i < 10; i++) {
                transmit(formatHex(RFID.buff[i]));
            }
            transmit(0x0D);
            sei();
        }
    }
}