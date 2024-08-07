
// Optional peripherals, to support:
// - pin sensors eg PIR / radar
// - servos, eg camera pan / tilt / steer
// - DS18B20 temperature sensor
// - battery voltage measurement
// - lamp LED driver (PWM or WS2812)
// - H-bridge motor controller (MCPWM)
// - 3 pin joystick 
// - MY9221 based LED Bar, eg 10 segment Grove LED Bar
// - 4 pin 28BYJ-48 Stepper Motor with ULN2003 Motor Driver
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
// s60sc 2022 - 2024
//

#include "appGlobals.h"
#include "driver/ledc.h"

// IO Extender use
bool useIOextender = false; // true to use IO Extender, otherwise false
bool useUART0 = true; // true to use UART0, false for UART1
int uartTxdPin;
int uartRxdPin;
#define EXT_IO_PING 199 // dummy pin number for ping heartbeat
static bool extIOpinged = true;

// peripherals used
bool pirUse; // true to use PIR for motion detection
bool lampUse; // true to use lamp
bool ledBarUse; // true to use led bar
uint8_t lampLevel; // brightness of on board lamp led 
bool lampAuto; // if true in conjunction with pirUse & lampUse, switch on lamp when PIR activated at night
bool lampNight; // if true, lamp comes on at night (not used)
int lampType; // how lamp is used
bool servoUse; // true to use pan / tilt servo control
bool voltUse; // true to report on ADC pin eg for for battery
bool wakeUse = false; // true to allow app to sleep and wake
bool stickUse; // true to use joystick
bool buzzerUse; // true to use buzzer
bool stepperUse; // true to use stepper motor

// Pins used by peripherals

// To use IO Extender, use config web page to set pin numbers on client to be those used on IO Extender
// and add EXTPIN, eg: on config web page, set ds18b20Pin to 110 (100 + 10) to use pin 10 on IO Extender
// and set ds18b20Pin to 10 on IO Extender
// If IO Extender not being used, ensure pins on ESP-Cam not defined for multiple use

// sensors 
int pirPin; // if pirUse is true
int lampPin; // if lampUse is true
int wakePin; // if wakeUse is true
int buzzerPin; // if buzzerUse is true

// Camera servos 
int servoPanPin; // if servoUse is true
int servoTiltPin;

// ambient / module temperature reading 
int ds18b20Pin; // if INCLUDE_DS18B20 true

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
int servoCenter = 90; // angle in degrees where servo is centered 

// configure battery monitor
int voltDivider; // set battVoltageDivider value to be divisor of input voltage from resistor divider
                 // eg: 100k / 100k would be divisor value 2
float voltLow; // voltage level at which to send out email alert
int voltInterval; // interval in minutes to check battery voltage

// buzzer duration
int buzzerDuration; // time buzzer sounds in seconds 

// RC pins and control
bool RCactive = false;
int motorRevPin;
int motorFwdPin;
int motorRevPinR;
int motorFwdPinR;
int servoSteerPin;
int lightsRCpin;
int pwmFreq = 50;
int maxSteerAngle;
int maxDutyCycle;
int minDutyCycle;
int maxTurnSpeed;
bool trackSteer = false;
bool allowReverse;
bool autoControl;
int waitTime; 
int stickzPushPin; // digital pin connected to switch output
int stickXpin; // analog pin connected to X output
int stickYpin; // analog pin connected to Y output

// MY9221 LED Bar pins
int ledBarClock;
int ledBarData;

// 28BYJ-48 Stepper Motor with ULN2003 Motor Driver
#define stepperPins 4 
uint8_t stepINpins[stepperPins];

static void doStep();
void setStickTimer(bool restartTimer, uint32_t interval);
void setLamp(uint8_t lampVal);

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
  // get PIR or radar sensor status 
  // if use external PIR, will have delayed response
  if (!externalPeripheral(pirPin)) pirVal = digitalRead(pirPin); 
  return pirVal; 
}

void buzzerAlert(bool buzzerOn) {
  // control active buzzer operation
  if (buzzerUse && !externalPeripheral(buzzerPin, buzzerOn)) {
    if (buzzerOn) {
      // turn buzzer on
      pinMode(buzzerPin, OUTPUT);
      digitalWrite(buzzerPin, HIGH); 
    } else digitalWrite(buzzerPin, LOW); // turn buzzer off
  }
}

// Control a Pan-Tilt-Camera stand using two servos connected to pins specified above
// Or control an RC servo
// Only tested for SG90 style servos
// Typically, wiring is:
// - orange: signal
// - red: 5V
// - brown: GND
//
#define PWM_FREQ 50 // hertz
#define DUTY_BIT_DEPTH 12 // max for ESP32-C3 is 14
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#define SERVO_PAN_CHANNEL LEDC_CHANNEL_3
#define SERVO_TILT_CHANNEL LEDC_CHANNEL_4
#define SERVO_STEER_CHANNEL LEDC_CHANNEL_5
#endif
TaskHandle_t servoHandle = NULL;
static int newTiltVal, newPanVal, newSteerVal;
static int oldPanVal, oldTiltVal, oldSteerVal; 

static int dutyCycle (int angle) {
  // calculate duty cycle for given angle
  angle = constrain(angle, servoMinAngle, servoMaxAngle);
  int pulseWidth = map(angle, servoMinAngle, servoMaxAngle, servoMinPulseWidth, servoMaxPulseWidth);
  return pow(2, DUTY_BIT_DEPTH) * pulseWidth * PWM_FREQ / USECS;
}

static int changeAngle(uint8_t servoPin, int newVal, int oldVal, bool useDelay = true) {
  // change angle of given servo
  int incr = newVal - oldVal > 0 ? 1 : -1;
  for (int angle = oldVal; angle != newVal + incr; angle += incr) {
    ledcWrite(servoPin, dutyCycle(angle));
    if (useDelay) delay(servoDelay); // set rate of change
  }
  return newVal;
}

static void servoTask(void* pvParameters) {
  // update servo position from user input
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    if (newSteerVal != oldSteerVal) oldSteerVal = changeAngle(servoSteerPin, newSteerVal, oldSteerVal, false);
    if (newPanVal != oldPanVal) oldPanVal = changeAngle(servoPanPin, newPanVal, oldPanVal);
    if (newTiltVal != oldTiltVal) oldTiltVal = changeAngle(servoTiltPin, newTiltVal, oldTiltVal);
#else
    if (newSteerVal != oldSteerVal) oldSteerVal = changeAngle(SERVO_STEER_CHANNEL, newSteerVal, oldSteerVal, false);
    if (newPanVal != oldPanVal) oldPanVal = changeAngle(SERVO_PAN_CHANNEL, newPanVal, oldPanVal);
    if (newTiltVal != oldTiltVal) oldTiltVal = changeAngle(SERVO_TILT_CHANNEL, newTiltVal, oldTiltVal);
#endif
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

void setSteering(int steerVal) {
  // change steering angle
  newSteerVal = steerVal;
  if (servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

static void prepServos() {
  if ((servoPanPin < EXTPIN) && servoUse) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    if (servoPanPin) ledcAttach(servoPanPin, PWM_FREQ, DUTY_BIT_DEPTH); 
    else LOG_WRN("No servo pan pin defined");
    if (servoTiltPin) ledcAttach(servoTiltPin, PWM_FREQ, DUTY_BIT_DEPTH);
    else LOG_WRN("No servo tilt pin defined");
    if (!servoPanPin && !servoTiltPin) servoUse = false;
  }
  if (servoSteerPin) ledcAttach(servoSteerPin, PWM_FREQ, DUTY_BIT_DEPTH);
#else
    if (servoPanPin) {
      ledcSetup(SERVO_PAN_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
      ledcAttachPin(servoPanPin, SERVO_PAN_CHANNEL);
    } else LOG_WRN("No servo pan pin defined");
    if (servoTiltPin) {
      ledcSetup(SERVO_TILT_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
      ledcAttachPin(servoTiltPin, SERVO_TILT_CHANNEL);
    } else LOG_WRN("No servo tilt pin defined");
    if (!servoPanPin && !servoTiltPin) servoUse = false;
  }
  if (servoSteerPin) {
    ledcSetup(SERVO_STEER_CHANNEL, PWM_FREQ, DUTY_BIT_DEPTH); 
    ledcAttachPin(servoSteerPin, SERVO_STEER_CHANNEL);
  }
#endif
  oldPanVal = oldTiltVal = oldSteerVal = servoCenter + 1;

  if (servoUse || servoSteerPin) {
    xTaskCreate(&servoTask, "servoTask", SERVO_STACK_SIZE, NULL, SERVO_PRI, &servoHandle); 
    // initial angle
    if (servoPanPin) setCamPan(servoCenter);
    if (servoTiltPin) setCamTilt(servoCenter);
    if (servoSteerPin) setSteering(servoCenter);
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

#if INCLUDE_DS18B20
#include <OneWire.h> 
#include <DallasTemperature.h>
#endif
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t temp_sensor = NULL;
  #else
#include "driver/temp_sensor.h"
  #endif
#endif

// configuration
static float dsTemp = NULL_TEMP;
TaskHandle_t DS18B20handle = NULL;
static bool haveDS18B20 = false;

static void DS18B20task(void* pvParameters) {
#if INCLUDE_DS18B20
  // get current temperature from DS18B20 device
  OneWire oneWire(ds18b20Pin);
  DallasTemperature sensors(&oneWire);
  while (true) {
    dsTemp = NULL_TEMP;
    sensors.begin();
    uint8_t deviceAddress[8];
    sensors.getAddress(deviceAddress, 0);
    if (deviceAddress[0] == 0x28) {
      uint8_t tryCnt = 10;
      while (tryCnt) {
        sensors.requestTemperatures(); 
        dsTemp = sensors.getTempCByIndex(0);
        // ignore occasional duff readings
        if (dsTemp > NULL_TEMP) tryCnt = 10;
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
#if INCLUDE_DS18B20
  if (ds18b20Pin < EXTPIN) {
    if (ds18b20Pin) {
      xTaskCreate(&DS18B20task, "DS18B20task", DS18B20_STACK_SIZE, NULL, DS18B20_PRI, &DS18B20handle); 
      haveDS18B20 = true;
      LOG_INF("Using DS18B20 sensor");
    } else LOG_WRN("No DS18B20 pin defined, using chip sensor if present");
  }
#endif
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  // setup internal sensor
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  temperature_sensor_install(&temp_sensor_config, &temp_sensor);
  temperature_sensor_enable(temp_sensor);
  #else
  temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
  temp_sensor.dac_offset = TSENS_DAC_L2;  // TSENS_DAC_L2 is default. L4(-40℃ ~ 20℃), L2(-10℃ ~ 80℃) L1(20℃ ~ 100℃) L0(50℃ ~ 125℃)
  temp_sensor_set_config(temp_sensor);
  temp_sensor_start();
  #endif
#endif
}

float readTemperature(bool isCelsius, bool onlyDS18) {
  // return latest read temperature value in celsius (true) or fahrenheit (false), unless error
#if INCLUDE_DS18B20
  // use external DS18B20 sensor if available, else use local value
  if (haveDS18B20) externalPeripheral(ds18b20Pin);
#endif
  if (onlyDS18) return dsTemp;
  if (!haveDS18B20) {
#if CONFIG_IDF_TARGET_ESP32
    // convert on chip raw temperature in F to Celsius degrees
    dsTemp = (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    temperature_sensor_get_celsius(temp_sensor, &dsTemp); 
  #else
    temp_sensor_read_celsius(&dsTemp); 
  #endif
#endif
  }
  return (dsTemp > NULL_TEMP) ? (isCelsius ? dsTemp : (dsTemp * 1.8) + 32.0) : dsTemp;
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
TaskHandle_t battHandle = NULL;
float readVoltage()  {
  // use external voltage sensor if available, else use local value
  externalPeripheral(voltPin);
  return currentVoltage;
}

static void battTask(void* parameter) {
  if (voltInterval < 1) voltInterval = 1;
  while (true) {
    // convert analog reading to corrected voltage.  analogReadMilliVolts() not working
    currentVoltage = (float)(smoothAnalog(voltPin)) * 3.3 * voltDivider / MAX_ADC;

    static bool sentExtAlert = false;
    if (currentVoltage < voltLow && !sentExtAlert) {
      sentExtAlert = true; // only sent once per esp32 session
      char battMsg[20];
      sprintf(battMsg, "Voltage is %0.2fV", currentVoltage);
      externalAlert("Low battery", battMsg);
    }
    delay(voltInterval * 60 * 1000); // mins
  }
  vTaskDelete(NULL);
}

static void setupBatt() {
  if (voltUse && (voltPin < EXTPIN)) {
    if (voltPin) {
      xTaskCreate(&battTask, "battTask", BATT_STACK_SIZE, NULL, BATT_PRI, &battHandle);
      LOG_INF("Monitor batt voltage");
      debugMemory("setupBatt");
    } else LOG_WRN("No voltage pin defined");
  }
}

/********************* LED Lamp Driver **********************/

#define RGB_BITS 24  // WS2812 has 24 bit color in RGB order
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#define LAMP_LEDC_CHANNEL 2 // Use channel not required by camera
#endif
static bool lampInit = false;
static bool PWMled = true;
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
static rmt_obj_t* rmtWS2812;
#endif
static rmt_data_t ledData[RGB_BITS];

static void setupLamp() {
  // setup lamp LED according to board type
  // assumes led wired as active high (ESP32 lamp led on pin 4 is active high, signal led on pin 33 is active low)
#if defined(LED_GPIO_NUM)
  if (lampPin <= 0) lampPin = LED_GPIO_NUM;
#endif
  bool haveWS2812 = false;
#if defined(USE_WS2812)
  haveWS2812 = true;
#endif
  if ((lampPin < EXTPIN) && lampUse) {
    if (lampPin) {
      lampInit = true;
      if (haveWS2812 && lampPin) {
        // WS2812 RGB high intensity led
        PWMled = false;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        if (rmtInit(lampPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) 
          LOG_INF("Setup WS2812 Lamp Led on pin %d", lampPin);
        else LOG_WRN("Failed to setup WS2812 with pin %u", lampPin);
#else
        rmtWS2812 = rmtInit(lampPin, true, RMT_MEM_64);
        if (rmtWS2812 == NULL) LOG_WRN("Failed to setup WS2812 with pin %u", lampPin);
        else {
          rmtSetTick(rmtWS2812, 100);
          LOG_INF("Setup WS2812 Lamp Led on pin %d", lampPin);
        }
#endif
      } else {
        // assume PWM LED
        PWMled = true;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        ledcAttach(lampPin, 5000, DUTY_BIT_DEPTH); // freq, resolution
#else
        ledcSetup(LAMP_LEDC_CHANNEL, 5000, DUTY_BIT_DEPTH); 
        ledcAttachPin(lampPin, LAMP_LEDC_CHANNEL); 
#endif
        setLamp(0);
        LOG_INF("Setup PWM Lamp Led on pin %d", lampPin);
      }
    } else {
      lampUse = false;
      LOG_WRN("No Lamp Led pin defined");
    }
  } 
}

static void lampWrite(uint8_t pin, uint32_t value, uint32_t valueMax = 15) {
  uint32_t duty = (pow(2, DUTY_BIT_DEPTH) / valueMax) * min(value, valueMax);
  ledcWrite(pin, duty);
}

void setLamp(uint8_t lampVal) {
  // control lamp status
  if (!lampUse) lampVal = 0;
  if (!externalPeripheral(lampPin, lampVal)) {
    if (!lampInit) setupLamp();
    if (lampInit) {
      if (PWMled) {
        // set lamp brightness using PWM (0 = off, 15 = max)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        lampWrite(lampPin, lampVal);
#else
        lampWrite(LAMP_LEDC_CHANNEL, lampVal);
#endif
      } else {
        // assume WS2812 LED - set white color and apply lampVal (0 = off, 15 = max)
        uint8_t RGB[3]; // each color is 8 bits
        lampVal = lampVal == 15 ? 255 : lampVal * 16;
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
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
        rmtWrite(lampPin, ledData, RGB_BITS, RMT_WAIT_FOR_EVER);
#else
        rmtWrite(rmtWS2812, ledData, RGB_BITS);
#endif
      }
    }
  }
}

void twinkleLed(uint8_t ledPin, uint16_t interval, uint8_t blinks) {
  // twinkle led, for given number of blinks, 
  //  with given interval in ms between blinks
  bool ledState = true;
  for (int i=0; i<blinks*2; i++) {
    digitalWrite(ledPin, ledState);
    delay(interval);
    ledState = !ledState;
  }
}

void setLights(bool lightsOn) {
  // External on / off light
  if (lightsRCpin > 0) digitalWrite(lightsRCpin, lightsOn);
}

/********************* interact with UART **********************/

void setPeripheralResponse(const byte pinNum, const uint32_t responseData) {
  // callback for Client uart task 
  // updates peripheral stored input value when response received
  // map received pin number to peripheral
  LOG_VRB("Pin %d, data %u", pinNum, responseData);
  if (pinNum == pirPin) 
    memcpy(&pirVal, &responseData, sizeof(pirVal));  // set PIR status
  else if (pinNum == voltPin)
    memcpy(&currentVoltage, &responseData, sizeof(currentVoltage));  // set current batt voltage
  else if (pinNum == ds18b20Pin)
    memcpy(&dsTemp, &responseData, sizeof(dsTemp));  // set current temperature
  else if (pinNum == EXT_IO_PING) 
    extIOpinged = true;
  else if (pinNum != lampPin && pinNum != servoPanPin && pinNum != servoTiltPin) 
    LOG_WRN("Undefined pin number requested: %d ", pinNum);
}

uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData) {
  // callback for IO Extender to interact with peripherals
  uint32_t responseData = 0;
  int ival;
  LOG_VRB("Pin %d, data %u", pinNum, receivedData);
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
  } else LOG_WRN("Undefined pin number requested: %d ", pinNum);
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


/*
MCPWM peripheral has 2 units, each unit can support:
- 3 pairs of PWM outputs (6 pins)
- 3 fault input pins to detect faults like overcurrent, overvoltage, etc.
- 3 sync input pins to synchronize output signals
- 3 input pins to gather feedback from controlled motors, using e.g. hall sensors

MX1508 DC Motor Driver with PWM Control
- 4 PWM gpio inputs, 2 per motor (forward & reverse)
- Two H-channel drive circuits for 2 DC motors 
- 1.5A (peak 2A)
- 2-10V DC input, 1.8-7V Dc output
- Outputs are OUT1 - OUT4 corresponding to IN1 to IN4
- IN1 / OUT1 A1
- IN2 / OUT2 B1
- IN3 / OUT3 A2
- IN4 / OUT4 B2
*/

////#include "driver/mcpwm_prelude.h" // v3.x
#define CONFIG_MCPWM_SUPPRESS_DEPRECATE_WARN true 
#include "driver/mcpwm.h" // v2.x

static void prepMotor(mcpwm_unit_t mcUnit, int fwdPin, int revPin) {
  // setup gpio pins used for motor (forward, optional reverse), and pwm frequency
  LOG_INF("initialising MCPWM unit %d, using pins %d, %d", mcUnit, fwdPin, revPin);
  mcpwm_gpio_init(mcUnit, MCPWM0A, fwdPin);
  if (motorRevPin > 0) mcpwm_gpio_init(mcUnit, MCPWM0B, revPin); 
  mcpwm_config_t pwm_config;
  pwm_config.frequency = pwmFreq;  // pwm frequency
  pwm_config.cmpr_a = 0;    // duty cycle of PWMxA
  pwm_config.cmpr_b = 0;    // duty cycle of PWMxb
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
  // Configure PWM0A & PWM0B with above settings
  mcpwm_init(mcUnit, MCPWM_TIMER_0, &pwm_config); 
}

void prepMotors() {
#if !CONFIG_IDF_TARGET_ESP32C3
  if (RCactive) {
    if (motorFwdPin > 0) {
      prepMotor(MCPWM_UNIT_0, motorFwdPin, motorRevPin);
      if (trackSteer) prepMotor(MCPWM_UNIT_1, motorFwdPinR, motorRevPinR);
    } else LOG_WRN("RC motor pins not defined");
  }
#else
  RCactive = false;
  LOG_WRN("This function not compatible with ESP32-C3");
#endif
}

static void motorDirection(float duty_cycle, mcpwm_unit_t mcUnit, bool goFwd) {
#if !CONFIG_IDF_TARGET_ESP32C3
  mcpwm_set_signal_low(mcUnit, MCPWM_TIMER_0, goFwd ? MCPWM_OPR_B : MCPWM_OPR_A);
  // motor moves in given direction, with given duty cycle %
  if (duty_cycle > 0.0) {
    mcpwm_set_duty(mcUnit, MCPWM_TIMER_0, goFwd ? MCPWM_OPR_A : MCPWM_OPR_B, duty_cycle);
    // call this each time, if previously in low/high state
    mcpwm_set_duty_type(mcUnit, MCPWM_TIMER_0, goFwd ? MCPWM_OPR_A : MCPWM_OPR_B, MCPWM_DUTY_MODE_0); 
  } else {
    // stop motor
    mcpwm_set_signal_low(mcUnit, MCPWM_TIMER_0, goFwd ? MCPWM_OPR_A : MCPWM_OPR_B);
  }
#endif
}

void motorSpeed(int speedVal, bool leftMotor) {
  // speedVal is signed duty cycle, convert to unsigned float
  if (abs(speedVal) < minDutyCycle) speedVal = 0;
  float speedValFloat = (float)(abs(speedVal));
  if (leftMotor) {
    // left motor steering or all motor direction
    if (motorRevPin && speedVal < 0) motorDirection(speedValFloat, MCPWM_UNIT_0, false); 
    else if (motorFwdPin) motorDirection(speedValFloat, MCPWM_UNIT_0, true);
  } else {
    // right motor steering
    if (motorRevPinR && speedVal < 0) motorDirection(speedValFloat, MCPWM_UNIT_1, false); 
    else if (motorFwdPinR) motorDirection(speedValFloat, MCPWM_UNIT_1, true);
  }
}

static inline int clampValue(int value, int maxValue) {
  // clamp value to the allowable range
  return value > maxValue ? maxValue : (value < -maxValue ? -maxValue : value);
}

void trackSteeering(int controlVal, bool steering) {
  // set left and right motor speed values depending on requested speed and request steering angle
  // steering = true ? controlVal = steer angle : controlVal = speed change
  static int driveSpeed = 0; // -ve for reverse
  static int steerAngle = 0; // -ve for left turn
  steering ? steerAngle = controlVal - servoCenter : driveSpeed = controlVal;
  int turnSpeed = (clampValue(steerAngle, maxSteerAngle) * maxTurnSpeed / 2) / maxSteerAngle; 
  if (driveSpeed < 0) turnSpeed = 0 - turnSpeed;
  motorSpeed(clampValue(driveSpeed + turnSpeed, maxDutyCycle)); // left
  motorSpeed(clampValue(driveSpeed - turnSpeed, maxDutyCycle), false); //right
}

/********************************* joystick *************************************/

// HW-504 Joystick
// Use X axis  for steering, Y axis for motor, push button for lights toggle
// Requires 2 analog pins and 1 digital pin. Ideally supply voltage should be 3.1V
// X axis is longer edge of board

static const int sRate = 1; // samples per analog reading
static int xOffset = 0; // x zero offset
static int yOffset = 0; // y zero offset
static bool lightsChanged = false;
TaskHandle_t stickHandle = NULL;

static void IRAM_ATTR buttonISR() {
  // joystick button pressed - toggle state
  lightsChanged = !lightsChanged;
}

static void IRAM_ATTR stickISR() {
  // interrupt at timer rate
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (stickHandle) {
    vTaskNotifyGiveFromISR(stickHandle, &xHigherPriorityTaskWoken); 
    if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
  }
}

void setStickTimer(bool restartTimer, uint32_t interval) {
  // determines joystick polling rate or stepper speed
  static hw_timer_t* stickTimer = NULL;
  // stop timer if running
  if (stickTimer) {
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    timerAlarmDisable(stickTimer);  
#endif
    timerDetachInterrupt(stickTimer); 
    timerEnd(stickTimer);
    stickTimer = NULL;
  }
  if (restartTimer) {
    // (re)start timer interrupt per required interval
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    stickTimer = timerBegin(OneMHz); // 1 MHz
    timerAttachInterrupt(stickTimer, &stickISR);
    timerAlarm(stickTimer, interval, true, 0); // in usecs
#else
    stickTimer = timerBegin(2, 8000, true); // 0.1ms tick
    int stickInterval = waitTime * 10; // in units of 0.1ms 
    timerAlarmWrite(stickTimer, stickInterval, true); 
    timerAlarmEnable(stickTimer);
    timerAttachInterrupt(stickTimer, &stickISR, true);
#endif
  }
}

static void stickTask (void *pvParameter) {
  static bool lightsStatus = false;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (stickUse) {
      // get joystick position, adjusted for zero offset
      int xPos = smoothAnalog(stickXpin, sRate);
      int steerAngle = (xPos > CENTER_ADC + xOffset) ? map(xPos, CENTER_ADC + xOffset, MAX_ADC, servoCenter, servoCenter + maxSteerAngle)
        : map(xPos, 0, CENTER_ADC + xOffset, servoCenter - maxSteerAngle, servoCenter); 
      setSteering(steerAngle);
      
      int yPos = smoothAnalog(stickYpin, sRate);
      // reverse orientation of Y axis so up is forward
      int motorCycle = (yPos > CENTER_ADC + yOffset) ? map(yPos, CENTER_ADC + yOffset, MAX_ADC, 0, 0 - maxDutyCycle)
        : map(yPos, 0, CENTER_ADC + yOffset, maxDutyCycle, 0); 
      if (abs(motorCycle) < minDutyCycle) motorCycle = 0; // deadzone
      motorSpeed(motorCycle);
      
      if (lightsChanged != lightsStatus) setLights(lightsChanged);
      lightsStatus = lightsChanged;
      LOG_VRB("Xpos %d, Ypos %d, button %d", xPos, yPos, lightsStatus);
    }
    if (stepperUse) doStep();
  }
} 

static void prepJoystick() {
  if (stickUse) {
    if (stickXpin > 0 && stickYpin > 0) {
      // obtain offsets at joystick resting position
      xOffset = smoothAnalog(stickXpin, 8) - CENTER_ADC;
      yOffset = smoothAnalog(stickYpin, 8) - CENTER_ADC;
      LOG_VRB("X-offset: %d, Y-offset: %d", xOffset, yOffset);
      if (stickzPushPin > 0) {
        pinMode(stickzPushPin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(stickzPushPin), buttonISR, FALLING); 
      }
      if (stickHandle == NULL) xTaskCreate(&stickTask, "stickTask", STICK_STACK_SIZE , NULL, STICK_PRI, &stickHandle);
      setStickTimer(true, waitTime * 1000);
      LOG_INF("Joystick available");
    } else {
      stickUse = false;
      LOG_WRN("Joystick pins not defined");
    }
  }
}

/****************************** stepper motors *************************************/

// 28BYJ-48 Geared Stepper Motor with ULN2003 Motor Driver
// Uses stickTask & stickTimer

#define stepsPerRevolution (32 * 64) // number of steps in geared 28BYJ-48 rotation
static uint32_t stepsToDo; // total steps requested
static uint32_t stepDelay; // delay in usecs between each step for required speed
static uint8_t seqIndex = 0;
static bool clockwise = true;

void setStepperPin(uint8_t pinNum, uint8_t pinPos) {
  stepINpins[pinPos] = pinNum;
}

static void prepStepper() {
  if (stepperUse) {
    if (stepINpins[0] > 0 && stepINpins[1] > 0) {
      if (stickHandle == NULL) xTaskCreate(&stickTask, "stickTask", STICK_STACK_SIZE , NULL, STICK_PRI, &stickHandle);   
      LOG_INF("Stepper motor available");
    } else {
      stepperUse = false;
      LOG_WRN("Stepper pins not defined");
    }
  }
}

void stepperRun(float RPM, float revFraction, bool _clockwise) {
  // RPM is stepper motor rotation speed
  // revFraction is required movement as a fraction of full rotation
  uint32_t usecsPerRev = 60 * USECS / RPM; // duration of 1 rev
  stepsToDo = revFraction * stepsPerRevolution;
  stepDelay = usecsPerRev / stepsPerRevolution;
  clockwise = _clockwise;
  seqIndex = clockwise ? 0 : stepperPins - 1;
  for (int i = 0; i < stepperPins; i++) pinMode(stepINpins[i], OUTPUT);
  // start stickTimer that calls task
  setStickTimer(true, stepDelay);
}

// Pin order is IN1, IN2, IN3, IN4 for correct full stepping
static const uint8_t pinSequence[stepperPins][stepperPins] = {
  {1, 1, 0, 0},
  {0, 1, 1, 0},
  {0, 0, 1, 1},
  {1, 0, 0, 1}
};

static void doStep() {
  // called from sticktask for single step
  if (stepsToDo) {
    for (int i = 0; i < stepperPins; i++) digitalWrite(stepINpins[i], pinSequence[seqIndex][i]);
    if (!--stepsToDo) {
      setStickTimer(false, 0);  // stop task timer
      for (int i = 0; i < stepperPins; i++) pinMode(stepINpins[i], INPUT); // stop unnecessary power use
#if INCLUDE_PGRAM
      stepperDone();
#endif
    }
    if (clockwise) seqIndex = (seqIndex == stepperPins - 1) ? 0 : seqIndex + 1;
    else seqIndex = (seqIndex == 0) ? stepperPins - 1 : seqIndex - 1;
  }
}


/******************* MY9221 LED Bar ***************************/
/*
 LED segment bar with MY9221 LED driver, eg Grove LED Bar
 Wiring:
    Black  GND
    Red    3V3
    White  DCKI Clock pin
    Yellow D1   Data pin
    
 Can be used as a gauge, eg display sound level
 */

#define MY9221_COUNT 12 // max number of leds addressable by MY9221 LED driver
#define LEDBAR_COUNT 10 // number of leds in bar display
#define LED_OFF 0x00
#define LED_FULL 0xFF

static bool reverse = true; // from which end to light leds, true is green -> red on Grove LED Bar
static uint8_t ledLevel[LEDBAR_COUNT];

static void ledBarLatch() {
  // display uploaded register by triggering internal-latch function
  digitalWrite(ledBarClock, LOW); 
  delayMicroseconds(250); // minimum 220us
  // Internal-latch control cycle
  bool dataVal = false;
  for (uint8_t i = 0; i < 8; i++, dataVal = !dataVal) {
    digitalWrite(ledBarData, dataVal ? HIGH : LOW);
    delayMicroseconds(1); // > min pulse length 230ns 
  }
}

static void ledBarSend(uint16_t bits) {
  // output led value as clocked 16 bits (only 8 LSB set for 8 bit greyscale)
  bool clockVal = false;
  for (int i = 15; i >= 0; i--, clockVal = !clockVal) {
    digitalWrite(ledBarData, (bits >> i) & 1 ? HIGH : LOW);
    digitalWrite(ledBarClock, clockVal ? HIGH : LOW);
  }
}

void ledBarClear() {
  for (uint8_t i = 0; i < LEDBAR_COUNT; i++) ledLevel[i] = LED_OFF;
}

void ledBrightness(uint8_t whichLed, float brightness) {
  // brightness is a float 0.0 <> 1.0, converted to one of 8 brightness levels or off
  ledLevel[whichLed] |= (1 << (uint8_t)(8 * brightness)) - 1;
}

void ledBarUpdate() {
  // update MY9221 208 bit register with required values
  if (ledBarUse) {
    ledBarSend(0); // initial 16 bit command, as 8 bit greyscale mode + defaults
    // 12 * 16 bits LED greyscale PWM values
    for (uint8_t i = 0; i < LEDBAR_COUNT; i++) // 10 * 16 bits
      ledBarSend(reverse ? ledLevel[LEDBAR_COUNT - 1 - i] : ledLevel[i]);
    // fill register for remaining unused channels
    for (uint8_t i = 0; i < MY9221_COUNT - LEDBAR_COUNT; i++) ledBarSend(LED_OFF);
    ledBarLatch();
  }
}
       
void ledBarGauge(float level) {
  // set how many leds to be switched on and their brightness
  // as a proportion of level between 0.0 and 1.0
  // least significant leds are full brightness and most significant led
  // has a proportional brightness
  level = abs(level);
  if (ledBarUse) {
    ledBarClear();
    uint8_t fullLedCnt = (uint8_t)(level * LEDBAR_COUNT);
    for (uint8_t i = 0; i < fullLedCnt; i++) ledLevel[i] = LED_FULL;
    // set brightness for most significant lit led
    ledBrightness(fullLedCnt, (LEDBAR_COUNT * level) - fullLedCnt); 
    ledBarUpdate();
  }
}

static void prepLedBar() {
  // initialise led state and setup pins
  if (ledBarUse && ledBarClock && ledBarData) {
    pinMode(ledBarClock, OUTPUT);
    pinMode(ledBarData, OUTPUT);
    ledBarClear();
    ledBarUpdate();
    LOG_INF("Setup %d Led Bar with pins %d, %d", LEDBAR_COUNT, ledBarClock, ledBarData);
  } else ledBarUse = false;
}

/**********************************************/

#if (!INCLUDE_UART)
bool externalPeripheral(byte pinNum, uint32_t outputData) {
  // dummy
  return false;
}
#endif

void prepPeripherals() {
  // initial setup of each peripheral on client or extender
  setupADC();
  setupBatt();
#if INCLUDE_UART
  prepUart();
#endif
  setupLamp();
  prepPIR();
  prepTemperature();
  prepServos();  
  prepMotors();
  prepJoystick();
  prepStepper();
  prepLedBar();
  debugMemory("prepPeripherals");
}
