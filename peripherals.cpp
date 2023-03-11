
// Optional peripherals, to support:
// - pin sensors eg PIR
// - pin controllers eg Lamp
// - servos, eg camera pan / tilt
// - DS18B20 temperature sensor
// - battery voltage measurement
// - lamp led driver (PWM or WS2812)
//
// Peripherals can be hosted directly on the client ESP, or on
// a separate IO Extender ESP if the client ESP has limited free 
// pins, eg ESP-Cam module
// External peripherals should have low data rate and not require fast response,
// so interrupt driven input pins should be monitored internally by the client.
// Peripherals that need a clocked data stream such as microphones are not suitable
//
// Pin numbers must be > 0.
//
// The client and extender must be compiled with the same version of 
// the peripherals.cpp and have compatible configuration settings
// with respect to pin numbers etc
//
// s60sc 2022

#include "appGlobals.h"

// following peripheral requires additional libraries: OneWire and DallasTemperature
//#define INCLUDE_DS18B20 // uncomment to include DS18B20 temp sensor if fitted

// IO Extender use
bool useIOextender; // true to use IO Extender, otherwise false
bool useUART0; // true to use UART0, false for UART1
int uartTxdPin;
int uartRxdPin;
#define EXT_IO_PING 199 // dummy pin number for ping heartbeat
static bool extIOpinged = true;

// peripherals used
bool pirUse; // true to use PIR for motion detection
bool lampUse; // true to use lamp
uint8_t lampLevel; // brightness of on board lamp led 
bool lampAuto; // if true in conjunction with pirUse & lampUse, switch on lamp when PIR activated at night
bool servoUse; // true to use pan / tilt servo control
bool voltUse; // true to report on ADC pin eg for for battery
// microphone cannot be used on IO Extender
bool micUse; // true to use external I2S microphone 

// Pins used by peripherals

// To use IO Extender, use config web page to set pin numbers on client to be those used on IO Extender
// and add EXTPIN, eg: on config web page, set ds18b20Pin to 110 (100+ 10) to use pin 10 on IO Extender
// and set ds18b20Pin to 10 on IO Extender
// If IO Extender not being used, ensure pins on ESP-Cam not defined for multiple use

// sensors 
int pirPin; // if pirUse is true
int lampPin; // if lampUse is true

// Pan / Tilt Servos 
int servoPanPin; // if servoUse is true
int servoTiltPin;

// ambient / module temperature reading 
int ds18b20Pin; // if INCLUDE_DS18B20 uncommented

// batt monitoring 
// only pin 33 can be used on ESP32-Cam module as it is the only available analog pin
int voltPin; 

// additional peripheral configuration
// configure for specific servo model, eg for SG90
int servoMinAngle; // degrees
int servoMaxAngle;
int servoMinPulseWidth; // usecs
int servoMaxPulseWidth;
int servoDelay; // control rate of change of servo angle using delay

// configure battery monitor
int voltDivider; // set battVoltageDivider value to be divisor of input voltage from resistor divider
                 // eg: 100k / 100k would be divisor value 2
int voltLow; // voltage level at which to send out email alert
int voltInterval; // interval in minutes to check battery voltage


void doIOExtPing() {
  // check that IO_Extender is available
  if (useIOextender && !IS_IO_EXTENDER) {
    // client sends ping
    if (!extIOpinged) LOG_WRN("IO_Extender failed to ping");
    extIOpinged = false;
    externalPeripheral(EXT_IO_PING);
    // extIOpinged set by setPeripheralResponse() from io extender
  }
}

// individual pin sensor / controller functions

bool pirVal = false;

bool getPIRval() {
  // get PIR status 
  // if use external PIR, will have delayed response
  if (!externalPeripheral(pirPin)) pirVal = digitalRead(pirPin); 
  return pirVal; 
}
 

// Control a Pan-Tilt-Camera stand using two servos connected to pins specified above

#include "driver/ledc.h"

#define PWM_FREQ 50 // hertz
#define DUTY_BIT_DEPTH 14 // max for ESP32-C3
#define USECS 1000000
#define SERVO_PAN_CHANNEL LEDC_CHANNEL_3
#define SERVO_TILT_CHANNEL LEDC_CHANNEL_4
TaskHandle_t servoHandle = NULL;
static int newTiltVal, newPanVal;
static int oldPanVal = 91;
static int oldTiltVal = 91;

static int dutyCycle (int angle) {
  // calculate duty cycle for given angle
  angle = constrain(angle, servoMinAngle, servoMaxAngle);
  int pulseWidth = map(angle, servoMinAngle, servoMaxAngle, servoMinPulseWidth, servoMaxPulseWidth);
  return pow(2, DUTY_BIT_DEPTH) * pulseWidth * PWM_FREQ / USECS;
}

static int changeAngle(uint8_t chan, int newVal, int oldVal) {
  // change angle of given servo
  int incr = newVal - oldVal > 0 ? 1 : -1;
  for (int angle = oldVal; angle != newVal + incr; angle += incr) {
    ledcWrite(chan, dutyCycle(angle));
    delay(servoDelay); // set rate of change
  }
  return newVal;
}

static void servoTask(void* pvParameters) {
  // update servo position from user input
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (newPanVal != oldPanVal) oldPanVal = changeAngle(SERVO_PAN_CHANNEL, newPanVal, oldPanVal);
    if (newTiltVal != oldTiltVal) oldTiltVal = changeAngle(SERVO_TILT_CHANNEL, newTiltVal, oldTiltVal);
  }
}

void setCamPan(int panVal) {
  // change camera pan angle
  newPanVal = panVal;
  if (servoUse && !externalPeripheral(servoPanPin, panVal)) 
    if (servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

void setCamTilt(int tiltVal) {
  // change camera tilt angle
  newTiltVal = tiltVal;
  if (servoUse && !externalPeripheral(servoTiltPin, tiltVal))
    if (servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

static void prepServos() {
  if ((servoPanPin < EXTPIN) && servoUse) {
    if (servoPanPin) {
      ledcSetup(SERVO_PAN_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
      ledcAttachPin(servoPanPin, SERVO_PAN_CHANNEL);
    } else LOG_WRN("No servo pan pin defined");
    if (servoTiltPin) {
      ledcSetup(SERVO_TILT_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
      ledcAttachPin(servoTiltPin, SERVO_TILT_CHANNEL);
    } else LOG_WRN("No servo tilt pin defined");

    if (servoPanPin || servoTiltPin) {
      xTaskCreate(&servoTask, "servoTask", 1024, NULL, 1, &servoHandle); 
      // initial angle
      setCamPan(90);
      setCamTilt(90);
      LOG_INF("Servos available");
    } else servoUse = false;
  }
}


/* Read temperature from DS18B20 connected to pin specified above
    Use Arduino Manage Libraries to install OneWire and DallasTemperature
    DS18B20 is a one wire digital temperature sensor
    Pin layout from flat front L-R: Gnd, data, 3V3.
    Need a 4.7k resistor between 3V3 and data line
    Runs in its own task as there is a 750ms delay to get temperature

    If DS18B20 is not present, use ESP internal temperature sensor
*/

#ifdef INCLUDE_DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#endif
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "driver/temp_sensor.h"
#endif

// configuration
#define NO_TEMP -127
static float dsTemp = NO_TEMP;
TaskHandle_t DS18B20handle = NULL;
static bool haveDS18B20 = false;

static void DS18B20task(void* pvParameters) {
#ifdef INCLUDE_DS18B20
  // get current temperature from DS18B20 device
  OneWire oneWire(ds18b20Pin);
  DallasTemperature sensors(&oneWire);
  while (true) {
    sensors.begin();
    uint8_t deviceAddress[8];
    sensors.getAddress(deviceAddress, 0);
    if (deviceAddress[0] == 0x28) {
      uint8_t tryCnt = 10;
      while (tryCnt) {
        sensors.requestTemperatures(); 
        dsTemp = sensors.getTempCByIndex(0);
        // ignore occasional duff readings
        if (dsTemp > NO_TEMP) tryCnt = 10;
        else tryCnt--;
        delay(1000);
      }   
    } 
    // retry setting up ds18b20
    delay(10000);
  }
#endif
}

void prepTemperature() {
#if defined(INCLUDE_DS18B20)
  if (ds18b20Pin < EXTPIN) {
    if (ds18b20Pin) {
      size_t stacksize = 1024;
#if CONFIG_IDF_TARGET_ESP32S3
      stacksize = 1024 * 2;
#endif
      xTaskCreate(&DS18B20task, "DS18B20task", stacksize, NULL, 1, &DS18B20handle); 
      haveDS18B20 = true;
      LOG_INF("Using DS18B20 sensor");
    } else LOG_WRN("No DS18B20 pin defined, using chip sensor if present");
  }
#endif
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  // setup internal sensor
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor.dac_offset = TSENS_DAC_L2;  // TSENS_DAC_L2 is default. L4(-40℃ ~ 20℃), L2(-10℃ ~ 80℃) L1(20℃ ~ 100℃) L0(50℃ ~ 125℃)
  temp_sensor_set_config(temp_sensor);
  temp_sensor_start();
#endif
}

float readTemperature(bool isCelsius) {
  // return latest read temperature value in celsius (true) or fahrenheit (false), unless error
#if defined(INCLUDE_DS18B20)
  // use external DS18B20 sensor if available, else use local value
  externalPeripheral(ds18b20Pin);
#endif
#if CONFIG_IDF_TARGET_ESP32
  // convert on chip raw temperature in F to Celsius degrees
  if (!haveDS18B20) dsTemp = (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  if (!haveDS18B20) temp_sensor_read_celsius(&dsTemp); 
#endif
  return (dsTemp > NO_TEMP) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
}

float getNTCcelsius (uint16_t resistance, float oldTemp) {
  // convert NTC thermistor resistance reading to celsius
  double Temp = log(resistance);
  Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * Temp * Temp )) * Temp);
  Temp = (Temp == 0) ? oldTemp : Temp - 273.15; // if 0 then didnt get a reading
  return (float) Temp;
}

/************ battery monitoring ************/
// Read voltage from battery connected to ADC pin
// input battery voltage may need to be reduced by voltage divider resistors to keep it below 3V3.
static float currentVoltage = -1.0; // no monitoring

float readVoltage()  {
  // use external voltage sensor if available, else use local value
  externalPeripheral(voltPin);
  return currentVoltage;
}

static void battTask(void* parameter) {
  delay(20 * 1000); // allow time for esp32 to start up
  if (voltInterval < 1) voltInterval = 1;
  while (true) {
    static bool sentEmailAlert = false;
    // convert analog reading to corrected voltage.  analogReadMilliVolts() not working
    currentVoltage = (float)(smoothAnalog(voltPin)) * 3.3 * voltDivider / pow(2, ADC_BITS);

#ifdef INCLUDE_SMTP
    if (currentVoltage < voltLow && !sentEmailAlert) {
      sentEmailAlert = true; // only sent once per esp32 session
      smtpBufferSize = 0; // no attachment
      char battMsg[20];
      sprintf(battMsg, "Voltage is %0.2fV", currentVoltage);
      emailAlert("Low battery", battMsg);
    }
#endif
    delay(voltInterval * 60 * 1000); // mins
  }
  vTaskDelete(NULL);
}

static void setupBatt() {
  if (voltUse && (voltPin < EXTPIN)) {
    if (voltPin) {
      setupADC();
      xTaskCreate(&battTask, "battTask", 2048, NULL, 1, NULL);
      LOG_INF("Monitor batt voltage");
      debugMemory("setupBatt");
    } else LOG_WRN("No voltage pin defined");
  }
}

/********************* LED Lamp Driver **********************/

#define RGB_BITS 24  // WS2812 has 24 bit color in RGB order
#define LAMP_LEDC_CHANNEL 2 // Use channel not required by camera
#if defined(CAMERA_MODEL_ESP32S3_EYE)
static rmt_obj_t* rmtWS2812;
static rmt_data_t ledData[RGB_BITS];
#endif

void setupLamp() {
  // setup lamp LED according to board type
  // assumes led wired as active high (ESP32 lamp led on pin 4 is active high, signal led on pin 33 is active low)
  if ((lampPin < EXTPIN) && lampUse) {
    if (lampPin) {
#if defined(CAMERA_MODEL_AI_THINKER)
      // High itensity white led
      ledcSetup(LAMP_LEDC_CHANNEL, 5000, 4);
      ledcAttachPin(lampPin, LAMP_LEDC_CHANNEL); 
      LOG_INF("Setup Lamp Led for ESP32 Cam board");

#elif defined(CAMERA_MODEL_ESP32S3_EYE)
      // Single WS2812 RGB high intensity led
      rmtWS2812 = rmtInit(lampPin, true, RMT_MEM_64);
      if (rmtWS2812 == NULL) LOG_ERR("Failed to setup WS2812 with pin %u", lampPin);
      else {
        rmtSetTick(rmtWS2812, 100);
        LOG_INF("Setup Lamp Led for ESP32S3 Cam board");
      }
#else
      // unknown board, assume PWM LED
      ledcSetup(LAMP_LEDC_CHANNEL, 5000, 4);
      ledcAttachPin(lampPin, LAMP_LEDC_CHANNEL); 
      LOG_INF("Setup Lamp Led");
#endif
    } else {
      lampUse = false;
      LOG_WRN("No Lamp Led pin defined");
    }
  } 
}

void setLamp(uint8_t lampVal) {
  if (lampUse) {
    lampLevel = lampVal;
#if defined(CAMERA_MODEL_AI_THINKER)
    // set lamp brightness using PWM (0 = off, 15 = max)
    if (!externalPeripheral(lampPin, lampVal)) ledcWrite(LAMP_LEDC_CHANNEL, lampLevel);

#elif defined(CAMERA_MODEL_ESP32S3_EYE)
    // Set white color and apply lampLevel (0 = off, 15 = max)
    uint8_t RGB[3]; // each color is 8 bits
    lampVal = lampLevel == 15 ? 255 : lampLevel * 16;
    for (uint8_t i = 0; i < 3; i++) {
      RGB[i] = lampVal;
      // apply WS2812 bit encoding pulse timing per bit
      for (uint8_t j = 0; j < 8; j++) { 
        int bit = (i * 8) + j;
        if ((RGB[i] << j) & 0x80) { // get left most bit first
          // bit = 1
          ledData[bit].level0 = 1;
          ledData[bit].duration0 = 8;
          ledData[bit].level1 = 0;
          ledData[bit].duration1 = 4;
        } else {
          // bit = 0
          ledData[bit].level0 = 1;
          ledData[bit].duration0 = 4;
          ledData[bit].level1 = 0;
          ledData[bit].duration1 = 8;
        }
      }
    }
    rmtWrite(rmtWS2812, ledData, RGB_BITS);
#else
    // other board
    // set lamp brightness using PWM (0 = off, 15 = max)
    if (!externalPeripheral(lampPin, lampVal)) ledcWrite(LAMP_LEDC_CHANNEL, lampLevel);
#endif
  }
}


/********************* interact with UART **********************/

void setPeripheralResponse(const byte pinNum, const uint32_t responseData) {
  // callback for Client uart task 
  // updates peripheral stored input value when response received
  // map received pin number to peripheral
  if (pinNum == pirPin) 
    memcpy(&pirVal, &responseData, sizeof(pirVal));  // set PIR status
  else if (pinNum == voltPin)
    memcpy(&currentVoltage, &responseData, sizeof(currentVoltage));  // set current batt voltage
  else if (pinNum == ds18b20Pin)
    memcpy(&dsTemp, &responseData, sizeof(dsTemp));  // set current temperature
  else if (pinNum == EXT_IO_PING) 
    extIOpinged = true;
  else if (pinNum != lampPin && pinNum != servoPanPin && pinNum != servoTiltPin) 
    LOG_ERR("Undefined pin number requested: %d ", pinNum);
}

uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData) {
  // callback for IO Extender to interact with peripherals
  uint32_t responseData = 0;
  int ival;
  // map received pin number to peripheral
  if (pinNum == servoTiltPin) {
    // send tilt angle to servo
    memcpy(&ival, &receivedData, sizeof(ival)); 
    setCamTilt(ival);
  } else if (pinNum == servoPanPin) {
    // send pan angle to servo
    memcpy(&ival, &receivedData, sizeof(ival)); 
    setCamPan(ival);
  } else if (pinNum == pirPin) {
    // get PIR status
    bool bval = getPIRval();
    memcpy(&responseData, &bval, sizeof(bval)); 
  } else if (pinNum == lampPin) {
    // set Lamp status
    memcpy(&ival, &receivedData, sizeof(ival)); 
    setLamp(ival);
  } else if (pinNum == ds18b20Pin) {
    // get current temperature
    float fval = dsTemp;
    memcpy(&responseData, &fval, sizeof(fval)); 
  } else if (pinNum == voltPin) {
    // get current batt voltage
    float fval = currentVoltage;
    memcpy(&responseData, &fval, sizeof(fval)); 
  } else if (pinNum == (EXT_IO_PING - EXTPIN)) {
    LOG_INF("Received client ping");
  } else LOG_ERR("Undefined pin number requested: %d ", pinNum);
  return responseData;
}

static void prepPIR() {
  if ((pirPin < EXTPIN) && pirUse) {
    if (pirPin) pinMode(pirPin, INPUT_PULLDOWN); // pulled high for active
    else {
      pirUse = false;
      LOG_WRN("No PIR pin defined");
    }
  }
}

void prepPeripherals() {
  // initial setup of each peripheral on client or extender
  setupBatt();
  prepUart();
  setupLamp();
  prepPIR();
  prepTemperature();
  prepServos();  
  debugMemory("prepPeripherals");
}
