/*
 *** NOT TESTED ***

 Control a Pan-Tilt-Camera stand using two servos connected to specified pins in myConfig.h
 Incorporates code from fork developed by @Styne13 https://github.com/Styne13/ESP32-CAM_MJPEG2SD-PanTiltServo
 Use Arduino Manage Libraries to install ServoESP32
 
 s60sc 2022
 */

#include "myConfig.h"

#ifdef INCLUDE_PANTILT
#include <Servo.h> //https://github.com/RoboticsBrno/ServoESP32

// configuration
static Servo ServoRotate;
static Servo ServoTilt;
#endif

void updateCamPan(int panVal) {
#ifdef INCLUDE_PANTILT
  // update pan position from user input
  ServoRotate.write(panVal);
#endif
}

void updateCamTilt(int tiltVal) {
#ifdef INCLUDE_PANTILT
  // update tilt position from user input
  ServoTilt.write(tiltVal);
#endif
}

void prepPanTilt() {
#ifdef INCLUDE_PANTILT
  // prep servos
  ServoRotate.attach(SERVO_PAN_PIN, 2, 0, 180, 544, 2400);
  ServoTilt.attach(SERVO_TILT_PIN, 3, 0, 180, 544, 2400);
  ServoRotate.write(90);
  ServoTilt.write(90);
#endif
}
