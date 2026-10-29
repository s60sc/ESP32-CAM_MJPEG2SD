
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
  - A : Action button
  - B : Radio Buttons B:lab1:lab2:etc
  - C : Checkbox (as slider)
  - D : Display only text
  - L : Binary string input
  - N : Number
  - R : Range (as slider) R:min:max:step
  - S : Select options S:lab1:lab2:etc
  - T : Editable Text
  - X : Text input field not updated by app

  s60sc 2022, 2024
*/

#include "appGlobals.h"
#include <charconv> // Required for std::from_chars

static std::vector<std::vector<std::string>> configs;
static Preferences prefs; 
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

static int getKeyPos(const std::string& thisKey) {
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
    // update value unless read only, eg button
    if (configs[keyPos][3] != "A") {
      if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); 
      configs[keyPos][1] = thisVal;
      if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
    }
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

static void loadVectItem(const std::string& keyValGrpLabel) {
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
  File file = STORAGE.open(CONFIG_FILE_PATH, FILE_WRITE);
  char configLine[FILE_NAME_LEN + 101];
  int cfgCnt = 0;
  if (!file) LOG_WRN("Failed to save to configs file");
  else {
    sort(configs.begin(), configs.end());
    configs.erase(unique(configs.begin(), configs.end()), configs.end()); // remove any dups
    for (const auto& row: configs) {
      // recreate config file with updated content
      if ((row[0].length() >= 5 && row[0].compare(row[0].length() - 5, 5, "_Pass") == 0))
        // replace passwords with asterisks
        snprintf(configLine, FILE_NAME_LEN + 100, "%s%c%.*s%c%s%c%s%c%s\n", row[0].c_str(), DELIM, (int)row[1].length(), FILLSTAR, DELIM, row[2].c_str(), DELIM, row[3].c_str(), DELIM, row[4].c_str());
      else snprintf(configLine, FILE_NAME_LEN + 100, "%s%c%s%c%s%c%s%c%s\n", row[0].c_str(), DELIM, row[1].c_str(), DELIM, row[2].c_str(), DELIM, row[3].c_str(), DELIM, row[4].c_str());
      file.write((uint8_t*)configLine, strlen(configLine));
      cfgCnt++;
    }
    LOG_ALT("Config file saved %d entries", cfgCnt);
  }
  file.close();
}

static bool loadConfigVect() {
  // force config vector into psram if available
  if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); 
  configs.reserve(MAX_CONFIGS);
  // extract each config line from file
  File file = STORAGE.open(CONFIG_FILE_PATH, FILE_READ);
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
#if INCLUDE_RTSP
  prefs.putString("RTSP_Pass", RTSP_Pass);
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
#if INCLUDE_RTSP
  prefs.getString("RTSP_Pass", RTSP_Pass, MAX_PWD_LEN);
#endif
  prefs.end();
  return true;
}

// Add compile-time MurmurHash3 function for safe string switching.
static constexpr uint32_t murmur3_scramble(uint32_t k) {
    k *= 0xcc9e2d51u;
    k = (k << 15u) | (k >> 17u);
    k *= 0x1b873593u;
    return k;
}

static constexpr uint32_t murmur3_32(const char* str) {
    uint32_t h = 0x9747b28cu; // Standard seed
    uint32_t k = 0u;
    uint32_t len = 0u;
    
    // Calculate string length at compile time.
    while (str[len] != '\0') {
        len++;
    }

    uint32_t i = 0u;
    // Process 4-byte blocks.
    for (; i + 4u <= len; i += 4u) {
        k = (uint32_t)str[i] | ((uint32_t)str[i + 1u] << 8u) | 
            ((uint32_t)str[i + 2u] << 16u) | ((uint32_t)str[i + 3u] << 24u);
        h ^= murmur3_scramble(k);
        h = (h << 13u) | (h >> 19u);
        h = h * 5u + 0xe6546b64u;
    }

    // Process remaining bytes.
    k = 0u;
    uint32_t rem = len & 3u;
    if (rem >= 3u) k ^= (uint32_t)str[i + 2u] << 16u;
    if (rem >= 2u) k ^= (uint32_t)str[i + 1u] << 8u;
    if (rem >= 1u) {
        k ^= (uint32_t)str[i];
        k = murmur3_scramble(k);
        h ^= k;
    }

    // Mix final hash value.
    h ^= len;
    h ^= h >> 16u;
    h *= 0x85ebca6bu;
    h ^= h >> 13u;
    h *= 0xc2b2ae35u;
    h ^= h >> 16u;

    return h;
}

void updateStatus(const char* variable, const char* _value, bool fromUser) {
  // called from controlHandler() to update app status from changes made on browser
  // or from loadConfig() to update app status from stored preferences
  
  // Defensive programming: prevent null pointer dereference
  if (variable == nullptr || _value == nullptr) {
    return;
  }

  bool res = true;
  char value[IN_FILE_NAME_LEN];
  strncpy(value, _value, sizeof(value));
  value[sizeof(value) - 1] = '\0'; // DEFECT FIX: Ensure null-termination

#if INCLUDE_MQTT
  if (mqtt_active) {
    // +2 for '=' and null terminator, preventing potential silent truncation
    char buff[(IN_FILE_NAME_LEN * 2) + 2];
    snprintf(buff, sizeof(buff), "%s=%s", variable, value);
    mqttPublishPath("state", buff);
  }
#endif

  int intVal = 0;
  const char* start = value;
  
  // Use std::from_chars for safe, locale-independent string-to-int conversion.
  // It stops at the first invalid character or the end of the buffer.
  std::from_chars(start, value + sizeof(value), intVal);

  // Runtime hash of the variable name determines the action.
  // The constexpr murmur3_32 function guarantees compile-time evaluation of these case labels.
  switch (murmur3_32(variable)) {
    case murmur3_32("hostName"):
      strncpy(hostName, value, MAX_HOST_LEN - 1);
      hostName[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_SSID"):
      strncpy(ST_SSID, value, MAX_HOST_LEN - 1);
      ST_SSID[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_Pass"):
      if (value[0] != '*') {
        strncpy(ST_Pass, value, MAX_PWD_LEN - 1);
        ST_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("ST_ip"):
      strncpy(ST_ip, value, MAX_IP_LEN - 1);
      ST_ip[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_gw"):
      strncpy(ST_gw, value, MAX_IP_LEN - 1);
      ST_gw[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_sn"):
      strncpy(ST_sn, value, MAX_IP_LEN - 1);
      ST_sn[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_ns1"):
      strncpy(ST_ns1, value, MAX_IP_LEN - 1);
      ST_ns1[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("ST_ns2"):
      strncpy(ST_ns2, value, MAX_IP_LEN - 1);
      ST_ns2[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("Auth_Name"):
      strncpy(Auth_Name, value, MAX_HOST_LEN - 1);
      Auth_Name[MAX_HOST_LEN - 1] = '\0';
      updateAuthCache(); // Immediately update the cached credentials
      break;
    case murmur3_32("Auth_Pass"):
      if (value[0] != '*') {
        strncpy(Auth_Pass, value, MAX_PWD_LEN - 1);
        Auth_Pass[MAX_PWD_LEN - 1] = '\0';
        updateAuthCache(); // Immediately update the cached credentials
      }
      break;
    case murmur3_32("AP_ip"):
      strncpy(AP_ip, value, MAX_IP_LEN - 1);
      AP_ip[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("AP_gw"):
      strncpy(AP_gw, value, MAX_IP_LEN - 1);
      AP_gw[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("AP_sn"):
      strncpy(AP_sn, value, MAX_IP_LEN - 1);
      AP_sn[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("AP_SSID"):
      strncpy(AP_SSID, value, MAX_HOST_LEN - 1);
      AP_SSID[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("AP_Pass"):
      if (value[0] != '*') {
        strncpy(AP_Pass, value, MAX_PWD_LEN - 1);
        AP_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("allowAP"):
      allowAP = (intVal != 0);
      break;
    case murmur3_32("useHttps"):
      useHttps = (intVal != 0);
      break;
    case murmur3_32("useSecure"):
      useSecure = (intVal != 0);
      break;
    case murmur3_32("doGetExtIP"):
      doGetExtIP = (intVal != 0);
      break;
    case murmur3_32("netMode"):
      netMode = intVal;
      break;
    case murmur3_32("ethCS"):
      ethCS = intVal;
      break;
    case murmur3_32("ethInt"):
      ethInt = intVal;
      break;
    case murmur3_32("ethRst"):
      ethRst = intVal;
      break;
    case murmur3_32("ethSclk"):
      ethSclk = intVal;
      break;
    case murmur3_32("ethMiso"):
      ethMiso = intVal;
      break;
    case murmur3_32("ethMosi"):
      ethMosi = intVal;
      break;
    case murmur3_32("extIP"):
      strncpy(extIP, value, MAX_IP_LEN - 1);
      extIP[MAX_IP_LEN - 1] = '\0';
      break;

#if INCLUDE_TGRAM
    case murmur3_32("tgramUse"):
      tgramUse = (intVal != 0);
      if (tgramUse) {
#if INCLUDE_SMTP
        smtpUse = false;
#endif
        updateConfigVect("smtpUse", "0");
      }
      break;
    case murmur3_32("tgramToken"):
      strncpy(tgramToken, value, MAX_PWD_LEN - 1);
      tgramToken[MAX_PWD_LEN - 1] = '\0';
      break;
    case murmur3_32("tgramChatId"):
      strncpy(tgramChatId, value, MAX_IP_LEN - 1);
      tgramChatId[MAX_IP_LEN - 1] = '\0';
      break;
#endif

#if INCLUDE_FTP_HFS
    case murmur3_32("fsServer"):
      strncpy(fsServer, value, MAX_HOST_LEN - 1);
      fsServer[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("fsPort"):
      fsPort = intVal;
      break;
    case murmur3_32("ftpUser"):
      strncpy(ftpUser, value, MAX_HOST_LEN - 1);
      ftpUser[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("FS_Pass"):
      if (value[0] != '*') {
        strncpy(FS_Pass, value, MAX_PWD_LEN - 1);
        FS_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("fsWd"):
      strncpy(fsWd, value, FILE_NAME_LEN - 1);
      fsWd[FILE_NAME_LEN - 1] = '\0';
      break;
    case murmur3_32("fsUse"):
      fsUse = (intVal != 0);
      break;
    case murmur3_32("autoUpload"):
      autoUpload = (intVal != 0);
      break;
    case murmur3_32("deleteAfter"):
      deleteAfter = (intVal != 0);
      break;
    case murmur3_32("useFtps"):
      useFtps = (intVal != 0);
      break;
#endif

#if INCLUDE_SMTP
    case murmur3_32("smtpUse"):
      smtpUse = (intVal != 0);
      if (smtpUse) {
#if INCLUDE_TGRAM
        tgramUse = false;
#endif
        updateConfigVect("tgramUse", "0");
      }
      break;
    case murmur3_32("smtp_login"):
      strncpy(smtp_login, value, MAX_HOST_LEN - 1);
      smtp_login[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("smtp_server"):
      strncpy(smtp_server, value, MAX_HOST_LEN - 1);
      smtp_server[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("smtp_email"):
      strncpy(smtp_email, value, MAX_HOST_LEN - 1);
      smtp_email[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("SMTP_Pass"):
      if (value[0] != '*') {
        strncpy(SMTP_Pass, value, MAX_PWD_LEN - 1);
        SMTP_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("smtp_port"):
      smtp_port = intVal;
      break;
    case murmur3_32("smtpMaxEmails"):
      alertMax = intVal;
      break;
#endif

#if INCLUDE_MQTT
    case murmur3_32("mqtt_active"):
      mqtt_active = (intVal != 0);
      if (!mqtt_active) {
        stopMqttClient();
      }
      break;
    case murmur3_32("mqtt_broker"):
      strncpy(mqtt_broker, value, MAX_HOST_LEN - 1);
      mqtt_broker[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("mqtt_port"):
      // DEFECT MITIGATION: Original code lacked guaranteed null-termination.
      strncpy(mqtt_port, value, 5);
      mqtt_port[4] = '\0'; 
      break;
    case murmur3_32("mqtt_user"):
      strncpy(mqtt_user, value, MAX_HOST_LEN - 1);
      mqtt_user[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("mqtt_user_Pass"):
      if (value[0] != '*') {
        strncpy(mqtt_user_Pass, value, MAX_PWD_LEN - 1);
        mqtt_user_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("mqtt_topic_prefix"):
      strncpy(mqtt_topic_prefix, value, (FILE_NAME_LEN / 2) - 1);
      mqtt_topic_prefix[(FILE_NAME_LEN / 2) - 1] = '\0';
      break;
#endif

#if INCLUDE_RTSP
    case murmur3_32("RTSP_Name"):
      strncpy(RTSP_Name, value, MAX_HOST_LEN - 1);
      RTSP_Name[MAX_HOST_LEN - 1] = '\0';
      break;
    case murmur3_32("RTSP_Pass"):
      if (value[0] != '*') {
        strncpy(RTSP_Pass, value, MAX_PWD_LEN - 1);
        RTSP_Pass[MAX_PWD_LEN - 1] = '\0';
      }
      break;
    case murmur3_32("rtsp00Video"):
      rtspVideo = streamVid = (intVal != 0);
      break;
    case murmur3_32("rtsp01Audio"):
      rtspAudio = streamAud = (intVal != 0);
      break;
    case murmur3_32("rtsp02Subtitles"):
      rtspSubtitles = streamSrt = (intVal != 0);
      break;
    case murmur3_32("rtsp03Port"):
      rtspPort = intVal;
      break;
    case murmur3_32("rtsp04VideoPort"):
      rtpVideoPort = intVal;
      break;
    case murmur3_32("rtsp05AudioPort"):
      rtpAudioPort = intVal;
      break;
    case murmur3_32("rtsp06SubtitlesPort"):
      rtpSubtitlesPort = intVal;
      break;
    case murmur3_32("rtsp07Ip"):
      strncpy(RTP_ip, value, MAX_IP_LEN - 1);
      RTP_ip[MAX_IP_LEN - 1] = '\0';
      break;
    case murmur3_32("rtsp08MaxC"):
      rtspMaxClients = intVal;
      break;
    case murmur3_32("rtsp09TTL"):
      rtpTTL = intVal;
      break;
#endif

    // Other settings
    case murmur3_32("clockUTC"):
      syncToBrowser((uint32_t)intVal);
      break;
    case murmur3_32("timezone"):
      strncpy(timezone, value, FILE_NAME_LEN - 1);
      timezone[FILE_NAME_LEN - 1] = '\0';
      break;
    case murmur3_32("ntpServer"):
      strncpy(ntpServer, value, FILE_NAME_LEN - 1);
      ntpServer[FILE_NAME_LEN - 1] = '\0';
      break;
    case murmur3_32("alarmHour"):
      alarmHour = (uint8_t)intVal;
      break;
    case murmur3_32("sdMinCardFreeSpace"):
      sdMinCardFreeSpace = intVal;
      break;
    case murmur3_32("sdFreeSpaceMode"):
      sdFreeSpaceMode = intVal;
      break;
    case murmur3_32("responseTimeoutSecs"):
      responseTimeoutSecs = intVal;
      break;
    case murmur3_32("wifiTimeoutSecs"):
      wifiTimeoutSecs = intVal;
      break;
    case murmur3_32("usePing"):
      usePing = (intVal != 0);
      break;
    case murmur3_32("dbgVerbose"):
      dbgVerbose = (intVal != 0);
      Serial.setDebugOutput(dbgVerbose);
      break;
    case murmur3_32("logType"):
      logType = intVal;
      remote_log_init();
      break;
    case murmur3_32("sdLog"):
      sdLog = (intVal != 0);
      remote_log_init();
      break;
    case murmur3_32("refreshVal"):
      refreshVal = intVal;
      break;
    case murmur3_32("formatIfMountFailed"):
      formatIfMountFailed = (intVal != 0);
      break;
    case murmur3_32("resetLog"):
      reset_log();
      break;
    case murmur3_32("clear"):
      savePrefs(false); // /control?clear=1
      break;
    case murmur3_32("deldata"):
      if (intVal != 0) {
        deleteFolderOrFile(DATA_DIR); // entire folder
      } else {
        // manually specified file, eg control?deldata=favicon.ico
        char delFile[FILE_NAME_LEN];
        int dlen = snprintf(delFile, FILE_NAME_LEN, "%s/%s", DATA_DIR, value);
        // DEFECT FIX: snprintf returns length excluding null terminator. 
        // If dlen >= FILE_NAME_LEN, the string was truncated and lacks null-termination.
        if (dlen >= FILE_NAME_LEN) {
          LOG_WRN("File name %s too long", value);
        } else {
          deleteFolderOrFile(delFile);
        }
      }
      // Preserved original behavior: restart occurs regardless of truncation warning
      doRestart("user requested restart after data deletion");
      break;
    case murmur3_32("showsys"):
      showSys();
      break;
    case murmur3_32("save"):
      if (intVal != 0) {
        savePrefs();
      }
      saveConfigVect();
      break;

    default:
      res = updateAppStatus(variable, value, fromUser);
      if (!res) {
        if (fromUser) {
          updateConfigVect(variable, value); // in case value previously set in different compilation state
          LOG_WRN("Unable to use %s as required cpp file not included", variable);
        } else {
          LOG_VRB("Unrecognised config: %s", variable);
        }
      }
      break;
  }

  if (res) {
    updateConfigVect(variable, value);
  }
}

void buildJsonString(uint8_t filter) {
  // called from statusHandler() to build json string with current status to return to browser 
  char* p = jsonBuff;
  *p++ = '{';
  if (filter < 2) {
    // build json string for main page refresh
    p = buildAppJsonString((bool)filter);
    p += sprintf(p, "\"cfgGroup\":\"-1\",");
    // generic footer
    currEpoch = getEpoch(); 
    p += sprintf(p, "\"clockUTC\":\"%lu\",", (uint32_t)currEpoch); 
    char timeBuff[20];
    strftime(timeBuff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
    p += sprintf(p, "\"clock\":\"%s\",", timeBuff);
    formatElapsedTime(timeBuff, millis()); // rolls over after 49.7 days due to max uint32
    p += sprintf(p, "\"up_time\":\"%s\",", timeBuff);   
    p += sprintf(p, "\"free_heap\":\"%s\",", fmtSize(ESP.getFreeHeap()));    
    p += sprintf(p, "\"wifi_rssi\":\"%i dBm\",", netRSSI() );  
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
#if INCLUDE_RTSP
      p += sprintf(p, "\"RTSP_Pass\":\"%.*s\",", strlen(RTSP_Pass), FILLSTAR);
#endif
      // session constants
      p += sprintf(p, "\"fw_version\":\"%s\",", APP_VER); 
      p += sprintf(p, "\"macAddressEfuse\":\"%012llX\",", ESP.getEfuseMac() ); 
      p += sprintf(p, "\"macAddressWiFi\":\"%s\",", netMacAddress().c_str() ); 
      p += sprintf(p, "\"extIP\":\"%s\",", extIP); 
      p += sprintf(p, "\"httpPort\":\"%u\",", HTTP_PORT); 
      p += sprintf(p, "\"httpsPort\":\"%u\",", HTTPS_PORT);
      p += sprintf(p, "\"ip\":\"%s\",", formatIPstr());
    }
  } else {
    // build json string for requested config group
    updateAppStatus("custom", "");
    uint8_t cfgGroup = filter - 10; // filter number is length of url query string, config group number is length of string - 10
    p += sprintf(p, "\"cfgGroup\":\"%u\",", cfgGroup);
    char pwdHide[MAX_PWD_LEN] = {0};  // used to replace password value with asterisks
    for (const auto& row : configs) {
      if (atoi(row[2].c_str()) == cfgGroup) {
        int valSize = (int)row[1].length();
        if (valSize < sizeof(pwdHide)) {
          strncpy(pwdHide, FILLSTAR, valSize); 
          pwdHide[valSize] = 0;
        }
        // for each config item, list - key:value, key:label text, key:type identifier
        p += sprintf(p, "\"%s\":\"%s\",\"lab%s\":\"%s\",\"typ%s\":\"%s\",", row[0].c_str(),
          row[0].find("_Pass") == std::string::npos ? row[1].c_str() : pwdHide, row[0].c_str(), 
          row[4].c_str(), row[0].c_str(), row[3].c_str()); 
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
    file = STORAGE.open(CONFIG_FILE_PATH, FILE_WRITE);
    if (file) {
      // apply initial defaults
      uint8_t* p = (uint8_t*)appConfig;
      int cfgLen = strlen(appConfig);
      while (cfgLen > 0) {
        int toWrite = min(512, cfgLen);
        file.write(p, toWrite);
        p += toWrite;
        cfgLen -= toWrite;
      }
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
  file = STORAGE.open(CONFIG_FILE_PATH, FILE_READ);
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
#if INCLUDE_CERTS
    loadCerts();
#else
    if (useHttps) {
      LOG_WRN("Need to compile with INCLUDE_CERTS true to use HTTPS");
      useHttps = false;
    }
#endif
    debugMemory("loadConfig");
    return true;
  }
  // no config file
  snprintf(startupFailure, SF_LEN, STARTUP_FAIL "No file: %s", CONFIG_FILE_PATH);
  return false;
}
