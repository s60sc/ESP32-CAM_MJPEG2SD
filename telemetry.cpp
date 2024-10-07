//
// Telemetry data recorded to storage during camera recording
// Formatted as CSV file for presentation in spreadsheet
// and as a SRT file to provide video subtitles when used with a media player
// Sensor data obtained from user supplied libraries and code
// Need to check 'Use telemetry recording' under Peripherals button on Edit Config web page
// and have downloaded relevant device libraries.
// Best used on ESP32S3, not tested on ESP32

// s60sc 2023, 2024

#include "appGlobals.h"

#if INCLUDE_TELEM
#if !INCLUDE_I2C
#error "Need INCLUDE_I2C true"
#endif

#include <Wire.h>

// If separate I2C pins are not defined, then the telemetry I2C devices
// share the camera I2C pins: SIOD_GPIO_NUM and SIOC_GPIO_NUM in camera_pins.h are shared

#define NUM_BUFF 2 // CSV, SRT
#define MAX_LINE_LEN 128 // adjust to be max size of formatted telemetry row

TaskHandle_t telemetryHandle = NULL;
bool teleUse = false;
static int teleInterval = 1;
static char* teleBuf[NUM_BUFF]; // csv and srt telemetry data buffers
size_t highPoint[NUM_BUFF]; // indexes to buffers
static bool capturing = false;
static char teleFileName[FILE_NAME_LEN];
char srtBuffer[MAX_LINE_LEN]; // store each srt entry, for subtitle streaming
char csvHeader[MAX_LINE_LEN]; // column headers for CSV file
size_t srtBytes = 0;

/*************** USER TO MODIFY CODE BELOW for REQUIRED SENSORS ******************/

// example code for BMx280 and MPU9250 I2C sensors 
// if using GY-91 board (combination BMP280 + MPU9250), 
//  then in periphsI2C.cpp, set both USE_BMx280 and USE_MPU9250 to true

// user defined CSV header row per device used, must start with a comma
#define BME_CSV ",Temperature (C),Humidity (%),Pressure (mb),Altitude (m)"
#define BMP_CSV ",Temperature (C),Pressure (mb),Altitude (m)"
#define MPU_CSV ",Heading,Pitch,Roll"
// user defined SRT content line per device used, must start with 2 spaces
#define BME_SRT "  %0.1fC  %0.1fRH  %0.1fmb  %0.1fm"
#define BMP_SRT "  %0.1fC  %0.1fmb  %0.1fm"
#define MPU_SRT "  %0.1f  %0.1f  %0.1f"

static bool isBME = false;

static bool setupSensors() {
  // setup required sensors
  bool res = false;
#if USE_BMx280  
  if (checkI2Cdevice("BMx280")) {
    bool isBME = identifyBMx();
    LOG_INF("%s available", isBME ? "BME280" : "BMP280");
    if (isBME) strncat(csvHeader, BME_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    else strncat(csvHeader, BMP_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    res = true;
  } else LOG_WRN("%s not available", isBME ? "BME280" : "BMP280");
#endif

#if USE_MPU9250
  if (checkI2Cdevice("MPU9250")) {
    LOG_INF("MPU9250 available");
    strncat(csvHeader, MPU_CSV, MAX_LINE_LEN - strlen(csvHeader) - 1);
    res = true;
  } else LOG_WRN("MPU9250 not available");
#endif
  return res; 
}

static void getSensorData() {
  // get sensor data and format as csv row & srt entry in buffers
#if USE_BMx280
  float* bmxData = getBMx280();
  if (isBME) {
    highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f,%0.1f", bmxData[0], bmxData[3], bmxData[1], bmxData[2]);
    highPoint[1] += sprintf(teleBuf[1] + highPoint[1], BME_SRT, bmxData[0], bmxData[3], bmxData[1], bmxData[2]);
  } else {
    highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f", bmxData[0], bmxData[1], bmxData[2]);
    highPoint[1] += sprintf(teleBuf[1] + highPoint[1], BMP_SRT, bmxData[0], bmxData[1], bmxData[2]);
  }
#if INCLUDE_MQTT
  if (mqtt_active) {
    sprintf(jsonBuff, "{\"Temp\":\"%0.1f\", \"TIME\":\"%s\"}", bmxData[0], esp_log_system_timestamp());
    mqttPublish(jsonBuff);
  }
#endif
#endif

#if USE_MPU9250
  float* mpuData = getMPU9250();
  highPoint[0] += sprintf(teleBuf[0] + highPoint[0], ",%0.1f,%0.1f,%0.1f", mpuData[0], mpuData[1], mpuData[2]); 
  highPoint[1] += sprintf(teleBuf[1] + highPoint[1], MPU_SRT, mpuData[0], mpuData[1], mpuData[2]);  
#endif
}

/*************** LEAVE CODE BELOW AS IS UNLESS YOU KNOW WHAT YOUR DOING ******************/

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
    srtBytes = min(highPoint[1] - startData, (size_t)MAX_LINE_LEN);
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
    highPoint[0] = sprintf(teleBuf[0], "Time%s\n", csvHeader); 
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
    // initialise I2C port separate from camera if required
    if (I2Csda > 0) prepI2C();
    // setup telemetry collection and recording task
    prepI2Cdevices();
    teleInterval = srtInterval;
    for (int i=0; i < NUM_BUFF; i++) teleBuf[i] = psramFound() ? (char*)ps_malloc(RAMSIZE + MAX_LINE_LEN) : (char*)malloc(RAMSIZE + MAX_LINE_LEN);
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
  if (teleUse) strcpy(teleFileName, fileName); 
  capturing = false; // stop task
}

#endif
