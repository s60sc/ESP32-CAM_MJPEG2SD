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
#include <Wire.h>

#define NUM_BUFF 2 

TaskHandle_t telemetryHandle = NULL;
bool teleUse = false;
int teleInterval = 1;
static char* teleBuf[NUM_BUFF]; // csv and srt telemetry data buffers
size_t highPoint[NUM_BUFF]; // indexes to buffers
static bool capturing = false;
static char teleFileName[FILE_NAME_LEN];

static bool scanI2C();
static bool checkI2C(byte addr);

/*************** USER TO MODIFY CODE BELOW for REQUIRED SENSORS ******************/

// example code for BMP280 and MPU9250 I2C sensors on GY-91 board
//#define USE_GY91 // uncomment to support GY-91 board (BMP280 + MPU9250)

// user defined header row, first field is always Time, row must end with \n
#define TELEHEADER "Time,Temperature (C),Pressure (mb),Altitude (m),Heading,Pitch,Roll\n"
#define BUF_OVERFLOW 100 // set to be max size of formatted telemetry row

// if require I2C, define which pins to use for I2C bus
// if pins not correctly defined for board, spurious results will occur
#define I2C_SDA 20
#define I2C_SCL 21

#ifdef USE_GY91
#include <BMx280I2C.h>
#define BMP_ADDRESS 0x76 
#define STD_PRESSURE 1013.25 // standard pressure mb at sea level
#define DEGREE_SYMBOL "\xC2\xB0"
BMx280I2C bmp280(BMP_ADDRESS);

#include "MPU9250.h"
// accel axis orientation on GY-91:                      
// - X : short side (pitch)
// - Y : long side (roll)
// - Z : up (yaw from true N)
#define MPU_ADDRESS 0x68 
// Note internal AK8963 magnetometer is at address 0x0C
#define LOCAL_MAG_DECLINATION (4 + 56/60)  // see https://www.magnetic-declination.com/
MPU9250 mpu9250;
#endif

static bool setupSensors() {
  // setup required sensors
  bool res = false;
#ifdef USE_GY91
  Wire.begin(I2C_SDA, I2C_SCL); // join I2C bus as master 
  LOG_INF("I2C started at %dkHz", Wire.getClock() / 1000);
  if (!scanI2C()) return false;

  if (bmp280.begin()) {
    LOG_INF("BMP280 available");
    // set defaults
    bmp280.resetToDefaults();
    bmp280.writeOversamplingPressure(BMx280MI::OSRS_P_x16);
    bmp280.writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
  } else {
    LOG_WRN("BMP280 not available at address 0x%02X", BMP_ADDRESS);
    return false;
  } 
  
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
  res = true; 
#endif
  return res; 
}

static void getSensorData() {
  // get sensor data and format as csv row & srt entry in buffers
#ifdef USE_GY91
  bmp280.measure();
  if (bmp280.hasValue()) {
    float bmpPressure = bmp280.getPressure() * 0.01;  // pascals to mb/hPa
    float bmpAltitude = 44330.0 * (1.0 - pow(bmpPressure / STD_PRESSURE, 1.0 / 5.255)); // altitude in meters
    highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "%0.1f,%0.1f,%0.1f,", bmp280.getTemperature(), bmpPressure, bmpAltitude);
    highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "  %0.1fC  %0.1fmb  %0.1fm", bmp280.getTemperature(), bmpPressure, bmpAltitude);
  } else for (int i=0; i< 2; i++) highPoint[i] += sprintf(teleBuf[i] + highPoint[i], "-,-,-,");
  
  if (mpu9250.update()) {
    highPoint[0] += sprintf(teleBuf[0] + highPoint[0], "%0.1f,%0.1f,%0.1f,", mpu9250.getYaw(), mpu9250.getPitch(), mpu9250.getRoll()); 
    highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "  %0.1f  %0.1f  %0.1f", mpu9250.getYaw(), mpu9250.getPitch(), mpu9250.getRoll()); 
  }
#endif
}

/*************** LEAVE CODE BELOW AS IS ******************/

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
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%d\n%s --> ", srtSeqNo++, timeStr);
      srtTime += sampleInterval;
      formatElapsedTime(timeStr, srtTime, true);
      highPoint[1] += sprintf(teleBuf[1] + highPoint[1], "%s\n", timeStr);
      // write current time for csv row
      time_t currEpoch = getEpoch();
      for (int i = 0; i < NUM_BUFF; i++) highPoint[i] += strftime(teleBuf[i] + highPoint[i], 10, "%H:%M:%S,", localtime(&currEpoch));
      // get data from sensors 
      getSensorData();
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
    LOG_INF("Saved telemetry files");
  }
}

void prepTelemetry() {
  // called by app initialisation
  if (teleUse) {
    for (int i=0; i < NUM_BUFF; i++) teleBuf[i] = psramFound() ? (char*)ps_malloc(RAMSIZE + BUF_OVERFLOW) : (char*)malloc(RAMSIZE + BUF_OVERFLOW);
    if (setupSensors()) xTaskCreate(&telemetryTask, "telemetryTask", TELEM_STACK_SIZE, NULL, 3, &telemetryHandle);
    else teleUse = false;
    LOG_INF("Telemetry recording %s available", teleUse ? "is" : "NOT");
    debugMemory("prepTelemetry");
  }
}

void startTelemetry() {
  // called when camera recording started
  if (teleUse && telemetryHandle != NULL) xTaskNotifyGive(telemetryHandle); // wake up task
}

void stopTelemetry(const char* fileName) {
  // called when camera recording stopped
  if (teleUse) {
    strcpy(teleFileName, fileName); 
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
      LOG_INF("I2C device present at address: 0x%02X", address);
      numDevices++;
    }
  }
  LOG_INF("I2C devices found: %d", numDevices);
  return (bool)numDevices;
}
