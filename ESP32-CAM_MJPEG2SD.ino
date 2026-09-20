/*
* Capture ESP32 Cam JPEG images into a AVI file and store on SD
* AVI files stored on the SD card can also be selected and streamed to a browser as MJPEG.
*
* s60sc 2020 - 2026
*/

#include "ESP32-CAM_MJPEG2SD.h"

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
    appSetup(); 
    checkMemory();
  }
}

void loop() {
  // confirm not blocked in setup
  LOG_INF("=============== Total tasks: %u ===============\n", uxTaskGetNumberOfTasks() - 1);
  delay(1000);
  vTaskDelete(NULL); // free 8k ram
}
