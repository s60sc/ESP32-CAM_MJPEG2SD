
/* 
  Management and storage of application configuration state.
  Configuration file stored on flash or SD, except passwords which are stored in NVS
   
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

  s60sc 2022, 2024
*/

#include "appGlobals.h"

static fs::FS fp = STORAGE;
static std::vector<std::vector<std::string>> configs;
static Preferences prefs; 
char* jsonBuff = NULL;
static char appId[16];
static char variable[FILE_NAME_LEN] = {0};
static char value[IN_FILE_NAME_LEN] = {0};
time_t currEpoch = 0;

/********************* generic Config functions ****************************/

static bool getNextKeyVal() {
  // return next key and value from configs on each call in key order
  static int row = 0;
  if (row++ < configs.size()) {
    strncpy(variable, configs[row - 1][0].c_str(), sizeof(variable) - 1);
    strncpy(value, configs[row - 1][1].c_str(), sizeof(value) - 1); 
    return true;
  }
  // end of vector reached, reset
  row = 0;
  return false;
}

void showConfigVect() {
  for (const std::vector<std::string>& innerVector : configs) {
    // Print each element of the inner vector
    for (const std::string& element : innerVector) printf("%s,", element.c_str());
    printf("\n"); // Add a newline after each inner vector
  }
}

void reloadConfigs() {
  while (getNextKeyVal()) updateStatus(variable, value, false);
#if INCLUDE_MQTT
  if (mqtt_active) {
    buildJsonString(1);
    mqttPublishPath("status", jsonBuff);
  }
#endif
}

static int getKeyPos(std::string thisKey) {
  // get location of given key to retrieve other elements
  if (configs.empty()) return -1;
  auto lower = std::lower_bound(configs.begin(), configs.end(), thisKey, [](
    const std::vector<std::string> &a, const std::string &b) { 
    return a[0] < b;}
  );
  int keyPos = std::distance(configs.begin(), lower); 
  if (keyPos < configs.size() && thisKey == configs[keyPos][0]) return keyPos;
//  else LOG_VRB("Key %s not found", thisKey.c_str()); 
  return -1; // not found
}

bool updateConfigVect(const char* variable, const char* value) {
  std::string thisKey(variable);
  std::string thisVal(value);
  int keyPos = getKeyPos(thisKey);
  if (keyPos >= 0) {
    // update value
    if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); 
    configs[keyPos][1] = thisVal;
    if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
    return true;    
  }
  return false; 
}

bool retrieveConfigVal(const char* variable, char* value) {
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
  if (configs.size() > MAX_CONFIGS) LOG_ERR("Config file entries: %u exceed max: %u", configs.size(), MAX_CONFIGS);
}

static void saveConfigVect() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_WRITE);
  char configLine[FILE_NAME_LEN + 101];
  if (!file) LOG_WRN("Failed to save to configs file");
  else {
    sort(configs.begin(), configs.end());
    configs.erase(unique(configs.begin(), configs.end()), configs.end()); // remove any dups
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
  // force config vector into psram if available
  if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); 
  configs.reserve(MAX_CONFIGS);
  // extract each config line from file
  File file = fp.open(CONFIG_FILE_PATH, FILE_READ);
  while (file.available()) {
    String configLineStr = file.readStringUntil('\n');
    if (configLineStr.length()) loadVectItem(configLineStr.c_str());
  } 
  // sort vector by key (element 0 in row)
  std::sort(configs.begin(), configs.end(), [] (
    const std::vector<std::string> &a, const std::vector<std::string> &b) {
    return a[0] < b[0];}
  );
  // return malloc to default 
  if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
  file.close();
  return true;
}

static bool savePrefs(bool retain = true) {
  // use preferences for passwords
  if (!prefs.begin(APP_NAME, false)) {  
    LOG_WRN("Failed to save preferences");
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
#if INCLUDE_FTP_HFS
  prefs.putString("FS_Pass", FS_Pass);
#endif
#if INCLUDE_SMTP
  prefs.putString("SMTP_Pass", SMTP_Pass);
#endif
#if INCLUDE_MQTT
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
    prefs.getString("ST_SSID", ST_SSID, MAX_PWD_LEN); // max 15 chars
    updateConfigVect("ST_SSID", ST_SSID);
  } 

  prefs.getString("ST_Pass", ST_Pass, MAX_PWD_LEN);
  updateConfigVect("ST_Pass", ST_Pass);
  prefs.getString("AP_Pass", AP_Pass, MAX_PWD_LEN);
  prefs.getString("Auth_Pass", Auth_Pass, MAX_PWD_LEN); 
#if INCLUDE_FTP_HFS
  prefs.getString("FS_Pass", FS_Pass, MAX_PWD_LEN);
#endif
#if INCLUDE_SMTP
  prefs.getString("SMTP_Pass", SMTP_Pass, MAX_PWD_LEN);
#endif
#if INCLUDE_MQTT
  prefs.getString("mqtt_user_Pass", mqtt_user_Pass, MAX_PWD_LEN);
#endif
  prefs.end();
  return true;
}

void updateStatus(const char* variable, const char* _value, bool fromUser) {
  // called from controlHandler() to update app status from changes made on browser
  // or from loadConfig() to update app status from stored preferences
  bool res = true;
  char value[IN_FILE_NAME_LEN];
  strncpy(value, _value, sizeof(value));  
#if INCLUDE_MQTT
  if (mqtt_active) {
    char buff[(IN_FILE_NAME_LEN * 2)];
    snprintf(buff, IN_FILE_NAME_LEN * 2, "%s=%s", variable, value);
    mqttPublishPath("state", buff);
  }
#endif

  int intVal = atoi(value); 
  if (!strcmp(variable, "hostName")) strncpy(hostName, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "ST_SSID")) strncpy(ST_SSID, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "ST_Pass") && value[0] != '*') strncpy(ST_Pass, value, MAX_PWD_LEN-1);

  else if (!strcmp(variable, "ST_ip")) strncpy(ST_ip, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "ST_gw")) strncpy(ST_gw, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "ST_sn")) strncpy(ST_sn, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "ST_ns1")) strncpy(ST_ns1, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "ST_ns1")) strncpy(ST_ns2, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "Auth_Name")) strncpy(Auth_Name, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "Auth_Pass") && value[0] != '*') strncpy(Auth_Pass, value, MAX_PWD_LEN-1);
  else if (!strcmp(variable, "AP_ip")) strncpy(AP_ip, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "AP_gw")) strncpy(AP_gw, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "AP_sn")) strncpy(AP_sn, value, MAX_IP_LEN-1);
  else if (!strcmp(variable, "AP_SSID")) strncpy(AP_SSID, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "AP_Pass") && value[0] != '*') strncpy(AP_Pass, value, MAX_PWD_LEN-1); 
  else if (!strcmp(variable, "allowAP")) allowAP = (bool)intVal;
  else if (!strcmp(variable, "useHttps")) useHttps = (bool)intVal;
  else if (!strcmp(variable, "useSecure")) useSecure = (bool)intVal;
  else if (!strcmp(variable, "doGetExtIP")) doGetExtIP = (bool)intVal;  
  else if (!strcmp(variable, "extIP")) strncpy(extIP, value, MAX_IP_LEN-1);
#if INCLUDE_TGRAM
  else if (!strcmp(variable, "tgramUse")) {
    tgramUse = (bool)intVal;
    if (tgramUse) {
#if INCLUDE_SMTP
      smtpUse = false;
#endif
      updateConfigVect("smtpUse", "0");
    }
  }
  else if (!strcmp(variable, "tgramToken")) strncpy(tgramToken, value, MAX_PWD_LEN-1);
  else if (!strcmp(variable, "tgramChatId")) strncpy(tgramChatId, value, MAX_IP_LEN-1);
#endif
#if INCLUDE_FTP_HFS
  else if (!strcmp(variable, "fsServer")) strncpy(fsServer, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "fsPort")) fsPort = intVal;
  else if (!strcmp(variable, "ftpUser")) strncpy(ftpUser, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "FS_Pass") && value[0] != '*') strncpy(FS_Pass, value, MAX_PWD_LEN-1);
  else if (!strcmp(variable, "fsWd")) strncpy(fsWd, value, FILE_NAME_LEN-1);
  else if(!strcmp(variable, "fsUse")) fsUse = (bool)intVal;
  else if(!strcmp(variable, "autoUpload")) autoUpload = (bool)intVal;
  else if(!strcmp(variable, "deleteAfter")) deleteAfter = (bool)intVal;
  else if(!strcmp(variable, "useFtps")) useFtps = (bool)intVal;
#endif
#if INCLUDE_SMTP
  else if (!strcmp(variable, "smtpUse")) {
    smtpUse = (bool)intVal;
    if (smtpUse) {
#if INCLUDE_TGRAM
      tgramUse = false;
#endif
      updateConfigVect("tgramUse", "0");
    }
  }
  else if (!strcmp(variable, "smtp_login")) strncpy(smtp_login, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "smtp_server")) strncpy(smtp_server, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "smtp_email")) strncpy(smtp_email, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "SMTP_Pass") && value[0] != '*') strncpy(SMTP_Pass, value, MAX_PWD_LEN-1);
  else if (!strcmp(variable, "smtp_port")) smtp_port = intVal;
  else if (!strcmp(variable, "smtpMaxEmails")) alertMax = intVal;
#endif
#if INCLUDE_MQTT
  else if (!strcmp(variable, "mqtt_active")) {
    mqtt_active = (bool)intVal;
    if (!mqtt_active) stopMqttClient();
  } 
  else if (!strcmp(variable, "mqtt_broker")) strncpy(mqtt_broker, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "mqtt_port")) strncpy(mqtt_port, value, 4);
  else if (!strcmp(variable, "mqtt_user")) strncpy(mqtt_user, value, MAX_HOST_LEN-1);
  else if (!strcmp(variable, "mqtt_user_Pass") && value[0] != '*') strncpy(mqtt_user_Pass, value, MAX_PWD_LEN-1);
  else if (!strcmp(variable, "mqtt_topic_prefix")) strncpy(mqtt_topic_prefix, value, (FILE_NAME_LEN/2)-1);
#endif

  // Other settings
  else if (!strcmp(variable, "clockUTC")) syncToBrowser((uint32_t)intVal);      
  else if (!strcmp(variable, "timezone")) strncpy(timezone, value, FILE_NAME_LEN-1);
  else if (!strcmp(variable, "ntpServer")) strncpy(ntpServer, value, FILE_NAME_LEN-1);
  else if (!strcmp(variable, "alarmHour")) alarmHour = (uint8_t)intVal;
  else if (!strcmp(variable, "sdMinCardFreeSpace")) sdMinCardFreeSpace = intVal;
  else if (!strcmp(variable, "sdFreeSpaceMode")) sdFreeSpaceMode = intVal;
  else if (!strcmp(variable, "responseTimeoutSecs")) responseTimeoutSecs = intVal;
  else if (!strcmp(variable, "wifiTimeoutSecs")) wifiTimeoutSecs = intVal;
  else if (!strcmp(variable, "usePing")) usePing = (bool)intVal;
  else if (!strcmp(variable, "dbgVerbose")) {
    dbgVerbose = (intVal) ? true : false;
    Serial.setDebugOutput(dbgVerbose);
  } 
  else if (!strcmp(variable, "logType")) {
    logType = intVal;
    wsLog = (logType == 1) ? true : false;
    remote_log_init();
  } 
  else if (!strcmp(variable, "sdLog")) {
    sdLog = (bool)intVal; 
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
      if (dlen > FILE_NAME_LEN) LOG_WRN("File name %s too long", value);
      else deleteFolderOrFile(delFile);
    }
    doRestart("user requested restart after data deletion"); 
  }
  else if (!strcmp(variable, "save")) {
    if (intVal) savePrefs();
    saveConfigVect();
  } else {
    res = updateAppStatus(variable, value, fromUser);
    if (!res) {
      if (fromUser) LOG_WRN("Trying to config %s but feature not included", variable);
      else LOG_VRB("Unrecognised config: %s", variable);
    }
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
    currEpoch = getEpoch(); 
    p += sprintf(p, "\"clockUTC\":\"%lu\",", (uint32_t)currEpoch); 
    char timeBuff[20];
    strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
    p += sprintf(p, "\"clock\":\"%s\",", timeBuff);
    formatElapsedTime(timeBuff, millis());
    p += sprintf(p, "\"up_time\":\"%s\",", timeBuff);   
    p += sprintf(p, "\"free_heap\":\"%s\",", fmtSize(ESP.getFreeHeap()));    
    p += sprintf(p, "\"wifi_rssi\":\"%i dBm\",", WiFi.RSSI() );  
    p += sprintf(p, "\"fw_version\":\"%s\",", APP_VER); 
    p += sprintf(p, "\"macAddressEfuse\":\"%012llX\",", ESP.getEfuseMac() ); 
    p += sprintf(p, "\"macAddressWiFi\":\"%s\",", WiFi.macAddress().c_str() ); 
    p += sprintf(p, "\"extIP\":\"%s\",", extIP); 
    p += sprintf(p, "\"httpPort\":\"%u\",", HTTP_PORT); 
    p += sprintf(p, "\"httpsPort\":\"%u\",", HTTPS_PORT); 
    if (!filter) {
      // populate first part of json string from config vect
      for (const auto& row : configs) 
        p += sprintf(p, "\"%s\":\"%s\",", row[0].c_str(), row[1].c_str());
      p += sprintf(p, "\"logType\":\"%d\",", logType);
      // passwords stored in prefs on NVS 
      p += sprintf(p, "\"ST_Pass\":\"%.*s\",", strlen(ST_Pass), FILLSTAR);
      p += sprintf(p, "\"AP_Pass\":\"%.*s\",", strlen(AP_Pass), FILLSTAR);
      p += sprintf(p, "\"Auth_Pass\":\"%.*s\",", strlen(Auth_Pass), FILLSTAR);
#if INCLUDE_FTP_HFS
      p += sprintf(p, "\"FS_Pass\":\"%.*s\",", strlen(FS_Pass), FILLSTAR);
#endif
#if INCLUDE_SMTP
      p += sprintf(p, "\"SMTP_Pass\":\"%.*s\",", strlen(SMTP_Pass), FILLSTAR);
#endif
#if INCLUDE_MQTT
      p += sprintf(p, "\"mqtt_user_Pass\":\"%.*s\",", strlen(mqtt_user_Pass), FILLSTAR);
#endif
    }
  } else {
    // build json string for requested config group
    updateAppStatus("custom", "");
    uint8_t cfgGroup = filter - 10; // filter number is length of url query string, config group number is length of string - 10
    p += sprintf(p, "\"cfgGroup\":\"%u\",", cfgGroup);
    char pwdHide[MAX_PWD_LEN] = {0, };  // used to replace password value with asterisks
    for (const auto& row : configs) {
      if (atoi(row[2].c_str()) == cfgGroup) {
        int valSize = strlen(row[1].c_str());
        if (valSize < sizeof(pwdHide)) {
          strncpy(pwdHide, FILLSTAR, valSize); 
          pwdHide[valSize] = 0;
        }
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

static bool checkConfigFile() {
  // check config file exists
  File file;
  if (!STORAGE.exists(CONFIG_FILE_PATH)) {
    // create from default in appGlobals.h
    file = fp.open(CONFIG_FILE_PATH, FILE_WRITE);
    if (file) {
      // apply initial defaults
      file.write((uint8_t*)appConfig, strlen(appConfig));
      sprintf(hostName, "%s_%012llX", APP_NAME, ESP.getEfuseMac());
      char cfg[100];
      sprintf(cfg, "appId~%s~99~~na\n", APP_NAME);
      file.write((uint8_t*)cfg, strlen(cfg));
      sprintf(cfg, "hostName~%s~%d~T~Device host name\n", hostName, HOSTNAME_GRP);
      file.write((uint8_t*)cfg, strlen(cfg));
      sprintf(cfg, "AP_SSID~%s~0~T~AP SSID name\n", hostName);
      file.write((uint8_t*)cfg, strlen(cfg));
      sprintf(cfg, "cfgVer~%u~99~T~na\n", CFG_VER);
      file.write((uint8_t*)cfg, strlen(cfg));
      file.close();
      LOG_INF("Created %s from local store", CONFIG_FILE_PATH);
      return true;
    } else {
      LOG_WRN("Failed to create file %s", CONFIG_FILE_PATH);
      return false;
    }
  }

  // file exists, check if valid
  bool goodFile = true;
  file = fp.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || !file.size()) {
    LOG_WRN("Failed to load file %s", CONFIG_FILE_PATH);
    goodFile = false;
  } else {
    // check file contents are valid
    loadConfigVect();
    if (!retrieveConfigVal("cfgVer", appId)) goodFile = false; // obsolete config file
    else if (atoi(appId) != CFG_VER) goodFile = false; // outdated config file
    if (!goodFile) LOG_WRN("Delete old %s", CONFIG_FILE_PATH);
    else {
      // cleanup storage if config file for different app
      retrieveConfigVal("appId", appId);
      if (strcmp(appId, APP_NAME)) {
        LOG_WRN("Delete invalid %s, expected %s, got %s", CONFIG_FILE_PATH, APP_NAME, appId);
        savePrefs(false);
        goodFile = false;
      }
    }
    configs.clear();
  }
  file.close();
  if (!goodFile) {
    deleteFolderOrFile(DATA_DIR);
    STORAGE.mkdir(DATA_DIR);
  }
  return goodFile;
}

bool loadConfig() {
  // called on startup
  LOG_INF("Load config");
  bool res = checkConfigFile();
  if (!res) res = checkConfigFile(); // to recreate file if deleted on first call
  if (res) {
    loadConfigVect();
    //showConfigVect();
    loadPrefs(); // overwrites any corresponding entries in config
    // load variables from stored config vector
    reloadConfigs();
    debugMemory("loadConfig");
    return true;
  }
  // no config file
  snprintf(startupFailure, SF_LEN, STARTUP_FAIL "No file: %s", CONFIG_FILE_PATH);
  return false;
}
