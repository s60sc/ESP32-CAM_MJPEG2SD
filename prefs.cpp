
/* 
  Management and storage of application configuration state.
  Configuration file stored on spiffs, except passwords which are stored in NVS
   
  Workflow:
  loadConfig:
    file -> loadConfigMap+loadKeyVal -> map -> getNextKeyVal+updatestatus+updateAppStatus -> vars 
                                               retrieveConfigMap (as required)
  statusHandler:
    map -> buildJsonString+buildAppJsonString -> browser 
  controlHandler: 
    browser -> updateStatus+updateAppStatus -> vars -> updateConfigMap -> map -> saveConfigMap -> file 

  s60sc 2022
*/

# include "myConfig.h"

static fs::FS fp = STORAGE;
static std::map<std::string, std::string> configs;
static Preferences prefs; 
char* jsonBuff = NULL;

static bool updateConfigMap(const char* variable, const char* value);

/********************* mjpeg2sd specific function **********************/

static esp_err_t updateAppStatus(const char* variable, const char* value) {
  // update vars from browser input
  esp_err_t res = ESP_OK; 
  int intVal = atoi(value);
  sensor_t * s = esp_camera_sensor_get();
  if (!strcmp(variable, "framesize")) {
    if(s->pixformat == PIXFORMAT_JPEG) {
      fsizePtr = intVal;
      res = s->set_framesize(s, (framesize_t)fsizePtr);
    }
  }
  else if(!strcmp(variable, "fps")) FPS = intVal;
  else if(!strcmp(variable, "minf")) minSeconds = intVal;
  else if(!strcmp(variable, "updateFPS")) fsizePtr = intVal;
  else if(!strcmp(variable, "stopStream")) stopPlaying();
  else if(!strcmp(variable, "lamp")) controlLamp((bool)intVal);
  else if(!strcmp(variable, "motion")) motionVal = intVal;
  else if(!strcmp(variable, "enableMotion")){
    //Turn on/off motion detection 
    useMotion = (intVal) ? true : false; 
    LOG_INF("%s motion detection", useMotion ? "Enabling" : "Disabling");
  }
  else if(!strcmp(variable, "timeLapseOn")) timeLapseOn = intVal;
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
  
  //Other settings
  else if(!strcmp(variable, "clockUTC")) syncToBrowser(value);      
  else if(!strcmp(variable, "timezone")) strcpy(timezone,value);

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
  else if(!strcmp(variable, "camPan")) updateCamPan(intVal);
  else if(!strcmp(variable, "camTilt")) updateCamTilt(intVal);
  else res = ESP_FAIL;
  return res;
}

static void buildAppJsonString(bool quick) {
  // build app specific part of json string
  updateConfigMap("fps", String(setFPS(0)).c_str()); // value related to requested framesize 
  char* p = jsonBuff + 1;
  p += sprintf(p, "\"llevel\":%u,", lightLevel);
  p += sprintf(p, "\"night\":%s,", nightTime ? "\"Yes\"" : "\"No\"");
  float aTemp = readDS18B20temp(true);
  if (aTemp > -127.0) p += sprintf(p, "\"atemp\":\"%0.1f\",", aTemp);
  else p += sprintf(p, "\"atemp\":\"n/a\",");
  if (currentVoltage < 0) p += sprintf(p, "\"battv\":\"n/a\",");
  else p += sprintf(p, "\"battv\":\"%0.1fV\",", currentVoltage);  
  p += sprintf(p, "\"forceRecord\":%u,", forceRecord ? 1 : 0);  
  //Other settings 
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
    if (!quick) {
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
  //p += sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
  if (!quick) p += sprintf(p, "\"sfile\":%s,", "\"None\"");
  *p = 0;
}

/********************* generic Config functions ****************************/

static bool getNextKeyVal(char* keyName, char* keyVal) {
  // return next key and value from config map on each call
  static std::map<std::string, std::string>::iterator it = configs.begin();
  if (it != configs.end()) {
    strcpy(keyName, it->first.c_str());
    strcpy(keyVal, it->second.c_str()); 
    it++;
    return true;
  }
  // end of map reached
  it = configs.begin();
  return false;
}

static bool updateConfigMap(const char* variable, const char* value) {
  std::string thisKey(variable);
  std::string thisVal(value);
  if (configs.find(thisKey) != configs.end()) {
    configs[thisKey] = thisVal; 
    return true;
  } 
  LOG_DBG("Key %s not found", variable);
  return false; 
}

static bool retrieveConfigMap(const char* variable, char* value) {
  std::string thisKey(variable);
  std::string thisVal(value);
  if (configs.find(thisKey) != configs.end()) {
    strcpy(value, configs[thisKey].c_str()); 
    return true;
  } 
  LOG_WRN("Key %s not found", variable);
  return false; 
}

static void loadKeyVal(const std::string keyValPair) {
  // extract a key value pair from input and load into configs map
  if (keyValPair.length()) {
    size_t colon = keyValPair.find(':');
    if (colon != std::string::npos) {
      configs[keyValPair.substr(0, colon)] =
        keyValPair.substr(colon + 1, keyValPair.length()); 
    } else LOG_ERR("Unable to parse <%s>, len %u", keyValPair.c_str(), keyValPair.length());
  }
}

static void saveConfigMap() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_WRITE);
  if (!file) LOG_ERR("Failed to save to configs file");
  else {
    for (const auto& elem : configs) {
      file.write((uint8_t*)elem.first.c_str(), elem.first.length()); 
      file.write((uint8_t*)":", 1);
      file.write((uint8_t*)elem.second.c_str(), elem.second.length());
      file.write((uint8_t*)"\n", 1);
    }
    file.close();
    LOG_INF("Config file saved");
  }
}

static void loadConfigMap() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file) LOG_ERR("Failed to load file %s", CONFIG_FILE_PATH);
  else {
    configs.erase(configs.begin(), configs.end());
    esp_err_t res = ESP_OK; 
    std::string keyValPair;
    // populate configs map from configs file
    while (true) {
      String kvPairStr = file.readStringUntil('\n');
      keyValPair = ALLOW_SPACES ? kvPairStr.c_str() : std::regex_replace(kvPairStr.c_str(), std::regex("\\s+"), "");
      if (!keyValPair.length()) break;
      loadKeyVal(keyValPair);
    } 
    file.close();
  }
}

static bool savePrefs(bool bClear = false) {
  // use preferences for passwords
  if (!prefs.begin(APP_NAME, false)) {  
    LOG_ERR("Failed to save preferences");
    return false;
  }
  if (bClear) { 
    prefs.clear(); 
    LOG_ERR("Cleared preferences");
    return true;
  }
  prefs.putString("st_ssid", ST_SSID);
  prefs.putString("st_pass", ST_Pass);
  prefs.putString("ap_pass", AP_Pass); 
#ifdef INCLUDE_FTP          
  prefs.putString("ftp_pass", ftp_pass);
#endif
#ifdef INCLUDE_SMTP
  prefs.putString("smtp_pass", smtp_pass);
#endif
  prefs.end();
  return true;
}

static bool loadPrefs() {
  // use preferences for passwords
  if (!prefs.begin(APP_NAME, false)) {  
    savePrefs(); // if prefs do not yet exist
    return false;
  }
  prefs.getString("st_ssid", ST_SSID, 32);
  prefs.getString("st_pass", ST_Pass, MAX_PWD_LEN);
  prefs.getString("ap_pass", AP_Pass, MAX_PWD_LEN); 
#ifdef INCLUDE_FTP
  prefs.getString("ftp_pass", ftp_pass, MAX_PWD_LEN);
#endif
#ifdef INCLUDE_SMTP
  prefs.getString("smtp_pass", smtp_pass, MAX_PWD_LEN);
#endif
  prefs.end();
  return true;
}

bool updateStatus(const char* variable, const char* value) {
  // called from controlHandler() to update app status from changes made on browser
  // or from loadConfig() to update app status from stored preferences
  int intVal = atoi(value);
  int fltVal = atof(value);
  updateConfigMap(variable, value); // update config map
  updateAppStatus(variable, value); 
  if (!strcmp(variable, "hostName")) strcpy(hostName, value);
  else if(!strcmp(variable, "ST_SSID")) strcpy(ST_SSID, value);
  else if(!strcmp(variable, "ST_Pass")) strcpy(ST_Pass, value);
#ifdef INCLUDE_FTP
  else if(!strcmp(variable, "ftp_server")) strcpy(ftp_server, value);
  else if(!strcmp(variable, "ftp_port")) ftp_port = intVal;
  else if(!strcmp(variable, "ftp_user")) strcpy(ftp_user, value);
  else if(!strcmp(variable, "ftp_pass")) strcpy(ftp_pass, value);
  else if(!strcmp(variable, "ftp_wd")) strcpy(ftp_wd, value);
#endif
#ifdef INCLUDE_SMTP
  else if(!strcmp(variable, "smtp_port")) smtp_port = intVal;
  else if(!strcmp(variable, "smtp_login")) strcpy(smtp_login, value);
  else if(!strcmp(variable, "smtp_server")) strcpy(smtp_server, value);
  else if(!strcmp(variable, "smtp_email")) strcpy(smtp_email, value);
  else if(!strcmp(variable, "smtp_pass")) strcpy(smtp_pass, value);
#endif
  else if(!strcmp(variable, "dbgVerbose")) {
    dbgVerbose = (intVal) ? true : false;
    Serial.setDebugOutput(dbgVerbose);
  } 
  else if(!strcmp(variable, "logMode")) {  
    logMode = intVal; 
    if (!(logMode == 2 && WiFi.status() != WL_CONNECTED)) remote_log_init();
    else logMode = 0;
  }
  else if(!strcmp(variable, "resetLog")) reset_log(); 
  else if(!strcmp(variable, "reset")) return false;
  else if(!strcmp(variable, "clear")) savePrefs(true); // /control?clear=1
  else if(!strcmp(variable, "deldata")) {  
    // /control?deldata=1
    if ((fs::SPIFFSFS*)&STORAGE == &SPIFFS) startSpiffs(true);
    else deleteFolderOrFile(DATA_DIR);
    return false;
  }
  else if(!strcmp(variable, "save")) {
    saveConfigMap();
    savePrefs();
  } 
  return true;
}

void buildJsonString(bool quick) {
  // called from statusHandler() to build json string with current status to return to browser 
  char* p = jsonBuff;
  *p++ = '{';
  buildAppJsonString(quick);
  p += strlen(jsonBuff) - 1;
  if (!quick) {
    // populate first part of json string from config map
    for (const auto& elem : configs) 
      p += sprintf(p, "\"%s\":\"%s\",", elem.first.c_str(), elem.second.c_str());
    // stored on NVS 
    p += sprintf(p, "\"ST_Pass\":\"%.*s\",",strlen(ST_Pass), FILLSTAR);
    p += sprintf(p, "\"AP_Pass\":\"%.*s\",",strlen(AP_Pass), FILLSTAR);
#ifdef INCLUDE_FTP 
    p += sprintf(p, "\"ftp_pass\":\"%.*s\",",strlen(ftp_pass), FILLSTAR);
#endif
#ifdef INCLUDE_SMTP
    p += sprintf(p, "\"smtp_pass\":\"%.*s\",",strlen(smtp_pass), FILLSTAR);
#endif
    // other
    p += sprintf(p, "\"fw_version\":\"%s\",", APP_VER); 
  }
  *p--;
  *p++ = '}'; // overwrite final comma
  *p = 0;
}

bool loadConfig() {
  // called on startup
  LOG_INF("Load config");
  if (jsonBuff == NULL) jsonBuff = (char*)ps_malloc(JSON_BUFF_LEN); 
  loadPrefs();
  loadConfigMap();

  // set default hostname if config is null
  AP_SSID.toUpperCase();
  retrieveConfigMap("hostName", hostName);
  if (!strcmp(hostName, "null") || !strlen(hostName)) {
    strcpy(hostName, AP_SSID.c_str());
    updateStatus("hostName", hostName);
  }

  // ST_SSID value priority: configs > prefs > hardcoded
  retrieveConfigMap("ST_SSID", jsonBuff);
  if (!strcmp(jsonBuff, "null")) {
    // not defined in configs, use retrieved prefs 
    if (strlen(ST_SSID)) {
      updateConfigMap("ST_SSID", ST_SSID);
    }
    else {
      // not in prefs, use hardcoded values
      updateConfigMap("ST_SSID", ST_SSID);
      updateConfigMap("ST_ip", ST_ip);
      updateConfigMap("ST_gw", ST_gw);
      updateConfigMap("ST_sn", ST_sn);
      updateConfigMap("ST_ns1", ST_ns1);
      updateConfigMap("ST_ns1", ST_ns2);
    }
  }

  // load variables from stored config map
  char variable[32] = {0,};
  char value[FILE_NAME_LEN] = {0,};
  while(getNextKeyVal(variable, value)) updateStatus(variable, value);
  if (strlen(ST_SSID)) LOG_INF("Using ssid: %s%s %s", ST_SSID, !strlen(ST_ip) ? " " : " with static ip ", ST_ip);
  return true;
}
