<h1> <img src="./image/logo.jpg" width="31" height="46" /> Pharma-Tracker </h1>

Pharma-Tracker, our senior design project, consists of multiple parts:  
* __The Receiver__ consists of a loop antenna resonating with a matched capacitor value, filters, amplifiers, and an ATtiny13 microcontroller to decode the manchester encoded data
* __The Main board__ consists of the LCD screen, buttons, speaker circuitry and ATMEGA644 microcontroller containing the software that implements most of the logic of the system.
* __The WiFi board__ consists of the ESP8266 Wifi module and additional circuitry required to convert the voltages from 5V to 3.3V
* __The web-server__ written in python and its purpose is to accept the connections made by the Wifi module and store them in the database

![overview](./image/information_flow_overview.png)

## Manchester decoding algorithm for the receiver
To decode the manchester encoded data we need to account for two things:  
*  We need to be able to distinguish between long and short pulses. This can be done either using input capture or fast sampling. Since input capture is not available on the ATtiny13, we decided to use fast sampling. Meaning, we sample faster than the data-rate of the information.
*  We need to somehow achieve synchronization so that we can determine the boundries of each bit period. Since in manchester encoding, 0 is encoded as the transition from high to low, 1 is encoded as the transition from low to high, and the transition occurs in the middle of the bit period, we need a method to find when one period ends and the other period begins.  

##### The following picture shows a small segment from the manchester encoded RFID captured at the input pin of the microcontroller:
![waveform1](https://github.com/Babtsov/Senior-Design/blob/master/image/RFID_manchester_encoded.png)
As can be seen the length of the long pulses are 500uS and the short ones are 250uS.
