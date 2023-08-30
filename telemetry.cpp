//
// Telemetry recording to storage during camera recording
// Formmatted as CSV file for presentation in spreadsheet
// Sensor data obtained from user supplied libraries and code
// Need to check 'Use telemetry recording' under Peripherals button on Edit Config web page.
// Best used on ESP32S3, not tested on ESP32

// s60sc 2023

#include "appGlobals.h"
#include <Wire.h>

TaskHandle_t telemetryHandle = NULL;
bool teleUse = false;
int teleInterval = 1;
static char* teleBuf; // telemetry data buffer
static bool capturing = false;
static char teleFileName[FILE_NAME_LEN];

static bool scanI2C();
static bool checkI2C(byte addr);

/*************** USER TO MODIFY CODE BELOW for REQUIRED SENSORS ******************/

#define BUF_OVERFLOW 100 // set to be max size of formatted telemetry row

// user defined header names, first is always Time, row must end with \n
#define HEADER "Time,Temperature (C),Pressure (mb),Altitude (m),aX,aY,aZ,gX,gY,gZ,mX,mY,mZ\n"

// example code for BMP280 and MPU9250 I2C sensors on GY-91 board

//#define USE_GY91 // uncomment to support GY-91 board

// if require I2C, define which pins to use for I2C bus
#define I2C_SDA 20
#define I2C_SCL 21

#ifdef USE_GY91
#include <BMx280I2C.h>
#define BMP_ADDRESS 0x76 
#define STD_PRESSURE 1013.25 // standard pressure mb at sea level
BMx280I2C bmp280(BMP_ADDRESS);

#include <MPU9250_asukiaaa.h>
#define MPU_ADDRESS 0x68 
MPU9250_asukiaaa mpu9250;
#endif

static bool setupSensors() {
  // setup required sensors
#ifdef USE_GY91
  Wire.begin(I2C_SDA, I2C_SCL); // join I2C bus as master 
  if (!scanI2C()) return false;

  if (bmp280.begin()) {
    LOG_INF("BMP280 available");
    // set defaults
    bmp280.resetToDefaults();
    bmp280.writeOversamplingPressure(BMx280MI::OSRS_P_x16);
    bmp280.writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
  } else {
    LOG_WRN("BMP280 not available at address");
    return false;
  } 

  if (checkI2C(MPU_ADDRESS)) { 
    mpu9250.setWire(&Wire);
    mpu9250.beginAccel();
    mpu9250.beginGyro();
    mpu9250.beginMag();
    LOG_INF("MPU9250 available");

  } else {
    LOG_WRN("MPU9250 not available at address");
    return false;
  }
#endif

  return true;
}

static size_t getSensorData(size_t highPoint) {
  // get sensor data and format as csv row in buffer
#ifdef USE_GY91
  bmp280.measure();
  while (!bmp280.hasValue()) delay(10);
  float bmpPressure = bmp280.getPressure() * 0.01;  // pascals to mb/hPa
  float bmpAltitude = 44330.0 * (1.0 - pow(bmpPressure / STD_PRESSURE, 1.0 / 5.255)); // altitude in meters
  highPoint += sprintf(teleBuf + highPoint, "%0.1f,%0.1f,%0.1f,", bmp280.getTemperature(), bmpPressure, bmpAltitude);

  // raw uncalibrated values
  mpu9250.accelUpdate();
  highPoint += sprintf(teleBuf + highPoint, "%0.1f,%0.1f,%0.1f,", mpu9250.accelX(), mpu9250.accelY(), mpu9250.accelZ());
  mpu9250.gyroUpdate();
  highPoint += sprintf(teleBuf + highPoint, "%0.1f,%0.1f,%0.1f,", mpu9250.gyroX(), mpu9250.gyroY(), mpu9250.gyroZ());
  mpu9250.magUpdate();
  highPoint += sprintf(teleBuf + highPoint, "%0.1f,%0.1f,%0.1f,", mpu9250.magX(), mpu9250.magY(), mpu9250.magZ());
#endif
  return highPoint;
}

/*************** LEAVE CODE BELOW AS IS ******************/

static void telemetryTask(void* pvParameters) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    capturing = true;
    uint32_t sampleInterval = 1000 * (teleInterval < 1 ? 1 : teleInterval);
    // open storage file
    if (STORAGE.exists(TELETEMP)) STORAGE.remove(TELETEMP);
    File teleFile = STORAGE.open(TELETEMP, FILE_WRITE);
    // write header row to buffer
    size_t highPoint = sprintf(teleBuf, "%s", HEADER); 
    
    // loop while camera recording
    while (capturing) {
      uint32_t startTime = millis();
      // write current time for this row
      time_t currEpoch = getEpoch();
      highPoint += strftime(teleBuf + highPoint, 10, "%H:%M:%S,", localtime(&currEpoch));
      // get data from sensors 
      highPoint = getSensorData(highPoint);
      // add newline to finish row
      highPoint += sprintf(teleBuf + highPoint, "\n"); 
      
      // if marker overflows buffer, write to storage
      if (highPoint >= RAMSIZE) {
        highPoint -= RAMSIZE;
        teleFile.write((uint8_t*)teleBuf, RAMSIZE);
        // push overflow to buffer start
        memcpy(teleBuf, teleBuf+RAMSIZE, highPoint);
      }
      // wait for next collection interval
      while (millis() - sampleInterval < startTime) delay(10);
    }
    
    // capture finished, write remaining buff to storage 
    if (highPoint) teleFile.write((uint8_t*)teleBuf, highPoint);
    teleFile.close();
    // rename temp file to specific file name
    STORAGE.rename(TELETEMP, teleFileName); 
    LOG_INF("Saved telemetery file %s", teleFileName);
  }
}

void prepTelemetry() {
  // called by app initialisation
  if (teleUse) {
    teleBuf = psramFound() ? (char*)ps_malloc(RAMSIZE + BUF_OVERFLOW) : (char*)malloc(RAMSIZE + BUF_OVERFLOW);
    if (setupSensors()) xTaskCreate(&telemetryTask, "telemetryTask", 1024 * 4, NULL, 3, &telemetryHandle);
    else teleUse = false;
    LOG_INF("Telemetry recording %s available", teleUse ? "is" : "NOT");
  }
}

void startTelemetry() {
  // called when camera recording started
  if (teleUse) xTaskNotifyGive(telemetryHandle); // wake up task
}

void stopTelemetry(const char* fileName) {
  // called when camera recording stopped
  if (teleUse) {
    strcpy(teleFileName, fileName); 
    // derive telemetry file name from avi file name with csv extension
    changeExtension(teleFileName, CSV_EXT);
    capturing = false; // stop task
  }
}

static bool checkI2C(byte addr) {
  // check if device present at address
  Wire.beginTransmission(addr);
  return !Wire.endTransmission(true);
}

static bool scanI2C() {
  // identify addresses of active I2C devices
  int numDevices = 0;
  for (byte address = 0; address < 127; address++) {
    if (checkI2C(address)) {
      LOG_INF("I2C device present at address: 0x%x", address);
      numDevices++;
    }
  }
  LOG_INF("I2C devices found: %d", numDevices);
  return (bool)numDevices;
}
