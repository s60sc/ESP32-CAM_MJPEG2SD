
/* 
  Management and storage of application configuration state.
  Configuration file stored on spiffs, except passwords which are stored in NVS
   
  Workflow:
  loadConfig:
    file -> loadConfigVect+loadKeyVal -> vector -> getNextKeyVal+updatestatus+updateAppStatus -> vars 
                                               retrieveConfigVal (as required)
  statusHandler:
    vector -> buildJsonString+buildAppJsonString -> browser 
  controlHandler: 
    browser -> updateStatus+updateAppStatus -> vars -> updateConfigVect -> vector -> saveConfigVect -> file 

  s60sc 2022
*/

#include "globals.h"

static fs::FS fp = STORAGE;
static std::vector<std::vector<std::string>> configs;
static Preferences prefs; 
char* jsonBuff = NULL;


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

int getKeyPos(std::string thisKey) {
  // get location of given key to retrieve other elements
  if (configs.empty()) return -1;
  auto lower = std::lower_bound(configs.begin(), configs.end(), thisKey, [](
    const std::vector<std::string> &a, const std::string &b) { 
    return a[0] < b;}
  );
  int keyPos = std::distance(configs.begin(), lower); 
  if (thisKey == configs[keyPos][0]) return keyPos;
  else LOG_DBG("Key %s not found", thisKey.c_str());
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

static void loadVect(const std::string keyValGrpLabel) {
  // extract a config tokens from input and load into configs vector
  // comprises key : val : group : label
  std::string token[4];
  int i = 0;
  if (keyValGrpLabel.length()) {
    std::istringstream ss(keyValGrpLabel);
    while(std::getline(ss, token[i++], ':'));
    if (i != 5) LOG_ERR("Unable to parse '%s', len %u", keyValGrpLabel.c_str(), keyValGrpLabel.length());
    else {
      if (!ALLOW_SPACES) token[1].erase(std::remove(token[1].begin(), token[1].end(), ' '), token[1].end());
      if (token[3][token[3].size() - 1] == '\r') token[3].erase(token[3].size() - 1);
      configs.push_back({token[0], token[1], token[2], token[3]});
    }
  }
}

static void saveConfigVect() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_WRITE);
  char configLine[100];
  if (!file) LOG_ERR("Failed to save to configs file");
  else {
    for (const auto& row: configs) {
      // recreate config file with updated content
      sprintf(configLine, "%s:%s:%s:%s\n", row[0].c_str(), row[1].c_str(), row[2].c_str(), row[3].c_str());
      file.write((uint8_t*)configLine, strlen(configLine));
    }
    file.close();
    LOG_INF("Config file saved");
  }
}

static bool loadConfigVect() {
  File file = fp.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file || !file.size()) {
    LOG_ERR("Failed to load file %s", CONFIG_FILE_PATH);
    if (!file.size()) STORAGE.remove(CONFIG_FILE_PATH);
    return false;
  } else {
    // force vector into psram if available
    if (psramFound()) heap_caps_malloc_extmem_enable(0); 
    configs.reserve(MAX_CONFIGS);
    // extract each config line from file
    while (true) {
      String configLineStr = file.readStringUntil('\n');
      if (!configLineStr.length()) break;
      loadVect(configLineStr.c_str());
    } 
    // sort vector by key (element 0 in row)
    std::sort(configs.begin(), configs.end(), [] (
      const std::vector<std::string> &a, const std::vector<std::string> &b) {
      return a[0] < b[0];}
    );
    // return malloc to default 
    if (psramFound()) heap_caps_malloc_extmem_enable(4096);
    file.close();
  }
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
    LOG_ERR("Cleared preferences");
    return true;
  }
  prefs.putString("st_ssid", ST_SSID);
  prefs.putString("st_pass", ST_Pass);
  prefs.putString("ap_pass", AP_Pass); 
  prefs.putString("auth_pass", Auth_Pass); 
#ifdef INCLUDE_FTP          
  prefs.putString("ftp_pass", ftp_pass);
#endif
#ifdef INCLUDE_SMTP
  prefs.putString("smtp_pass", smtp_pass);
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
  prefs.getString("st_ssid", ST_SSID, 32);
  prefs.getString("st_pass", ST_Pass, MAX_PWD_LEN);
  prefs.getString("ap_pass", AP_Pass, MAX_PWD_LEN); 
  prefs.getString("auth_pass", Auth_Pass, MAX_PWD_LEN); 
#ifdef INCLUDE_FTP
  prefs.getString("ftp_pass", ftp_pass, MAX_PWD_LEN);
#endif
#ifdef INCLUDE_SMTP
  prefs.getString("smtp_pass", smtp_pass, MAX_PWD_LEN);
#endif
  prefs.end();
  return true;
}

bool updateStatus(const char* variable, const char* _value) {
  // called from controlHandler() to update app status from changes made on browser
  // or from loadConfig() to update app status from stored preferences
  bool res = true;
  char value[MAX_PWD_LEN];
  strcpy(value, _value);
  int intVal = atoi(value); 
  if (!strcmp(variable, "hostName")) strcpy(hostName, value);
  else if(!strcmp(variable, "ST_SSID")) strcpy(ST_SSID, value);
  else if(!strcmp(variable, "ST_Pass")) {
    strcpy(ST_Pass, value);
    // dont store actual password in configs.txt
    strncpy(value, FILLSTAR, strlen(value));
  }
  else if(!strcmp(variable, "ST_ip")) strcpy(ST_ip, value);
  else if(!strcmp(variable, "ST_gw")) strcpy(ST_gw, value);
  else if(!strcmp(variable, "ST_sn")) strcpy(ST_sn, value);
  else if(!strcmp(variable, "ST_ns1")) strcpy(ST_ns1, value);
  else if(!strcmp(variable, "ST_ns1")) strcpy(ST_ns2, value);
  else if(!strcmp(variable, "Auth_User")) strcpy(Auth_User, value);
  else if(!strcmp(variable, "Auth_Pass")) {
    strcpy(Auth_Pass, value);
    strncpy(value, FILLSTAR, strlen(value));;
  }
#ifdef INCLUDE_FTP
  else if(!strcmp(variable, "ftp_server")) strcpy(ftp_server, value);
  else if(!strcmp(variable, "ftp_port")) ftp_port = intVal;
  else if(!strcmp(variable, "ftp_user")) strcpy(ftp_user, value);
  else if(!strcmp(variable, "ftp_pass")) {
    strcpy(ftp_pass, value);
    strncpy(value, FILLSTAR, strlen(value));
  }
  else if(!strcmp(variable, "ftp_wd")) strcpy(ftp_wd, value);
#endif
#ifdef INCLUDE_SMTP
  else if(!strcmp(variable, "smtpUse")) smtpUse = (bool)intVal;
  else if(!strcmp(variable, "smtp_login")) strcpy(smtp_login, value);
  else if(!strcmp(variable, "smtp_server")) strcpy(smtp_server, value);
  else if(!strcmp(variable, "smtp_email")) strcpy(smtp_email, value);
  else if(!strcmp(variable, "smtp_pass")) {
    strcpy(smtp_pass, value);
    strncpy(value, FILLSTAR, strlen(value));
  }
  else if(!strcmp(variable, "smtp_port")) smtp_port = intVal;
#endif
  else if(!strcmp(variable, "responseTimeoutSecs")) responseTimeoutSecs = intVal;
  else if(!strcmp(variable, "allowAP")) allowAP = intVal;
  else if(!strcmp(variable, "AP_Pass")) {
    strcpy(AP_Pass, value);
    strncpy(value, FILLSTAR, strlen(value));
  }
  else if(!strcmp(variable, "wifiTimeoutSecs")) wifiTimeoutSecs = intVal;
  // update after passwords obfuscated
  updateConfigVect(variable, value); 
  updateAppStatus(variable, value);
  
  if(!strcmp(variable, "dbgVerbose")) {
    dbgVerbose = (intVal) ? true : false;
    Serial.setDebugOutput(dbgVerbose);
  } 
  else if(!strcmp(variable, "useIOextender")) useIOextender = (bool)intVal; 
  else if(!strcmp(variable, "logMode")) {
    logMode = (bool)intVal; 
    remote_log_init();
  }
  else if(!strcmp(variable, "refreshVal")) refreshVal = intVal * 1000; 
  else if(!strcmp(variable, "resetLog")) reset_log(); 
  else if(!strcmp(variable, "reset")) res = false;
  else if(!strcmp(variable, "clear")) savePrefs(false); // /control?clear=1
  else if(!strcmp(variable, "deldata")) {  
    if ((fs::SPIFFSFS*)&STORAGE == &SPIFFS) startSpiffs(true); // entire folder
    else {
      // SD card
      if (intVal) deleteFolderOrFile(DATA_DIR); // entire folder
      else {
        // specified file, eg control?deldata=configs.txt
        char delFile[FILE_NAME_LEN];
        int dlen = snprintf(delFile, FILE_NAME_LEN, "%s/%s", DATA_DIR, value);
        if (dlen > FILE_NAME_LEN) LOG_ERR("File name %s too long", value);
        deleteFolderOrFile(delFile);
      }
    }
    res = false;
  }
  else if (!strcmp(variable, "save")) {
    savePrefs();
    saveConfigVect();
  } 
  return res;
}

void buildJsonString(uint8_t filter) {
  // called from statusHandler() to build json string with current status to return to browser 
  char* p = jsonBuff;
  *p++ = '{';
  if (filter < 2) {
    // build json string for main page refresh
    buildAppJsonString((bool)filter);
    p += strlen(jsonBuff) - 1;
    if (!filter) {
      // populate first part of json string from config vect
      for (const auto& row : configs) 
        p += sprintf(p, "\"%s\":\"%s\",", row[0].c_str(), row[1].c_str());
      // stored on NVS 
      p += sprintf(p, "\"ST_Pass\":\"%.*s\",",strlen(ST_Pass), FILLSTAR);
      p += sprintf(p, "\"AP_Pass\":\"%.*s\",",strlen(AP_Pass), FILLSTAR);
      p += sprintf(p, "\"Auth_Pass\":\"%.*s\",",strlen(Auth_Pass), FILLSTAR);
  #ifdef INCLUDE_FTP 
      p += sprintf(p, "\"ftp_pass\":\"%.*s\",",strlen(ftp_pass), FILLSTAR);
  #endif
  #ifdef INCLUDE_SMTP
      p += sprintf(p, "\"smtp_pass\":\"%.*s\",",strlen(smtp_pass), FILLSTAR);
  #endif
      // other
      p += sprintf(p, "\"fw_version\":\"%s\",", APP_VER); 
    }
  } else {
    // build json string for requested config group
    uint8_t cfgGroup = filter - 10;
    for (const auto& row : configs) {
      if (atoi(row[2].c_str()) == cfgGroup) {
        p += sprintf(p, "\"lab%s\":\"%s\",\"%s\":\"%s\",", 
          row[0].c_str(), row[3].c_str(), row[0].c_str(), row[1].c_str()); 
      }
    }
  }
  *p = 0;
  *(--p) = '}'; // overwrite final comma
}

bool loadConfig() {
  // called on startup
  LOG_INF("Load config");
  if (jsonBuff == NULL) {
    jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN); 
  }

  if (!loadConfigVect()) {
    loadPrefs();
    return false;
  }
  // set default hostname if config is null
  AP_SSID.toUpperCase();
  retrieveConfigVal("hostName", hostName);
  if (!strlen(hostName)) {
    strcpy(hostName, AP_SSID.c_str());
    updateStatus("hostName", hostName);
  }

  // ST_SSID value priority: configs > prefs > hardcoded
  retrieveConfigVal("ST_SSID", jsonBuff);
  if (!strlen(jsonBuff)) {
    // not defined in configs, use retrieved prefs 
    if (strlen(ST_SSID)) updateConfigVect("ST_SSID", ST_SSID);
    else {
      // not in prefs, use hardcoded values
      updateConfigVect("ST_SSID", ST_SSID);
      updateConfigVect("ST_ip", ST_ip);
      updateConfigVect("ST_gw", ST_gw);
      updateConfigVect("ST_sn", ST_sn);
      updateConfigVect("ST_ns1", ST_ns1);
      updateConfigVect("ST_ns1", ST_ns2);
    }
  }
  // load variables from stored config vector
  char variable[32] = {0,};
  char value[FILE_NAME_LEN] = {0,};
  while (getNextKeyVal(variable, value)) updateStatus(variable, value);
  loadPrefs();
  if (strlen(ST_SSID)) LOG_INF("Using ssid: %s%s %s", ST_SSID, !strlen(ST_ip) ? " " : " with static ip ", ST_ip);
  return true;
}
