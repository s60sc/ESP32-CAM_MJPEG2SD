/*
 To apply web based OTA update.
 In Arduino IDE:
 - select Tools / Partition Scheme / Minimal SPIFFS
 - select Sketch / Export compiled Binary
 On browser, enter <ESP32-cam ip address>:82, e.g 192.168.1.100:82
 On returned page, select Choose file and navigate to .bin file in sketch folder, then press Update

 s60sc 2020
 */

#define USE_OTA true

#include <WebServer.h>
#include <Update.h>
#include "OTApage.h"

WebServer ota(82); // listen on port 82

void OTAprereq();

void OTAsetup() {
  if (USE_OTA) {
    Serial.println("OTA on port 82");
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
        Serial.printf("Update: %s\n", upload.filename.c_str());
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
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else Update.printError(Serial);
      }
    });
    /*
    ota.on("/log", HTTP_GET, []() {
      ota.sendHeader("Connection", "close");
      doMessageLog(); // present message log for display (not part of ota)
      ota.send(200, "text/plain", logBuff); // sends as download not display
      free(logBuff);
    }); */
    ota.begin();
  }
}

bool OTAlistener() { 
  if (USE_OTA) {
    ota.handleClient();
    delay(10);
    return true;
  } else return false;
}
