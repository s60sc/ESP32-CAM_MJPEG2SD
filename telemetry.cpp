//
// Telemetry data recorded to storage during camera recording
// Formatted as CSV file for presentation in spreadsheet
// and as a SRT file to provide video subtitles when used with a media player
// Sensor data obtained from user supplied libraries and code
// Need to check 'Use telemetry recording' under Peripherals button on Edit Config web page
// and have downloaded relevant device libraries.
// Best used on ESP32S3, not tested on ESP32

// s60sc 2023

#include "appGlobals.h"

#if INCLUDE_TELEM

#define NUM_BUFF 2 
#define MAX_SRT_LEN 128 // store each srt entry, for subtitle streaming

TaskHandle_t telemetryHandle = NULL;
bool teleUse = false;
static int teleInterval = 1;
static char* teleBuf[NUM_BUFF]; // csv and srt telemetry data buffers
size_t highPoint[NUM_BUFF]; // indexes to buffers
static bool capturing = false;
static char teleFileName[FILE_NAME_LEN];
char srtBuffer[MAX_SRT_LEN];
size_t srtBytes = 0;

/*************** USER TO MODIFY CODE BELOW for REQUIRED SENSORS ******************/

// This code is for initializing and reading data from various sensors (BMP280, BME280, GY-91) 
// and formatting the data for telemetry output.
//#define USE_GY91 // uncomment to support GY-91 board (BMP280 + MPU9250)
//#define USE_BMP280 // uncomment to support GY-BMP280 board (BMP280)
#define USE_BME280 // uncomment to support GY-BME280 board (BME280)

// Define the telemetry header row based on the sensor used
// The first field is always Time, and the row must end with \n
#if defined(USE_BMP280)
  #define USE_BMx280
  #define TELEHEADER "Time,Temperature (C),Pressure (mb),Altitude (m)\n"
#elif defined(USE_BME280)
  #define USE_BMx280
  #define TELEHEADER "Time,Temperature (C),Humidity (%),Pressure (mb),Altitude (m)\n"
#elif defined(USE_GY91)
  #define USE_BMx280
  #define TELEHEADER "Time,Temperature (C),Pressure (mb),Altitude (m),Heading,Pitch,Roll\n"
#else
  #define TELEHEADER "\n"
#endif

// Buffer overflow size for formatted telemetry row
#define BUF_OVERFLOW 100 // set to be max size of formatted telemetry row

// if require I2C, define which pins to use for I2C bus
// if pins not correctly defined for board, spurious results will occur
#ifndef I2C_SDA
#define I2C_SDA 20
#define I2C_SCL 21
#endif

#ifdef USE_BMx280
#include <BMx280I2C.h>
#define BMx_ADDRESS 0x76 // I2C address for BMx280 sensor
#define STD_PRESSURE 1013.25 // Standard atmospheric pressure at sea level in mb
#define DEGREE_SYMBOL "\xC2\xB0" // Degree symbol for temperature display
BMx280I2C bmx280(BMx_ADDRESS); // Create an instance of the BMx280 sensor
#endif

#ifdef USE_GY91
#include "MPU9250.h"
// Accelerometer axis orientation on GY-91:                   
// - X : short side (pitch)
// - Y : long side (roll)
// - Z : up (yaw from true N)
#define MPU_ADDRESS 0x68 
// Note internal AK8963 magnetometer is at address 0x0C

// Local magnetic declination (in degrees) for your location
#define LOCAL_MAG_DECLINATION (4 + 56/60) // You can find the value for your location at https://www.magnetic-declination.com/
MPU9250 mpu9250; // Create an instance of the MPU9250 sensor
#endif

static bool setupSensors() {
  // setup required sensors
  bool res = true;
    initializeI2C(I2C_SDA, I2C_SCL);

  #ifdef USE_BMx280
    if (bmx280.begin()) {
      LOG_INF("BMx280 available");
      // Set sensor to default settings
      bmx280.resetToDefaults();
      bmx280.writeOversamplingPressure(BMx280MI::OSRS_P_x16);
      bmx280.writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
      //if sensor is a BME280, set an oversampling setting for humidity measurements.
      if (bmx280.isBME280()) {
        bmx280.writeOversamplingHumidity(BMx280MI::OSRS_H_x16);
      }
    } else {
      LOG_WRN("BMX280 not available at address 0x%02X", BMx_ADDRESS);
      return false;
    }
  #endif

  #ifdef USE_GY91
    if (mpu9250.setup(MPU_ADDRESS)) {
      mpu9250.setMagneticDeclination(LOCAL_MAG_DECLINATION);
      mpu9250.selectFilter(QuatFilterSel::MADGWICK);
      mpu9250.setFilterIterations(15);
      LOG_INF("MPU9250 calibrating, leave still");
      mpu9250.calibrateAccelGyro();
  //    LOG_INF("Move MPU9250 in a figure of eight until done");
  //    delay(2000);
  //    mpu9250.calibrateMag();
      LOG_INF("MPU9250 available");
    }
    else {
      LOG_WRN("MPU9250 not available at address 0x%02X", MPU_ADDRESS);
      return false;
    }
  #endif
  return res; 
}

static void getSensorData() {
  // Get sensor data and format as CSV row & SRT entry in buffers
  #ifdef USE_GY91
    if (mpu9250.update()) {
      highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "%0.1f,%0.1f,%0.1f,", mpu9250.getYaw(), mpu9250.getPitch(), mpu9250.getRoll()); 
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "  %0.1f  %0.1f  %0.1f", mpu9250.getYaw(), mpu9250.getPitch(), mpu9250.getRoll()); 
    }
  #endif
  #ifdef USE_BMx280
    bmx280.measure();
    while (!bmx280.hasValue());
    if (bmx280.hasValue()) {
      float bmxPressure = bmx280.getPressure() * 0.01;  // pascals to mb/hPa
      float bmxAltitude = 44330.0 * (1.0 - pow(bmxPressure / STD_PRESSURE, 1.0 / 5.255)); // altitude in meters
      if (bmx280.isBME280()) {
        highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "%0.1f,%0.1f,%0.1f,%0.1f,", bmx280.getTemperature(), bmx280.getHumidity(), bmxPressure, bmxAltitude);
        highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "  %0.1fC  %0.1fRH  %0.1fhPa  %0.1fm", bmx280.getTemperature(), bmx280.getHumidity(), bmxPressure, bmxAltitude);
        #if INCLUDE_MQTT
          if (mqtt_active) {
            sprintf(jsonBuff, "{\"Temp\":\"%s\", \"TIME\":\"%s\"}", bmx280.getTemperature(), esp_log_system_timestamp());
            mqttPublish(jsonBuff);
          }
        #endif
      } else {
        highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "%0.1f,%0.1f,%0.1f,", bmx280.getTemperature(), bmxPressure, bmxAltitude);
        highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "  %0.1fC  %0.1fmb  %0.1fm", bmx280.getTemperature(), bmxPressure, bmxAltitude);
      }

    } else for (int i=0; i< 2; i++) highPoint[i] += sprintf(teleBuf[i] + highPoint[i], "-,-,-,");
  #endif
}

/*************** LEAVE CODE BELOW AS IS ******************/

void storeSensorData(bool fromStream) {
  // can be called from telemetry task or streaming task
  if (fromStream) {
    // called fron streaming task
    if (capturing) return; // as being stored by telemetry task
    else highPoint[0] = highPoint[1] = 0;
  }
  size_t startData = highPoint[1];
  getSensorData();
  if (!srtBytes) { 
    srtBytes = min(highPoint[1] - startData, (size_t)MAX_SRT_LEN);
    memcpy(srtBuffer, teleBuf[1] + startData, srtBytes);
  }
}

static void telemetryTask(void* pvParameters) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    capturing = true;
    int srtSeqNo = 1;
    uint32_t srtTime = 0;
    char timeStr[10];
    uint32_t sampleInterval = 1000 * (teleInterval < 1 ? 1 : teleInterval);
    // open storage file
    if (STORAGE.exists(TELETEMP)) STORAGE.remove(TELETEMP);
    if (STORAGE.exists(SRTTEMP)) STORAGE.remove(SRTTEMP);
    File teleFile = STORAGE.open(TELETEMP, FILE_WRITE);
    File srtFile = STORAGE.open(SRTTEMP, FILE_WRITE);
    // write CSV header row to buffer
    highPoint[0] = sprintf(teleBuf[0], "%s", TELEHEADER); 
    highPoint[1] = 0;
    
    // loop while camera recording
    while (capturing) {
      uint32_t startTime = millis();
      // write header for this subtitle
      formatElapsedTime(timeStr, srtTime, true);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%d\n%s,000 --> ", srtSeqNo++, timeStr);
      srtTime += sampleInterval;
      formatElapsedTime(timeStr, srtTime, true);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%s,000\n", timeStr);
      // write current time for csv row and srt entry
      time_t currEpoch = getEpoch();
      for (int i = 0; i < NUM_BUFF; i++) highPoint[i] += strftime(teleBuf[i] + highPoint[i], 10, "%H:%M:%S", localtime(&currEpoch));
      // get and store data from sensors 
      storeSensorData(false);
      // add newline to finish row
      highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "\n"); 
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "\n\n");
      
      // if marker overflows buffer, write to storage
      for (int i = 0; i < NUM_BUFF; i++) {
        if (highPoint[i] >= RAMSIZE) {
          highPoint[i] -= RAMSIZE;
          if (i) srtFile.write((uint8_t*)teleBuf[i], RAMSIZE);
          else teleFile.write((uint8_t*)teleBuf[i], RAMSIZE);
          // push overflow to buffer start
          memcpy(teleBuf[i], teleBuf[i]+RAMSIZE, highPoint[i]);
        }
      }
      // wait for next collection interval
      while (millis() - sampleInterval < startTime) delay(10);
    }
    
    // capture finished, write remaining buff to storage 
    if (highPoint[0]) teleFile.write((uint8_t*)teleBuf[0], highPoint[0]);
    if (highPoint[1]) srtFile.write((uint8_t*)teleBuf[1], highPoint[1]);
    teleFile.close();
    srtFile.close();
    // rename temp files to specific file names using avi file name with relevant extension
    changeExtension(teleFileName, CSV_EXT);
    STORAGE.rename(TELETEMP, teleFileName);
    changeExtension(teleFileName, SRT_EXT);
    STORAGE.rename(SRTTEMP, teleFileName);
    LOG_INF("Saved %d entries in telemetry files", srtSeqNo);
  }
}

void prepTelemetry() {
  // called by app initialisation
  if (teleUse) {
    teleInterval = srtInterval;
    for (int i=0; i < NUM_BUFF; i++) teleBuf[i] = psramFound() ? (char*)ps_malloc(RAMSIZE + BUF_OVERFLOW) : (char*)malloc(RAMSIZE + BUF_OVERFLOW);
    if (setupSensors()) xTaskCreate(&telemetryTask, "telemetryTask", TELEM_STACK_SIZE, NULL, TELEM_PRI, &telemetryHandle);
    else teleUse = false;
    LOG_INF("Telemetry recording %s available", teleUse ? "is" : "NOT");
    debugMemory("prepTelemetry");
  }
}

bool startTelemetry() {
  // called when camera recording started
  bool res = true;
  if (teleUse && telemetryHandle != NULL) xTaskNotifyGive(telemetryHandle); // wake up task
  else res = false;
  return res;
}

void stopTelemetry(const char* fileName) {
  // called when camera recording stopped
  if (teleUse) {
    strcpy(teleFileName, fileName); 
    capturing = false; // stop task
  }
}

#endif
