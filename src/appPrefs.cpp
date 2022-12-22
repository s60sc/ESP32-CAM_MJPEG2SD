// mjpeg2sd specific config functions
//
// s60sc 2022

#include "globals.h"

bool updateAppStatus(const char* variable, const char* value) {
  // update vars from browser input
  bool res = true; 
  int intVal = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
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
  else if(!strcmp(variable, "minf")) minSeconds = intVal; 
  else if(!strcmp(variable, "stopStream")) stopPlaying();
  else if(!strcmp(variable, "lamp")) setLamp((bool)intVal);
  else if(!strcmp(variable, "motion")) motionVal = intVal;
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
    //Turn on/off motion detection 
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
  else if(!strcmp(variable, "uploadMove")) {
    ftpFileOrFolder(value);  
    deleteFolderOrFile(value);
  }
  else if(!strcmp(variable, "delete")) {
    stopPlayback = true;
    deleteFolderOrFile(value);
  }
  else if(!strcmp(variable, "record")) doRecording = (intVal) ? true : false;   
  else if(!strcmp(variable, "forceRecord")) forceRecord = (intVal) ? true : false;                                       
  else if(!strcmp(variable, "dbgMotion")) {
    // only enable show motion if motion detect enabled
    if (intVal && useMotion) dbgMotion = true;
    else  dbgMotion = false;
    doRecording = !dbgMotion;
  }
  
  // peripherals
  else if(!strcmp(variable, "useIOextender")) useIOextender = (bool)intVal;
  else if(!strcmp(variable, "pirUse")) pirUse = (bool)intVal;
  else if(!strcmp(variable, "lampUse")) lampUse = (bool)intVal;
  else if(!strcmp(variable, "lampAuto")) lampAuto = (bool)intVal;
  else if(!strcmp(variable, "servoUse")) servoUse = (bool)intVal;
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
  else if(!strcmp(variable, "voltLow")) voltLow = intVal;
  else if(!strcmp(variable, "voltInterval")) voltInterval = intVal;

  //Other settings
  else if(!strcmp(variable, "clockUTC")) syncToBrowser(value);      
  else if(!strcmp(variable, "timezone")) strcpy(timezone,value);
  else if(!strcmp(variable, "smtpFrame")) smtpFrame = intVal;
  else if(!strcmp(variable, "smtpMaxEmails")) smtpMaxEmails = intVal;
  else if(!strcmp(variable, "sdMinCardFreeSpace")) sdMinCardFreeSpace = intVal;
  else if(!strcmp(variable, "sdFreeSpaceMode")) sdFreeSpaceMode = intVal;
  else if(!strcmp(variable, "sdFormatIfMountFailed")) sdFormatIfMountFailed = (bool)intVal;
  // camera settings
  else if(!strcmp(variable, "quality")) res = s->set_quality(s, intVal);
  else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, intVal);
  else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, intVal);
  else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, intVal);
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
  else if(!strcmp(variable, "camPan")) setCamPan(intVal);
  else if(!strcmp(variable, "camTilt")) setCamTilt(intVal);
  else {
    if (!strcmp(variable, "smtpUse") && !strcmp(variable, "wifiTimeoutSecs") && !strcmp(variable, "responseTimeoutSecs")) {
      LOG_WRN("Unrecognised config: %s", variable);
      res = ESP_FAIL;
    }
  }
  return res;
}

void buildAppJsonString(bool filter) {
  // build app specific part of json string
  char* p = jsonBuff + 1;
  p += sprintf(p, "\"llevel\":%u,", lightLevel);
  p += sprintf(p, "\"night\":%s,", nightTime ? "\"Yes\"" : "\"No\"");
  float aTemp = readDS18B20temp(true);
  if (aTemp > -127.0) p += sprintf(p, "\"atemp\":\"%0.1f\",", aTemp);
  else p += sprintf(p, "\"atemp\":\"n/a\",");
  if (currentVoltage < 0) p += sprintf(p, "\"battv\":\"n/a\",");
  else p += sprintf(p, "\"battv\":\"%0.1fV\",", currentVoltage);  
  p += sprintf(p, "\"forceRecord\":%u,", forceRecord ? 1 : 0);  
  p += sprintf(p, "\"forcePlayback\":%u,", doPlayback ? 1 : 0);  
  // Other settings 
  struct timeval tv;
  gettimeofday(&tv, NULL);
  time_t currEpoch = tv.tv_sec;
  char timeBuff[20];
  strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
  p += sprintf(p, "\"clock\":\"%s\",", timeBuff);
  strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", gmtime(&currEpoch));
  p += sprintf(p, "\"clockUTC\":\"%s\",", timeBuff); 
  getUpTime(timeBuff);
  
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
  p += sprintf(p, "\"up_time\":\"%s\",", timeBuff);   
  p += sprintf(p, "\"free_heap\":\"%u KB\",", (ESP.getFreeHeap() / 1024));    
  p += sprintf(p, "\"free_psram\":\"%u KB\",", (ESP.getFreePsram() / 1024));    
  p += sprintf(p, "\"wifi_rssi\":\"%i dBm\",", WiFi.RSSI() );  
  p += sprintf(p, "\"refreshVal\":%u,", refreshVal);  
  p += sprintf(p, "\"progressBar\":%u,", percentLoaded);  
  if (percentLoaded == 100) percentLoaded = 0;
  //p += sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
  if (!filter) p += sprintf(p, "\"sfile\":%s,", "\"None\"");
  *p = 0;
}

void appDataFiles() {
  // callback from setupAssist.cpp, for any app specific files 
}
