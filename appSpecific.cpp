// mjpeg2sd app specific functions
//
// Direct access URLs for NVR:
// - Video streaming: app_ip/sustain?video=1 
// - Audio streaming: app_ip/sustain?audio=1
// - Subtitle streaming: app_ip/sustain?srt=1
// - Stills: app_ip/control?still=1
//
// s60sc 2022 - 2024

#include "appGlobals.h"

static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN];
static char alertCaption[100];
static bool alertReady = false;
static bool depthColor = true;
static bool devHub = false;
char AuxIP[MAX_IP_LEN];
bool useUart = false; 
volatile audioAction THIS_ACTION = PASS_ACTION;
static void stopRC();

/************************ webServer callbacks *************************/

bool updateAppStatus(const char* variable, const char* value, bool fromUser) {
  // update vars from browser input
  esp_err_t res = ESP_OK; 
  sensor_t* s = esp_camera_sensor_get();
  int intVal = atoi(value);
  float fltVal = atof(value);
  if (!strcmp(variable, "custom")) return res;
#ifndef AUXILIARY
  else if (!strcmp(variable, "stopStream")) stopSustainTask(intVal);
  else if (!strcmp(variable, "stopPlaying")) stopPlaying();
  else if (!strcmp(variable, "minf")) minSeconds = intVal; 
  else if (!strcmp(variable, "motionVal")) motionVal = intVal;
  else if (!strcmp(variable, "moveStartChecks")) moveStartChecks = intVal;
  else if (!strcmp(variable, "moveStopSecs")) moveStopSecs = intVal;
  else if (!strcmp(variable, "maxFrames")) maxFrames = intVal;
  else if (!strcmp(variable, "detectMotionFrames")) detectMotionFrames = intVal;
  else if (!strcmp(variable, "detectNightFrames")) detectNightFrames = intVal;
  else if (!strcmp(variable, "detectNumBands")) detectNumBands = intVal;
  else if (!strcmp(variable, "detectStartBand")) detectStartBand = intVal;
  else if (!strcmp(variable, "detectEndBand")) detectEndBand = intVal;
  else if (!strcmp(variable, "detectChangeThreshold")) detectChangeThreshold = intVal;
  else if (!strcmp(variable, "mlUse")) mlUse = (bool)intVal;
  else if (!strcmp(variable, "mlProbability")) mlProbability = fltVal < 0 ? 0.0 : (fltVal > 1.0 ? 1.0 : fltVal);
  else if (!strcmp(variable, "depthColor")) {
    depthColor = (bool)intVal;
    colorDepth = depthColor ? RGB888_BYTES : GRAYSCALE_BYTES;
  }
  else if (!strcmp(variable, "enableMotion")) {
    // Turn on/off motion detection 
    useMotion = (intVal) ? true : false; 
    if (fsizePtr > 16 && useMotion) {
      useMotion = false;
      updateConfigVect("enableMotion", "0");
      LOG_WRN("Motion detection disabled as frame size %s is too large", frameData[fsizePtr].frameSizeStr);
    } else LOG_INF("%s motion detection", useMotion ? "Enabling" : "Disabling");
  }
  else if (!strcmp(variable, "timeLapseOn")) timeLapseOn = intVal;
  else if (!strcmp(variable, "tlSecsBetweenFrames")) tlSecsBetweenFrames = intVal;
  else if (!strcmp(variable, "tlDurationMins")) tlDurationMins = intVal;
  else if (!strcmp(variable, "tlPlaybackFPS")) tlPlaybackFPS = intVal;  
  else if (!strcmp(variable, "streamNvr")) streamNvr = (bool)intVal; 
  else if (!strcmp(variable, "streamSnd")) streamSnd = (bool)intVal; 
  else if (!strcmp(variable, "streamSrt")) streamSrt = (bool)intVal; 
  else if (!strcmp(variable, "lswitch")) nightSwitch = intVal;
#endif
#if INCLUDE_FTP_HFS
  else if (!strcmp(variable, "upload")) fsStartTransfer(value); 
#endif
  else if (!strcmp(variable, "delete")) {
    stopPlayback = true;
    deleteFolderOrFile(value);
  }
  else if (!strcmp(variable, "record")) doRecording = (intVal) ? true : false;   
  else if (!strcmp(variable, "forceRecord")) forceRecord = (intVal) ? true : false; 
  else if (!strcmp(variable, "dbgMotion")) {
    // only enable show motion if motion detect enabled
    dbgMotion = (intVal && useMotion) ? true : false;
    doRecording = !dbgMotion;
  }
  else if (!strcmp(variable, "devHub")) devHub = (bool)intVal;   
  // peripherals
#if INCLUDE_PERIPH
  else if (!strcmp(variable, "pirUse")) pirUse = (bool)intVal;
  else if (!strcmp(variable, "lampLevel")) {
    lampLevel = intVal;
    if (!lampType) setLamp(lampLevel); // manual
  }
  else if (!strcmp(variable, "lampType")) {
    lampType = intVal;
    lampAuto = lampNight = false;
    if (lampType == 1) lampAuto = true; // lamp activated by PIR
    if (!lampType) setLamp(lampLevel); 
    else setLamp(0); 
  }
  else if (!strcmp(variable, "relayPin")) relayPin = intVal;
  else if (!strcmp(variable, "relayMode")) relayMode = (bool)intVal;
  else if (!strcmp(variable, "relaySwitch")) digitalWrite(relayPin, intVal);
  else if (!strcmp(variable, "SVactive")) SVactive = (bool)intVal;
  else if (!strcmp(variable, "voltUse")) voltUse = (bool)intVal;
  else if (!strcmp(variable, "pirPin")) pirPin = intVal;
  else if (!strcmp(variable, "lampPin")) lampPin = intVal;
  else if (!strcmp(variable, "servoPanPin")) servoPanPin = intVal;
  else if (!strcmp(variable, "servoTiltPin")) servoTiltPin = intVal;
  else if (!strcmp(variable, "voltPin")) voltPin = intVal;
  else if (!strcmp(variable, "servoSteerPin")) servoSteerPin = intVal;
  else if (!strcmp(variable, "servoDelay")) servoDelay = intVal;
  else if (!strcmp(variable, "servoMinAngle")) servoMinAngle = intVal;
  else if (!strcmp(variable, "servoMaxAngle")) servoMaxAngle = intVal;
  else if (!strcmp(variable, "servoMinPulseWidth")) servoMinPulseWidth = intVal;
  else if (!strcmp(variable, "servoMaxPulseWidth")) servoMaxPulseWidth = intVal;
  else if (!strcmp(variable, "servoCenter")) servoCenter = intVal;
  else if (!strcmp(variable, "voltDivider")) voltDivider = intVal;
  else if (!strcmp(variable, "voltLow")) voltLow = fltVal;
  else if (!strcmp(variable, "voltInterval")) voltInterval = intVal;
  else if (!strcmp(variable, "buzzerUse")) buzzerUse = (bool)intVal;  
  else if (!strcmp(variable, "buzzerPin")) buzzerPin = intVal; 
  else if (!strcmp(variable, "buzzerDuration")) buzzerDuration = intVal;
  else if (!strcmp(variable, "ds18b20Pin")) ds18b20Pin = intVal;
#endif
#if INCLUDE_I2C
  else if (!strcmp(variable, "I2Csda")) I2Csda = intVal;
  else if (!strcmp(variable, "I2Cscl")) I2Cscl = intVal;
#endif
#if INCLUDE_AUDIO
  else if (!strcmp(variable, "micRem")) {
    micRem = bool(intVal);
    LOG_INF("Remote mic is %s", micRem ? "On" : "Off");
    if (micRem && !ampVol) LOG_WRN("Amp volume is off");
  }
  else if (!strcmp(variable, "spkrRem")) {
    spkrRem = (bool)intVal;
    LOG_INF("Remote speaker is %s", spkrRem ? "On" : "Off");
    if (spkrRem && !micGain) LOG_WRN("Mic gain is off");
  }
  else if (!strcmp(variable, "micGain")) micGain = intVal;
  else if (!strcmp(variable, "micSckPin")) micSckPin = intVal;
  else if (!strcmp(variable, "micSWsPin")) micSWsPin = intVal;
  else if (!strcmp(variable, "micSdPin")) micSdPin = intVal;
  else if (!strcmp(variable, "ampVol")) ampVol = intVal;
  else if (!strcmp(variable, "mampBckIo")) mampBckIo = intVal;
  else if (!strcmp(variable, "mampSwsIo")) mampSwsIo = intVal;
  else if (!strcmp(variable, "mampSdIo")) mampSdIo = intVal;
  else if (!strcmp(variable, "AudActive")) AudActive = intVal;
#endif
#if INCLUDE_TELEM
  else if (!strcmp(variable, "teleUse")) teleUse = (bool)intVal;
#endif
  else if (!strcmp(variable, "teleInterval")) srtInterval = intVal;
  else if (!strcmp(variable, "wakeUse")) wakeUse = (bool)intVal;
  else if (!strcmp(variable, "wakePin")) wakePin = intVal;
#if INCLUDE_MCPWM
  else if (!strcmp(variable, "motorRevPin")) motorRevPin = intVal;
  else if (!strcmp(variable, "motorFwdPin")) motorFwdPin = intVal;
  else if (!strcmp(variable, "motorRevPinR")) motorRevPinR = intVal;
  else if (!strcmp(variable, "motorFwdPinR")) {
    motorFwdPinR = intVal;
    if (motorFwdPinR > 0) trackSteer = true; // use track steering if pin defined
  }
  else if (!strcmp(variable, "pwmFreq")) pwmFreq = intVal;
#endif
#ifndef AUXILIARY
  else if (!strcmp(variable, "AuxIP")) strncpy(AuxIP, value, MAX_IP_LEN-1);
#endif
#if INCLUDE_PERIPH
  else if (!strcmp(variable, "RCactive")) {
    RCactive = (bool)intVal;
    bool aux = false;
#ifdef AUXILIARY
    aux = true;
#endif
#if INCLUDE_MCPWM
    useBDC = (useUart && !aux) ? false : (bool)intVal;
#endif
  }
  else if (!strcmp(variable, "heartbeatRC")) heartbeatRC = intVal;
  else if (!strcmp(variable, "maxSteerAngle")) maxSteerAngle = intVal;  
  else if (!strcmp(variable, "maxDutyCycle")) maxDutyCycle = intVal;  
  else if (!strcmp(variable, "minDutyCycle")) minDutyCycle = intVal;  
  else if (!strcmp(variable, "maxTurnSpeed")) maxTurnSpeed = intVal;  
  else if (!strcmp(variable, "allowReverse")) allowReverse = (bool)intVal;   
  else if (!strcmp(variable, "autoControl")) autoControl = (bool)intVal; 
  else if (!strcmp(variable, "waitTime")) waitTime = intVal;    
  else if (!strcmp(variable, "lightsRCpin")) lightsRCpin = intVal;
  else if (!strcmp(variable, "stickUse")) stickUse = (bool)intVal; 
  else if (!strcmp(variable, "stickXpin")) stickXpin = intVal; 
  else if (!strcmp(variable, "stickYpin")) stickYpin = intVal; 
  else if (!strcmp(variable, "stickzPushPin")) stickzPushPin = intVal; 
#endif
#if (INCLUDE_PGRAM && INCLUDE_PERIPH)
  else if (!strcmp(variable, "stepIN1pin")) setStepperPin((uint8_t)intVal, 0);
  else if (!strcmp(variable, "stepIN2pin")) setStepperPin((uint8_t)intVal, 1);
  else if (!strcmp(variable, "stepIN3pin")) setStepperPin((uint8_t)intVal, 2);
  else if (!strcmp(variable, "stepIN4pin")) setStepperPin((uint8_t)intVal, 3);
  else if (!strcmp(variable, "PGactive")) {
    PGactive = stepperUse = (bool)intVal;
    if (PGactive) setLamp(0);
  }
  else if (!strcmp(variable, "numberOfPhotos")) numberOfPhotos = intVal;
  else if (!strcmp(variable, "gearing")) gearing = fltVal;
  else if (!strcmp(variable, "RPM")) tRPM = intVal;
  else if (!strcmp(variable, "clockwise")) clockWise = (bool)intVal;
  else if (!strcmp(variable, "timeForPhoto")) timeForPhoto = intVal;
  else if (!strcmp(variable, "timeForFocus")) timeForFocus = intVal;
  else if (!strcmp(variable, "pinShutter")) pinShutter = intVal;
  else if (!strcmp(variable, "pinFocus")) pinFocus = intVal;
  else if (!strcmp(variable, "extCam")) extCam = (bool)intVal;
#endif

#if INCLUDE_EXTHB
  // External Heartbeat
  else if (!strcmp(variable, "external_heartbeat_active")) external_heartbeat_active = (bool)intVal;
  else if (!strcmp(variable, "external_heartbeat_domain")) snprintf(external_heartbeat_domain, MAX_HOST_LEN, "%s", value);
  else if (!strcmp(variable, "external_heartbeat_uri")) snprintf(external_heartbeat_uri, FILE_NAME_LEN, "%s", value);
  else if (!strcmp(variable, "external_heartbeat_port")) external_heartbeat_port = intVal;
  else if (!strcmp(variable, "external_heartbeat_token")) snprintf(external_heartbeat_token, MAX_HOST_LEN, "%s", value);
#endif

  else if (!strcmp(variable, "useUart")) useUart = (bool)intVal;
#if INCLUDE_UART
  else if (!strcmp(variable, "uartTxdPin")) uartTxdPin = intVal;
  else if (!strcmp(variable, "uartRxdPin")) uartRxdPin = intVal;
#endif

#ifndef AUXILIARY
  // camera settings
  else if (!strcmp(variable, "xclkMhz")) xclkMhz = intVal;
  else if (!strcmp(variable, "framesize")) {
    fsizePtr = intVal;
    if (s) {
      if (s->set_framesize(s, (framesize_t)fsizePtr) != ESP_OK) res = false;
      // update default FPS for this frame size
      if (playbackHandle != NULL) {
        setFPSlookup(fsizePtr);
        updateConfigVect("fps", String(FPS).c_str()); 
      }
      if (fsizePtr > 16 && useMotion) {
        useMotion = false;
        updateConfigVect("enableMotion", "0");
        LOG_WRN("Motion detection disabled as frame size %s is too large", frameData[fsizePtr].frameSizeStr);
      }
    }
  }
  else if (!strcmp(variable, "fps")) {
    FPS = intVal;
    if (playbackHandle != NULL) setFPS(FPS);
  }
  else if (s) {
    if (!strcmp(variable, "quality")) res = s->set_quality(s, intVal);
    else if (!strcmp(variable, "contrast")) res = s->set_contrast(s, intVal);
    else if (!strcmp(variable, "brightness")) res = s->set_brightness(s, intVal);
    else if (!strcmp(variable, "saturation")) res = s->set_saturation(s, intVal);
    else if (!strcmp(variable, "denoise")) res = s->set_denoise(s, intVal);    
    else if (!strcmp(variable, "sharpness")) res = s->set_sharpness(s, intVal);    
    else if (!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)intVal);
    else if (!strcmp(variable, "colorbar")) res = s->set_colorbar(s, intVal);
    else if (!strcmp(variable, "awb")) res = s->set_whitebal(s, intVal);
    else if (!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, intVal);
    else if (!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, intVal);
    else if (!strcmp(variable, "hmirror")) res = s->set_hmirror(s, intVal);
    else if (!strcmp(variable, "vflip")) res = s->set_vflip(s, intVal);
    else if (!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, intVal);
    else if (!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, intVal);
    else if (!strcmp(variable, "aec_value")) res = s->set_aec_value(s, intVal);
    else if (!strcmp(variable, "aec2")) res = s->set_aec2(s, intVal);
    else if (!strcmp(variable, "dcw")) res = s->set_dcw(s, intVal);
    else if (!strcmp(variable, "bpc")) res = s->set_bpc(s, intVal);
    else if (!strcmp(variable, "wpc")) res = s->set_wpc(s, intVal);
    else if (!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, intVal);
    else if (!strcmp(variable, "lenc")) res = s->set_lenc(s, intVal);
    else if (!strcmp(variable, "special_effect")) res = s->set_special_effect(s, intVal);
    else if (!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, intVal);
    else if (!strcmp(variable, "ae_level")) res = s->set_ae_level(s, intVal);
    else res = ESP_FAIL;
  }
#endif
  return res == ESP_OK ? true : false;
}

static bool extractKeyVal(const char* wsMsg) {
  // extract key 
  strncpy(variable, wsMsg, FILE_NAME_LEN - 1); 
  char* endPtr = strchr(variable, '=');
  if (endPtr != NULL) {
    *endPtr = 0; // split variable into 2 strings, first is key name
    strcpy(value, variable + strlen(variable) + 1); // value is now second part of string
    return true;
  } else LOG_ERR("Invalid query string: %s", wsMsg);
  return false;
} 

esp_err_t appSpecificWebHandler(httpd_req_t *req, const char* variable, const char* value) {
  // update handling requiring response specific to mjpeg2sd
  if (!strcmp(variable, "sfile")) {
    // get folders / files on SD, save received filename if has required extension
    strcpy(inFileName, value);
    if (!forceRecord) doPlayback = listDir(inFileName, jsonBuff, JSON_BUFF_LEN, AVI_EXT); // browser control
    else strcpy(jsonBuff, "{}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonBuff);
  } 
  else if (!strcmp(variable, "updateFPS")) {
    // requires response with updated default fps
    sprintf(jsonBuff, "{\"fps\":\"%u\"}", setFPSlookup(fsizePtr));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, jsonBuff);
  } 
  else if (!strcmp(variable, "still")) {
    // send single jpeg to browser
    uint32_t startTime = millis();
    doKeepFrame = true;
    while (doKeepFrame && millis() - startTime < MAX_FRAME_WAIT) delay(100);
    if (!doKeepFrame && alertBufferSize) {
      httpd_resp_set_type(req, "image/jpeg");
      httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
      httpd_resp_send(req, (const char*)alertBuffer, alertBufferSize);   
      uint32_t jpegTime = millis() - startTime;
      LOG_INF("JPEG: %uB in %ums", alertBufferSize, jpegTime);
      alertBufferSize = 0;
    } else LOG_WRN("Failed to get still");
  } else if (!strcmp(variable, "svg")) {
    // build svg image for use by another app's hub instead of image
    const char* svgHtml = R"~(
        <svg width="200" height="200" xmlns="http://www.w3.org/2000/svg">
          <rect width="100%" height="100%" fill="lightgray"/>
          <text x="50%" y="50%" text-anchor="middle" alignment-baseline="middle" font-size="30">
    )~";
    
    httpd_resp_set_type(req, "image/svg+xml");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.svg");
    httpd_resp_sendstr_chunk(req, svgHtml);
    httpd_resp_sendstr_chunk(req, "MJPE2SD");
    httpd_resp_sendstr_chunk(req, "°C</text></svg>");
    httpd_resp_sendstr_chunk(req, NULL);
  } else return ESP_FAIL;
  return ESP_OK;
}

static bool setPeripheral(char cmd, int controlVal, bool fromUart) {
  bool res = true;
  switch (cmd) {
#if INCLUDE_MCPWM
    case 'M': 
      // motor speed
      if (trackSteer) trackSteeering(controlVal, false);
      else motorSpeed(controlVal); 
    break;
    case 'D':
      // steering
      if (trackSteer) trackSteeering(controlVal, true);
      else setSteering(controlVal);
    break;
#endif
#if INCLUDE_PERIPH
    case 'L':
      // lights
      setLightsRC((bool)controlVal);
    break;
    case 'P':
      // camera pan servo
      setCamPan(controlVal);
    break;
    case 'T':
      // camera tilt servo
      setCamTilt(controlVal);
    break;
#endif
#if INCLUDE_PGRAM
    case 'G':
      // photogrammetry control
      takePhotos(bool(controlVal));
    break;
#endif
    case 'K': 
      // cam browser conn closed
#ifdef AUXILIARY
      if (fromUart) 
#endif
        stopRC();
    break;
    default:
      res = false;
    break;
  }
  return res;
}

void appSpecificWsHandler(const char* wsMsg) {
  // message from web socket
  int wsLen = strlen(wsMsg) - 1;
  char cmd = (char)wsMsg[0];
  int controlVal = atoi(wsMsg + 1); // skip first char
  bool aux = false;
#ifdef AUXILIARY
  aux = true;
#endif
  if (useUart && !aux) {
#if INCLUDE_UART 
    // send command over uart to auxiliary
    if (!writeUart(cmd, (uint32_t)controlVal)) LOG_WRN("Failed to send data to Auxiliary over UART");
#endif
  } else {
    if (!setPeripheral(cmd, controlVal, false)) {
      switch (cmd) {
        case 'X':
#if INCLUDE_AUDIO
          // stop remote mic stream
          stopAudio = true;
#endif
        break;
        case 'C': 
          // control request
          if (extractKeyVal(wsMsg + 1)) updateStatus(variable, value);
        break;
        case 'S': 
          // status request
          buildJsonString(wsLen); // required config number 
          logPrint("%s\n", jsonBuff);
        break;   
        case 'U': 
          // update or control request
          memcpy(jsonBuff, wsMsg + 1, wsLen); // remove 'U'
          parseJson(wsLen);
        break;
        case 'H': 
          // browser keepalive heartbeat
          heartBeatDone = true;
        break;
        case 'K': 
          // kill websocket connection
          killSocket();
        break;
        default:
          LOG_WRN("unknown command %s", wsMsg);
        break;
      }
    }
  }
}

void appSpecificWsBinHandler(uint8_t* wsMsg, size_t wsMsgLen) {
#if INCLUDE_AUDIO
  browserMicInput(wsMsg, wsMsgLen);
#endif
}

void buildAppJsonString(bool filter) {
  // build app specific part of json string
  char* p = jsonBuff + 1;
  p += sprintf(p, "\"llevel\":%u,", lightLevel);
  p += sprintf(p, "\"night\":%s,", nightTime ? "\"Yes\"" : "\"No\"");
  float aTemp = readTemperature(true);
  if (aTemp > -127.0) p += sprintf(p, "\"atemp\":\"%0.1f\",", aTemp);
  else p += sprintf(p, "\"atemp\":\"n/a\",");
  float currentVoltage = readVoltage();
  if (currentVoltage < 0) p += sprintf(p, "\"battv\":\"n/a\",");
  else p += sprintf(p, "\"battv\":\"%0.1fV\",", currentVoltage); 
  if (forcePlayback && !doPlayback) {
    // switch off playback 
    forcePlayback = false;
    p += sprintf(p, "\"forcePlayback\":0,");  
  }
  p += sprintf(p, "\"showRecord\":%u,", (uint8_t)((isCapturing && doRecording) || forceRecord));
  p += sprintf(p, "\"camModel\":\"%s\",", camModel);
#if INCLUDE_PERIPH
  p += sprintf(p, "\"SVactive\":\"%d\",", SVactive); 
 #if INCLUDE_AUDIO
  p += sprintf(p, "\"AudActive\":\"%d\",", AudActive); 
 #endif
 #if (INCLUDE_PGRAM)
  p += sprintf(p, "\"PGactive\":\"%d\",", PGactive); 
 #endif
#endif
#if INCLUDE_MCPWM
  p += sprintf(p, "\"maxSteerAngle\":\"%d\",", maxSteerAngle); 
  p += sprintf(p, "\"maxDutyCycle\":\"%d\",", maxDutyCycle);
  p += sprintf(p, "\"minDutyCycle\":\"%d\",", minDutyCycle);  
  p += sprintf(p, "\"allowReverse\":\"%d\",", allowReverse);   
  p += sprintf(p, "\"autoControl\":\"%d\",", autoControl);
  p += sprintf(p, "\"waitTime\":\"%d\",", waitTime); 
  p += sprintf(p, "\"RCactive\":\"%d\",", RCactive); 
  p += sprintf(p, "\"maxSteerAngle\":\"%d\",", maxSteerAngle); 
  p += sprintf(p, "\"maxDutyCycle\":\"%d\",", maxDutyCycle);
  p += sprintf(p, "\"minDutyCycle\":\"%d\",", minDutyCycle);  
  p += sprintf(p, "\"allowReverse\":\"%d\",", allowReverse);   
  p += sprintf(p, "\"autoControl\":\"%d\",", autoControl);
  p += sprintf(p, "\"waitTime\":\"%d\",", waitTime); 
  p += sprintf(p, "\"heartbeatRC\":\"%d\",", heartbeatRC); 
#endif
  p += sprintf(p, "\"sustainId\":\"%u\",", sustainId);     
  // Extend info
  uint8_t cardType = 99; // not MMC
  if ((fs::SDMMCFS*)&STORAGE == &SD_MMC) cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) p += sprintf(p, "\"card\":\"%s\",", "NO card");
  else {
    if (!filter) {
      if (cardType == CARD_MMC) p += sprintf(p, "\"card\":\"%s\",", "MMC"); 
      else if (cardType == CARD_SD) p += sprintf(p, "\"card\":\"%s\",", "SDSC");
      else if (cardType == CARD_SDHC) p += sprintf(p, "\"card\":\"%s\",", "SDHC"); 
      else if (cardType == 99) p += sprintf(p, "\"card\":\"%s\",", "LittlrFS"); 
    }
    if ((fs::SDMMCFS*)&STORAGE == &SD_MMC) p += sprintf(p, "\"card_size\":\"%s\",", fmtSize(SD_MMC.cardSize()));
    p += sprintf(p, "\"used_bytes\":\"%s\",", fmtSize(STORAGE.usedBytes()));
    p += sprintf(p, "\"free_bytes\":\"%s\",", fmtSize(STORAGE.totalBytes() - STORAGE.usedBytes()));
    p += sprintf(p, "\"total_bytes\":\"%s\",", fmtSize(STORAGE.totalBytes()));
  }
  p += sprintf(p, "\"free_psram\":\"%s\",", fmtSize(ESP.getFreePsram()));     
#if INCLUDE_FTP_HFS
  p += sprintf(p, "\"progressBar\":%d,", percentLoaded);  
  if (percentLoaded == 100) percentLoaded = 0;
#endif
  //p += sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
  *p = 0;
}

/******************************************************************/

void externalAlert(const char* subject, const char* message) {
  // alert any configured external servers
#if INCLUDE_TGRAM
  if (tgramUse) tgramAlert(subject, message);
#endif
#if INCLUDE_SMTP
  if (smtpUse) emailAlert(subject, message);
#endif
}

void displayAudioLed(int16_t audioSample) {
}

void setupAudioLed() {
}

int8_t checkPotVol(int8_t adjVol) {
  return adjVol; // dummy
}

void applyFilters() {
  applyVolume();
}

#if !INCLUDE_PERIPH
float readVoltage() {
  return -1.0;
}
float readTemperature(bool isCelsius, bool onlyDS18) {
  return readInternalTemp();
}
#endif

void setInputPeripheral(uint8_t cmd, uint32_t controlVal) {
  // set data on client for data received from auxiliary input peripheral
  // not used
  //if ((char)cmd == 'I') memcpy(&pirVal, &controlVal, sizeof(pirVal));  // set PIR status
}

int getInputPeripheral(uint8_t cmd) {
  // auxiliary get data from input peripheral, for return to client
  // not used
  uint32_t inputVal = -1;
  if ((char)cmd == 'I') {
     // get PIR status
    bool pirVal = getPIRval();
    memcpy(&inputVal, &pirVal, sizeof(pirVal)); 
  }
  return inputVal;
}

bool setOutputPeripheral(uint8_t cmd, uint32_t rxValue) {
  // auxiliary sends data to output peripheral
  int controlValue;
  memcpy(&controlValue, &rxValue, sizeof(controlValue));
  return setPeripheral((char)cmd, controlValue, true);
}

bool appDataFiles() {
  // callback from setupAssist.cpp, for any app specific files 
  return true;
}

void currentStackUsage() {
  checkStackUse(captureHandle, 0);
#if INCLUDE_DS18B20
  checkStackUse(DS18B20handle, 1);
#endif
#if INCLUDE_SMTP
  checkStackUse(emailHandle, 2);
#endif
  checkStackUse(fsHandle, 3);
  checkStackUse(logHandle, 4);
#if INCLUDE_AUDIO
  checkStackUse(audioHandle, 5);
#endif
#if INCLUDE_MQTT
  checkStackUse(mqttTaskHandle, 6);
#endif
  // 7: pingtask
  checkStackUse(playbackHandle, 8);
  checkStackUse(servoHandle, 9);
  checkStackUse(stickHandle, 10);
#if INCLUDE_TGRAM
  checkStackUse(telegramHandle, 11);
#endif
#if INCLUDE_TELEM
  checkStackUse(telemetryHandle, 12);
#endif
#if INCLUDE_UART
  checkStackUse(uartRxHandle, 13);
#endif
  // 14: http webserver
  for (int i=0; i < numStreams; i++) checkStackUse(sustainHandle[i], 15 + i);
}

static void stopRC() {
  // stop RC movement if connection lost
#if INCLUDE_PERIPH
  setLightsRC(false);
#endif
#if INCLUDE_MCPWM
  if (motorFwdPin > 0) motorSpeed(0, true);
  if (motorFwdPinR > 0) motorSpeed(0, false); 
#endif
}

#if INCLUDE_PERIPH
static void heartBeatTask (void *pvParameter) {
  // check on aux that ws and / or uart connection available
  while (true) {
    delay((heartbeatRC + 1) * 1000); // 1 sec more than browser heartbeat rate
    if (!heartBeatDone) stopRC(); // stop RC as no heartbeat received
    heartBeatDone = false;
  }
}
 
void startHeartbeat() {
  // start heartbeat to check websocket and / or uart connectivity for RC control
  if (RCactive || useUart) {
    if (heartBeatHandle == NULL) xTaskCreate(&heartBeatTask, "heartBeatTask", HB_STACK_SIZE, NULL, HB_PRI, &heartBeatHandle);
  }
}
#endif 

void doAppPing() {
  if (DEBUG_MEM) {
    currentStackUsage();
    checkMemory();
  }
  if (checkAlarm()) {
    remoteServerReset();
    getExtIP();
#if INCLUDE_SMTP
    if (smtpUse) {
      emailCount = 0;
      LOG_INF("Reset daily email allowance");
    }
#endif
    LOG_INF("Daily rollover");
  }
#if INCLUDE_EXTHB
  if (external_heartbeat_active) sendExternalHeartbeat();
#endif
#if INCLUDE_PERIPH
    static bool atNight = false;
#endif
  // check for night time actions
  if (isNight(nightSwitch)) {
    if (wakeUse && wakePin) {
      // to use LDR on wake pin, connect it between pin and 3V3
      // uses internal pulldown resistor as voltage divider
      // but may need to add external pull down between pin
      // and GND to alter required light level for wakeup
#ifndef AUXILIARY
      digitalWrite(PWDN_GPIO_NUM, 1); // power down camera
#endif
      goToSleep(wakePin, true);
    }
#if INCLUDE_PERIPH
    if (relayPin && relayMode && !atNight) {
      // turn on relay at night
      digitalWrite(relayPin, HIGH);
      atNight = true; 
    }
  } else if (relayPin && relayMode && atNight) {
    // turn off relay if day
    digitalWrite(relayPin, LOW); 
    atNight = false; 
#endif
  }
}

/************** telegram app specific **************/

void tgramAlert(const char* subject, const char* message) {
  // send motion alert to Telegram
  const char* pos1 = strchr(subject + 1, '/'); // extract filename
  const char* pos2 = strrchr(subject + 1, '.'); // remove extension
  // make filename into command
  if (pos1 != NULL && pos2 != NULL) {
    strncpy(alertCaption, pos1, pos2 - pos1);
    alertCaption[pos2 - pos1] = 0;
    strcat(alertCaption, " from ");
    strncat(alertCaption, hostName, sizeof(alertCaption) - strlen(alertCaption) - 1);
    if (alertBufferSize) alertReady = true; // return image
  } else LOG_WRN("Unable to send motion alert");
}

static bool downloadAvi(const char* userCmd) {
  char* pos = strchr(userCmd, '_'); // if contains '_', assume filename
  if (pos != NULL) {
    // add folder name and avi extension to incoming file name
    char fileName[FILE_NAME_LEN];
    strncpy(fileName, userCmd, FILE_NAME_LEN - 1);
    pos = strchr(fileName, '_');
    memmove(pos, fileName, sizeof(fileName) - (pos - fileName));
    strncat(fileName, ".avi", sizeof(fileName - 1) - strlen(fileName)); 
    if (STORAGE.exists(fileName)) sendTgramFile(fileName, "video/x-msvideo", "");
    else sendTgramMessage("AVI file not found: ", fileName, "");
  }
  return (bool)pos;
}

static void saveRamLog() {
  // save ramlog to storage for upload to telegram
  File ramFile = STORAGE.open(DATA_DIR "/ramlog" TEXT_EXT, FILE_WRITE);
  int startPtr, endPtr;
  startPtr = endPtr = mlogEnd;  
  // write log in chunks
  do {
    int maxChunk = startPtr < endPtr ? endPtr - startPtr : RAM_LOG_LEN - startPtr;
    size_t chunkSize = std::min(CHUNKSIZE, maxChunk);    
    if (chunkSize > 0) ramFile.write((uint8_t*)messageLog + startPtr, chunkSize);
    startPtr += chunkSize;
    if (startPtr >= RAM_LOG_LEN) startPtr = 0;
  } while (startPtr != endPtr);
  ramFile.close();
}

void appSpecificTelegramTask(void* p) {
#if INCLUDE_TGRAM
  // process Telegram interactions
  snprintf(tgramHdr, FILE_NAME_LEN - 1, "%s\n Ver: " APP_VER "\n\n/snap\n\n/log", hostName); 
  sendTgramMessage("Rebooted", "", "");
  char userCmd[FILE_NAME_LEN];
  
  while (true) {
    // service requests from Telegram
    if (getTgramUpdate(userCmd)) {     
      if (!strcmp(userCmd, "/snap")) {
        doKeepFrame = true;
        delay(1000); // time to get frame
        sprintf(userCmd, "/snap from %s", hostName);
        sendTgramPhoto(alertBuffer, alertBufferSize, userCmd);
      } else if (!strcmp(userCmd, "/log")) {
        saveRamLog();
        sprintf(userCmd, "/log from %s", hostName);
        sendTgramFile(DATA_DIR "/ramlog" TEXT_EXT, "text/plain", userCmd);
        deleteFolderOrFile(DATA_DIR "/ramlog" TEXT_EXT);
      } else {
        // initially assume it is an avi file download request
        if (!downloadAvi(userCmd)) sendTgramMessage("Request not recognised: ", userCmd, "");
      }
    } else {
      // send out any outgoing alerts from app
      if (alertReady) {
        alertReady = false;
        sendTgramPhoto(alertBuffer, alertBufferSize, alertCaption);
        alertBufferSize = 0;
      } else delay(5000); // avoid thrashing
    }
  }
#endif
  vTaskDelete(NULL);
}

/************** default app configuration **************/
const char* appConfig = R"~(
ST_SSID~~99~~na
fsPort~21~99~~na
fsServer~~99~~na
ftpUser~~99~~na
fsWd~~99~~na
fsUse~~99~~na
smtp_port~465~99~~na
smtp_server~smtp.gmail.com~99~~na
smtp_login~~99~~na
smtp_email~~99~~na
Auth_Name~~99~~na
useHttps~~99~~na
useSecure~~99~~na
useFtps~~99~~na
extIP~~99~~na
restart~~99~~na
sdLog~0~99~~na
xclkMhz~20~98~~na
ae_level~-2~98~~na
aec~1~98~~na
aec2~1~98~~na
aec_value~204~98~~na
agc~1~98~~na
agc_gain~0~98~~na
autoUpload~0~98~~na
deleteAfter~0~98~~na
awb~1~98~~na
awb_gain~1~98~~na
bpc~1~98~~na
brightness~0~98~~na
colorbar~0~98~~na
contrast~0~98~~na
dcw~1~98~~na
enableMotion~1~98~~na
fps~20~98~~na
framesize~9~98~~na
gainceiling~0~98~~na
hmirror~0~98~~na
lampLevel~0~98~~na
lenc~1~98~~na
lswitch~10~98~~na
micGain~0~98~~na
ampVol~0~98~~na
minf~5~98~~na
motionVal~8~98~~na
quality~12~98~~na
raw_gma~1~98~~na
record~1~98~~na
saturation~0~98~~na
sharpness~0~98~~na
denoise~4~98~~na
special_effect~0~98~~na
timeLapseOn~0~98~~na
timezone~GMT0~98~~na
vflip~0~98~~na
wb_mode~0~98~~na
wpc~1~98~~na
ST_ip~~0~T~Static IP address
ST_gw~~0~T~Router IP address
ST_sn~255.255.255.0~0~T~Router subnet
ST_ns1~~0~T~DNS server
ST_ns2~~0~T~Alt DNS server
AP_Pass~~0~T~AP Password
AP_ip~~0~T~AP IP Address if not 192.168.4.1
AP_sn~~0~T~AP subnet
AP_gw~~0~T~AP gateway
allowAP~1~0~C~Allow simultaneous AP 
doGetExtIP~1~0~C~Enable get external IP
wifiTimeoutSecs~30~0~N~WiFi connect timeout (secs)
logType~0~99~N~Output log selection
ntpServer~pool.ntp.org~0~T~NTP Server address
alarmHour~1~2~N~Hour of day for daily actions
refreshVal~5~2~N~Web page refresh rate (secs)
responseTimeoutSecs~10~2~N~Server response timeout (secs)
useUart~0~3~C~Use UART for Auxiliary connection
uartTxdPin~~3~N~UART TX pin
uartRxdPin~~3~N~UART RX pin
tlSecsBetweenFrames~600~1~N~Timelapse interval (secs)
tlDurationMins~720~1~N~Timelapse duration (mins)
tlPlaybackFPS~1~1~N~Timelapse playback FPS
moveStartChecks~5~1~N~Checks per second for start motion
moveStopSecs~2~1~N~Non movement to stop recording (secs)
maxFrames~20000~1~N~Max frames in recording
detectMotionFrames~5~1~N~Num changed frames to start motion
detectNightFrames~10~1~N~Min dark frames to indicate night
detectNumBands~10~1~N~Total num of detection bands
detectStartBand~3~1~N~Top band where motion is checked
detectEndBand~8~1~N~Bottom band where motion is checked
detectChangeThreshold~15~1~N~Pixel difference to indicate change
mlUse~0~1~C~Use Machine Learning
mlProbability~0.8~1~N~ML minimum positive probability 0.0 - 1.0
depthColor~0~1~C~Color depth for motion detection: Gray <> RGB
streamNvr~0~1~C~Enable NVR Video stream: /sustain?video=1
streamSnd~0~1~C~Enable NVR Audio stream: /sustain?audio=1
streamSrt~0~1~C~Enable NVR Subtitle stream: /sustain?srt=1
smtpUse~0~2~C~Enable email sending
smtpMaxEmails~10~2~N~Max daily alerts
sdMinCardFreeSpace~100~2~N~Min free MBytes on SD before action
sdFreeSpaceMode~1~2~S:No Check:Delete oldest:Ftp then delete~Action mode on SD min free
formatIfMountFailed~0~2~C~Format file system on failure
pirUse~0~3~C~Use PIR for detection
lampType~0~3~S:Manual:PIR~How lamp activated
SVactive~0~3~C~Enable servo use
pirPin~~3~N~Pin used for PIR
lampPin~~3~N~Pin used for Lamp
servoPanPin~~6~N~Pin used for Pan Servo
servoTiltPin~~6~N~Pin used for Tilt Servo
ds18b20Pin~~3~N~Pin used for DS18B20 temperature sensor
AudActive~0~3~C~Show audio configuration
micSckPin~-1~7~N~Microphone I2S SCK pin
micSWsPin~-1~7~N~Microphone I2S WS, PDM CLK pin
micSdPin~-1~7~N~Microphone I2S SD, PDM DAT pin
mampBckIo~-1~7~N~Amplifier I2S BCLK (SCK) pin
mampSwsIo~-1~7~N~Amplifier I2S LRCLK (WS) pin
mampSdIo~-1~7~N~Amplifier I2S DIN pin
servoDelay~0~6~N~Delay between each 1 degree change (ms)
servoMinAngle~0~6~N~Set min angle for servo model
servoMaxAngle~180~6~N~Set max angle for servo model
servoMinPulseWidth~544~6~N~Set min pulse width for servo model (usecs)
servoMaxPulseWidth~2400~6~N~Set max pulse width for servo model (usecs)
servoCenter~90~6~N~Angle at which servo centered
voltDivider~2~3~N~Voltage divider resistor ratio
voltLow~3~3~N~Warning level for low voltage
voltInterval~5~3~N~Voltage check interval (mins)
voltPin~~3~N~ADC Pin used for battery voltage
voltUse~0~3~C~Use Voltage check
wakePin~~3~N~Pin used for to wake app from sleep
wakeUse~0~3~C~Deep sleep app during night
mqtt_active~0~2~C~Mqtt enabled
mqtt_broker~~2~T~Mqtt server ip to connect
mqtt_port~1883~2~N~Mqtt server port
mqtt_user~~2~T~Mqtt user name
mqtt_user_Pass~~2~T~Mqtt user password
mqtt_topic_prefix~homeassistant/~2~T~Mqtt topic path prefix
external_heartbeat_active~0~2~C~External Heartbeat Server enabled
external_heartbeat_domain~~2~T~Heartbeat receiver domain or IP (eg. www.espsee.com)
external_heartbeat_uri~~2~T~Heartbeat receiver URI (eg. /heartbeat/)
external_heartbeat_port~443~2~N~Heartbeat receiver port
external_heartbeat_token~~2~T~Heartbeat receiver auth token
usePing~1~0~C~Use ping
teleUse~0~3~C~Use telemetry recording
teleInterval~1~3~N~Telemetry collection interval (secs)
RCactive~0~3~C~Enable remote control
servoSteerPin~~4~N~Pin used for steering servo
motorRevPin~~4~N~Pin used for motor reverse / left track 
motorFwdPin~~4~N~Pin used for motor forward / left track 
motorRevPinR~~4~N~Pin used for right track reverse
motorFwdPinR~~4~N~Pin used for right track forward
lightsRCpin~~4~N~Pin used for RC lights output
heartbeatRC~5~4~N~RC connection heartbeat time (secs)
AuxIP~~3~T~Send RC / Servo / PG commands to auxiliary IP
stickXpin~~4~N~Pin used for joystick steering
stickYpin~~4~N~Pin used for joystick motor
stickzPushPin~~4~N~Pin used for joystick lights
stickUse~0~4~C~Use joystick
pwmFreq~50~4~N~RC Motor PWM frequency
maxSteerAngle~45~4~N~Max steering angle from straightahead
maxTurnSpeed~50~4~N~Max tracked turn speed differential 
maxDutyCycle~100~4~N~Max motor duty cycle % (speed)
minDutyCycle~10~4~N~Min motor duty cycle % (stop)
allowReverse~1~4~C~Reverse motion required
autoControl~1~4~C~Stop motor or center steering if control inactive
waitTime~20~4~N~Min wait (ms) between RC updates to app
tgramUse~0~2~C~Use Telegram Bot
tgramToken~~2~T~Telegram Bot token
tgramChatId~~2~T~Telegram chat identifier
devHub~0~2~C~Show Camera Hub tab
buzzerUse~0~3~C~Use active buzzer
buzzerPin~~3~N~Pin used for active buzzer
buzzerDuration~~3~N~Duration of buzzer sound in secs
stepIN1pin~-1~5~N~Stepper IN1 pin number
stepIN2pin~-1~5~N~Stepper IN2 pin number
stepIN3pin~-1~5~N~Stepper IN3 pin number
stepIN4pin~-1~5~N~Stepper IN4 pin number
PGactive~0~3~C~Enable photogrammetry
numberOfPhotos~20~5~N~Number of photos
RPM~1~5~N~Turntable revolution speed as RPM
gearing~5.7~5~N~Turntable / motor gearing ratio
clockwise~1~5~C~Clockwise turntable if true
timeForFocus~0~5~N~Time allocated to auto focus (secs)
timeForPhoto~2~5~N~Time allocated to take photo (secs)
pinShutter~-1~5~N~Pin connected to camera shutter
pinFocus~-1~5~N~Pin connected to camera focus
extCam~0~5~C~Use external camera
AtakePhotos~Start~5~A~Start photogrammetry
BabortPhotos~Abort~5~A~Abort photogrammetry
relayPin~-1~3~N~Pin to switch relay 
relayMode~0~3~S:Manual:Night~How relay activated
relaySwitch~0~3~C~Switch relay off / on
I2Csda~-1~3~N~I2C SDA pin if unshared
I2Cscl~-1~3~N~I2C SCL pin if unshared
)~";
