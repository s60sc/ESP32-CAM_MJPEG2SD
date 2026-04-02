/*
* Capture ESP32 Cam JPEG images into a AVI file and store on SD
* AVI files stored on the SD card can also be selected and streamed to a browser as MJPEG.
*
* s60sc 2020 - 2026
*/

#include "appGlobals.h"

void setup() {
  if (utilsStartup()) {
#ifndef AUXILIARY
    LOG_INF("Selected board %s", CAM_BOARD);
    prepCam();
#else
    LOG_INF("AUXILIARY mode without camera");
#endif
  }

  // connect network (WiFi or Ethernet per config) and start web server
  if (startNetwork()) {
    // start rest of services
#ifndef AUXILIARY
    startSustainTasks(); 
#endif
#if INCLUDE_SMTP
    prepSMTP(); 
#endif
#if INCLUDE_FTP_HFS
    prepUpload();
#endif
#if INCLUDE_UART
    prepUart();
#endif
#if INCLUDE_PERIPH
    prepPeripherals();
  #if INCLUDE_MCPWM 
    prepMotors();
  #endif
#endif
#if INCLUDE_AUDIO
    prepAudio(); 
#endif
#if INCLUDE_TGRAM
    prepTelegram();
#endif
#if INCLUDE_I2C
    prepI2C();
  #if INCLUDE_TELEM
    prepTelemetry();
  #endif
#endif
#if INCLUDE_PERIPH
    startHeartbeat();
#endif
#ifndef AUXILIARY
 #if INCLUDE_RTSP
    prepRTSP();
 #endif
    if (!prepRecording()) {
      snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Insufficient memory, remove optional features");
      LOG_WRN("%s", startupFailure);
    }
#endif
    checkMemory();
  }
}

void loop() {
  // confirm not blocked in setup
  LOG_INF("=============== Total tasks: %u ===============\n", uxTaskGetNumberOfTasks() - 1);
  delay(1000);
  vTaskDelete(NULL); // free 8k ram
}
