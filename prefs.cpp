
/* 
  Management and storage of application configuration state.
  Configuration file stored on spiffs or SD, except passwords which are stored in NVS
   
  Workflow:
  loadConfig:
    file -> loadConfigVect+loadKeyVal -> vector -> getNextKeyVal+updatestatus+updateAppStatus -> vars 
                                                   retrieveConfigVal (as required)
  statusHandler:
    vector -> buildJsonString+buildAppJsonString -> browser 
  controlHandler: 
    browser -> updateStatus+updateAppStatus -> updateConfigVect -> vector -> saveConfigVect -> file 
                                            -> vars
                                            
  config field types:
  - T : Text
  - N : Number
  - S : Select options S:lab1:lab2:etc
  - C : Checkbox (as slider)
  - D : Display only
  - R : Range (as slider) R:min:max:step
  - B : Radio Buttons B:lab1:lab2:etc

  s60sc 2022
*/

#include "appGlobals.h"

static fs::FS fp = STORAGE;
static std::vector<std::vector<std::string>> configs;
static Preferences prefs; 
char* jsonBuff = NULL;
bool configLoaded = false;
static char appId[16];


/********************* generic Config functions ****************************/

static bool getNextKeyVal(char* keyName, char* keyVal) {
  // return next key and value from configs on each call in key order
  static int row = 0;
  if (row++ < configs.size()) {
    strcpy(keyName, configs[row - 1][0].c_str());
    strcpy(keyVal, configs[row - 1][1].c_str()); 
    return true;
  }
  // end of vector reached, reset
  row = 0;
  return false;
}

static int getKeyPos(std::string thisKey) {
  // get location of given key to retrieve other elements
  if (configs.empty()) return -1;
  auto lower = std::lower_bound(configs.begin(), configs.end(), thisKey, [](
    const std::vector<std::string> &a, const std::string &b) { 
    return a[0] < b;}
  );
  int keyPos = std::distance(configs.begin(), lower); 
  if (thisKey == configs[keyPos][0]) return keyPos;
//  else LOG_DBG("Key %s not found", thisKey.c_str()); 
  return -1; // not found
}

bool updateConfigVect(const char* variable, const char* value) {
  std::string thisKey(variable);
  std::string thisVal(value);
  int keyPos = getKeyPos(thisKey);
  if (keyPos >= 0) {
    // update value
    if (psramFound()) heap_caps_malloc_extmem_enable(0); 
    configs[keyPos][1] = thisVal;
    if (psramFound()) heap_caps_malloc_extmem_enable(4096); 
    return true;    
  }
  return false; 
}

static bool retrieveConfigVal(const char* variable, char* value) {
  std::string thisKey(variable);
  int keyPos = getKeyPos(thisKey);
  if (keyPos >= 0) {
    strcpy(value, configs[keyPos][1].c_str()); 
    return true;  
  } else {
    value[0] = 0; // empty string
    LOG_WRN("Key %s not set", variable);
  }
  return false; 
}

static void loadVectItem(const std::string keyValGrpLabel) {
  // extract a config tokens from input and load into configs vector
  // comprises key : val : group : type : label
  const int tokens = 5;
  std::string token[tokens];
  int i = 0;
  if (keyValGrpLabel.length()) {
    std::istringstream ss(keyValGrpLabel);
    while (std::getline(ss, token[i++], DELIM));
    if (i != tokens+1) LOG_ERR("Unable to parse '%s', len %u", keyValGrpLabel.c_str(), keyValGrpLabel.length());
    else {
      if (!ALLOW_SPACES) token[1].erase(std::remove(token[1].begin(), token[1].end(), ' '), token[1].end());
      if (token[tokens-1][token[tokens-1].size() - 1] == '\r') token[tokens-1].erase(token[tokens-1].size() - 1);
      configs.push_back({token[0], token[1], token[2], token[3], token[4]});
    }
  }
  if (configs.size() > MAX_CONFIGS) LOG_ALT("Config file entries: %u exceed max: %u", configs.size(), MAX_CONFIGS);
}

static void saveConfigVect() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_WRITE);
  char configLine[FILE_NAME_LEN + 100];
  if (!file) LOG_ALT("Failed to save to configs file");
  else {
    for (const auto& row: configs) {
      // recreate config file with updated content
      if (!strcmp(row[0].c_str() + strlen(row[0].c_str()) - 5, "_Pass")) 
        // replace passwords with asterisks
        snprintf(configLine, FILE_NAME_LEN + 100, "%s%c%.*s%c%s%c%s%c%s\n", row[0].c_str(), DELIM, strlen(row[1].c_str()), FILLSTAR, DELIM, row[2].c_str(), DELIM, row[3].c_str(), DELIM, row[4].c_str());
      else snprintf(configLine, FILE_NAME_LEN + 100, "%s%c%s%c%s%c%s%c%s\n", row[0].c_str(), DELIM, row[1].c_str(), DELIM, row[2].c_str(), DELIM, row[3].c_str(), DELIM, row[4].c_str());
      file.write((uint8_t*)configLine, strlen(configLine));
    }
    LOG_ALT("Config file saved");
  }
  file.close();
}

static bool loadConfigVect() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || !file.size()) {
    LOG_ERR("Failed to load file %s", CONFIG_FILE_PATH);
    if (!file.size()) {
      file.close();
      STORAGE.remove(CONFIG_FILE_PATH);
    }
    return false;
  } else {
    // force vector into psram if available
    if (psramFound()) heap_caps_malloc_extmem_enable(0); 
    configs.reserve(MAX_CONFIGS);
    // extract each config line from file
    while (true) {
      String configLineStr = file.readStringUntil('\n');
      if (!configLineStr.length()) break;
      loadVectItem(configLineStr.c_str());
    } 
    // sort vector by key (element 0 in row)
    std::sort(configs.begin(), configs.end(), [] (
      const std::vector<std::string> &a, const std::vector<std::string> &b) {
      return a[0] < b[0];}
    );
    // return malloc to default 
    if (psramFound()) heap_caps_malloc_extmem_enable(4096);
  }
  file.close();
  return true;
}

static bool savePrefs(bool retain = true) {
  // use preferences for passwords
  if (!prefs.begin(APP_NAME, false)) {  
    LOG_ERR("Failed to save preferences");
    return false;
  }
  if (!retain) { 
    prefs.clear(); 
    LOG_INF("Cleared preferences");
    return true;
  }
  prefs.putString("ST_SSID", ST_SSID);
  prefs.putString("ST_Pass", ST_Pass);
  prefs.putString("AP_Pass", AP_Pass); 
  prefs.putString("Auth_Pass", Auth_Pass); 
#ifdef INCLUDE_FTP          
  prefs.putString("FTP_Pass", FTP_Pass);
#endif
#ifdef INCLUDE_SMTP
  prefs.putString("SMTP_Pass", SMTP_Pass);
#endif
#ifdef INCLUDE_MQTT
  prefs.putString("mqtt_user_Pass", mqtt_user_Pass);
#endif
  prefs.end();
  LOG_INF("Saved preferences");
  return true;
}

static bool loadPrefs() {
  // use preferences for passwords
  if (!prefs.begin(APP_NAME, false)) {  
    savePrefs(); // if prefs do not yet exist
    return false;
  }
  if (!strlen(ST_SSID)) {
     // first call only after instal
    prefs.getString("ST_SSID", ST_SSID, MAX_PWD_LEN);
    updateConfigVect("ST_SSID", ST_SSID);
  } 

  prefs.getString("ST_Pass", ST_Pass, MAX_PWD_LEN);
  updateConfigVect("ST_Pass", ST_Pass);
  prefs.getString("AP_Pass", AP_Pass, MAX_PWD_LEN);
  prefs.getString("Auth_Pass", Auth_Pass, MAX_PWD_LEN); 
#ifdef INCLUDE_FTP
  prefs.getString("FTP_Pass", FTP_Pass, MAX_PWD_LEN);
#endif
#ifdef INCLUDE_SMTP
  prefs.getString("SMTP_Pass", SMTP_Pass, MAX_PWD_LEN);
#endif
#ifdef INCLUDE_MQTT
  prefs.getString("mqtt_user_Pass", mqtt_user_Pass, MAX_PWD_LEN);
#endif
  prefs.end();
  return true;
}

void updateStatus(const char* variable, const char* _value) {
  // called from controlHandler() to update app status from changes made on browser
  // or from loadConfig() to update app status from stored preferences
  bool res = true;
  char value[FILE_NAME_LEN];
  strcpy(value, _value);  
#ifdef INCLUDE_MQTT
  if (mqtt_active) {
    char buff[FILE_NAME_LEN * 2];
    sprintf(buff,"%s=%s",variable, value);
    mqttPublish(buff);
  }
#endif
  int intVal = atoi(value); 
  if (!strcmp(variable, "hostName")) strcpy(hostName, value);
  else if (!strcmp(variable, "ST_SSID")) strcpy(ST_SSID, value);
  else if (!strcmp(variable, "ST_Pass") && strchr(value, '*') == NULL) strcpy(ST_Pass, value);
  else if (!strcmp(variable, "ST_ip")) strcpy(ST_ip, value);
  else if (!strcmp(variable, "ST_gw")) strcpy(ST_gw, value);
  else if (!strcmp(variable, "ST_sn")) strcpy(ST_sn, value);
  else if (!strcmp(variable, "ST_ns1")) strcpy(ST_ns1, value);
  else if (!strcmp(variable, "ST_ns1")) strcpy(ST_ns2, value);
  else if (!strcmp(variable, "Auth_Name")) strcpy(Auth_Name, value);
  else if (!strcmp(variable, "Auth_Pass") && strchr(value, '*') == NULL) strcpy(Auth_Pass, value);
  else if (!strcmp(variable, "AP_ip")) strcpy(AP_ip, value);
  else if (!strcmp(variable, "AP_gw")) strcpy(AP_gw, value);
  else if (!strcmp(variable, "AP_sn")) strcpy(AP_sn, value);
  else if (!strcmp(variable, "AP_SSID")) strcpy(AP_SSID, value);
  else if (!strcmp(variable, "AP_Pass") && strchr(value, '*') == NULL) strcpy(AP_Pass, value); 
  else if (!strcmp(variable, "allowAP")) allowAP = (bool)intVal;
#ifdef INCLUDE_FTP
  else if (!strcmp(variable, "ftp_server")) strcpy(ftp_server, value);
  else if (!strcmp(variable, "ftp_port")) ftp_port = intVal;
  else if (!strcmp(variable, "ftp_user")) strcpy(ftp_user, value);
  else if (!strcmp(variable, "FTP_Pass") && strchr(value, '*') == NULL) strcpy(FTP_Pass, value);
  else if (!strcmp(variable, "ftp_wd")) strcpy(ftp_wd, value);
#endif
#ifdef INCLUDE_SMTP
  else if (!strcmp(variable, "smtpUse")) smtpUse = (bool)intVal;
  else if (!strcmp(variable, "smtp_login")) strcpy(smtp_login, value);
  else if (!strcmp(variable, "smtp_server")) strcpy(smtp_server, value);
  else if (!strcmp(variable, "smtp_email")) strcpy(smtp_email, value);
  else if (!strcmp(variable, "SMTP_Pass") && strchr(value, '*') == NULL) strcpy(SMTP_Pass, value);
  else if (!strcmp(variable, "smtp_port")) smtp_port = intVal;
  else if (!strcmp(variable, "smtpFrame")) smtpFrame = intVal;
  else if (!strcmp(variable, "smtpMaxEmails")) smtpMaxEmails = intVal;
#endif
#ifdef INCLUDE_MQTT
  else if (!strcmp(variable, "mqtt_active")) {
    mqtt_active = (bool)intVal;
    if (mqtt_active) startMqttClient();
    else stopMqttClient();
  } 
  else if (!strcmp(variable, "mqtt_broker")) strcpy(mqtt_broker, value);
  else if (!strcmp(variable, "mqtt_port")) strcpy(mqtt_port, value);
  else if (!strcmp(variable, "mqtt_user")) strcpy(mqtt_user, value);
  else if (!strcmp(variable, "mqtt_user_Pass")) strcpy(mqtt_user_Pass, value);
  else if (!strcmp(variable, "mqtt_topic_prefix")) strcpy(mqtt_topic_prefix, value);
#endif

  // Other settings
  else if (!strcmp(variable, "clockUTC")) syncToBrowser((uint32_t)intVal);      
  else if (!strcmp(variable, "timezone")) strcpy(timezone, value);
  else if (!strcmp(variable, "ntpServer")) strcpy(ntpServer, value);
  else if (!strcmp(variable, "sdMinCardFreeSpace")) sdMinCardFreeSpace = intVal;
  else if (!strcmp(variable, "sdFreeSpaceMode")) sdFreeSpaceMode = intVal;
  else if (!strcmp(variable, "responseTimeoutSecs")) responseTimeoutSecs = intVal;
  else if (!strcmp(variable, "wifiTimeoutSecs")) wifiTimeoutSecs = intVal;
  else if (!strcmp(variable, "dbgVerbose")) {
    dbgVerbose = (intVal) ? true : false;
    Serial.setDebugOutput(dbgVerbose);
  } 
  else if (!strcmp(variable, "logMode")) {
    logMode = (bool)intVal; 
    remote_log_init();
  }
  else if (!strcmp(variable, "refreshVal")) refreshVal = intVal; 
  else if (!strcmp(variable, "formatIfMountFailed")) formatIfMountFailed = (bool)intVal;
  else if (!strcmp(variable, "resetLog")) reset_log(); 
  else if (!strcmp(variable, "clear")) savePrefs(false); // /control?clear=1
  else if (!strcmp(variable, "deldata")) {  
    if (intVal) deleteFolderOrFile(DATA_DIR); // entire folder
    else {
      // manually specified file, eg control?deldata=favicon.ico
      char delFile[FILE_NAME_LEN];
      int dlen = snprintf(delFile, FILE_NAME_LEN, "%s/%s", DATA_DIR, value);
      if (dlen > FILE_NAME_LEN) LOG_ERR("File name %s too long", value);
      else deleteFolderOrFile(delFile);
    }
    doRestart("user requested restart after data deletion"); 
  }
  else if (!strcmp(variable, "save")) {
    savePrefs();
    saveConfigVect();
  } else {
    res = updateAppStatus(variable, value);
//    if (!res) LOG_DBG("Unrecognised config: %s", variable);
  }
  if (res) updateConfigVect(variable, value);  
}

void buildJsonString(uint8_t filter) {
  // called from statusHandler() to build json string with current status to return to browser 
  char* p = jsonBuff;
  *p++ = '{';
  if (filter < 2) {
    // build json string for main page refresh
    buildAppJsonString((bool)filter);
    p += strlen(jsonBuff) - 1;
    p += sprintf(p, "\"cfgGroup\":\"-1\",");
    p += sprintf(p, "\"alertMsg\":\"%s\",", alertMsg); 
    alertMsg[0] = 0;
    // generic footer
    time_t currEpoch = getEpoch(); 
    p += sprintf(p, "\"clockUTC\":\"%u\",", (uint32_t)currEpoch); 
    char timeBuff[20];
    strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
    p += sprintf(p, "\"clock\":\"%s\",", timeBuff);
    formatElapsedTime(timeBuff, millis());
    p += sprintf(p, "\"up_time\":\"%s\",", timeBuff);   
    p += sprintf(p, "\"free_heap\":\"%u KB\",", (ESP.getFreeHeap() / 1024));    
    p += sprintf(p, "\"wifi_rssi\":\"%i dBm\",", WiFi.RSSI() );  
    p += sprintf(p, "\"fw_version\":\"%s\",", APP_VER); 

    if (!filter) {
      // populate first part of json string from config vect
      for (const auto& row : configs) 
        p += sprintf(p, "\"%s\":\"%s\",", row[0].c_str(), row[1].c_str());
      // passwords stored in prefs on NVS 
      p += sprintf(p, "\"ST_Pass\":\"%.*s\",", strlen(ST_Pass), FILLSTAR);
      p += sprintf(p, "\"AP_Pass\":\"%.*s\",", strlen(AP_Pass), FILLSTAR);
      p += sprintf(p, "\"Auth_Pass\":\"%.*s\",", strlen(Auth_Pass), FILLSTAR);
  #ifdef INCLUDE_FTP 
      p += sprintf(p, "\"FTP_Pass\":\"%.*s\",", strlen(FTP_Pass), FILLSTAR);
  #endif
  #ifdef INCLUDE_SMTP
      p += sprintf(p, "\"SMTP_Pass\":\"%.*s\",", strlen(SMTP_Pass), FILLSTAR);
  #endif
  #ifdef INCLUDE_MQTT
      p += sprintf(p, "\"mqtt_user_Pass\":\"%.*s\",", strlen(mqtt_user_Pass), FILLSTAR);
  #endif
    }
  } else {
    // build json string for requested config group
    uint8_t cfgGroup = filter - 10; // filter number is length of url query string, config group number is length of string - 10
    p += sprintf(p, "\"cfgGroup\":\"%u\",", cfgGroup);
    char pwdHide[MAX_PWD_LEN];
    for (const auto& row : configs) {
      if (atoi(row[2].c_str()) == cfgGroup) {
        strncpy(pwdHide, FILLSTAR, strlen(row[1].c_str()));
        pwdHide[strlen(row[1].c_str())] = 0;
        // for each config item, list - key:value, key:label text, key:type identifier
        p += sprintf(p, "\"%s\":\"%s\",\"lab%s\":\"%s\",\"typ%s\":\"%s\",", row[0].c_str(),
          strstr(row[0].c_str(), "_Pass") == NULL ? row[1].c_str() : pwdHide, row[0].c_str(), row[4].c_str(), row[0].c_str(), row[3].c_str()); 
      }
    }
  }
  *p = 0;
  *(--p) = '}'; // overwrite final comma
  if (p - jsonBuff >= JSON_BUFF_LEN) LOG_ERR("jsonBuff overrun by: %u bytes", (p - jsonBuff) - JSON_BUFF_LEN);
}

void initStatus(int cfgGroup, int delayVal) {
  // update app status for given config group
  for (const auto& row : configs) {
    if (atoi(row[2].c_str()) == cfgGroup) updateAppStatus(row[0].c_str(), row[1].c_str());
    delay(delayVal);
  }
}

bool loadConfig() {
  // called on startup
  LOG_INF("Load config");
  if (jsonBuff == NULL) {
    jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN); 
  }
  char variable[32] = {0,};
  char value[FILE_NAME_LEN] = {0,};
  if (loadConfigVect()) {
    retrieveConfigVal("appId", appId);
    if (strcmp(appId, APP_NAME)) {
      // cleanup storage for different app
      sprintf(startupFailure, "Wrong configs.txt file, expected %s, got %s", APP_NAME, appId);
      deleteFolderOrFile(DATA_DIR);
      savePrefs(false);
      return false;
    }
    loadPrefs(); // overwrites any corresponding entries in config
    // set default hostname and AP SSID if config is null
    retrieveConfigVal("hostName", hostName);
    if (!strlen(hostName)) {
      sprintf(hostName, "%s_%012llX", APP_NAME, ESP.getEfuseMac());
      updateConfigVect("hostName", hostName);
    }
    retrieveConfigVal("AP_SSID", AP_SSID);
    if (!strlen(AP_SSID)) {
      strcpy(AP_SSID, hostName);
      updateConfigVect("AP_SSID", AP_SSID);
    }
  
    // load variables from stored config vector
    while (getNextKeyVal(variable, value)) updateStatus(variable, value);
    configLoaded = true;
    debugMemory("loadConfig");
    return true;
  }
  // no config file
  loadPrefs(); 
  while (getNextKeyVal(variable, value)) updateStatus(variable, value);
  return false;
}
