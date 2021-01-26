/*
    DSC Keybus Interface

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dscKeybusInterface.h"

byte dscKeybusInterface::dscClockPin;
byte dscKeybusInterface::dscReadPin;
byte dscKeybusInterface::dscWritePin;
char dscKeybusInterface::writeKey;
byte dscKeybusInterface::writePartition;
byte dscKeybusInterface::writeByte;
byte dscKeybusInterface::writeBit;
bool dscKeybusInterface::virtualKeypad;
bool dscKeybusInterface::processModuleData;
byte dscKeybusInterface::panelData[dscReadSize];
byte dscKeybusInterface::panelByteCount;
byte dscKeybusInterface::panelBitCount;
volatile bool dscKeybusInterface::writeReady;
volatile byte dscKeybusInterface::moduleData[dscReadSize];
volatile bool dscKeybusInterface::moduleDataCaptured;
volatile byte dscKeybusInterface::moduleByteCount;
volatile byte dscKeybusInterface::moduleBitCount;
volatile bool dscKeybusInterface::writeAlarm;
volatile bool dscKeybusInterface::writeCmd;
volatile bool dscKeybusInterface::writeAsterisk;
volatile bool dscKeybusInterface::wroteAsterisk;
volatile bool dscKeybusInterface::bufferOverflow;
volatile byte dscKeybusInterface::panelBufferLength;
volatile byte dscKeybusInterface::panelBuffer[dscBufferSize][dscReadSize];
volatile byte dscKeybusInterface::panelBufferBitCount[dscBufferSize];
volatile byte dscKeybusInterface::panelBufferByteCount[dscBufferSize];
volatile byte dscKeybusInterface::isrPanelData[dscReadSize];
volatile byte dscKeybusInterface::isrPanelByteCount;
volatile byte dscKeybusInterface::isrPanelBitCount;
volatile byte dscKeybusInterface::isrPanelBitTotal;
volatile byte dscKeybusInterface::isrModuleData[dscReadSize];
volatile byte dscKeybusInterface::isrModuleByteCount;
volatile byte dscKeybusInterface::isrModuleBitCount;
volatile byte dscKeybusInterface::isrModuleBitTotal;
volatile byte dscKeybusInterface::currentCmd;
volatile byte dscKeybusInterface::statusCmd;
volatile unsigned long dscKeybusInterface::clockHighTime;
volatile unsigned long dscKeybusInterface::keybusTime;


dscKeybusInterface::dscKeybusInterface(byte setClockPin, byte setReadPin, byte setWritePin) {
  dscClockPin = setClockPin;
  dscReadPin = setReadPin;
  dscWritePin = setWritePin;
  if (dscWritePin != 255) virtualKeypad = true;
  writeReady = true;
  processRedundantData = true;
  displayTrailingBits = true;
  processModuleData = true;
  writePartition = 1;
}


void dscKeybusInterface::begin(Stream &_stream) {
  pinMode(dscClockPin, INPUT);
  pinMode(dscReadPin, INPUT);
  if (virtualKeypad) pinMode(dscWritePin, OUTPUT);
  stream = &_stream;

  // Platform-specific timers trigger a read of the data line 250us after the Keybus clock changes

  // Arduino Timer1 calls ISR(TIMER1_OVF_vect) from dscClockInterrupt() and is disabled in the ISR for a one-shot timer
  #if defined(__AVR__)
  TCCR1A = 0;
  TCCR1B = 0;
  TIMSK1 |= (1 << TOIE1);

  // esp8266 timer1 calls dscDataInterrupt() from dscClockInterrupt() as a one-shot timer
  #elif defined(ESP8266)
  timer1_isr_init();
  timer1_attachInterrupt(dscDataInterrupt);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  #endif

  // Generates an interrupt when the Keybus clock rises or falls - requires a hardware interrupt pin on Arduino
  attachInterrupt(digitalPinToInterrupt(dscClockPin), dscClockInterrupt, CHANGE);
}


bool dscKeybusInterface::handlePanel() {

  // Checks if Keybus data is detected and sets a status flag if data is not detected for 3s
  noInterrupts();
  if (millis() - keybusTime > 3000) keybusConnected = false;  // dataTime is set in dscDataInterrupt() when the clock resets
  else keybusConnected = true;
  interrupts();
  if (previousKeybus != keybusConnected) {
    previousKeybus = keybusConnected;
    keybusChanged = true;
    statusChanged = true;
    if (!keybusConnected) return true;
  }

  // Writes keys when multiple keys are sent as a char array
  if (writeKeysPending) writeKeys(writeKeysArray);

  // Skips processing if the panel data buffer is empty
  if (panelBufferLength == 0) return false;

  // Copies data from the buffer to panelData[]
  static byte panelBufferIndex = 1;
  byte dataIndex = panelBufferIndex - 1;
  for (byte i = 0; i < dscReadSize; i++) panelData[i] = panelBuffer[dataIndex][i];
  panelBitCount = panelBufferBitCount[dataIndex];
  panelByteCount = panelBufferByteCount[dataIndex];
  panelBufferIndex++;

  // Resets counters when the buffer is cleared
  noInterrupts();
  if (panelBufferIndex > panelBufferLength) {
    panelBufferIndex = 1;
    panelBufferLength = 0;
  }
  interrupts();

  // Waits at startup for the 0x05 status command or a command with valid CRC data to eliminate spurious data.
  static bool firstClockCycle = true;
  if (firstClockCycle) {
    if (validCRC() || panelData[0] == 0x05) firstClockCycle = false;
    else return false;
  }

  // Skips redundant data sent constantly while in installer programming
  static byte previousCmd0A[dscReadSize];
  static byte previousCmdE6_20[dscReadSize];
  switch (panelData[0]) {
    case 0x0A:  // Status in programming
      if (redundantPanelData(previousCmd0A, panelData)) return false;
      break;

    case 0xE6:
      if (panelData[2] == 0x20 && redundantPanelData(previousCmdE6_20, panelData)) return false;  // Status in programming, zone lights 33-64
      break;
  }
  if (dscPartitions > 4) {
    static byte previousCmdE6_03[dscReadSize];
    if (panelData[0] == 0xE6 && panelData[2] == 0x03 && redundantPanelData(previousCmdE6_03, panelData, 8)) return false;  // Status in alarm/programming, partitions 5-8
  }

  // Skips redundant data from periodic commands sent at regular intervals, skipping is a configurable
  // option and the default behavior to help see new Keybus data when decoding the protocol
  if (!processRedundantData) {
    static byte previousCmd[dscReadSize];
    if (redundantPanelData(previousCmd, panelData))
    {
       return false;
    }
  }

  //Process home key
  processHomeKey();
  // Processes valid panel data
  processPanel_Zones();


  return true;
}


bool dscKeybusInterface::handleModule() {
  if (!moduleDataCaptured) return false;
  moduleDataCaptured = false;
  return true;
}


// Sets up writes if multiple keys are sent as a char array
void dscKeybusInterface::write(const char * receivedKeys) {
  writeKeysArray = receivedKeys;
  if (writeKeysArray[0] != '\0') writeKeysPending = true;
  stream->println("Entering Write");
  writeKeys(writeKeysArray);
}


// Writes multiple keys from a char array
void dscKeybusInterface::writeKeys(const char * writeKeysArray) {
  static byte writeCounter = 0;
  if (writeKeysPending && writeReady && writeCounter < strlen(writeKeysArray)) {
    if (writeKeysArray[writeCounter] != '\0') {
      write(writeKeysArray[writeCounter]);
      writeCounter++;
      if (writeKeysArray[writeCounter] == '\0') {
        writeKeysPending = false;
        writeCounter = 0;
      }
    }
  }
}


// Specifies the key value to be written by dscClockInterrupt() and selects the write partition.  This includes a 500ms
// delay after alarm keys to resolve errors when additional keys are sent immediately after alarm keys.
void dscKeybusInterface::write(const char receivedKey) {
  static unsigned long previousTime;


  // Sets the binary to write for virtual keypad keys
  if (writeReady && millis() - previousTime > 500) {
    bool validKey = true;

    switch (receivedKey) {
      case '0': writeKey = 0xCF; break;
      case '1': writeKey = 0xDD; break;
      case '2': writeKey = 0xBD; break;
      case '3': writeKey = 0x7D; break;
      case '4': writeKey = 0xDB; break;
      case '5': writeKey = 0xBB; break;
      case '6': writeKey = 0x7B; break;
      case '7': writeKey = 0xD7; break;
      case '8': writeKey = 0xB7; break;
      case '9': writeKey = 0x77; break;
      case 'r':
      case 'R': writeKey = 0xAF; break; //READ
      case 'a':
      case 'A': writeKey = 0x6F; break; //ADDRESS
      case '#': writeKey = 0xEF; writeCmd = true; break; //ENTER
      case 'b':
      case 'B': writeKey = 0xF7; writeCmd = true; break; //BYPASS
      case 'h':
      case 'H': writeKey = 0xFD; writeCmd = true; break; //HOME
      case 'c':
      case 'C': writeKey = 0xFB; writeCmd = true; break; //CODE
      default: {
        validKey = false;
        break;
      }


    }

    // Sets the writing position in dscClockInterrupt() for the currently set partition
    if (dscPartitions < writePartition) writePartition = 1;

    writeByte = 0;
    writeBit = 1;

    if (writeAlarm) previousTime = millis();  // Sets a marker to time writes after keypad alarm keys
    if (validKey) writeReady = false;         // Sets a flag indicating that a write is pending, cleared by dscClockInterrupt()
  }
}


bool dscKeybusInterface::redundantPanelData(byte previousCmd[], volatile byte currentCmd[], byte checkedBytes) {
  bool redundantData = true;
  for (byte i = 0; i < checkedBytes; i++) {
    if (previousCmd[i] != currentCmd[i]) {
      redundantData = false;
      break;
    }
  }
  if (redundantData) return true;
  else {
    for (byte i = 0; i < dscReadSize; i++) previousCmd[i] = currentCmd[i];
    return false;
  }
}


bool dscKeybusInterface::validCRC() {
  return panelBitCount >= 24;
}


// Called as an interrupt when the DSC clock changes to write data for virtual keypad and setup timers to read
// data after an interval.
#if defined(__AVR__)
void dscKeybusInterface::dscClockInterrupt() {
#elif defined(ESP8266)
void ICACHE_RAM_ATTR dscKeybusInterface::dscClockInterrupt() {
#endif

  // Data sent from the panel and keypads/modules has latency after a clock change (observed up to 160us for keypad data).
  // The following sets up a timer for both Arduino/AVR and Arduino/esp8266 that will call dscDataInterrupt() in
  // 250us to read the data line.

  // AVR Timer1 calls dscDataInterrupt() via ISR(TIMER1_OVF_vect) when the Timer1 counter overflows
  #if defined(__AVR__)
  TCNT1=61535;            // Timer1 counter start value, overflows at 65535 in 250us
  TCCR1B |= (1 << CS10);  // Sets the prescaler to 1

  // esp8266 timer1 calls dscDataInterrupt() directly as set in begin()
  #elif defined(ESP8266)
  timer1_write(1250);
  #endif


  static unsigned long previousClockHighTime;
  if (digitalRead(dscClockPin) == HIGH) {
    if (virtualKeypad) digitalWrite(dscWritePin, LOW);  // Restores the data line after a virtual keypad write
    previousClockHighTime = micros();
  }

  else {
    clockHighTime = micros() - previousClockHighTime;  // Tracks the clock high time to find the reset between commands

    // Virtual keypad
    if (virtualKeypad) {
      static unsigned long previousTime;
      static bool setWriteReady = false;
      static bool writeStart = false;
      static bool isCommand = false;
      static bool writeRepeat = false;
      static char originalKey;
      // Writes a F/A/P alarm key and repeats the key on the next immediate command from the panel (0x1C verification)
      //if (writeAlarm && !writeReady) {
      if ((!writeReady && !setWriteReady) || writeRepeat) {

        isCommand = writeCmd;//writeKey == 0xEF || writeKey == 0xF7 || writeKey == 0xFD;
        //isCommand = writeCmd;
        if(isCommand){
          writeCmd = false;
          if(!writeRepeat){
            originalKey = writeKey;
            writeKey = 0xFF;
          }
        }
        // Writes the first bit by shifting the alarm key data right 7 bits and checking bit 0
        if (isrPanelBitTotal == 1) {
          if (!((writeKey >> 7) & 0x01)) {
            digitalWrite(dscWritePin, HIGH);
          }
          writeStart = true;  // Resolves a timing issue where some writes do not begin at the correct bit
        }

        // Writes the remaining alarm key data
        else if (writeStart && isrPanelBitTotal > 1 && isrPanelBitTotal <= 8) {
          if (!((writeKey >> (8 - isrPanelBitTotal)) & 0x01)) digitalWrite(dscWritePin, HIGH);
        }
        else if(writeStart && isrPanelBitTotal == 24) {
          if(isCommand || writeKey == 0xFF) digitalWrite(dscWritePin, HIGH);
          writeStart = false;
          previousTime = millis();
          if (writeRepeat)
          {
            writeRepeat = false;
            setWriteReady = true;
          }
          else if(isCommand || writeKey == 0xFF){
              writeRepeat = true;
              writeKey = originalKey;
            }
          else
          {
              setWriteReady = true;
          }
        }
      }

      if(setWriteReady && (millis() - previousTime) > 300){
        writeReady = true;
        previousTime = millis();
        setWriteReady = false;
      }

    }
  }
}


// Interrupt function called after 250us by dscClockInterrupt() using AVR Timer1, disables the timer and calls
// dscDataInterrupt() to read the data line
#if defined(__AVR__)
ISR(TIMER1_OVF_vect) {
  TCCR1B = 0;  // Disables Timer1
  dscKeybusInterface::dscDataInterrupt();
}
#endif


// Interrupt function called by AVR Timer1 and esp8266 timer1 after 250us to read the data line
#if defined(__AVR__)
void dscKeybusInterface::dscDataInterrupt() {
#elif defined(ESP8266)
void ICACHE_RAM_ATTR dscKeybusInterface::dscDataInterrupt() {
#endif

  static bool skipData = false;

  // Panel sends data while the clock is high
  if (digitalRead(dscClockPin) == HIGH) {

    // Stops processing Keybus data at the dscReadSize limit
    if (isrPanelByteCount >= dscReadSize) skipData = true;

    else {
      if (isrPanelBitCount < 8) {
        // Data is captured in each byte by shifting left by 1 bit and writing to bit 0
        isrPanelData[isrPanelByteCount] <<= 1;
        if (digitalRead(dscReadPin) == HIGH) {
          isrPanelData[isrPanelByteCount] |= 1;
        }
      }

      if (isrPanelBitTotal == 8) {
        // Tests for a status command, used in dscClockInterrupt() to ensure keys are only written during a status command
        switch (isrPanelData[0]) {
          case 0x05:
          case 0x0A: statusCmd = 0x05; break;
          case 0x1B: statusCmd = 0x1B; break;
          default: statusCmd = 0; break;
        }

        // Stores the stop bit by itself in byte 1 - this aligns the Keybus bytes with panelData[] bytes
        isrPanelBitCount = 0;
        isrPanelByteCount++;
      }

      // Increments the bit counter if the byte is incomplete
      else if (isrPanelBitCount < 7) {
        isrPanelBitCount++;
      }

      // Byte is complete, set the counters for the next byte
      else {
        isrPanelBitCount = 0;
        isrPanelByteCount++;
      }

      isrPanelBitTotal++;
    }
  }

  // Keypads and modules send data while the clock is low
  else {
    static bool moduleDataDetected = false;

    // Keypad and module data is not buffered and skipped if the panel data buffer is filling
    if (processModuleData && isrModuleByteCount < dscReadSize && panelBufferLength <= 1) {

      // Data is captured in each byte by shifting left by 1 bit and writing to bit 0
      if (isrModuleBitCount < 8) {
        isrModuleData[isrModuleByteCount] <<= 1;
        if (digitalRead(dscReadPin) == HIGH) {
          isrModuleData[isrModuleByteCount] |= 1;
        }
        else moduleDataDetected = true;  // Keypads and modules send data by pulling the data line low
      }

      // Stores the stop bit by itself in byte 1 - this aligns the Keybus bytes with moduleData[] bytes
      if (isrModuleBitTotal == 7) {
        isrModuleData[1] = 1;  // Sets the stop bit manually to 1 in byte 1
        isrModuleBitCount = 0;
        isrModuleByteCount += 2;
      }

      // Increments the bit counter if the byte is incomplete
      else if (isrModuleBitCount < 7) {
        isrModuleBitCount++;
      }

      // Byte is complete, set the counters for the next byte
      else {
        isrModuleBitCount = 0;
        isrModuleByteCount++;
      }

      isrModuleBitTotal++;
    }

    // Saves data and resets counters after the clock cycle is complete (high for at least 1ms)
    if (clockHighTime > 1000) {
      keybusTime = millis();

      // Skips incomplete and redundant data from status commands - these are sent constantly on the keybus at a high
      // rate, so they are always skipped.  Checking is required in the ISR to prevent flooding the buffer.
      if (isrPanelBitTotal < 8) skipData = true;
      else switch (isrPanelData[0]) {
        static byte previousCmd05[dscReadSize];
        static byte previousCmd1B[dscReadSize];
        case 0x05:  // Status: partitions 1-4
          if (redundantPanelData(previousCmd05, isrPanelData, isrPanelByteCount)) skipData = true;
          break;

        case 0x1B:  // Status: partitions 5-8
          if (redundantPanelData(previousCmd1B, isrPanelData, isrPanelByteCount)) skipData = true;
          break;
      }

      // Stores new panel data in the panel buffer
      currentCmd = isrPanelData[0];
      if (panelBufferLength == dscBufferSize) bufferOverflow = true;
      else if (!skipData && panelBufferLength < dscBufferSize) {
        for (byte i = 0; i < dscReadSize; i++) panelBuffer[panelBufferLength][i] = isrPanelData[i];
        panelBufferBitCount[panelBufferLength] = isrPanelBitTotal;
        panelBufferByteCount[panelBufferLength] = isrPanelByteCount;
        panelBufferLength++;
      }

      // Resets the panel capture data and counters
      for (byte i = 0; i < dscReadSize; i++) isrPanelData[i] = 0;
      isrPanelBitTotal = 0;
      isrPanelBitCount = 0;
      isrPanelByteCount = 0;
      skipData = false;

      if (processModuleData) {

        // Stores new keypad and module data - this data is not buffered
        if (moduleDataDetected) {
          moduleDataDetected = false;
          moduleDataCaptured = true;  // Sets a flag for handleModules()
          for (byte i = 0; i < dscReadSize; i++) moduleData[i] = isrModuleData[i];
          moduleBitCount = isrModuleBitTotal;
          moduleByteCount = isrModuleByteCount;
        }

        // Resets the keypad and module capture data and counters
        for (byte i = 0; i < dscReadSize; i++) isrModuleData[i] = 0;
        isrModuleBitTotal = 0;
        isrModuleBitCount = 0;
        isrModuleByteCount = 0;
      }
    }
  }
}
