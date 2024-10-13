
// Optional peripherals, to support:
// - pin sensors eg PIR / radar
// - servos, eg camera pan / tilt / steer
// - DS18B20 temperature sensor
// - battery voltage measurement
// - lamp LED driver (PWM or WS2812 / SK6812)
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

#if INCLUDE_PERIPH
#include "driver/ledc.h"

// peripherals used
bool pirUse; // true to use PIR for motion detection
bool ledBarUse; // true to use led bar
uint8_t lampLevel; // brightness of on board lamp led 
bool lampAuto = false; // if true in conjunction with pirUse, switch on lamp when PIR activated at night
bool lampNight; // if true, lamp comes on at night (not used)
int lampType; // how lamp is used
bool voltUse; // true to report on ADC pin eg for for battery
bool stickUse; // true to use joystick
bool buzzerUse; // true to use buzzer
bool stepperUse; // true to use stepper motor
bool SVactive; // true to use servos
TaskHandle_t heartBeatHandle = NULL;
bool RCactive = false;

// Pins used by peripherals

// sensors 
int pirPin; // if pirUse is true
int lampPin;
int buzzerPin; // if buzzerUse is true

// Camera servos 
int servoPanPin;
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
int servoSteerPin;
int lightsRCpin;
int heartbeatRC;
int maxSteerAngle;
int maxDutyCycle;
int minDutyCycle;
int maxTurnSpeed;
bool allowReverse;
bool autoControl;
int waitTime; 
int stickzPushPin; // digital pin connected to switch output
int stickXpin; // analog pin connected to X output
int stickYpin; // analog pin connected to Y output
int relayPin;
bool relayMode;

// MY9221 LED Bar pins
int ledBarClock;
int ledBarData;

// 28BYJ-48 Stepper Motor with ULN2003 Motor Driver
#define stepperPins 4 
uint8_t stepINpins[stepperPins];

static void doStep();
void setStickTimer(bool restartTimer, uint32_t interval);
void setLamp(uint8_t lampVal);


// individual pin sensor / controller functions

bool getPIRval() {
  // get PIR or radar sensor status 
  return digitalRead(pirPin); 
}

void buzzerAlert(bool buzzerOn) {
  // control active buzzer operation
  if (buzzerUse) {
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
    if (newSteerVal != oldSteerVal) oldSteerVal = changeAngle(servoSteerPin, newSteerVal, oldSteerVal, false);
    if (newPanVal != oldPanVal) oldPanVal = changeAngle(servoPanPin, newPanVal, oldPanVal);
    if (newTiltVal != oldTiltVal) oldTiltVal = changeAngle(servoTiltPin, newTiltVal, oldTiltVal);
  }
}

void setCamPan(int panVal) {
  // change camera pan angle
  newPanVal = panVal;
  if (servoPanPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

void setCamTilt(int tiltVal) {
  // change camera tilt angle
  newTiltVal = tiltVal;
  if (servoTiltPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

void setSteering(int steerVal) {
  // change steering angle
  newSteerVal = steerVal;
  if (servoSteerPin && servoHandle != NULL) xTaskNotifyGive(servoHandle);
}

static void prepServos() {
  if (SVactive) {
    if (servoPanPin) ledcAttach(servoPanPin, PWM_FREQ, DUTY_BIT_DEPTH); 
    else LOG_WRN("No servo pan pin defined");
    if (servoTiltPin) ledcAttach(servoTiltPin, PWM_FREQ, DUTY_BIT_DEPTH);
    else LOG_WRN("No servo tilt pin defined");
  }
  if (RCactive && servoSteerPin) ledcAttach(servoSteerPin, PWM_FREQ, DUTY_BIT_DEPTH);
  oldPanVal = oldTiltVal = oldSteerVal = servoCenter + 1;

  if (SVactive || (RCactive && servoSteerPin)) {
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
#include <OneWire.h> // https://github.com/PaulStoffregen/OneWire
#include <DallasTemperature.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
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
  if (ds18b20Pin) {
    xTaskCreate(&DS18B20task, "DS18B20task", DS18B20_STACK_SIZE, NULL, DS18B20_PRI, &DS18B20handle); 
    haveDS18B20 = true;
    LOG_INF("Using DS18B20 sensor");
  } else LOG_WRN("No DS18B20 pin defined, using chip sensor if present");
#endif
}

float readTemperature(bool isCelsius, bool onlyDS18) {
  // return latest read temperature value in celsius (true) or fahrenheit (false), unless error
  if (onlyDS18) return dsTemp;
  if (!haveDS18B20) dsTemp = readInternalTemp();
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
  if (voltUse) {
  	if (voltPin) {
      xTaskCreate(&battTask, "battTask", BATT_STACK_SIZE, NULL, BATT_PRI, &battHandle);
      LOG_INF("Monitor batt voltage");
      debugMemory("setupBatt");
    } else LOG_WRN("No voltage pin defined");
  }
}

/********************* LED Lamp Driver **********************/

#define RGB_BITS 24  // WS2812 / SK6812 has 24 bit color in RGB order
static bool lampInit = false;
#if defined(USE_WS2812)
static rmt_data_t ledData[RGB_BITS];
#endif

static void setupLamp() {
  // setup lamp LED according to board type
  // assumes led wired as active high (ESP32 lamp led on pin 4 is active high, signal led on pin 33 is active low)
  lampInit = false;
#if defined(LED_GPIO_NUM)
  if (lampPin <= 0) {
    lampPin = LED_GPIO_NUM;
    char lampPinStr[3];
    sprintf(lampPinStr, "%d", lampPin);
    updateStatus("lampPin", lampPinStr);
  }
#endif

  if (lampPin) {
    lampInit = true;
#if defined(USE_WS2812)
    // WS2812 RGB high intensity led
    if (rmtInit(lampPin, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000)) 
      LOG_INF("Setup WS2812 Lamp Led on pin %d", lampPin);
    else {
      LOG_WRN("Failed to setup WS2812 on pin %u", lampPin);
      lampInit = false;
    }
#else
    // assume PWM LED
    ledcAttach(lampPin, 5000, DUTY_BIT_DEPTH); // freq, resolution
    setLamp(0);
    LOG_INF("Setup PWM Lamp Led on pin %d", lampPin);
#endif
  }
  if (lightsRCpin > 1) pinMode(lightsRCpin, OUTPUT);
}

void setLamp(uint8_t lampVal) {
  // control lamp status
  if (lampPin) {
    if (!lampInit) setupLamp();
    if (lampInit) {
#if defined(USE_WS2812)
      // WS2812 LED - set white color and apply lampVal (0 = off, 15 = max)
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
      rmtWrite(lampPin, ledData, RGB_BITS, RMT_WAIT_FOR_EVER);
#else
      // assume PWM LED, set lamp brightness using PWM (0 = off, 15 = max)
      uint8_t valueMax = 15;
      uint32_t duty = (pow(2, DUTY_BIT_DEPTH) / valueMax) * min(lampVal, valueMax);
      ledcWrite(lampPin, duty);
#endif
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

void setLightsRC(bool lightsOn) {
  // on / off RC light 
  if (lightsRCpin > 0) digitalWrite(lightsRCpin, lightsOn);
}

static void prepPIR() {
  if (pirUse) {
    if (pirPin) pinMode(pirPin, INPUT_PULLDOWN); // pulled high for active
    else {
      pirUse = false;
      LOG_WRN("No PIR pin defined");
    }
  }
  if (relayPin) pinMode(relayPin, OUTPUT);
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
    timerDetachInterrupt(stickTimer); 
    timerEnd(stickTimer);
    stickTimer = NULL;
  }
  if (restartTimer) {
    // (re)start timer interrupt per required interval
    stickTimer = timerBegin(OneMHz); // 1 MHz
    timerAttachInterrupt(stickTimer, &stickISR);
    timerAlarm(stickTimer, interval, true, 0); // in usecs
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
#if INCLUDE_MCPWM
      motorSpeed(motorCycle);
#endif
      if (lightsChanged != lightsStatus) setLightsRC(lightsChanged);
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
      LOG_INF("Stepper motor on pins: %d, %d, %d, %d", stepINpins[0], stepINpins[1], stepINpins[2], stepINpins[3]);
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
#if (INCLUDE_PGRAM && INCLUDE_PERIPH)
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

void prepPeripherals() {
  // initial setup of each peripheral on client or extender
  setupADC();
  setupBatt();
  setupLamp();
  prepPIR();
  prepTemperature();
  prepServos();  
  prepJoystick();
  prepStepper();
  prepLedBar();
  debugMemory("prepPeripherals");
}

#endif
