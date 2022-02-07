/*
 DS18B20 is a one wire digital temperature sensor
 Pin layout from flat front L-R: Gnd, data, 3V3.
 Need a 4.7k resistor between 3V3 and data line

 Runs in its own task as there is a 750ms delay to get temperature
 Another task is used to poll for the temperature on a timer

 Need to install the DallasTemperature library

 s60sc 2020
 */

#include "myConfig.h"

#ifdef INCLUDE_DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#else
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read();
}
#endif

// configuration
static const uint8_t retries = 10;
static float dsTemp = -127;
TaskHandle_t getDS18Handle = NULL;
static bool DS18Bfound = false;

static void getDS18tempTask(void* pvParameters) {
  // get current temperature from DS18B20 device
#ifdef INCLUDE_DS18B20
  OneWire oneWire(DS18B_PIN);
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

void prepDS18B20() {
#ifdef INCLUDE_DS18B20
  xTaskCreatePinnedToCore(&getDS18tempTask, "getDS18tempTask", 1024, NULL, 1, &getDS18Handle, 1); 
  delay(1000);
  if (DS18Bfound) {LOG_INF("DS18B20 device available");}
  else {LOG_WRN("DS18B20 device not present");}
#endif
}

void tryDS18B20() {
  // retry for DS18B20 device connection
  if (!DS18Bfound) xTaskNotifyGive(getDS18Handle); 
  delay(500); // give it time
  if (DS18Bfound) {LOG_INF("DS18B20 available");}
  else {LOG_WRN("DS18B20 device not found");} // in case not working 
}

float readDS18B20temp(bool isCelsius) {
#ifdef INCLUDE_DS18B20
  // return latest read DS18B20 value in celsius (true) or fahrenheit (false), unless error
  return (dsTemp > -127) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
#else
  //Convert on chip raw temperature in F to Celsius degrees
  return (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present    
#endif
}
