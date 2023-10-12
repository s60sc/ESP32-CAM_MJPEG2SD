
// Additional code used for my personal copy of published apps
// NOT to be stored on github
// Supplies my specific wifi SSIDs and passwords, and private app variants eg Side Alarm
//
// Camera uses timers 0 & 1
// mjpeg2sd uses timer 3 for frame rate
// Timer 2 used for polling (joystick)
//
// my SMTP PWD: "vdvwcwfttbuovtvh" (need to regen if account password changed)
// my FTP PWD: "a1zYetanother1"
// my default AP Password: "1234567890"  (WPA_PSK passwords must be minimum 8 chars)
//
/*
Data file version management.

Register changes:
- Increment *_VER in appGlobals.h for any /data file changes
- MUST update master configs.txt *Ver to match appGlobals.h *_VER
HOWEVER configs.txt only needs to be uploaded if the file itself has changed (other than *Ver values)

general principle: 
- if versions dont match, file is deleted and version numbers updated
- so it is assumed that when file subsequently exists (github / ota / copy) it is the correct version

WARNING:
- if testing with new version, upload new configs.txt BEFORE new app
- otherwise will download old version of configs.txt from github 
- which will cause endless loop of delete / download
--------------------------------------------

Downloading:

configs.txt file does not exist:
- setupAssist() downloads it
- it either has no *Ver or each *Ver must match appGlobals.h 

no *Ver:
- loadConfig() calls loadVectItem() to add *Ver=0 to configs vector
  updatedVers = true;
- generic

has *Ver:
- loadConfig() loads *Ver from configs.txt
- generic

generic:
- updateStatus() calls updateVer() for *Ver
- updateVer(): if *_VER constant in appGlobals.h != *Ver from configs.txt, delete data folder
  calls updateConfigVect() to match configs vector with appGlobals.h 
  updatedVers = true;
- if (updatedVers) saveConfigVect() updates config.txt with new properties
- setupAssist() downloads any missing files
*/
// APP NOTES:
//
// SIDE_ALARM
// - uncomment #SIDE_ALARM
// - see ledTask() for settings
// - in mjpeg2sd.htm, change:
//     else if (key == "refreshLog") getLog('/web?log.txt');
//   to:
//     else if (key == "refreshLog") getLog();
// 
// ESP32-CAM_MJPEG2SD:
// - uncomment INCLUDE_SMTP, INCLUDE_FTP
// - set STORAGE to SD_MMC
// - https://kandi.openweaver.com/c++/s60sc/ESP32-CAM_MJPEG2SD
//
// TuyaDevice on ESP32-C3 (inverse applies if testing on ESP32 with SD):
// - Comment out INCLUDE_SMTP, INCLUDE_FTP
// - set UART0 to true 
// - set STORAGE to LittleFS
//
// VoiceChanger on Freenove S3. For mic and amp use pins: 1, 14, 21, 41, 42, 47

#include "appGlobals.h" 
#include "esp_freertos_hooks.h"

// **************** my wifi config ****************/ 

static int findWifi() {
  // find suitable wifi access point with strongest signal
  int ssidIndex = -1;
  int bestSignal = -999;  
  int numNetworks = WiFi.scanNetworks();
  for (int i=0; i < numNetworks; i++) {
    if (strstr(WiFi.SSID(i).c_str(), "bisk") != NULL) {
      // dont use bisk0ts as cam web page cant be accessed - reason unknown
      if (strcmp(WiFi.SSID(i).c_str(), "bisk0ts") != 0) { 
        int sigStrength = WiFi.RSSI(i);
        if (sigStrength > bestSignal) {
          bestSignal = sigStrength;
          ssidIndex = i;
        }
        LOG_INF("Network: %s; signal strength: %d dBm; Encryption: %s; channel: %u", WiFi.SSID(i).c_str(), sigStrength, getEncType(i), WiFi.channel(i));
      }
    }
    yield();
  }
  return ssidIndex;
}

// setup wifi for personal environment
static bool prepWifi() {
  // set up wifi
  if (WiFi.status() != WL_CONNECTED) {
    int ssidIndex = findWifi();
    if (ssidIndex >= 0) {
      updateStatus("ST_SSID", WiFi.SSID(ssidIndex).c_str());
      updateStatus("ST_Pass", "lr15next"); 
      updateStatus("ST_ip", "192.168.1." STATIC_IP_OCTAL);
      updateStatus("ST_sn", "255.255.255.0");
      updateStatus("ST_gw", "192.168.1.1");
      updateStatus("ST_ns1", "192.168.1.1");
    } else {
      LOG_WRN("No suitable WiFi access point found");
      return false;
    }
  }
  return true; // already connnected
}

/*********** side of house yale camera specific ***********/

#ifdef SIDE_ALARM
static void ledTask(void *arg) {
  // flash external led on pin 4 in yale alarm
  // need to set lampUse on, manual activation, and night value for lampLevel 
  // set pirUse on and allocate pin to generate ambient light val
  // set configs vals for led flash timings below  
  int fullDark = 2; // lowest lightLevel value
  int fullLevel = 15; // max lampLevel
  delay(10000);
  int tempInterval = 0;
  while (true) {
    // led flash timings
    int onSecs = voltInterval == 0 ? 2000 : voltInterval * 1000;
    int offSecs = voltDivider == 0 ? 5000 : voltDivider * 1000;
    int offLevel = (int)voltLow == 0 ? 1 : (int)voltLow;
    // requested light level at night
    int onLevel = lampLevel == 0 ? 7 : lampLevel; 
    // max poss brightness during day
    if (lightLevel >= nightSwitch) onLevel = fullLevel;
    // reduce light level from full to requested during dusk
    else if (lightLevel > fullDark) onLevel = lampLevel + ((fullLevel - lampLevel) 
      * (lightLevel - fullDark)  / (nightSwitch - fullDark));
    setLamp(onLevel);
    delay(onSecs);
    setLamp(offLevel);
    delay(offSecs);
    tempInterval += onSecs + offSecs;
    if (tempInterval >= 5 * 60 * 1000) {
      // once per 5 minutes
      float camTemp = readTemperature(true);
      if (camTemp > 80.0) LOG_WRN("Cam temp: %0.1f", camTemp);
      tempInterval = 0;
    }
    delay(100); // in case onSecs + offSecs = 0
  }
}
#endif

/****************** initial setup ****************/

void devSetup() {
  LOG_WRN("***** Using devSetup *****");
  prepWifi();
#ifdef SIDE_ALARM
  useMotion = false;
  sdFreeSpaceMode = 0;
  sdMinCardFreeSpace = 0;
  doRecording = false;
  logMode = false;
  // ledTask only used for mjpeg2sd sideAlarm
  xTaskCreate(ledTask, "ledTask", 2048, NULL, 1, NULL);
#endif
  // debugMemory("devSetup"); // misleading as starts wifi, uses about 2k
}
