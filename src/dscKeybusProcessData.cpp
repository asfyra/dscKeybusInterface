

#include "dscKeybusInterface.h"

void dscKeybusInterface::processHomeKey() {
  previousHomeKey = moduleData[0] == 0xFD || (previousHomeKey  && (moduleData[0] == 0xFF || moduleData[0] == 0xEF));
}
void dscKeybusInterface::processPanel_Zones() {
  static unsigned long previousTroubleChange;
  if (!validCRC()) return;
  

  // Trouble status
  if (bitRead(panelData[3],3)) trouble = true;
  else trouble = false;
  if (trouble != previousTrouble && millis() - previousTroubleChange > 3000) {
    previousTrouble = trouble;
    troubleChanged = true;
    statusChanged = true;
    previousTroubleChange = millis();
  }

  //Power Trouble
  if (bitRead(panelData[3],2)) powerTrouble = true;
  else powerTrouble = false;

  if(powerTrouble != previousPowerTrouble){
    previousPowerTrouble = powerTrouble;
    powerChanged = true;
    statusChanged = true;
  }

  byte partitionIndex = 0;

  bool armedFlag = !bitRead(panelData[3], 0);
  
  armedStay[partitionIndex] = previousHomeKey && armedFlag;
  armedAway[partitionIndex] = !previousHomeKey && armedFlag; // haven't find a way to distinguish
  armed[partitionIndex] = armedFlag;
    
  if (previousArmed[partitionIndex] != armedFlag) {
    previousArmed[partitionIndex] = armedFlag;
    armedChanged[partitionIndex] = true;
    statusChanged = true;
    previousHomeKey = false;
  }
 
  // Open zones 1-8 status is stored in openZones[0] and openZonesChanged[0]: Bit 0 = Zone 1 ... Bit 7 = Zone 8
  byte zoneData = panelData[2] >> 1; 
  openZones[0] = zoneData;
  byte zonesChanged = openZones[0] ^ previousOpenZones[0];
  if (zonesChanged != 0) {
    previousOpenZones[0] = openZones[0];
    openZonesStatusChanged = true;
    statusChanged = true;

    for (byte zoneBit = 0; zoneBit < 8; zoneBit++) {
      if (bitRead(zonesChanged, zoneBit)) {
        bitWrite(openZonesChanged[0], zoneBit, 1);
        if (bitRead(zoneData, zoneBit)) bitWrite(openZones[0], zoneBit, 1);
        else bitWrite(openZones[0], zoneBit, 0);
      }
    }
  }
}




