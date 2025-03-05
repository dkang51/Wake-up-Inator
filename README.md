# Wake-up-Inator

This is an Arduino and Python system to wake you up optimally.

## Software Requirements

Currently this has a hardwired connection, so you will need a computer that has software to run the Arduino code and python code locally, using Python3 and Arduino to run the files. Wifi is also required.

## Hardware Requirements

The system requires an Iphone to connect (wired) to a PC that runs the Python3 script. A breadboard with a fan, heater, motors (servo/stepper), a speaker/buzzer, LED lights, and transistor are also needed.
For the breadboard setup view the project description for the wiring diagram. The system also uses 2 ESP32 Dev Boards

# Running
1. After breadboard setup, connect the IPhone to the PC and run the Python script. Ensure WIFI connection with the boards and phone on the same WIFI. Then run the 2 Arduino programs.
2. After disconnecting the phone, go to the Arduino programs and open serial monitor and set up wake-up times and settings. On wake-up time, hardware will run according to environment!
