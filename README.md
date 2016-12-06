<h1> <img src="./image/logo.jpg" width="31" height="46" /> PharmaTracker <img src="./image/logo.jpg" width="31" height="46" /> </h1>

PharmaTracker, our senior design project, consists of multiple parts:  
* __The Decoder board__ consists of a loop antenna resonating with a matched capacitor value, filters, amplifiers, and an ATtiny13 microcontroller to decode the manchester encoded data
* __The Main board__ consists of the LCD screen, buttons, speaker circuitry and ATMEGA644 microcontroller containing the software that implements most of the logic of the system.
* __The WiFi board__ consists of the ESP8266 Wifi module and additional circuitry required to convert the voltages from 5V to 3.3V
* __The web-server__ written in python and its purpose is to accept the connections made by the Wifi module and store them in the database

![overview](./image/information_flow_overview.png)

## Implementing the decoder board software
A major complexity of the decoder part of the project is to implement the decoding of the manchester encoded data captured at the input of the ATtiny13.  
In order to implement the decoding algorithm, several things need to be accounted for:
*  We need to be able to distinguish between long and short pulses. This can be done either using input capture or fast sampling. Since input capture is not available on the ATtiny13, we decided to use fast sampling. Meaning, we sample faster than the data-rate of the information.
*  We need to somehow achieve synchronization so that we can determine the boundries of each bit period. Since in manchester encoding, 0 is encoded as the transition from high to low, 1 is encoded as the transition from low to high, and the transition occurs in the middle of the bit period, we need a method to find when one period ends and the other period begins.  

##### The following picture shows a small segment from the manchester encoded RFID captured at the input pin of the microcontroller:
![waveform1](https://github.com/Babtsov/Senior-Design/blob/master/image/RFID_manchester_encoded.png)
As can be seen the length of the long pulses are 500uS and the short ones are 250uS.
### The decoding algorithm 
The algorithm for Manchester decoded is described below in a python-like psudocode
```python

decoded_bitsteam = []
def get_first_manchester():
	while True:
		count = the number of consecutive samples of high or low until a voltage change is detected
		if count >= long_pulse_value:
			break
	decoded_bitsteam.append(current_voltage)

def get_next_manchester():
	current_bit = decoded_bitsteam[-1] # get last element decoded
	count = the number of consecutive samples of high or low until a voltage 
	if count >= long_pulse_value:
		decoded_bitsteam.append(current_bit ^ 1)
	else:
		count_rest = the number of consecutive samples of high or low until a voltage
		assert(count_rest < long_pulse_value) # make sure the next pulse is short as well
		decoded_bitsteam.append(current_bit)
```
It is worth noting that the actual implementation inside the ATtiny13 didn't store the decoded bitsteam as a global array (due to lack of data memory space). Instead it constructed the bytes as it was decoding the bits, and stored the decoded bytes rather than discete bits.

### Communicating with the main board
Since the ATtiny13 doesn't have a built in UART support, the protocol had to be implemented in software. 
The following snippet shows how it was done:
```C
void transmit(unsigned char data) {
    PORTB &= ~(1 << TRANSMIT_PIN);   // start bit
    _delay_us(BAUD_DELAY);
    for(int8_t i = 0; i < 8; i++) {
        if (data & 1) {
            PORTB |= (1 << TRANSMIT_PIN);
        } else {
            PORTB &= ~(1 << TRANSMIT_PIN);
        }
        _delay_us(BAUD_DELAY);
        data >>= 1;
    }
    PORTB |= (1 << TRANSMIT_PIN); // stop bit
    _delay_us(BAUD_DELAY);
}
```
The baud rate was chosen to be 2400 baud to make it compatible with the off the shelf Parralax reader module we used during development. Also it is worth mentioning that the clock of the ATTiny was increased to 9.6 MHz (to be able to produce 125khz square wave required for the analog circuit of the receiver). Since the clock accuracy is not that great, the BAUD_DELAY required to establish reliable communication with the main bord had to be tuned, and hence it slightly deviated from the theorical value required for 2400 baud.

## Implementing the IoT functionality
Similarly to the decoder board, the WiFi board also communicates using UART. Instead of 2400 baud, we used 9600 baud (although higher speeds are probably possible). The ESP8266 Wifi module was preprogrammed to automatically connect to a predefined network (created by the home router). Once the ESP8266 module establishs a connection to the wireless LAN network, it creates a TCP connection with the remote server, through which it exchanges the information.  
Since the ESP8266 didn't seem to have built in support for HTTP, we had to "implement" the various HTTP requests ourselves. For simplicity, we used a GET request to a special URL on the server to implement the data upload to the server (although technically a POST request would have been more appropriate for such an action). 
The following shows an example of such a GET request sent to the webserver:  
`GET /add/0F02D777CF/i HTTP/1.0`  
In this case, the RFID card number is 0F02D777CF and the action is `i` which stands for "checked in".
