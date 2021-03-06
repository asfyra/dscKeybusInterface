# Keybus Interface for Sigma MC-08 alarm system
This repo is a fork from the oroginal project https://github.com/taligentx/dscKeybusInterface, where you will find all the relative information and ways to attach to the alarm system

I have made the required changes for the library to be able to support some old Sigma MC-08 alarm systems.
This implementation uses the following key mappings:
* R: Read
* A: Address
* B: Bypass
* H; Home
* C: Code
* #: Enter

So when you send a command to the virtual keyboard, use the letters above to simulate the respective alarm buttons.

The system is fully tested on an ESP-8266, which is powered directly from the alarm board.

Many thanks to [taligentx](https://github.com/taligentx) for his great work !
