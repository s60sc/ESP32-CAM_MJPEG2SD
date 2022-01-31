/*
 To apply web based OTA update.
 In Arduino IDE:
 - select Tools / Partition Scheme / Minimal SPIFFS
 - select Sketch / Export compiled Binary
 On browser, enter <ESP32-cam ip address>:82, e.g 192.168.1.100:82
 On returned page, select Choose file and navigate to .bin file in sketch folder, then press Update

 s60sc 2020
 */

#include "myConfig.h"
#include "OTApage.h"

WebServer ota(82); // listen on port 82

void OTAsetup() {
  if (USE_OTA) {
    LOG_INF("OTA on port 82");
    ota.on("/", HTTP_GET, []() {
      // stop timer isrs, and free up heap space, or crashes esp32
      OTAprereq();
      ota.sendHeader("Connection", "close"); 
      ota.send(200, "text/html", OTApage);
    });
    ota.on("/update", HTTP_POST, []() {
      ota.sendHeader("Connection", "close");
      ota.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
      HTTPUpload& upload = ota.upload();      
      if (upload.status == UPLOAD_FILE_START) {
        LOG_INF("Update: %s", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        // if crashes, check that correct partition scheme has been selected
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
          LOG_INF("Update Success: %u, Rebooting...", upload.totalSize);
          delay(1000);
        } else Update.printError(Serial);
      }
    });
    ota.begin();
  }
}

bool OTAlistener() { 
  if (USE_OTA) {
    ota.handleClient();
    delay(5);
    return true;
  } else return false;
}
