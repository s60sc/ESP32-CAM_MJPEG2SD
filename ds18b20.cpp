/*
 DS18B20 is a one wire digital temperature sensor
 Pin layout from flat front L-R: Gnd, data, 3V3.
 Need a 4.7k resistor between 3V3 and data line

 Runs in its own task as there is a 750ms delay to get temperature
 Another task is used to poll for the temperature on a timer

 Need to install the DallasTemperature library

 s60sc 2020
 */

//#define USE_DS18B20 // uncomment to include DS18B20

#ifdef USE_DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#else
#include "Arduino.h"
#endif

// configuration
static const uint8_t DS18Bpin = 3; // labelled U0R on ESP32-CAM board
static const uint8_t retries = 10;
static float dsTemp = -127;
TaskHandle_t getDS18tempHandle = NULL;
static bool DS18Bfound = false;

static void getDS18tempTask(void* pvParameters) {
  // get current temperature from DS18B20 device
#ifdef USE_DS18B20
  OneWire oneWire(DS18Bpin);
  DallasTemperature sensors(&oneWire);
  while (true) {
    sensors.begin();
    uint8_t deviceAddress[8];
    sensors.getAddress(deviceAddress, 0);
    if (deviceAddress[0] == 0x28) {
      DS18Bfound = true;
      static uint8_t errCnt = 0;
      while (true) { // loop forever
        sensors.requestTemperatures(); 
        float currTemp = sensors.getTempCByIndex(0);
        // ignore occasional duff readings
        if (currTemp > -127) {
          dsTemp = currTemp;
          errCnt = 0;
        } else errCnt++;
        if (errCnt > retries) dsTemp = -127;
        delay(10000);
      } 
    } 
    DS18Bfound = false; 
    // allow task to be retried if fail to connect
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
#endif
}

bool prepDS18() {
#ifdef USE_DS18B20
  xTaskCreatePinnedToCore(&getDS18tempTask, "getDS18tempTask", 1024, NULL, 1, &getDS18tempHandle, 1); 
  delay(1000);
  return DS18Bfound;
#else
  return false;
#endif
}

bool tryDS18() {
  // retry for DS18B20 device connection
  if (!DS18Bfound) xTaskNotifyGive(getDS18tempHandle); 
  delay(500); // give it time
  return DS18Bfound;
}

float readDStemp(bool isCelsius) {
  // return latest read DS18B20 value in celsius (true) or fahrenheit (false), unless error
  return (dsTemp > -127) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
}
