
// Optional peripherals, to support:
// - pin sensors eg PIR
// - pin controllers eg Lamp
// - servos, eg camera pan / tilt
// - DS18B20 temperature sensor
// - battery voltage measurement
//
// Peripherals can be hosted directly on the client ESP, or on
// a separate IO Extender ESP if the client ESP has limited free 
// pins, eg ESP-Cam module
// External peripherals should have low data rate and not require fast response,
// so interrupt driven input pins should be monitored internally by the client.
// Peripherals that need a clocked data stream such as microphones are not suitable
//
// The client and extender must be compiled with the same version of 
// the peripherals.cpp and have compatible configuration settings
//
// s60sc 2022

#include "globals.h"

// following peripheral requires additional libraries - see peripherals.cpp
//#define INCLUDE_DS18B20 // uncomment to include DS18B20 temp sensor if fitted

#ifndef IS_ESP32_C3 // ESP32 ADC not compatible with ESP32-C3
//#define INCLUDE_VOLTAGE // uncomment to include battery voltage monitoring
#endif

// IO Extender use
bool useIOextender; // true to use IO Extender, otherwise false
bool useUART0; // true to use UART0, false for UART1
int uartTxdPin;
int uartRxdPin;
// peripherals used
bool pirUse; // true to use PIR for motion detection
bool lampUse; // true to use lamp
bool lampAuto; // if true in conjunction with pirUse & lampUse, switch on lamp when PIR activated at night
bool servoUse; // true to use pan / tilt servo control
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
// only pin 33 can be used on ESP-Cam module as it is the only available analog pin
int voltPin; // if INCLUDE_VOLTAGE uncommented

// microphone recording - mic.cpp
// INMP441 I2S microphone pinout, connect L/R to GND for left channel
int micSckPin; // I2S SCK
int micWsPin;  // I2S WS
int micSdPin;  // I2S SD

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

// individual pin sensor / controller functions

bool pirVal = false;

bool getPIRval() {
  // get PIR status 
  // if use external PIR, will have delayed response
  if (!externalPeripheral(pirPin)) pirVal = digitalRead(pirPin); 
  return pirVal; 
}
 
void setLamp(bool lampVal) {
  if (lampUse) {
    // switch lamp on / off
    if (!externalPeripheral(lampPin, lampVal)) digitalWrite(lampPin, lampVal); 
  }
}

// Control a Pan-Tilt-Camera stand using two servos connected to pins specified above

#include "driver/ledc.h"

#define PWM_FREQ 50 // hertz
#define DUTY_BIT_DEPTH 14 // max for ESP32-C3
#define USECS 1000000
#define SERVO_PAN_CHANNEL LEDC_CHANNEL_3
#define SERVO_TILT_CHANNEL LEDC_CHANNEL_4
TaskHandle_t servoHandle = NULL;
static int tiltVal, panVal, newTiltVal, newPanVal;
static int oldPanVal = 90;
static int oldTiltVal = 90;

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
  ledcSetup(SERVO_PAN_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
  ledcAttachPin(servoPanPin, SERVO_PAN_CHANNEL);
  ledcSetup(SERVO_TILT_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
  ledcAttachPin(servoTiltPin, SERVO_TILT_CHANNEL);
  xTaskCreate(&servoTask, "servoTask", 1024, NULL, 1, &servoHandle); 

  // initial angle
  setCamPan(90);
  setCamTilt(90);
  
  LOG_INF("Servos available");
}


/* Read temperature from DS18B20 connected to pin specified above
 Use Arduino Manage Libraries to install OneWire and DallasTemperature
 DS18B20 is a one wire digital temperature sensor
 Pin layout from flat front L-R: Gnd, data, 3V3.
 Need a 4.7k resistor between 3V3 and data line
 Runs in its own task as there is a 750ms delay to get temperature
*/

#ifdef INCLUDE_DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#endif
#ifndef IS_ESP32_C3
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#endif

// configuration
#define NO_TEMP -127
float dsTemp = NO_TEMP;
TaskHandle_t DS18B20handle = NULL;

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

void prepDS18B20() {
#ifdef INCLUDE_DS18B20
  if (ds18b20Pin > 0 && ds18b20Pin < EXTPIN)
    xTaskCreate(&DS18B20task, "DS18B20task", 1024, NULL, 1, &DS18B20handle); 
  else LOG_WRN("No DS18B20 pin defined");
#endif
}

float readDS18B20temp(bool isCelsius) {
  // return latest read DS18B20 value in celsius (true) or fahrenheit (false), unless error
  if (ds18b20Pin > 0) externalPeripheral(ds18b20Pin);
#ifndef IS_ESP32_C3
  // convert on chip raw temperature in F to Celsius degrees
  else dsTemp = (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present     
#endif
  return (dsTemp > NO_TEMP) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
}

/************ battery monitoring ************/
// Read voltage from battery connected to ADC pin - client peripheral only
#ifdef INCLUDE_VOLTAGE

#include "esp_adc_cal.h"
#define DEFAULT_VREF 1100 // if eFuse or two point not available on old ESPs
static esp_adc_cal_characteristics_t *adc_chars; // holds ADC characteristics
static const adc_atten_t ADCatten = ADC_ATTEN_DB_11; // attenuation level
static const adc_unit_t ADCunit = ADC_UNIT_1; // using ADC1
static const adc_bits_width_t ADCbits = ADC_WIDTH_BIT_11; // ADC bit resolution
#endif
float currentVoltage = -1.0; // no monitoring

#ifdef INCLUDE_VOLTAGE
static adc1_channel_t getADCchannel(int gpioNum) {
  // the 6 ESP32 pins that can be used for ADC input, in order: 32, 33, 34, 35, 36, 39
  const adc1_channel_t ADCchannel[] = {ADC1_CHANNEL_4, ADC1_CHANNEL_5, ADC1_CHANNEL_6, ADC1_CHANNEL_7, ADC1_CHANNEL_0, ADC1_CHANNEL_0, ADC1_CHANNEL_0, ADC1_CHANNEL_3}; 
  return ADCchannel[gpioNum - 32];
}
#endif

static void battVoltage() {
#ifdef INCLUDE_VOLTAGE
  // get multiple readings of battery voltage from ADC pin and average
  // input battery voltage may need to be reduced by voltage divider resistors to keep it below 3V3.
  #define NO_OF_SAMPLES 16 // ADC multisampling
  uint32_t ADCsample = 0;
  static bool sentEmailAlert = false;
  for (int j = 0; j < NO_OF_SAMPLES; j++) ADCsample += adc1_get_raw(getADCchannel(voltPin)); 
  ADCsample /= NO_OF_SAMPLES;
  // convert ADC averaged pin value to curve adjusted voltage in mV
  if (ADCsample > 0) ADCsample = esp_adc_cal_raw_to_voltage(ADCsample, adc_chars);
  currentVoltage = ADCsample * voltDivider / 1000.0; // convert to battery volts

#ifdef INCLUDE_SMTP
  if (currentVoltage < voltLow && !sentEmailAlert) {
    sentEmailAlert = true; // only sent once per esp32 session
    smtpBufferSize = 0; // no attachment
    char battMsg[20];
    sprintf(battMsg, "Voltage is %0.1fV", currentVoltage);
    emailAlert("Low battery", battMsg);
  }
#endif
#endif
}

static void battTask(void* parameter) {
  delay(20 * 1000); // allow time for esp32 to start up
  while (true) {
    battVoltage();
    delay(voltInterval * 60 * 1000); // mins
  }
  vTaskDelete(NULL);
}

void setupADC() {
  // Characterise ADC to generate voltage curve for battery monitoring 
#ifdef INCLUDE_VOLTAGE
  if (voltPin) {
    adc1_config_width(ADCbits);
    adc1_config_channel_atten(getADCchannel(voltPin), ADCatten);
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADCunit, ADCatten, ADCbits, DEFAULT_VREF, adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {LOG_INF("ADC characterised using eFuse Two Point Value");}
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {LOG_INF("ADC characterised using eFuse Vref");}
    else {LOG_INF("ADC characterised using Default Vref");}
    xTaskCreate(&battTask, "battTask", 2048, NULL, 1, NULL);
  }
#endif
}


/********************* interact with UART **********************/

void setPeripheralResponse(const byte pinNum, const uint32_t responseData) {
  // callback for uart Client task 
  // updates peripheral stored input value when response received
  // map received pin number to peripheral
  if (pinNum == pirPin) 
    memcpy(&pirVal, &responseData, sizeof(pirVal));  // set PIR status
  else if (pinNum == ds18b20Pin)
    memcpy(&dsTemp, &responseData, sizeof(dsTemp));  // set current temperature
  else if (pinNum != lampPin && pinNum != servoPanPin && pinNum != servoTiltPin) 
    LOG_ERR("Undefined pin number requested: %d ", pinNum);
}

uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData) {
  // callback for IO Extender to interact with peripherals
  uint32_t responseData = 0;
  // map received pin number to peripheral
  if (pinNum == servoTiltPin) {
    // send tilt angle to servo
    int ival;
    memcpy(&ival, &receivedData, sizeof(ival)); 
    setCamTilt(ival);
  } else if (pinNum == servoPanPin) {
    // send pan angle to servo
    int ival;
    memcpy(&ival, &receivedData, sizeof(ival)); 
    setCamPan(ival);
  } else if (pinNum == pirPin) {
    // get PIR status
    bool bval = getPIRval();
    memcpy(&responseData, &bval, sizeof(bval)); 
  } else if (pinNum == lampPin) {
    // set Lamp status
    bool bval;
    memcpy(&bval, &receivedData, sizeof(bval)); 
    setLamp(bval);
  } else if (pinNum == ds18b20Pin) {
    // get current temperature
    float fval = readDS18B20temp(true);
    memcpy(&responseData, &fval, sizeof(fval)); 
  } else LOG_ERR("Undefined pin number requested: %d ", pinNum);
  return responseData;
}

void prepPeripherals() {
  // initial setup of each peripheral on client or extender
  prepUart();
  if ((lampPin < EXTPIN) && lampUse) pinMode(lampPin, OUTPUT);
  if ((pirPin < EXTPIN) && pirUse) pinMode(pirPin, INPUT_PULLDOWN); // pulled high for active
  if (ds18b20Pin < EXTPIN) prepDS18B20();
  if ((servoPanPin < EXTPIN) && servoUse) prepServos();   
}
