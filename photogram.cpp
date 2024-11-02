// 
// Photogrammetry uses photographs taken from various angles to collect data about a 3D object
// that can converted by software to create a 3D image, eg for 3D printing a replica.
// To enable photographs to be taken from different angles a turntable hosting the object can be rotated at intervals
// in front of a static camera.
//
// The ESP can be used to control the turntable using a stepper motor, and take photograps either using its built in camera, 
// or by remotely controlling the shutter of a DSLR camera.

// A turntable can be 3D printed and driven by a 28BYJ-48 stepper motor with ULN2003 Motor Driver.
// 
// Example of 3D printed turntable: www.thingiverse.com/thing:4817279
// Example of circuit to interface to RS-60E3 Remote Switch for DSLR cameras: github.com/ch3p4ll3/ESP-Intervallometer#how-to-make-your-intervallometer
// Use Meshroom software to create a 3D image: alicevision.org/#meshroom
// Use Blender software to convert and modify the image for 3D printing: www.blender.org

// Use web interface to specify the parameters and pins to be used listed below. The turntable will make a complete rotation, stopping at regular intervals
// to take a photo depending on number of photos required. If the ESP camera is used, the photos are stored on the SD card as JPEG images in a folder
// named after the date time when the Start button was pressed.
// If the ESP lamp LED is enabled, it will be used as a flash.

// s60sc 2024

#include "appGlobals.h"

#if INCLUDE_PGRAM 
#if !INCLUDE_PERIPH
#error "Need INCLUDE_PERIPH true"
#endif

// Use web interface to specify the following parameters
uint8_t numberOfPhotos; // number of photos to be taken in a rotation of the turntable
float tRPM; // required turntable RPM
bool clockWise; // rotation direction of turntable
uint8_t timeForFocus; // time for DSLR auto focus in secs
// timeForPhoto is total time to allow for a photo in secs, need to: 
// - wait for turntable to stabilize
// - wait for ESP Lamp LED to illuminate if required
// - time allowed for auto focus if required
// - shutter time for photo
uint8_t timeForPhoto; 
int pinShutter; // pin used for RS-60E3 shutter control
int pinFocus; // pin used for RS-60E3 shutter control
uint8_t photosDone; // read only count of number of photos taken so far
float gearing; // number of rotation of stepper motor for one rotation of turntable
bool extCam = false; // whether to use external DSLR camera (true) or built in ESP Cam (false)
bool PGactive = false; 

static float mRPM; // stepper RPM derived from tRPM and gearing
static TaskHandle_t pgramHandle = NULL;
static char pFolder[20];

#define MAX_RPM 15.0 // max allowed stepper motor RPM
#define shutterTime 100 // time in ms to allow DSLR shutter to open and close

static void prepPgram() {
  if (extCam) {
    pinMode(pinShutter, OUTPUT); 
    if (pinFocus) pinMode(pinFocus, OUTPUT); 
    LOG_INF("External cam, shutter pin %d", pinShutter);
#ifdef AUXILIARY
  } else {
     // use built in cam
     lampAuto = true;
     useMotion = doRecording = doPlayback = timeLapseOn = false;   
     setLamp(0);
     // create folder
     time_t currEpoch = getEpoch();
     strftime(pFolder, sizeof(pFolder), "/%Y%m%d_%H%M%S", localtime(&currEpoch));
     STORAGE.mkdir(pFolder);
     LOG_INF("Built in cam, created photogrammetry folder %s", pFolder);
#endif
  }
}

#ifdef AUXILIARY
static void getPhoto() {
  LOG_WRN("Internal camera not available on auxiliary board");
  photosDone = numberOfPhotos;
  stepperDone();
}
#else 
static void getPhoto() {
  // use built in esp cam
  setLamp(lampLevel); // turn on lamp led as flash if required
  if (timeForPhoto * 1000 > MAX_FRAME_WAIT) delay((timeForPhoto * 1000) - MAX_FRAME_WAIT); // allow time for turntable to stabilise
  uint32_t startTime = millis();
  doKeepFrame = true;
  while (doKeepFrame && (millis() - startTime < MAX_FRAME_WAIT)) delay(100);
  if (!doKeepFrame && alertBufferSize) {
    // create file name 
    char pName[FILE_NAME_LEN];
    strcpy(pName, pFolder);
    time_t currEpoch = getEpoch();
    strftime(pName + strlen(pFolder), sizeof(pName), "/%Y%m%d_%H%M%S", localtime(&currEpoch));
    strcat(pName, JPG_EXT);
    File pFile = STORAGE.open(pName, FILE_WRITE);
    // save file to SD
    pFile.write((uint8_t*)alertBuffer, alertBufferSize);
    pFile.close();
    LOG_INF("Photo %u of % u saved in %s", photosDone + 1, numberOfPhotos, pName);
    alertBufferSize = 0;
  } else LOG_WRN("Failed to get photo");
  setLamp(0);
}
#endif

static void takePhoto() {
  // control external camera
  if (timeForFocus * 1000 > timeForPhoto * 1000 - shutterTime) timeForFocus = timeForPhoto - 1;
  uint32_t waitTime = (timeForPhoto - timeForFocus) * 1000 - shutterTime;
  delay(waitTime); // allow time for turntable to stabilise
  if (pinFocus) {
    // if using auto focus
    digitalWrite(pinFocus, HIGH);
    delay(timeForFocus * 1000); // allow time for auto focus   
  }
  digitalWrite(pinShutter, HIGH);
  delay(shutterTime);
  digitalWrite(pinShutter, LOW);
  if (pinFocus) digitalWrite(pinFocus, LOW);
  if (photosDone < numberOfPhotos) LOG_INF("Photo %u of %u taken", photosDone + 1, numberOfPhotos);
}

static void pgramTask (void *pvParameter) {
  // take sequence of photos in one revolution of turntable
  // turntable rotation requires gearing number of shutter motor rotations
  float angle = 1.0 / (float)numberOfPhotos; // ie angular fraction of one revolution
  photosDone = 0;
  prepPgram();
  LOG_INF("Start taking %u photos each %0.1f deg at %0.1f RPM", numberOfPhotos, angle * 360, tRPM);
  do {
    extCam ? takePhoto() : getPhoto();
    // !clockwise as turntable rotates opp to motor
    stepperRun(mRPM, angle * gearing, !clockWise); 
    // wait for stepper task to finish
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  } while (++photosDone < numberOfPhotos);
  LOG_INF("Completed taking photos");
  if (extCam) {
    pinMode(pinShutter, INPUT); // stop unneccessary power use
    if (pinFocus) pinMode(pinFocus, INPUT); // stop unneccessary power use
  }
  pgramHandle = NULL;
  vTaskDelete(NULL);
} 

void takePhotos(bool startPhotos) { 
  // start task
  if (stepperUse) {
    if (startPhotos) {
      mRPM = tRPM * gearing;
      if (mRPM > MAX_RPM) LOG_WRN("Requested stepper RPM %0.1f is too high", mRPM);
      else {
        if (pgramHandle == NULL) xTaskCreate(&pgramTask, "pgramTask", STICK_STACK_SIZE , NULL, STICK_PRI, &pgramHandle);
        else LOG_WRN("pgramTask still running");
      }
    } else {
      LOG_INF("User aborted taking photos");
      photosDone = numberOfPhotos;
      stepperDone();
    }
  }
}

void stepperDone() {
  // notify photogrammetry task for next step
  if (pgramHandle) xTaskNotifyGive(pgramHandle);
}

#endif
