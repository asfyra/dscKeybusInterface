
 #include "dscKeybusInterface.h"

/*
 *  Print messages
 */

void dscKeybusInterface::printPanelMessage() {
  switch (panelData[0]) {
      case 0x3F: stream->print(F("0")); break;
      case 0x06: stream->print(F("1")); break;
      case 0x5B: stream->print(F("2")); break;
      case 0x4F: stream->print(F("3")); break;
      case 0x66: stream->print(F("4")); break;
      case 0x6D: stream->print(F("5")); break;
      case 0x7D: stream->print(F("6")); break;
      case 0x07: stream->print(F("7")); break;
      case 0x7F: stream->print(F("8")); break;
      case 0x6F: stream->print(F("9")); break;
      case 0x77: stream->print(F("A")); break;
      case 0x71: stream->print(F("F")); break;
      case 0x73: stream->print(F("P")); break;
    default: {
      if (!validCRC()) {
        stream->print(F("[No CRC or CRC Error]"));
        return;
      }
      else stream->print(F("[CRC OK !]"));
      return;
    }
  }
}

void dscKeybusInterface::printModuleMessage() {
  stream->print(F("[Keypad] "));

    if (hideKeypadDigits
    && (moduleData[0] == 0xCF || moduleData[0] == 0xDD || moduleData[0] == 0xBD || moduleData[0] == 0x7D
      || moduleData[0] == 0xDB || moduleData[0] == 0xBB || moduleData[0] == 0x7B || moduleData[0] == 0xD7
      || moduleData[0] == 0xB7 || moduleData[0] == 0x77 )) {
    stream->print(F("[Digit]"));
    return;
  }

  
  switch (moduleData[0]) {
    case 0xCF: stream->print(F("K0")); break;
    case 0xDD: stream->print(F("K1")); break;
    case 0xBD: stream->print(F("K2")); break;
    case 0x7D: stream->print(F("K3")); break;
    case 0xDB: stream->print(F("K4")); break;
    case 0xBB: stream->print(F("K5")); break;
    case 0x7B: stream->print(F("K6")); break;
    case 0xD7: stream->print(F("K7")); break;
    case 0xB7: stream->print(F("K8")); break;
    case 0x77: stream->print(F("K9")); break;

    case 0xFF: stream->print(F("CMD")); break;
    case 0xEF: stream->print(F("ENTER")); break;

    case 0xF7: stream->print(F("BYPASS")); break;
    case 0xFD: stream->print(F("HOME")); break;
    case 0xAF: stream->print(F("READ")); break;
    case 0x6F: stream->print(F("ADDRESS")); break;
    case 0xFB: stream->print(F("CODE")); break;


    default:
      stream->print(F("Unrecognized cmd: 0x"));
      stream->print(moduleData[0], HEX);
      break;
  }

}



/*
 * Print binary
 */

void dscKeybusInterface::printPanelBinary(bool printSpaces) {
  for (byte panelByte = 0; panelByte < panelByteCount; panelByte++) {
    if (panelByte == 1) stream->print(panelData[panelByte]);  // Prints the stop bit
    else {
      for (byte mask = 0x80; mask; mask >>= 1) {
        if (mask & panelData[panelByte]) stream->print("1");
        else stream->print("0");
      }
    }
    if (printSpaces && (panelByte != panelByteCount - 1 || displayTrailingBits)) stream->print(" ");
  }

  if (displayTrailingBits) {
    byte trailingBits = (panelBitCount - 1) % 8;
    if (trailingBits > 0) {
      for (int i = trailingBits - 1; i >= 0; i--) {
        stream->print(bitRead(panelData[panelByteCount], i));
      }
    }
  }
}

void dscKeybusInterface::printModuleBinary(bool printSpaces) {
  for (byte moduleByte = 0; moduleByte < moduleByteCount; moduleByte++) {
    if (moduleByte == 1) stream->print(moduleData[moduleByte]);  // Prints the stop bit
    else if (hideKeypadDigits
            && (moduleByte == 2 || moduleByte == 3 || moduleByte == 8 || moduleByte == 9)
            && (moduleData[2] <= 0x27 || moduleData[3] <= 0x27 || moduleData[8] <= 0x27 || moduleData[9] <= 0x27)
            && !queryResponse)
              stream->print(F("........"));  // Hides keypad digits
    else {
      for (byte mask = 0x80; mask; mask >>= 1) {
        if (mask & moduleData[moduleByte]) stream->print("1");
        else stream->print("0");
      }
    }
    if (printSpaces && (moduleByte != moduleByteCount - 1 || displayTrailingBits)) stream->print(" ");
  }

  if (displayTrailingBits) {
    byte trailingBits = (moduleBitCount - 1) % 8;
    if (trailingBits > 0) {
      for (int i = trailingBits - 1; i >= 0; i--) {
        stream->print(bitRead(moduleData[moduleByteCount], i));
      }
    }
  }
}

/*
 * Print panel command as hex
 */
void dscKeybusInterface::printPanelCommand() {
  // Prints the hex value of command byte 0
  stream->print(F("0x"));
  if (panelData[0] < 16) stream->print("0");
  stream->print(panelData[0], HEX);
  stream->print(", 0x");
  if (panelData[2] < 16) stream->print("0");
  stream->print(panelData[2], HEX);
  stream->print(", 0x");
  if (panelData[3] < 16) stream->print("0");
  stream->print(panelData[3], HEX);
}
