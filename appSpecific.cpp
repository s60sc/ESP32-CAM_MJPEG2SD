// mjpeg2sd specific config functions
//
// s60sc 2022

#include "appGlobals.h"

bool updateAppStatus(const char* variable, const char* value) {
  // update vars from browser input
  esp_err_t res = ESP_OK; 
  sensor_t* s = esp_camera_sensor_get();
  int intVal = atoi(value);
  float fltVal = atof(value);
  if(!strcmp(variable, "minf")) minSeconds = intVal; 
  else if(!strcmp(variable, "stopStream")) stopPlaying();
  else if(!strcmp(variable, "motionVal")) motionVal = intVal;
  else if(!strcmp(variable, "moveStartChecks")) moveStartChecks = intVal;
  else if(!strcmp(variable, "moveStopSecs")) moveStopSecs = intVal;
  else if(!strcmp(variable, "maxFrames")) maxFrames = intVal;
  else if(!strcmp(variable, "detectMotionFrames")) detectMotionFrames = intVal;
  else if(!strcmp(variable, "detectNightFrames")) detectNightFrames = intVal;
  else if(!strcmp(variable, "detectNumBands")) detectNumBands = intVal;
  else if(!strcmp(variable, "detectStartBand")) detectStartBand = intVal;
  else if(!strcmp(variable, "detectEndBand")) detectEndBand = intVal;
  else if(!strcmp(variable, "detectChangeThreshold")) detectChangeThreshold = intVal;
  else if(!strcmp(variable, "enableMotion")){
    // Turn on/off motion detection 
    useMotion = (intVal) ? true : false; 
    LOG_INF("%s motion detection", useMotion ? "Enabling" : "Disabling");
  }
  else if(!strcmp(variable, "timeLapseOn")) timeLapseOn = intVal;
  else if(!strcmp(variable, "tlSecsBetweenFrames")) tlSecsBetweenFrames = intVal;
  else if(!strcmp(variable, "tlDurationMins")) tlDurationMins = intVal;
  else if(!strcmp(variable, "tlPlaybackFPS")) tlPlaybackFPS = intVal;  
  else if(!strcmp(variable, "lswitch")) nightSwitch = intVal;
  else if(!strcmp(variable, "micGain")) micGain = intVal;
  else if(!strcmp(variable, "autoUpload")) autoUpload = intVal;
  else if(!strcmp(variable, "upload")) ftpFileOrFolder(value);  
  else if(!strcmp(variable, "uploadMove")) ftpFileOrFolder(value, true);  
  else if(!strcmp(variable, "delete")) {
    stopPlayback = true;
    deleteFolderOrFile(value);
  }
  else if(!strcmp(variable, "record")) doRecording = (intVal) ? true : false;   
  else if(!strcmp(variable, "forceRecord")) forceRecord = (intVal) ? true : false;                                       
  else if(!strcmp(variable, "dbgMotion")) {
    // only enable show motion if motion detect enabled
    dbgMotion = (intVal && useMotion) ? true : false;
    doRecording = !dbgMotion;
  }
  
  // peripherals
  else if(!strcmp(variable, "useIOextender")) useIOextender = (bool)intVal;
  else if(!strcmp(variable, "uartTxdPin")) uartTxdPin = intVal;
  else if(!strcmp(variable, "uartRxdPin")) uartRxdPin = intVal;
  else if(!strcmp(variable, "pirUse")) pirUse = (bool)intVal;
  else if(!strcmp(variable, "lampLevel")) {
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
    if (lampType == 2) lampNight = true;
    if (!lampType) setLamp(lampLevel); // manual
    else setLamp(0); 
  }
  else if(!strcmp(variable, "servoUse")) servoUse = (bool)intVal;
  else if(!strcmp(variable, "voltUse")) voltUse = (bool)intVal;
  else if(!strcmp(variable, "micUse")) micUse = (bool)intVal;
  else if(!strcmp(variable, "pirPin")) pirPin = intVal;
  else if(!strcmp(variable, "lampPin")) lampPin = intVal;
  else if(!strcmp(variable, "servoPanPin")) servoPanPin = intVal;
  else if(!strcmp(variable, "servoTiltPin")) servoTiltPin = intVal;
  else if(!strcmp(variable, "ds18b20Pin")) ds18b20Pin = intVal;
  else if(!strcmp(variable, "voltPin")) voltPin = intVal;
  else if(!strcmp(variable, "micSckPin")) micSckPin = intVal;
  else if(!strcmp(variable, "micWsPin")) micWsPin = intVal;
  else if(!strcmp(variable, "micSdPin")) micSdPin = intVal;
  else if(!strcmp(variable, "servoDelay")) servoDelay = intVal;
  else if(!strcmp(variable, "servoMinAngle")) servoMinAngle = intVal;
  else if(!strcmp(variable, "servoMaxAngle")) servoMaxAngle = intVal;
  else if(!strcmp(variable, "servoMinPulseWidth")) servoMinPulseWidth = intVal;
  else if(!strcmp(variable, "servoMaxPulseWidth")) servoMaxPulseWidth = intVal;
  else if(!strcmp(variable, "voltDivider")) voltDivider = intVal;
  else if(!strcmp(variable, "voltLow")) voltLow = fltVal;
  else if(!strcmp(variable, "voltInterval")) voltInterval = intVal;
  else if(!strcmp(variable, "camPan")) setCamPan(intVal);
  else if(!strcmp(variable, "camTilt")) setCamTilt(intVal);
  else if(!strcmp(variable, "wakeUse")) wakeUse = (bool)intVal;
  else if(!strcmp(variable, "wakePin")) wakePin = intVal;
  
  // camera settings
  else if(!strcmp(variable, "xclkMhz")) xclkMhz = intVal;
  else if (s) {
    if (!strcmp(variable, "framesize")) {
      fsizePtr = intVal;
      if (s->set_framesize(s, (framesize_t)fsizePtr) != ESP_OK) res = false;
      // update default FPS for this frame size
      if (playbackHandle != NULL) {
        setFPSlookup(fsizePtr);
        updateConfigVect("fps", String(FPS).c_str()); 
      }
    }
    else if (!strcmp(variable, "fps")) {
      FPS = intVal;
      if (playbackHandle != NULL) setFPS(intVal);
    }
    else if(!strcmp(variable, "quality")) res = s->set_quality(s, intVal);
    else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, intVal);
    else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, intVal);
    else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, intVal);
    else if(!strcmp(variable, "denoise")) res = s->set_denoise(s, intVal);    
    else if(!strcmp(variable, "sharpness")) res = s->set_sharpness(s, intVal);    
    else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)intVal);
    else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, intVal);
    else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, intVal);
    else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, intVal);
    else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, intVal);
    else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, intVal);
    else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, intVal);
    else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, intVal);
    else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, intVal);
    else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, intVal);
    else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, intVal);
    else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, intVal);
    else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, intVal);
    else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, intVal);
    else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, intVal);
    else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, intVal);
    else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, intVal);
    else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, intVal);
    else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, intVal);
    else res = ESP_FAIL;
  }
  return res == ESP_OK ? true : false;
}

void wsAppSpecificHandler(const char* wsMsg) {
  // message from web socket
  int wsLen = strlen(wsMsg) - 1;
  switch ((char)wsMsg[0]) {
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
    // switch off playback on browser
    forcePlayback = false;
    p += sprintf(p, "\"forcePlayback\":0,");  
  }
  p += sprintf(p, "\"showRecord\":%u,", (uint8_t)isCapturing);
  p += sprintf(p, "\"camModel\":\"%s\",", camModel); 
  
  // Extend info
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) p += sprintf(p, "\"card\":\"%s\",", "NO card");
  else {
    if (!filter) {
      if (cardType == CARD_MMC) p += sprintf(p, "\"card\":\"%s\",", "MMC"); 
      else if (cardType == CARD_SD) p += sprintf(p, "\"card\":\"%s\",", "SDSC");
      else if (cardType == CARD_SDHC) p += sprintf(p, "\"card\":\"%s\",", "SDHC"); 
    }
    uint64_t cardSize = SD_MMC.cardSize() / ONEMEG;
    uint64_t totBytes = SD_MMC.totalBytes() / ONEMEG;
    uint64_t useBytes = SD_MMC.usedBytes() / ONEMEG;
    p += sprintf(p, "\"card_size\":\"%llu MB\",", cardSize);
    p += sprintf(p, "\"used_bytes\":\"%llu MB\",", useBytes);
    p += sprintf(p, "\"free_bytes\":\"%llu MB\",", totBytes - useBytes);
    p += sprintf(p, "\"total_bytes\":\"%llu MB\",", totBytes);
  }
  p += sprintf(p, "\"free_psram\":\"%u KB\",", (ESP.getFreePsram() / 1024));     
  p += sprintf(p, "\"progressBar\":%u,", percentLoaded);  
  if (percentLoaded == 100) percentLoaded = 0;
  //p += sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
  *p = 0;
}

bool appDataFiles() {
  // callback from setupAssist.cpp, for any app specific files 
  return true;
}

void doAppPing() {
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
    if (lampNight) setLamp(lampLevel);
  } else if (lampNight) setLamp(0);
}
