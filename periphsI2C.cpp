
// I2C driver and devices
//
// OLED SSD1306 display 128*64
// PCF8591 ADC
// BM*280 temperature, pressure, altitude for BMP280, + humidity for BME280
// MPU6050 6 axis accel & gyro
// MPU9250 9 axis accel & gyro & mag
// DS3231 RTC
// LCD 1602 display 2*16 
//
// To enable a device, set the appropriate USE_* define in appGlobals.h
//
// s60sc 2023, 2024
// incorporates contributions from rjsachse

#include "appGlobals.h"

#if INCLUDE_I2C

#define SENSOR_TIMEOUT 100 // max time in ms to wait for sensor response

#include <Wire.h>

// define which pins to use for I2C bus in call to initializeI2C()
// if pins not correctly defined for board, spurious results will occur
int I2Csda = -1;
int I2Cscl = -1;
static byte I2CDATA[10]; // store I2C data received or to be sent 
static int I2Cdevices = -1;
  
// I2C device names, indexed by address
static bool deviceStatus[128] = {false}; // whether device present
static const char* clientName[128] = {
  "", "", "", "", "", "", "", "", "", "", "", "", "AK8963", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "LCD1602", "", "", "", "", "", "", "", "",
  "OV2640", "", "", "", "", "", "", "", "", "", "", "", "OV5640/SSD1306", "SSD1306", "", "",
  "", "", "", "", "", "", "", "", "PCF8591", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
  "OV2640", "OV2640", "", "", "", "", "", "", "MPUxx50/DS3231", "MPUxx50", "", "", "", "", "", "",
  "", "", "", "", "", "", "BMx280", "BMx280", "OV5640", "OV5640", "", "", "", "", "", ""};

bool prepI2Cdevices();

/********************* Generic I2C Utilities ***********************/

static bool sendTransmission(int clientAddr, bool scanning) {
  // common function used to send request to I2C device and determine outcome
  byte result = Wire.endTransmission(true);
    /*1: data too long to fit in transmit buffer
      2: received NACK on transmit of address
      3: received NACK on transmit of data
      4: other error, e.g. switched off 
      5: i2c busy 
      8: unknown pcf8591 status */
      
  if (!scanning && result > 0) LOG_WRN("Client %s at 0x%x with connection error: %d", clientName[clientAddr], clientAddr, result);
  return (result == 0) ? true : false;
}

static void scanI2C() {
  // find details of any active I2C devices
  LOG_INF("I2C device scanning");
  for (byte address = 0; address < 127; address++) {
    Wire.beginTransmission(address);
    // only report error if client device meant to be present
    if (sendTransmission(address, true)) {
      LOG_INF("I2C device %s present at address: 0x%x", clientName[address], address);
      I2Cdevices++;
      deviceStatus[address] = true;
    }
  }
  LOG_INF("I2C devices found: %d", I2Cdevices);
}

static bool getI2Cdata (uint8_t clientAddr, uint8_t controlByte, uint8_t numBytes) {
  // send command to I2C client and receive response
  // clientAddr is the I2C address
  // controlByte is the control instruction
  // numBytes is number of bytes to request
  Wire.beginTransmission(clientAddr); // select which client to use
  Wire.write(controlByte); // send device command
  if (sendTransmission(clientAddr, false)) {
    // get required number of bytes
    Wire.requestFrom (clientAddr, numBytes);
    for (int i=0; i<numBytes; i++) I2CDATA[i] = Wire.read();
    return sendTransmission(clientAddr, false);
  } 
  return false; 
}

static bool sendI2Cdata(int clientAddr, uint8_t controlByte, uint8_t numBytes) {
  // send data to I2C device
  // clientAddr is the I2C address
  // controlByte is the control instruction
  // numBytes is number of bytes to send
  Wire.beginTransmission(clientAddr);
  if (controlByte) Wire.write(controlByte);
  for (int i=numBytes-1; i>=0; i--) Wire.write(I2CDATA[i]);
  return sendTransmission(clientAddr, false);
}

bool prepI2C() {
  // start I2C port and prep I2C peripherals
  if (I2Csda == I2Cscl) {
    LOG_ALT("I2C pins not defined");
    return false;
  }  
  Wire.begin(I2Csda, I2Cscl); // Join I2C bus as master
  LOG_INF("Initialise I2C at %dkHz using pins: %d, %d", Wire.getClock() / 1000, I2Csda, I2Cscl);
  I2Cdevices = 0;
  scanI2C();
  return prepI2Cdevices();
}

void prepI2Ccam(int camSda, int camScl) {
  // specific to camera app
  Wire.begin(camSda, camScl); // Join I2C bus as master
  LOG_INF("Initialise shared I2C at %dkHz using pins: %d, %d", Wire.getClock() / 1000, camSda, camScl);
  I2Cdevices = 0;
  scanI2C();
}

/***************************************** OLED Display *************************************/

#define SSD1306_BIaddr 0x3d // built in oled 
#define SSD1306_Extaddr 0x3c // external oled (also address for OV5640
#if USE_SSD1306
#include "SSD1306Wire.h" // https://github.com/ThingPulse/esp8266-oled-ssd1306
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
#endif

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
#if USE_BMx280
#define STD_PRESSURE 1013.25 // reference pressure in mB/hPa at sea level
#define DEGREE_SYMBOL "\xC2\xB0"

#include <BMx280I2C.h> // https://github.com/christandlg/BMx280MI
BMx280I2C bmxDef(BMx280_Def); 
BMx280I2C bmxAlt(BMx280_Alt);
BMx280I2C* thisBmx;

static bool BMx280ok = false;
static bool isBME = false;

static bool setupBMx() {
  // setup BMx280 if available
  if (!BMx280ok) {
    BMx280ok = true;
    if (deviceStatus[BMx280_Def]) thisBmx = &bmxDef;
    else if (deviceStatus[BMx280_Alt]) thisBmx = &bmxAlt;
    else BMx280ok = false;
    if (BMx280ok) {
      BMx280ok = thisBmx->begin();
      if (BMx280ok) {
        isBME = thisBmx->isBME280();
        thisBmx->resetToDefaults();
        thisBmx->writeOversamplingPressure(BMx280MI::OSRS_P_x16);
        thisBmx->writeOversamplingTemperature(BMx280MI::OSRS_T_x16);
        if (isBME) thisBmx->writeOversamplingHumidity(BMx280MI::OSRS_H_x16);
        thisBmx->measure();
      }
    }
    if (!BMx280ok) LOG_WRN("BMx280 not available");
  } 
  return BMx280ok;
}

float* getBMx280() { 
  // get and return pressure, temperature, altitude, humidity
  static float BMx280[4] = {0};
  if (BMx280ok) {
    thisBmx->measure();
    uint32_t bmxWait = millis();
    while(!thisBmx->hasValue() && millis() - bmxWait < SENSOR_TIMEOUT) delay(10);
    if (thisBmx->hasValue()) {
      // PSI = pascals * 0.000145
      // ambient temperature (but affected by chip heating)
      BMx280[0] = thisBmx->getTemperature(); // celsius 
      BMx280[1] = thisBmx->getPressure() * 0.01;  // pascals to mB/hPa
      BMx280[2] = 44330.0 * (1.0 - pow(BMx280[1] / STD_PRESSURE, 1.0 / 5.255)); // altitude in meters
      if (isBME) BMx280[3] = thisBmx->getHumidity(); // % relative humidity
    }
  }
  return BMx280;
}

bool identifyBMx() {
  return isBME;
}
#endif

/********************************** MPU6050 ************************************/

#define MPUxx50_HIGH 0x69 // MPU6050 / MPU9250 I2C address if AD0 pulled high
#define MPUxx50_LOW 0x68  // MPU6050 / MPU9250 I2C address if AD0 grounded
#if USE_MPU6050
// MPU6050 definitions - not gyroscope
#define SENS_2G (32768.0/2.0) // divider for 2G sensitivity reading
#define ACCEL_BYTES 6 // 2 bytes per axis
#define CONFIG 0x1A
#define ACCEL_CONFIG 0x1C
#define ACCEL_XOUT_H 0x3B
#define PWR_MGMT_1 0x6B

static uint8_t MPU6050addr;
static bool MPU6050ok = false;

bool sleepMPU6050(bool doSleep) {
  // power down or wake up MPU6050 
  I2CDATA[0] = doSleep ? 0x40 : 0x01;
  // PWR_MGMT_1 register set to sleep
  return sendI2Cdata(MPU6050addr, PWR_MGMT_1, 1);
}

static bool setupMPU6050() {
  if (!MPU6050ok) {
    MPU6050ok = true;
    if (deviceStatus[MPUxx50_HIGH]) MPU6050addr = MPUxx50_HIGH;
    else if (deviceStatus[MPUxx50_LOW]) MPU6050addr = MPUxx50_LOW;
    else MPU6050ok = false;
    if (MPU6050ok) {
      // set full range
      I2CDATA[0] = 0x00; 
      MPU6050ok = sendI2Cdata(MPU6050addr, CONFIG, 1);
      // wakeup the sensor 
      if (MPU6050ok) sleepMPU6050(false);
    } 
    if (!MPU6050ok) LOG_WRN("MPU6050 6 axis not available");
  }
  return MPU6050ok;
}

float* getMPU6050() {
  // get data from MPU6050 and return as array
  static float Gforce[4] = {0};
  if (MPU6050ok) {
    if (getI2Cdata(MPU6050addr, ACCEL_XOUT_H, ACCEL_BYTES+2)) { 
      // read 3 axis accelerometer & temperature
      int16_t raw[4]; // X, Y, Z, Temp
      for (int i=0; i<4; i++) raw[i] = I2CDATA[i*2] << 8 | I2CDATA[(i*2)+1]; 
      // each axis G force value, straight down is 1.0 if stationary
      for (int i=0; i<3; i++) Gforce[i] = raw[i] / SENS_2G;
      // determine gravity from all 3 axes (no linear velocity)
      float gXYZ = sqrt(pow(Gforce[0],2)+pow(Gforce[1],2)+pow(Gforce[2],2));
      LOG_VRB("gXYZ should be close to 1, is: %0.2f", gXYZ);
      // pitch in degrees - X axis
      float ratio = Gforce[0] / gXYZ;
      Gforce[0] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      // yaw in degrees - Y axis
      ratio = Gforce[1] / gXYZ;
      Gforce[1] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      // roll in degrees - Z axis
      ratio = Gforce[2] / gXYZ;
      Gforce[2] = (float)((ratio < 0.5) ? 90-fabs(asin(ratio)*RAD_TO_DEG) : fabs(acos(ratio)*RAD_TO_DEG));
      // temperature in degrees celsius
      Gforce[3] = ((float)raw[3] / 340.0) + 36.53; 
    }
  }
  return Gforce;
}
#endif

/********************************** MPU9250 ************************************/
/*
MPU9250 on GY-91
VIN: Voltage Supply Pin
3V3: 3.3v Regulator output
GND: 0V Power Supply
SCL: I2C Clock 
SDA: I2C Data 
SDO/SAO: I2C Address selection MPU9250
NCS: n/a
CSB: I2C Address selection BMP280
*/
#if USE_MPU9250
#include "MPU9250.h" // https://github.com/hideakitai/MPU9250
// accel axis orientation on GY-91:                      
// - X : short side (pitch)
// - Y : long side (roll)
// - Z : up (yaw from true N)
// Note internal AK8963 magnetometer is at address 0x0C
#define LOCAL_MAG_DECLINATION (4 + 56/60)  // see https://www.magnetic-declination.com/ for local value

static MPU9250 mpu9250;
static uint8_t MPU9250addr;
static bool MPU9250ok = false;

static bool setupMPU9250() {
  if (!MPU9250ok) {
    MPU9250ok = true;
    if (deviceStatus[MPUxx50_HIGH]) MPU9250addr = MPUxx50_HIGH;
    else if (deviceStatus[MPUxx50_LOW]) MPU9250addr = MPUxx50_LOW;
    else MPU9250ok = false;
    if (MPU9250ok) {
      if (mpu9250.setup(MPU9250addr)) {
        mpu9250.setMagneticDeclination(LOCAL_MAG_DECLINATION);
        mpu9250.selectFilter(QuatFilterSel::MADGWICK);
        mpu9250.setFilterIterations(15);
        LOG_INF("MPU9250 calibrating, leave still");
        mpu9250.calibrateAccelGyro();
  //    LOG_INF("Move MPU9250 in a figure of eight until done");
  //    delay(2000);
  //    mpu9250.calibrateMag();
      } else MPU9250ok = false;
    } 
    if (!MPU9250ok) LOG_WRN("MPU9250 9 axis not available");
  }
  return MPU9250ok;
}

float* getMPU9250() {
  // get data from MPU9250 and return as array
  // only some functions obtained
  static float Gforce[4] = {0};
  if (MPU9250ok) {
    uint32_t mpuWait = millis();
    while (!mpu9250.update() && millis() - mpuWait < SENSOR_TIMEOUT) delay(10);
    if (mpu9250.update()) {
      Gforce[0] = mpu9250.getYaw();
      Gforce[1] = mpu9250.getPitch();
      Gforce[2] = mpu9250.getRoll();
    }
  }
  return Gforce;
}
#endif

/********************************* DS3231 RTC ************************************/

#define DS3231_RTC 0x68 // real time clock (address may conflict with MPU6050)
#if USE_DS3231
#include "driver/rtc_io.h"
#include <RtcDS3231.h> // https://github.com/Makuna/Rtc/wiki
RtcDS3231<TwoWire> Rtc(Wire);

static bool DS3231ok = false;
static volatile bool RTCalarmFlag = false;

static void IRAM_ATTR RTCalarmISR() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  RTCalarmFlag = true;
  if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

static bool setupRTC() {
  // CONNECTIONS:
  // DS3231 SDA --> SDA
  // DS3231 SCL --> SCL
  // DS3231 VCC --> 3.3v or 5v
  // DS3231 GND --> GND
  // DS3231 SQW --> Alarm Interrupt Pin - needs pullup

  // set the interrupt pin to input mode with pullup
  static bool SQWpin = -1; // needs to be config item
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
    int nextHour = cycleRange(Rtc.GetDateTime().Hour()+alarmHour, 0, 23);
    int nextMin = cycleRange(Rtc.GetDateTime().Minute()+alarmMin, 0, 59);
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
#endif

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
	for (int i=0; i<strlen(str); i++) lcdSend((uint8_t)str[i], Rs);
}

void lcdSetCursorPos(uint8_t row, uint8_t col) {
  // set row and col of cursor position
	int row_offsets[] = {0x00, 0x40, 0x14, 0x54}; 
  if (row > NUM_ROWS) row = NUM_ROWS - 1;
  if (col > NUM_COLS) col = NUM_COLS - 1;
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
#endif

/**************************** Setup ******************************/

bool checkI2Cdevice(const char* devName) {
  // get current device status
  if (!strcmp(devName, "SSD1306")) return deviceStatus[SSD1306_BIaddr] || deviceStatus[SSD1306_Extaddr] ? true : false;
  if (!strcmp(devName, "PCF8591")) return deviceStatus[PCF8591addr];
  if (!strcmp(devName, "BMx280")) return deviceStatus[BMx280_Def] || deviceStatus[BMx280_Alt] ? true : false;
  if (!strcmp(devName, "MPU6050")) return deviceStatus[MPUxx50_HIGH] || deviceStatus[MPUxx50_LOW]  ? true : false;
  if (!strcmp(devName, "MPU9250")) return deviceStatus[MPUxx50_HIGH] || deviceStatus[MPUxx50_LOW]  ? true : false;
  if (!strcmp(devName, "DS3231")) return deviceStatus[DS3231_RTC];
  if (!strcmp(devName, "LCD1602")) return deviceStatus[LCD1602];
  LOG_WRN("Device name %s not recognised", devName);
  return false;
}

bool prepI2Cdevices() {
  // setup available I2C devices 
  // Note: only called externally by cam
  if (I2Cdevices < 0) LOG_ERR("prepI2C[cam]() not called");
  else if (I2Cdevices == 0) LOG_WRN("No I2C devices connected");
  else {
#if USE_SSD1306
    setupOled();
#endif
#if USE_BMx280
    setupBMx();
#endif
#if USE_MPU6050
    setupMPU6050();
#endif
#if USE_MPU9250
    setupMPU9250();
#endif
#if USE_DS3231
    setupRTC();
#endif
#if USE_LCD1602
    setupLCD1602();
#endif
    return true;
  }
  return false;
}

#endif
