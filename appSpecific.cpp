// mjpeg2sd app specific functions
//
// Direct access URLs:
// - Network streaming: app_ip/sustain?stream=1 
// - Stills: app_ip/control?still=1
//
// s60sc 2022, 2023

#include "appGlobals.h"

static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN];
static char alertCaption[100];
static bool alertReady = false;
static bool depthColor = true;

/************************ webServer callbacks *************************/

bool updateAppStatus(const char* variable, const char* value) {
  // update vars from browser input
  esp_err_t res = ESP_OK; 
  sensor_t* s = esp_camera_sensor_get();
  int intVal = atoi(value);
  float fltVal = atof(value);
  if (!strcmp(variable, "custom")) return res;
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
    LOG_INF("%s motion detection", useMotion ? "Enabling" : "Disabling");
  }
  else if (!strcmp(variable, "timeLapseOn")) timeLapseOn = intVal;
  else if (!strcmp(variable, "tlSecsBetweenFrames")) tlSecsBetweenFrames = intVal;
  else if (!strcmp(variable, "tlDurationMins")) tlDurationMins = intVal;
  else if (!strcmp(variable, "tlPlaybackFPS")) tlPlaybackFPS = intVal;  
  else if (!strcmp(variable, "nvrStream")) nvrStream = (bool)intVal; 
  else if (!strcmp(variable, "lswitch")) nightSwitch = intVal;
  else if (!strcmp(variable, "micGain")) micGain = intVal;
  else if (!strcmp(variable, "upload")) fsFileOrFolder(value); 
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
  
  // peripherals
  else if (!strcmp(variable, "useIOextender")) useIOextender = (bool)intVal;
  else if (!strcmp(variable, "uartTxdPin")) uartTxdPin = intVal;
  else if (!strcmp(variable, "uartRxdPin")) uartRxdPin = intVal;
  else if (!strcmp(variable, "pirUse")) pirUse = (bool)intVal;
  else if (!strcmp(variable, "lampLevel")) {
    lampLevel = intVal;
    if (!lampType) setLamp(lampLevel); // manual
  }
  else if (!strcmp(variable, "lampUse")) {
    lampUse = (bool)intVal;
    if (!lampType) setLamp(lampLevel); // manual
  }
  else if (!strcmp(variable, "lampType")) {
    lampType = intVal;
    lampAuto = lampNight = false;
    if (lampType == 1) lampAuto = true;
    if (!lampType) setLamp(lampLevel); // manual
    else setLamp(0); 
  }
  else if (!strcmp(variable, "servoUse")) servoUse = (bool)intVal;
  else if (!strcmp(variable, "voltUse")) voltUse = (bool)intVal;
  else if (!strcmp(variable, "micUse")) micUse = (bool)intVal;
  else if (!strcmp(variable, "pirPin")) pirPin = intVal;
  else if (!strcmp(variable, "lampPin")) lampPin = intVal;
  else if (!strcmp(variable, "servoPanPin")) servoPanPin = intVal;
  else if (!strcmp(variable, "servoTiltPin")) servoTiltPin = intVal;
  else if (!strcmp(variable, "ds18b20Pin")) ds18b20Pin = intVal;
  else if (!strcmp(variable, "voltPin")) voltPin = intVal;
  else if (!strcmp(variable, "micSckPin")) micSckPin = intVal;
  else if (!strcmp(variable, "micSWsPin")) micSWsPin = intVal;
  else if (!strcmp(variable, "micSdPin")) micSdPin = intVal;
  else if (!strcmp(variable, "servoDelay")) servoDelay = intVal;
  else if (!strcmp(variable, "servoMinAngle")) servoMinAngle = intVal;
  else if (!strcmp(variable, "servoMaxAngle")) servoMaxAngle = intVal;
  else if (!strcmp(variable, "servoMinPulseWidth")) servoMinPulseWidth = intVal;
  else if (!strcmp(variable, "servoMaxPulseWidth")) servoMaxPulseWidth = intVal;
  else if (!strcmp(variable, "servoCenter")) servoCenter = intVal;
  else if (!strcmp(variable, "voltDivider")) voltDivider = intVal;
  else if (!strcmp(variable, "voltLow")) voltLow = fltVal;
  else if (!strcmp(variable, "voltInterval")) voltInterval = intVal;
  else if (!strcmp(variable, "camPan")) setCamPan(intVal);
  else if (!strcmp(variable, "camTilt")) setCamTilt(intVal);
  else if (!strcmp(variable, "wakeUse")) wakeUse = (bool)intVal;
  else if (!strcmp(variable, "wakePin")) wakePin = intVal;
  else if (!strcmp(variable, "teleUse")) teleUse = (bool)intVal;
  else if (!strcmp(variable, "teleInterval")) teleInterval = intVal;
  else if (!strcmp(variable, "RCactive")) RCactive = (bool)intVal;
  else if (!strcmp(variable, "servoSteerPin")) servoSteerPin = intVal;
  else if (!strcmp(variable, "motorRevPin")) motorRevPin = intVal;
  else if (!strcmp(variable, "motorFwdPin")) motorFwdPin = intVal;
  else if (!strcmp(variable, "lightsRCpin")) lightsRCpin = intVal;
  else if (!strcmp(variable, "pwmFreq")) pwmFreq = intVal;
  else if (!strcmp(variable, "RClights")) setLights((bool)intVal);
  else if (!strcmp(variable, "maxSteerAngle")) maxSteerAngle = intVal;  
  else if (!strcmp(variable, "maxDutyCycle")) maxDutyCycle = intVal;  
  else if (!strcmp(variable, "minDutyCycle")) minDutyCycle = intVal;  
  else if (!strcmp(variable, "allowReverse")) allowReverse = (bool)intVal;   
  else if (!strcmp(variable, "autoControl")) autoControl = (bool)intVal; 
  else if (!strcmp(variable, "waitTime")) waitTime = intVal;    
  else if (!strcmp(variable, "stickUse")) stickUse = (bool)intVal; 
  else if (!strcmp(variable, "stickXpin")) stickXpin = intVal; 
  else if (!strcmp(variable, "stickYpin")) stickYpin = intVal; 
  else if (!strcmp(variable, "stickzPushPin")) stickzPushPin = intVal; 

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
    } else LOG_ERR("Failed to get still");
  }
  return ESP_OK;
}

void appSpecificWsHandler(const char* wsMsg) { 
  // message from web socket
  int wsLen = strlen(wsMsg) - 1;
  int controlVal = atoi(wsMsg + 1); // skip first char
  switch ((char)wsMsg[0]) {
    case 'M': 
      motorSpeed(controlVal);
    break;
    case 'D': 
      setSteering(controlVal);
    break;
    case 'C': 
      // control request
      if (extractKeyVal(wsMsg + 1)) updateStatus(variable, value);
    break;
    case 'H': 
      // keepalive heartbeat, return status
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
    case 'K': 
      // kill websocket connection
      killWebSocket();
    break;
    default:
      LOG_WRN("unknown command %c", (char)wsMsg[0]);
    break;
  }
}

esp_err_t appSpecificHeaderHandler(httpd_req_t *req) {
  // check if header field present, if so extract value 
  esp_err_t res = ESP_OK;
  // char value[IN_FILE_NAME_LEN];
  // res = extractHeaderVal()
  return res;
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
  p += sprintf(p, "\"RCactive\":\"%d\",", RCactive); 
  p += sprintf(p, "\"maxSteerAngle\":\"%d\",", maxSteerAngle); 
  p += sprintf(p, "\"maxDutyCycle\":\"%d\",",  maxDutyCycle);
  p += sprintf(p, "\"minDutyCycle\":\"%d\",", minDutyCycle);  
  p += sprintf(p, "\"allowReverse\":\"%d\",", allowReverse);   
  p += sprintf(p, "\"autoControl\":\"%d\",", autoControl);
  p += sprintf(p, "\"waitTime\":\"%d\",", waitTime); 
  p += sprintf(p, "\"sustainId\":\"%u\",", sustainId); 
    
  // Extend info
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) p += sprintf(p, "\"card\":\"%s\",", "NO card");
  else {
    if (!filter) {
      if (cardType == CARD_MMC) p += sprintf(p, "\"card\":\"%s\",", "MMC"); 
      else if (cardType == CARD_SD) p += sprintf(p, "\"card\":\"%s\",", "SDSC");
      else if (cardType == CARD_SDHC) p += sprintf(p, "\"card\":\"%s\",", "SDHC"); 
    }
    p += sprintf(p, "\"card_size\":\"%s\",", fmtSize(SD_MMC.cardSize()));
    p += sprintf(p, "\"used_bytes\":\"%s\",", fmtSize(SD_MMC.usedBytes()));
    p += sprintf(p, "\"free_bytes\":\"%s\",", fmtSize(SD_MMC.totalBytes() - SD_MMC.usedBytes()));
    p += sprintf(p, "\"total_bytes\":\"%s\",", fmtSize(SD_MMC.totalBytes()));
  }
  p += sprintf(p, "\"free_psram\":\"%s\",", fmtSize(ESP.getFreePsram()));     
  p += sprintf(p, "\"progressBar\":%d,", percentLoaded);  
  if (percentLoaded == 100) percentLoaded = 0;
  //p += sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
  *p = 0;
}

/******************************************************************/

void externalAlert(const char* subject, const char* message) {
  // alert any configured external servers
  if (tgramUse) tgramAlert(subject, message);
  if (smtpUse) emailAlert(subject, message);
}

bool appDataFiles() {
  // callback from setupAssist.cpp, for any app specific files 
  return true;
}

void currentStackUsage() {
  checkStackUse(captureHandle, 0);
  checkStackUse(DS18B20handle, 1);
  checkStackUse(emailHandle, 2);
  checkStackUse(fsHandle, 3);
  checkStackUse(logHandle, 4);
  checkStackUse(micHandle, 5);
  checkStackUse(mqttTaskHandle, 6);
  // 7: pingtask
  checkStackUse(playbackHandle, 8);
  checkStackUse(servoHandle, 9);
  checkStackUse(stickHandle, 10);
  checkStackUse(telegramHandle, 11);
  checkStackUse(telemetryHandle, 12);
  checkStackUse(uartClientHandle, 13);
  // 14: http webserver
  for (int i=0; i < numStreams; i++) checkStackUse(sustainHandle[i], 15 + i);
}

void doAppPing() {
  if (DEBUG_MEM) {
    currentStackUsage();
    checkMemory();
  }
  if (checkAlarm()) {
    getExtIP();
    if (smtpUse) {
      emailCount = 0;
      LOG_INF("Reset daily email allowance");
    }
    LOG_INF("Daily rollover");
  }
  doIOExtPing();
  // check for night time actions
  if (isNight(nightSwitch)) {
    if (wakeUse && wakePin) {
     // to use LDR on wake pin, connect it between pin and 3V3
     // uses internal pulldown resistor as voltage divider
     // but may need to add external pull down between pin
     // and GND to alter required light level for wakeup
     digitalWrite(PWDN_GPIO_NUM, 1); // power down camera
     goToSleep(wakePin, true);
    }
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
  } else LOG_ERR("Unable to send motion alert");
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

void appSpecificTelegramTask(void* p) {
  // process Telegram interactions
  snprintf(tgramHdr, FILE_NAME_LEN - 1, "%s\n Ver: " APP_VER "\n\n/snap", hostName); 
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
      } else delay(2000); // avoid thrashing
    }
  }
  vTaskDelete(NULL);
}
