
// I2C driver and devices
//
// OLED SSD1306 display 128*64
// PCF8591 ADC
// BM*280 temperature, pressure, altitude for BMP280, + humidity for BME280
// MPU6050 6 axis accel & gyro
// MPU9250 9 axis accel & gyro & mag
// VL53L1X TOF distance sensor
// DS3231 RTC
// LCD 1602 display 2*16 
//
// To enable a device, set the appropriate USE_* define in appGlobals.h
//
// s60sc 2023, 2024, 2025, 2026
// incorporates contributions from rjsachse

#include "appGlobals.h"

#if INCLUDE_I2C

#define SENSOR_TIMEOUT 100 // max time in ms to wait for sensor response
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define I2C0_CLOCK_HZ 400000
#else
#define I2C0_CLOCK_HZ 100000
#endif
#define I2C1_CLOCK_HZ 400000

#include <Wire.h>

// define which pins to use for I2C bus in call to initializeI2C()
// if pins not correctly defined for board, spurious results will occur
int I2C0sda = -1;
int I2C0scl = -1;
int I2C1sda = -1;
int I2C1scl = -1;
static byte I2CDATA[10]; // store I2C data received or to be sent 
static int I2C0devices = -1;
static int I2C1devices = -1;
static bool isShared = false;
  
// I2C device names, indexed by address
static bool deviceStatus[128] = {false}; // whether device present
static const char* clientName[128] = {
  "", "", "", "", "", "", "", "", "", "", "", "", "AK8963", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "PY260",
  "", "", "", "", "", "", "", "LCD1602", "", "VL531LX", "", "", "", "", "", "",
  "OV2640", "", "", "", "", "", "", "", "", "", "", "", "OV5640/SSD1306", "SSD1306", "", "",
  "", "", "", "", "", "", "", "", "PCF8591", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "OV2640", "OV2640", "", "", "", "", "", "", "MPUxxxx/DS3231", "MPUxxxx", "", "", "", "", "", "",
  "", "", "", "", "", "", "MS5611/BMx280", "BMx280/MS5611", "OV5640", "OV5640", "", "", "", "", "", ""};

static bool prepI2Cdevices();

/********************* Generic I2C Utilities ***********************/

static bool doTransmission(int clientAddr, bool scanning, int whichI2C = 0, TwoWire *thisWire = &Wire) {
  // common function used to send request to I2C device and determine outcome
  byte result = thisWire->endTransmission(true);
    /*1: data too long to fit in transmit buffer
      2: received NACK on transmit of address
      3: received NACK on transmit of data
      4: other error, e.g. switched off 
      5: i2c busy 
      8: unknown pcf8591 status */
  if (!scanning && result > 0) LOG_WRN("Client %s on I2C%d at addr 0x%02X with connection error: %d", 
      clientName[clientAddr], whichI2C, clientAddr, result);
  return result == 0;
}

static void scanI2C(int whichI2C = 0, TwoWire *thisWire = &Wire) {
  // find details of any active I2C devices
  LOG_INF("I2C%d device scanning", whichI2C);
  for (byte address = 1; address < 127; address++) {
    thisWire->beginTransmission(address);
    // only report error if client device meant to be present
    if (doTransmission(address, true, whichI2C, thisWire)) {
      LOG_INF("I2C device %s present on I2C%d at address: 0x%02X", clientName[address], whichI2C, address);
      whichI2C ? I2C1devices++ : I2C0devices++;
      deviceStatus[address] = true;
    }
  }
  LOG_INF("I2C%d devices found: %d", whichI2C, whichI2C ? I2C1devices : I2C0devices);
}

static bool getI2Cdata(uint8_t clientAddr, uint8_t controlByte, uint8_t numBytes, int whichI2C = 0, TwoWire *thisWire = &Wire) {
  // send command to I2C client and receive response
  // clientAddr is the I2C address
  // controlByte is the control instruction
  // numBytes is number of bytes to request
  thisWire->beginTransmission(clientAddr);  // select which client to use
  thisWire->write(controlByte);             // send device command
  if (doTransmission(clientAddr, true, whichI2C, thisWire)) {
    // get required number of bytes
    thisWire->requestFrom(clientAddr, numBytes);
    thisWire->readBytes(I2CDATA, numBytes);
    return doTransmission(clientAddr, false, whichI2C, thisWire);
  }
  return false;
}

static bool sendI2Cdata(int clientAddr, uint8_t controlByte, uint8_t numBytes, int whichI2C = 0, TwoWire *thisWire = &Wire) {
  // send data to I2C device
  // clientAddr is the I2C address
  // controlByte is the control instruction
  // numBytes is number of bytes to send
  thisWire->beginTransmission(clientAddr);
  if (controlByte) thisWire->write(controlByte);
  for (int i=numBytes-1; i>=0; i--) thisWire->write(I2CDATA[i]);
  return doTransmission(clientAddr, false, whichI2C, thisWire);
}

bool shareI2C(int sdaShare, int sclShare) {
  // apply camera pin definitions if I2C0 to be shared with camera
  /* needs arduino-esp32 core v3.3.0 or higher */
  if (I2C0sda < 0) { 
    // I2C bus shared with another peripheral, eg camera
    I2C0sda = sdaShare;
    I2C0scl = sclShare;
    isShared = true;
    Wire.begin(sdaShare, sclShare); // Join I2C bus as master
    Wire.setClock(I2C0_CLOCK_HZ);
    LOG_INF("I2C bus shared with camera");
  }
  return isShared;
}

bool prepI2C() {
  // start I2C port if not shared then prep I2C peripherals
  if (I2C0sda == I2C0scl) {
    LOG_ALT("I2C0 pins not defined: %d, %d", I2C0sda, I2C0scl);
    return false;
  }
  // start I2C0
  if (!isShared) {
    Wire.begin(I2C0sda, I2C0scl);  // Join I2C bus as master
    Wire.setClock(I2C0_CLOCK_HZ);  // default 100kHz, max 400kHz
  }
  LOG_INF("%sI2C0 initialised at %ldkHz using pins SDA: %d, SCL: %d", isShared ? "Shared " : "", 
      Wire.getClock() / 1000, I2C0sda, I2C0scl);
  I2C0devices = 0;
  scanI2C(0, &Wire);

#if !defined(CONFIG_IDF_TARGET_ESP32C3)
  // start I2C1 if required (pins defined)
  if (I2C1sda != I2C1scl) {
    // I2C1 pins defined
    Wire1.begin(I2C1sda, I2C1scl);  // Join I2C bus as master
    Wire1.setClock(I2C1_CLOCK_HZ);  // default 100kHz, max 400kHz
    LOG_INF("I2C1 initialised at %ldkHz using pins SDA: %d, SCL: %d", Wire.getClock() / 1000, I2C1sda, I2C1scl);
    I2C1devices = 0;
    scanI2C(1, &Wire1);
  }
#endif

  return prepI2Cdevices();
}


/***************************************** OLED Display *************************************/

#define SSD1306_BIaddr 0x3d // built in oled 
#define SSD1306_Extaddr 0x3c // external oled (also address for OV5640
#if USE_SSD1306

#if __has_include("../libraries/ESP8266_and_ESP32_OLED_driver_for_SSD1306_displays/src/SSD1306Wire.h")
#include "SSD1306Wire.h"  // https://github.com/ThingPulse/esp8266-oled-ssd1306

SSD1306Wire oledBI(SSD1306_BIaddr);
SSD1306Wire oledExt(SSD1306_Extaddr);
SSD1306Wire* thisOled;

static bool oledOK = false;
bool flipOled = false; // true if oled pins oriented above display

// OLED SSD1306 display 128*64
void oledLine(const char* msg, int hpos, int vpos, int msgwidth, int fontsize) { 
  // display text message on OLED SSD1306 display 
  // to avoid flicker, only call periodically
  // args: message string, horizontal pixel start, vertical pixel start, width to clear, font type
  // clear original line
  if (oledOK) {
    thisOled->setTextAlignment(TEXT_ALIGN_LEFT);
    thisOled->setColor(BLACK);
    thisOled->fillRect(hpos, vpos, msgwidth, fontsize*5/4); // allow for tails on fonts
    // display given text, fontsizes are 10, 16, 24, starting at horiz pixel hpos & vertical pixel vpos
    thisOled->setFont(ArialMT_Plain_10);
    if (fontsize == 16) thisOled->setFont(ArialMT_Plain_16);
    if (fontsize == 24) thisOled->setFont(ArialMT_Plain_24);
    thisOled->setColor(WHITE);
    thisOled->drawString(hpos, vpos, msg);
  }
}

static void tellTale() { 
  static bool ledState = false;
  ledState = !ledState;
  static const char* tellTaleStr[] = {"*", ""}; // shows that oled (& I2C) are running
  oledLine(tellTaleStr[ledState],124,60,4,10); 
}

void oledDisplay() {
  if (oledOK) {
    tellTale();   // oled telltale
    thisOled->display();
  }
}

static bool setupOled() {
  if (!oledOK) {
    oledOK = true;
    if (deviceStatus[SSD1306_BIaddr]) thisOled = &oledBI;
    else if (deviceStatus[SSD1306_Extaddr]) thisOled = &oledExt;
    else oledOK = false;
    if (oledOK) {
      thisOled->end();
      if (thisOled->init()) { if (flipOled) thisOled->flipScreenVertically(); }
      else oledOK = false;
    }
    if (!oledOK) LOG_WRN("SSD1306 oled not available");
  }
  return oledOK;
}

void finalMsg(const char* finalTxt) {
  if (oledOK) {
    // display message on persistent oled screen before esp32 goes to sleep
    thisOled->resetDisplay();
    oledLine(finalTxt,0,0,128,16);
    thisOled->display();
    delay(2000); // keep tag displayed
  }
}

#else
#error "Need to install ESP8266_and_ESP32_OLED_driver_for_SSD1306_displays library"
#endif

#endif // USE_SSD1306

/*********************** PCF8591 ************************/

#define PCF8591addr 0x48 // PCF8591 ADC

byte* getPCF8591() { // analog channels
/*   
   YL-40 module
   return the 4 ADC channel 8 bit values, using auto increment control instruction
   PC8591 commands:
   bits 0-1: channel 0 (00) -> 3 (11)
   bit 3: autoincrement
   bits 4-5: input programming, separate inputs (00), etc
   bit 6: analog out enable
  */
  static byte PCF8591[4] = {0}; 
  if (deviceStatus[PCF8591addr]) {
    if (getI2Cdata(PCF8591addr, 0x44, 5)) {
      // need to read 5 bytes, but ignore first as it is previous 0 channel
      // order high -> low channels 3 2 1 0
      for (int i = 0; i < 4; i++) PCF8591[i] = smoothAnalog(I2CDATA[i + 1]);
    } 
  } else LOG_WRN("PCF8591 ADC not available");
  return PCF8591;
}

/******************************* BMP280 / BME280 ******************************/

#define BMx280_Def 0x76 // BMX280 default address
#define BMx280_Alt 0x77 // BMX280 alternative address
static bool BMx280ok = false;

#if USE_BMx280
#define IS_POLLABLE

#define BME_ID 0x60
#define BMP_ID 0x58
#define STD_PRESSURE 1013.25f // reference pressure in mB/hPa at sea level
#define DEGREE_SYMBOL "\xC2\xB0"

#if __has_include("../libraries/SparkFun_BME280/src/SparkFunBME280.h")
#include <SparkFunBME280.h>

static float BMx280[4] = {0};
static bool isBME = false;
static float pressureMSL = STD_PRESSURE;
BME280 bmp;

#define EXT_MSL_HOST "api.open-meteo.com"
#define EXT_MSL_PATH "/v1/forecast?latitude=%0.6f&longitude=%0.6f&current=pressure_msl"

static float getSeaLevelPressure() {
  // Get Mean Sea Level pressure for given location
  NetworkClient hclient;
  char jsonVal[FILE_NAME_LEN] = "";
  if (remoteServerConnect(hclient, EXT_MSL_HOST, HTTP_PORT, GETEXTMSL)) {
    HTTPClient http;
    int httpCode = HTTP_CODE_NOT_FOUND;
    char extMslPath[100];
    sprintf(extMslPath, EXT_MSL_PATH, latLon[0], latLon[1]);
    if (http.begin(hclient, EXT_MSL_HOST, HTTP_PORT, extMslPath)) {
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        if (!getJsonValue(payload.c_str(), "pressure_msl", jsonVal, nullptr, 2)) LOG_WRN("'pressure_msl' field not present");
      } else LOG_WRN("MSL pressure request failed, error: %s", http.errorToString(httpCode).c_str());    
      http.end();
    }
    remoteServerClose(hclient);
  }
  return jsonVal[0] ? atof(jsonVal) : STD_PRESSURE; // in hPa / mbars
}

static bool setupBMx() {
  // setup BMx280 if available
  if (!BMx280ok) {
    BMx280ok = true;
    int bmpAddr = 0;
    if (deviceStatus[BMx280_Def]) bmpAddr = BMx280_Def;
    else if (deviceStatus[BMx280_Alt]) bmpAddr = BMx280_Alt;
    else BMx280ok = false;
    if (BMx280ok) {
      bmp.setI2CAddress(bmpAddr);
      BMx280ok = bmp.beginI2C(Wire);
      if (BMx280ok) {
        uint8_t chipID = bmp.begin();  // frig to get chip id
        if (chipID == BME_ID) isBME = true;
        else if (chipID != BMP_ID) BMx280ok = false;
        if (BMx280ok) {
          // apply settings
          bmp.setFilter(1);              // 0 to 4 is valid. Increase if more prop wash reduction needed
          bmp.setStandbyTime(0);         // 0.5ms
          bmp.setTempOverSample(1);      // 0 to 16 are valid. 0 disables temp sensing.
          bmp.setPressureOverSample(4);  // 0 to 16 are valid. 0 disables pressure sensing.
          if (isBME) bmp.setHumidityOverSample(1);
          bmp.setMode(MODE_NORMAL);  //MODE_SLEEP, MODE_FORCED, MODE_NORMAL is valid.
          pressureMSL = getSeaLevelPressure();
          bmp.setReferencePressure(pressureMSL * 100.0);
          LOG_INF("BM%c280 settings complete, local MSL pressure %0.1fhPa @ Lat %0.6f / Lon %0.6f",
                        isBME ? 'E' : 'P', pressureMSL, latLon[0], latLon[1]);
        } else LOG_WRN("Unknown BMx280 chip id 0x%02X", chipID);
      }
    }
    if (!BMx280ok) LOG_WRN("BM%c280 not available - check wiring", isBME ? 'E' : 'P');
  }
  return BMx280ok;
}

float* updateBMx280() {
  // get and store pressure, temperature, altitude, humidity
  if (BMx280ok) {
    BMx280[0] = bmp.readTempC();                     // celsius
    BMx280[1] = bmp.readFloatPressure() / 100.0f;     // hPa / mb
    BMx280[2] = bmp.readFloatAltitudeMeters();       // altitude in meters
    if (isBME) BMx280[3] = bmp.readFloatHumidity();  // % relative humidity
  }
  return BMx280;
}

float* getBMx280() {
  return BMx280;
}

bool identifyBMx() {
  return isBME;
}

#else
#error "Need to install SparkFun_BME280 library"
#endif // __has_include

#endif // USE_BMx280


/**************************** MPU6050 / MPU9250 ******************************/

#define MPUxxxx_HIGH 0x69 // MPU6050 / MPU9250 I2C address if AD0 pulled high
#define MPUxxxx_LOW 0x68  // MPU6050 / MPU9250 I2C address if AD0 grounded
static bool MPU9250ok = false;
static bool MPU6050ok = false;

#if USE_MPU
#define IS_POLLABLE

// MPU model identifiers
#define MPU9250_ID 0x71
#define MPU9255_ID 0x73
#define MPU6500_ID 0x70
#define MPU6050_ID 0x68
#define WHOAMI_REG 0x75

static float mpuData[11] = {0};
static char mpuModel[10] = "Unknown";
static bool haveMag = false;
static uint8_t MPUaddr;

/********************************* MPU9250 ********************************/

/*
MPU9250 on GY-91
VIN: Voltage Supply Pin for > 3V3, if not used can be connected to pull high SDO/SAO or CSB
3V3: 3.3v Regulator output / or 3V3 input
GND: 0V Power Supply
SCL: I2C Clock 
SDA: I2C Data 
SDO/SAO: I2C Address selection MPU9250, pull high for Alt addr
NCS: n/a
CSB: I2C Address selection BMP280, pull high for Alt addr
*/

#if __has_include("../libraries/MPU9250_WE/src/MPU9250_WE.h")
#include <MPU9250_WE.h>
#define USE_MPU9250

MPU9250_WE mpuLo_0 = MPU9250_WE(&Wire, MPUxxxx_LOW);
MPU9250_WE mpuHi_0 = MPU9250_WE(&Wire, MPUxxxx_HIGH);
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
MPU9250_WE mpuLo_1 = MPU9250_WE(&Wire1, MPUxxxx_LOW);
MPU9250_WE mpuHi_1 = MPU9250_WE(&Wire1, MPUxxxx_HIGH);
#endif
MPU9250_WE *mpu;

// accel axis orientation on GY-91:
// - X : short side (pitch)
// - Y : long side (roll)
// - Z : up (yaw from true N)
// Note internal AK8963 magnetometer is at address 0x0C
#define LOCAL_MAG_DECLINATION 0.0f // can manually set instead of web update
#define EXT_MAG_HOST "geomag.bgs.ac.uk"
#define EXT_MAG_PATH "/web_service/GMModels/wmm/2025/?latitude=%0.6f&longitude=%0.6f&format=json"
static float magDecl = LOCAL_MAG_DECLINATION;

static float getMagneticDeclination() {
  // Get Magnetic Declination for given location to magnetic compass correct for True North
  NetworkClient hclient;
  char jsonVal[FILE_NAME_LEN] = "";
  float sign = 1.0f;
  float angle = 0.0f;
  if (remoteServerConnect(hclient, EXT_MAG_HOST, HTTP_PORT, GETEXTMAG)) {
    HTTPClient http;
    int httpCode = HTTP_CODE_NOT_FOUND;
    char extMagPath[100];
    sprintf(extMagPath, EXT_MAG_PATH, latLon[0], latLon[1]);
    if (http.begin(hclient, EXT_MAG_HOST, HTTP_PORT, extMagPath)) {
      httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        if (getJsonValue(payload.c_str(), "declination", jsonVal, "value")) {
          angle = atof(jsonVal);
          if (getJsonValue(payload.c_str(), "declination", jsonVal, "units")) {
            if (strstr(jsonVal, "west")) sign = -1.0f;
          }
        } else LOG_WRN("'declination' field not present");
      } else LOG_WRN("Magnetic declination request failed, error: %s", http.errorToString(httpCode).c_str());    
      http.end();
    }
    remoteServerClose(hclient);
  }
  return jsonVal[0] ? sign * angle : LOCAL_MAG_DECLINATION; // in degrees
}

float* updateMPU9250data() {
  // get data from MPU9250 and return as array
  // only some functions obtained
  if (MPU9250ok) {
    xyzFloat a = mpu->getGValues(); // acc
    if (a.x > 1.0) a.x = 1.0;
    else if(a.x < -1.0) a.x = -1.0;
    float angleX = (asin(a.x)) * RAD_TO_DEG; 
    if (a.y > 1.0) a.y = 1.0;
    else if (a.y < -1.0) a.y = -1.0;
    float angleY = (asin(a.y)) * RAD_TO_DEG;
    if (a.z > 1.0) a.z = 1.0;
    else if(a.z < -1.0) a.z = -1.0;
    float angleZ = (asin(a.z)) * RAD_TO_DEG;
    
    mpuData[0] = (atan2(-angleX, sqrt(abs((angleY*angleY + angleZ*angleZ))))*180.0)/PI; // acc Pitch angle
    mpuData[1] = (atan2(angleY, angleZ)*180.0)/PI; // acc Roll angle
    mpuData[3] = mpu->getTemperature();  // celsius
    xyzFloat g = mpu->getGyrValues(); // gyro
    mpuData[4] = g.x;
    mpuData[5] = g.y;
    mpuData[6] = g.z;
    mpuData[7] = a.x;
    mpuData[8] = a.y;
    mpuData[9] = a.z; // acc vertical acceleration
    mpuData[10] = mpu->getResultantG(a);
#if !INCLUDE_DRONE
    xyzFloat m = mpu->getMagValues();
    // Assumes X = East, Y = North, Z = vertical and device level (not tilted)
    const float degrees = atan2(m.x, m.y) * 180 / PI;
    mpuData[2] = magDecl + fmod(fmod(degrees, 360.0f) + 360.0f, 360.0f); // compass north
#endif
#ifdef GY91_BACKWARD 
      // horizontal angles reversed
      mpuData[0] = -mpuData[0];
      mpuData[1] = -mpuData[1];
      mpuData[4] = -mpuData[4];
      mpuData[5] = -mpuData[5];
      mpuData[7] = -mpuData[7];
      mpuData[8] = -mpuData[8];
#endif
  }
  return mpuData;
}

static bool setupMPU9250(int whichI2c = 0) {
  if (!MPU9250ok) {
    if (whichI2c == 0) mpu = (MPUaddr) == MPUxxxx_HIGH ? &mpuHi_0 : &mpuLo_0; // default 12C0
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
    else mpu = (MPUaddr) == MPUxxxx_HIGH ? &mpuHi_1 : &mpuLo_1; // I2C1
#endif
    if (mpu->init()) {
      // calibration - drone needs to be level and still
      delay(1000);
      mpu->autoOffsets();
      mpu->enableGyrDLPF();
      mpu->setGyrDLPF(MPU9250_DLPF_2);           // 92Hz filter bandwidth
      mpu->setGyrRange(MPU9250_GYRO_RANGE_500);  // 500 deg/sec
      mpu->enableAccDLPF(true);
      mpu->setAccDLPF(MPU9250_DLPF_4);         // 20Hz filter bandwidth
      mpu->setAccRange(MPU9250_ACC_RANGE_4G);  // low speed drone
      mpu->setSampleRateDivider(4);            // 250Hz output rate
#if !INCLUDE_DRONE
      // prep magnetometer
      magDecl = getMagneticDeclination();
      if (mpu->initMagnetometer()) {
        mpu->setMagOpMode(AK8963_CONT_MODE_8HZ);  // continuous at 8Hz

        LOG_INF("MPU magnetometer calibrated, local magnetic declination %0.3f° @ Lat %0.6f / Lon %0.6f", magDecl, latLon[0], latLon[1]);
      } else haveMag = false;
#endif
      MPU9250ok = true;
      LOG_INF("%s calibrated and settings applied", mpuModel);
    } else LOG_WRN("%s not available - check wiring", mpuModel);
  } 
  return MPU9250ok;
}

#else
#error "Need to install MPU9250 library"
#endif // __has_include

#ifndef USE_MPU9250
static bool setupMPU9250(int whichI2C) {return false;} // dummy
#endif

/********************************* MPU6050 ********************************/

// For simple use as a motion sensor or tilt angle the MPU6050 accelerometer can be used without a library
// MPU6050 definitions - not gyroscope
#define SENS_2G (32768.0/2.0) // divider for 2G sensitivity reading
#define ACCEL_BYTES 6 // 2 bytes per axis
#define CONFIG 0x1A
#define ACCEL_CONFIG 0x1C
#define ACCEL_XOUT_H 0x3B
#define PWR_MGMT_1 0x6B

bool sleepMPU6050(bool doSleep) {
  // power down or wake up MPU6050 
  I2CDATA[0] = doSleep ? 0x40 : 0x01;
  // PWR_MGMT_1 register set to sleep
  return sendI2Cdata(MPUaddr, PWR_MGMT_1, 1);
}

static bool setupMPU6050() {
  if (!MPU6050ok) {
    // set full range
    I2CDATA[0] = 0x00; 
    MPU6050ok = sendI2Cdata(MPUaddr, CONFIG, 1);
    // wakeup the sensor 
    if (MPU6050ok) MPU6050ok = sleepMPU6050(false);
  } 
  return MPU6050ok;
}

static void updateMPU6050data() {
  // get data from MPU6050 and store in array
  if (MPU6050ok) {
    if (getI2Cdata(MPUaddr, ACCEL_XOUT_H, ACCEL_BYTES+2)) { 
      // read 3 axis accelerometer & temperature
      int16_t raw[4]; // X, Y, Z, Temp
      float axes[4];
      for (int i=0; i<4; i++) raw[i] = I2CDATA[i*2] << 8 | I2CDATA[(i*2)+1]; 
      // each axis G force value, straight down is 1.0 if stationary
      for (int i=0; i<3; i++) axes[i] = (float)raw[i] / SENS_2G;
      // determine gravity from all 3 axes (no linear velocity)
      float gXYZ = sqrt(pow(axes[0],2)+pow(axes[1],2)+pow(axes[2],2));
      LOG_VRB("gXYZ should be close to 1, is: %0.2f", gXYZ);
      // pitch in degrees - X axis
      float ratio = axes[0] / gXYZ;
      mpuData[0] = (float)((ratio < 0.5) ? 90-fabsf(asin(ratio)*RAD_TO_DEG) : fabsf(acos(ratio)*RAD_TO_DEG));
      // roll in degrees - Y axis 
      ratio = axes[1] / gXYZ;
      mpuData[1] = (float)((ratio < 0.5) ? 90-fabsf(asin(ratio)*RAD_TO_DEG) : fabsf(acos(ratio)*RAD_TO_DEG));
      // yaw in degrees - Z axis (inaccurate as gravity is constant)
      ratio = axes[2] / gXYZ;
      mpuData[2] = (float)((ratio < 0.5) ? 90-fabsf(asin(ratio)*RAD_TO_DEG) : fabsf(acos(ratio)*RAD_TO_DEG));
      // temperature in degrees celsius
      mpuData[3] = ((float)raw[3] / 340.0) + 36.53; 
    }
  }
}

/********************************* MPU common ********************************/

static bool setupMPU(int whichI2C = 0) {
  // identify MPU model
  bool MPUok = true;
  if (deviceStatus[MPUxxxx_HIGH]) MPUaddr = MPUxxxx_HIGH;
  else if (deviceStatus[MPUxxxx_LOW]) MPUaddr = MPUxxxx_LOW;
  else MPUok = false;
  if (MPUok) {
    if (whichI2C == 0) MPUok = getI2Cdata(MPUaddr, WHOAMI_REG, 1);
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
    if (whichI2C == 0) MPUok = getI2Cdata(MPUaddr, WHOAMI_REG, 1, 1, &Wire1);
#endif
    if (MPUok) {
      // determine IMU model using whoami register
      byte iData = I2CDATA[0];
      if (iData == MPU6050_ID) strcpy(mpuModel, "MPU6050");
      if (iData == MPU6500_ID) strcpy(mpuModel, "MPU6520");
      if (iData == MPU9250_ID) {
        strcpy(mpuModel, "MPU9250");
        haveMag = true;
      }
      if (iData == MPU9255_ID) {
        strcpy(mpuModel, "MPU9255");
        haveMag = true;
      }
    }
    MPUok = haveMag ? setupMPU9250(whichI2C) : setupMPU6050();
    if (MPUok) LOG_INF("%s available", mpuModel);
    else LOG_WRN("%s not available", mpuModel);
  } else LOG_WRN("MPU not found on I2C%d - check wiring", whichI2C);
  return MPUok;
}

float* getMPUdata() {
  return mpuData;
}

bool identifyMPU(char* _mpuModel) {
  strcpy(_mpuModel, mpuModel);
  return haveMag;
}

#endif // USE_MPU

/********************************* VL53L1X TOF ************************************/

#define TOF_ADDR 0x29

#if USE_VL53L1X

#if __has_include("../libraries/SparkFun_VL53L1X_4m_Laser_Distance_Sensor/src/SparkFun_VL53L1X.h")
#include <SparkFun_VL53L1X.h>

SFEVL53L1X tof[2];
static bool tofOK[2] = {false, false};

static bool setupTOF(uint8_t whichI2C = 0) {
  if (deviceStatus[TOF_ADDR]) {
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
    if (whichI2C == 0) tofOK[0] = tof[0].begin(Wire);
    else tofOK[1] = tof[1].begin(Wire1);
#endif
    tofOK[whichI2C] = !tofOK[whichI2C]; // as return of 0 is good
    if (tofOK[whichI2C]) {
      tof[whichI2C].setDistanceModeLong();
      tof[whichI2C].setTimingBudgetInMs(33);        // ~33 ms per measurement
      tof[whichI2C].setIntermeasurementPeriod(33);  // must be >= timing budget
      tof[whichI2C].startRanging();                 // begin continuous ranging
    }
  }
  if (!tofOK[whichI2C]) LOG_WRN("VL53L1X on I2C%d not found - check wiring", whichI2C);
  return tofOK[whichI2C];
}

uint16_t getTofData(uint8_t whichI2C) {
  static uint16_t rangeMm = 0;
  if (tofOK[whichI2C]) {
    if (tof[whichI2C].checkForDataReady()) {
      rangeMm = tof[whichI2C].getDistance();
      tof[whichI2C].clearInterrupt(); // required after each read to trigger the next measurement
    }
  } else rangeMm = 0; // flags as not available
  return rangeMm; // previous reading used if new one not received
}

#else
#error "Need to install SparkFun_VL53L1X_4m_Laser_Distance_Sensor library"
#endif

#endif  // USE_VL53L1X


/********************************* DS3231 RTC ************************************/

#define DS3231_RTC 0x68 // real time clock (address may conflict with MPU6050)
#if USE_DS3231
#include "driver/rtc_io.h"
#if __has_include("../libraries/Rtc_by_Makuna/src/RtcDS3231.h")
#include <RtcDS3231.h> // https://github.com/Makuna/Rtc/wiki

RtcDS3231<TwoWire> Rtc(Wire);
static bool DS3231ok = false;
static volatile bool RTCalarmFlag = false;

static void IRAM_ATTR RTCalarmISR() {
  RTCalarmFlag = true;
}

static bool setupRTC() {
  // CONNECTIONS:
  // DS3231 SDA --> SDA
  // DS3231 SCL --> SCL
  // DS3231 VCC --> 3.3v or 5v
  // DS3231 GND --> GND
  // DS3231 SQW --> Alarm Interrupt Pin - needs pullup

  // set the interrupt pin to input mode with pullup
  static int SQWpin = -1; // needs to be config item
  if (!DS3231ok) {
    if (deviceStatus[DS3231_RTC]) {
      pinMode(SQWpin, INPUT_PULLUP);

      Rtc.Begin();
      RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); // compilation time
      if (!Rtc.IsDateTimeValid()) {
        LOG_WRN("RTC lost confidence in the DateTime");
        Rtc.SetDateTime(compiled);
      }

      if (!Rtc.GetIsRunning()) {
        LOG_WRN("RTC was not actively running, starting now");
        Rtc.SetIsRunning(true);
      }

      RtcDateTime now = Rtc.GetDateTime();
      if (now < compiled) {
        LOG_WRN("RTC is older than compile time, updating DateTime");
        Rtc.SetDateTime(compiled);
      }
      
      Rtc.Enable32kHzPin(false);
      Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeAlarmBoth); // set to be alarm output
      Rtc.LatchAlarmsTriggeredFlags();  // throw away any old alarm state
      // setup alarm interrupt 
      attachInterrupt(digitalPinToInterrupt(SQWpin), RTCalarmISR, FALLING);
      
      DS3231ok = true;
    } else DS3231ok = false;
  }
  if (!DS3231ok) LOG_WRN("DS3231 RTC not available");
  return DS3231ok;
}

int cycleRange(int currVal, int minVal, int maxVal) {
  // cycle round values
  if (currVal < minVal) return maxVal;
  if (currVal > maxVal) return minVal;
  return currVal;
}

void setRTCintervalAlarm(int alarmHour, int alarmMin) {
  // Alarm 1 can be once per second, or at given time - seconds accuracy
  // Used here for repeated interval time (hours & minutes of interval) - so set multiple times
  // occurs on 30 secs mark to avoid clash with setRTCrolloverAlarm()
  // args are hours and mins to occur after current time
  if (DS3231ok) {
    int totalMins = Rtc.GetDateTime().Minute() + alarmMin;
    int nextMin   = totalMins % 60;
    int nextHour  = cycleRange(Rtc.GetDateTime().Hour() + alarmHour + totalMins / 60, 0, 23);
    DS3231AlarmOne alarm1(0, nextHour, nextMin, 30, DS3231AlarmOneControl_HoursMinutesSecondsMatch);
    Rtc.SetAlarmOne(alarm1);
  }
}

void setRTCspecificAlarm(int alarmHour, int alarmMin) {
  // Alarm 1 can be once per second, or at given time - seconds accuracy
  // Used here for specific time (hours & minutes of day) - so can be set multiple times
  // occurs on 30 secs mark to avoid clash with setRTCrolloverAlarm()
  // args are specific hour and minute of day to occur
  if (DS3231ok) {
    DS3231AlarmOne alarm1(0, alarmHour, alarmMin, 30, DS3231AlarmOneControl_HoursMinutesSecondsMatch);
    Rtc.SetAlarmOne(alarm1);
  }
}

void setRTCrolloverAlarm(int alarmHour, int alarmMin) {
  // Alarm 2 can be once per minute, or at a given time - minute accuracy
  // Used here for daily rollover alarm - set once
  if (DS3231ok) {
    DS3231AlarmTwo alarm2(0, alarmHour, alarmMin, DS3231AlarmTwoControl_HoursMinutesMatch);
    Rtc.SetAlarmTwo(alarm2);
  }
}

uint32_t getRTCtime() {
  // get current RTC time as epoch
  if (DS3231ok) {
    if (!Rtc.IsDateTimeValid()) LOG_WRN("RTC lost confidence in the DateTime!");
    return Rtc.GetDateTime().Unix32Time();
  }
  return 0;
}

int RTCalarmed() {
  // check if RTC alarm occurred and return alarm number
  int wasAlarmed = 0;
  if (DS3231ok) {
    if (RTCalarmFlag) { 
      RTCalarmFlag = false; // reset the flag
      DS3231AlarmFlag flag = Rtc.LatchAlarmsTriggeredFlags(); // which alarms triggered and reset for next
      if (flag & DS3231AlarmFlag_Alarm1) wasAlarmed = 1; 
      if (flag & DS3231AlarmFlag_Alarm2) wasAlarmed = 2;
    }
  }
  return wasAlarmed;
}

float RTCtemperature() {
  // internal temperature of DS3231
  if (DS3231ok) {
    RtcTemperature temp = Rtc.GetTemperature();
    return temp.AsFloatDegC();
  }
  return 0;
}

void RTCdatetime(char* datestring, int datestringLen) {
  // return RTC formatted date time string
  if (DS3231ok) {
    if (!Rtc.IsDateTimeValid()) LOG_WRN("RTC lost confidence in the DateTime!");
    RtcDateTime dt = Rtc.GetDateTime(); // seconds since jan 1 2000
    snprintf(datestring, datestringLen, "%02u/%02u/%04u %02u:%02u:%02u",
      dt.Day(), dt.Month(), dt.Year(), dt.Hour(), dt.Minute(), dt.Second());
  }
}

#else
#error "Need to install Rtc_by_Makuna library"
#endif

#endif // USE_DS3231

/**************************** LCD1602 ******************************/
// I2C LCD display: 2 lines, 16 cols 
// Derived from https://github.com/arduino-libraries/LiquidCrystal

#define LCD1602 0x27 // 16 chars by 2 lines LCD
#if USE_LCD1602

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// flags for backlight control
#define LCD_BACKLIGHT 0x08
#define LCD_NOBACKLIGHT 0x00

#define En 0b00000100  // Enable bit
#define Rw 0b00000010  // Read/Write bit
#define Rs 0b00000001  // Register select bit

#define NUM_ROWS 2
#define NUM_COLS 16

static bool LCD1602ok = false;
static uint8_t displaycontrol;
static uint8_t displaymode;
static uint8_t backlightval;

static void lcdWrite(uint8_t data) {
  if (LCD1602ok) {
    I2CDATA[0] = data | backlightval;
    sendI2Cdata(LCD1602, 0, 1); 
  }
}

static void writeNibble(uint8_t value) {
	lcdWrite(value);
	lcdWrite(value | En);	  // En high
	delayMicroseconds(1);		// pulse
	lcdWrite(value & ~En);	// En low
	delayMicroseconds(50);	// commands need > 37us to settle
}

static void lcdSend(uint8_t value, uint8_t mode = 0) {
  // write either command (mode = 0) or data, as two 4 bit values
  if (LCD1602ok) {
    writeNibble((value & 0xf0) | mode);
    writeNibble(((value << 4 ) & 0xf0) | mode); 
  }
}

void lcdBacklight(bool lightOn) {
  // Turn the backlight on / off
  backlightval = (lightOn) ? LCD_BACKLIGHT : LCD_NOBACKLIGHT;
  lcdWrite(backlightval);
}

void lcdClear() {
  // clear display, set cursor position to zero
  lcdSend(LCD_CLEARDISPLAY);
  delayMicroseconds(2000);  
}

void lcdHome() {
  // set cursor position to zero
  lcdSend(LCD_RETURNHOME);  
  delayMicroseconds(2000); 
}

void lcdDisplay(bool setDisplay) {
  // Turn the display on / off (not backlight)
  if (setDisplay) displaycontrol |= LCD_DISPLAYON;
  else displaycontrol &= ~LCD_DISPLAYON;
  lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}

static bool setupLCD1602() {  
  if (!LCD1602ok) {
    if (deviceStatus[LCD1602]) {
      LCD1602ok = true;
      delay(50); 
      lcdBacklight(false);
      delay(1000);
  
      // can only use 4 bit mode with PCF8574 as not enough pins for HD44780 8 bit.
      // use magic sequence to set it
      writeNibble(0x03 << 4);
      delayMicroseconds(4500); // wait min 4.1ms
      writeNibble(0x03 << 4);
      delayMicroseconds(4500); // wait min 4.1ms
      writeNibble(0x03 << 4);
      delayMicroseconds(150);
      writeNibble(0x02 << 4);
    
       // set initial display format
      lcdSend(LCD_FUNCTIONSET | LCD_4BITMODE | LCD_2LINE | LCD_5x8DOTS);  
      
      // turn on display and clear content
      displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF; 
      lcdDisplay(true);
      lcdClear();
      
      // set the entry mode and set cursor position to top left
      displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
      lcdSend(LCD_ENTRYMODESET | displaymode); 
      lcdHome();
      lcdBacklight(true); 
    } else LCD1602ok = false;
    if (!LCD1602ok) LOG_WRN("LCD1602 display not available");
  }
  return LCD1602ok;
}

void lcdPrint(const char* str) {
  // write string to lcd
  int len = strlen(str);
  for (int i=0; i<len; i++) lcdSend((uint8_t)str[i], Rs);
}

void lcdSetCursorPos(uint8_t row, uint8_t col) {
  // set row and col of cursor position
	int row_offsets[] = {0x00, 0x40, 0x14, 0x54}; 
  if (row >= NUM_ROWS) row = NUM_ROWS - 1;
  if (col >= NUM_COLS) col = NUM_COLS - 1;
	lcdSend(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

void lcdLineCursor(bool showLine) {
  // Turn the underline cursor on / off
  if (showLine) displaycontrol |= LCD_CURSORON;
  else displaycontrol &= ~LCD_CURSORON;
	lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}

void lcdBlinkCursor(bool showBlink) {
  // Turn the blinking cursor on / off
  if (showBlink) displaycontrol |= LCD_BLINKON;
  else displaycontrol &= ~LCD_BLINKON;
	lcdSend(LCD_DISPLAYCONTROL | displaycontrol);
}

void lcdScrollText(bool scrollLeft) {
  // scroll the current display left or right one position (no wrapping)
  uint8_t moveDir = (scrollLeft) ? LCD_MOVELEFT : LCD_MOVERIGHT;
  lcdSend(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | moveDir);
}

void lcdTextDirection(bool scrollLeft) {
  // write text forward or backward from cursor 
  if (scrollLeft) displaymode &= ~LCD_ENTRYLEFT;
  else displaymode |= LCD_ENTRYLEFT;
	lcdSend(LCD_ENTRYMODESET | displaymode);
}

void lcdAutoScroll(bool autoScroll) {
  // As each character entered at cursor, scroll previous text left
	if (autoScroll) displaymode |= LCD_ENTRYSHIFTINCREMENT; 
  else displaymode &= ~LCD_ENTRYSHIFTINCREMENT; 
	lcdSend(LCD_ENTRYMODESET | displaymode);
}

void lcdLoadCustom(uint8_t charLoc, uint8_t charmap[]) {
  // Load custom character
  // To create, see https://maxpromer.github.io/LCD-Character-Creator/
  // array of 8 lines of 5 bits, where bits represent pixel on / off
  // eg define & load custom char (degrees celsius symbol)
  // uint8_t celsius[] = {B01000, B10100, B01011, B00100, B00100, B00100, B00011, B00000};
  // enum customChar {CELSIUS, CC1, CC2, CC3, CC4, CC5, CC6, CC7};
  // lcdLoadCustom(CELSIUS, celsius);
  // lcdWriteCustom(CELSIUS);
  if (charLoc > 7) LOG_WRN("custom char number %u out of range", charLoc);
  else {
  	charLoc &= 0x7; // CGRAM location to load 0 - 7
  	lcdSend(LCD_SETCGRAMADDR | (charLoc << 3));
  	for (int i=0; i<8; i++)	lcdSend(charmap[i], Rs);
  }
}

void lcdWriteCustom(uint8_t charLoc) {
  // write one of 8 custom chars 
  if (charLoc > 7) LOG_WRN("custom char number %u out of range", charLoc);
  else lcdSend(charLoc, Rs);
}

#endif // USE_LCD1602


/************** Poll devices for data ***************/

bool accelUse = false;
int accelDeg = 2;

static bool haveMovement = false;

bool checkAccelMove() {
  return haveMovement;
}

#if !INCLUDE_DRONE // drone uses its own loop in camDrone.cpp
#ifdef IS_POLLABLE

static TaskHandle_t sensorPollHandle = NULL;
static int pollInterval = 1; // polling interval in secs

#if USE_MPU && defined(ISCAM)

static void getAccelMove() {
  // determine if sufficient accelerometer movement has occurred
  haveMovement = false;
  static int posData[2][3];
  int currMove = 0;
  for (int i = 0; i < 3; i++) {
    posData[0][i] = (int)mpuData[i];
    currMove += abs(posData[0][i] - posData[1][i]);
    posData[1][i] = posData[0][i];
  }
  if (currMove > accelDeg) haveMovement = true;
}

#endif // ISCAM

static void sensorPollTask(void* p) {
  uint32_t sampleInterval = 1000 * (pollInterval < 1 ? 1 : pollInterval);
  while (true) {
    uint32_t startTime = millis();
#if USE_MPU
 #ifdef USE_MPU9250
    if (MPU9250ok) updateMPU9250data();
 #endif
    if (MPU6050ok) updateMPU6050data();
  #ifdef ISCAM
    if (accelUse) getAccelMove();
  #endif
#endif
#if USE_BMx280
    if (BMx280ok) updateBMx280();
#endif
    // wait for next collection interval
    uint32_t elapsedTime = millis() - startTime;
    if (elapsedTime < sampleInterval) vTaskDelay(pdMS_TO_TICKS(sampleInterval - elapsedTime));
  }
}

static void startPollTask() {
  // poll input sensors for data
#if USE_TELEM
  pollInterval = srtInterval;
#endif
  if (sensorPollHandle == NULL) xTaskCreateWithCaps(sensorPollTask, "sensorPollTask", SENSOR_STACK_SIZE, NULL, SENSOR_PRI, &sensorPollHandle, STACK_MEM); 
}

#endif  // IS_POLLABLE

#endif // !INCLUDE_DRONE

/**************************** Setup ******************************/

bool checkI2Cdevice(const char* devName) {
  // get current device status
  if (!strcmp(devName, "SSD1306")) return deviceStatus[SSD1306_BIaddr] || deviceStatus[SSD1306_Extaddr] ? true : false;
  if (!strcmp(devName, "PCF8591")) return deviceStatus[PCF8591addr];
  if (!strcmp(devName, "BMx280")) return deviceStatus[BMx280_Def] || deviceStatus[BMx280_Alt] ? true : false;
  if (!strcmp(devName, "MPUxxxx")) return deviceStatus[MPUxxxx_HIGH] || deviceStatus[MPUxxxx_LOW]  ? true : false;
  if (!strcmp(devName, "DS3231")) return deviceStatus[DS3231_RTC];
  if (!strcmp(devName, "LCD1602")) return deviceStatus[LCD1602];
  if (!strcmp(devName, "VL53L1X")) return deviceStatus[TOF_ADDR];
  LOG_WRN("Device name %s not recognised", devName);
  return false;
}

static bool prepI2Cdevices() {
  // setup available I2C devices 
  bool res = false;
  if (I2C0devices == 0) LOG_WRN("No I2C0 devices connected");
  else {
#if USE_SSD1306
    setupOled();
#endif
#if USE_BMx280
    setupBMx();
#endif
#if USE_VL53L1X
    setupTOF();
#endif
#if USE_MPU
    setupMPU();
#endif
#if USE_DS3231
    setupRTC();
#endif
#if USE_LCD1602
    setupLCD1602();
#endif
#if !INCLUDE_DRONE && defined(IS_POLLABLE)
    startPollTask();
#endif
    res = true;
  }
  return res;
}

#endif // INCLUDE_I2C
