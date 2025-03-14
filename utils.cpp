
// General utilities not specific to this app to support:
// - wifi
// - NTP
// - remote logging
// - base64 encoding
// - device sleep
//
// s60sc 2021, 2023
// some functions based on code contributed by gemi254

#include "appGlobals.h"

bool dbgVerbose = false;
bool timeSynchronized = false;
bool monitorOpen = true;
bool dataFilesChecked = false;
// allow any startup failures to be reported via browser for remote devices
char startupFailure[SF_LEN] = {0};
size_t alertBufferSize = 0;
size_t maxAlertBuffSize = 32 * 1024;
byte* alertBuffer = NULL; // buffer for telegram / smtp alert image
RTC_NOINIT_ATTR uint32_t crashLoop;
RTC_NOINIT_ATTR char brownoutStatus;
static void initBrownout(void);
int wakePin; // if wakeUse is true
bool wakeUse = false; // true to allow app to sleep and wake
char* jsonBuff = NULL;

/************************** Wifi **************************/

#include <esp_task_wdt.h>
 
/** Do not hard code anything below here unless you know what you are doing **/
/** Use the web interface to configure wifi settings **/

char hostName[MAX_HOST_LEN] = ""; // Default Host name
char ST_SSID[MAX_HOST_LEN]  = ""; //Default router ssid
char ST_Pass[MAX_PWD_LEN] = ""; //Default router passd

// leave following blank for dhcp
char ST_ip[MAX_IP_LEN]  = ""; // Static IP
char ST_sn[MAX_IP_LEN]  = ""; // subnet normally 255.255.255.0
char ST_gw[MAX_IP_LEN]  = ""; // gateway to internet, normally router IP
char ST_ns1[MAX_IP_LEN] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[MAX_IP_LEN] = ""; // alternative DNS Server, can be blank

// Access point Config Portal SSID and Pass
char AP_SSID[MAX_HOST_LEN] = "";
char AP_Pass[MAX_PWD_LEN] = "";
char AP_ip[MAX_IP_LEN]  = ""; // Leave blank to use 192.168.4.1
char AP_sn[MAX_IP_LEN]  = "";
char AP_gw[MAX_IP_LEN]  = "";

// basic HTTP Authentication access to web page
char Auth_Name[MAX_HOST_LEN] = ""; 
char Auth_Pass[MAX_PWD_LEN] = "";

int responseTimeoutSecs = 10; // time to wait for FTP or SMTP response
bool allowAP = true;  // set to true to allow AP to startup if cannot connect to STA (router)
uint32_t wifiTimeoutSecs = 30; // how often to check wifi status
static bool APstarted = false;
esp_ping_handle_t pingHandle = NULL;
bool usePing = true;

static void startPing();
static void printGpioInfo();

static void setupMdnsHost() {  
  // set up MDNS service 
  char mdnsName[MAX_IP_LEN]; // max mdns host name length
  snprintf(mdnsName, MAX_IP_LEN, "%.*s", MAX_IP_LEN - 1, hostName);
  if (MDNS.begin(mdnsName)) {
    // Add service to MDNS
    MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("https", "tcp", HTTPS_PORT);
    //MDNS.addService("ws", "udp", 83);
    //MDNS.addService("ftp", "tcp", 21);    
    LOG_INF("mDNS service: http://%s.local", mdnsName);
  } else LOG_WRN("mDNS host: %s Failed", mdnsName);
  debugMemory("setupMdnsHost");
}

static const char* wifiStatusStr(wl_status_t wlStat) {
  switch (wlStat) {
    case WL_NO_SHIELD: return "wifi not initialised";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "not available, use AP";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "unable to connect";  
    case WL_STOPPED: return "wifi stopped";
    default: return "Invalid WiFi.status";
  }
}

const char* getEncType(int ssidIndex) {
  switch (WiFi.encryptionType(ssidIndex)) {
    case (WIFI_AUTH_OPEN): return "Open";
    case (WIFI_AUTH_WEP): return "WEP";
    case (WIFI_AUTH_WPA_PSK): return "WPA_PSK";
    case (WIFI_AUTH_WPA2_PSK): return "WPA2_PSK";
    case (WIFI_AUTH_WPA_WPA2_PSK): return "WPA_WPA2_PSK";
    case (WIFI_AUTH_WPA2_ENTERPRISE): return "WPA2_ENTERPRISE";
    case (WIFI_AUTH_MAX): return "AUTH_MAX";
    default: return "Not listed";
  }
}

static void onWiFiEvent(WiFiEvent_t event) {
  // callback to report on wifi events
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY: break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE: break;
    case ARDUINO_EVENT_WIFI_STA_START: LOG_INF("Wifi Station started, connecting to: %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_STA_STOP: LOG_INF("Wifi Station stopped %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_AP_START: {
      if (strlen(AP_SSID) && !strcmp(WiFi.softAPSSID().c_str(), AP_SSID)) {
        LOG_INF("Wifi AP SSID: %s started, use '%s://%s' to connect", WiFi.softAPSSID().c_str(), useHttps ? "https" : "http", WiFi.softAPIP().toString().c_str());
        APstarted = true;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STOP: {
      if (!strcmp(WiFi.softAPSSID().c_str(), AP_SSID)) {
        LOG_INF("Wifi AP stopped: %s", AP_SSID);
        APstarted = false;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: LOG_INF("Wifi Station IP, use '%s://%s' to connect", useHttps ? "https" : "http", WiFi.localIP().toString().c_str()); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: LOG_INF("Wifi Station lost IP"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: LOG_INF("WiFi Station connection to %s, using hostname: %s", ST_SSID, hostName); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: LOG_INF("WiFi Station disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: LOG_INF("WiFi AP client connection"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: LOG_INF("WiFi AP client disconnection"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6: LOG_INF("AP interface V6 IP addr is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6: LOG_INF("Station interface V6 IP addr is preferred"); break;
    default: LOG_WRN("WiFi Unhandled event %d", event); break;
  }
}

static void setWifiAP() {
  if (!APstarted) {
    // Set access point with static ip if provided
    if (strlen(AP_ip) > 1) {
      LOG_INF("Set AP static IP :%s, %s, %s", AP_ip, AP_gw, AP_sn);  
      IPAddress _ip, _gw, _sn, _ns1 ,_ns2;
      _ip.fromString(AP_ip);
      _gw.fromString(AP_gw);
      _sn.fromString(AP_sn);
      // set static ip
      WiFi.softAPConfig(_ip, _gw, _sn);
    } 
    WiFi.softAP(AP_SSID, AP_Pass);
    debugMemory("setWifiAP");
  }
}

static void setWifiSTA() {
  // set station with static ip if provided
  if (strlen(ST_ip) > 1) {
    IPAddress _ip, _gw, _sn, _ns1, _ns2;
    if (!_ip.fromString(ST_ip)) LOG_WRN("Failed to parse IP: %s", ST_ip);
    else {
      _ip.fromString(ST_ip);
      _gw.fromString(ST_gw);
      _sn.fromString(ST_sn);
      _ns1.fromString(ST_ns1);
      _ns2.fromString(ST_ns2);
      // set static ip
      WiFi.config(_ip, _gw, _sn, _ns1); // need DNS for SNTP
      LOG_INF("Wifi Station set static IP");
    } 
  } else LOG_INF("Wifi Station IP from DHCP");
  WiFi.enableIPv6(USE_IP6); 
  WiFi.begin(ST_SSID, ST_Pass);
  debugMemory("setWifiSTA");
}

bool startWifi(bool firstcall) {
  // start wifi station (and wifi AP if allowed or station not defined)
  if (firstcall) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false); // prevent the flash storage WiFi credentials
    WiFi.setAutoReconnect(false); // Set whether module will attempt to reconnect to an access point in case it is disconnected
    WiFi.softAPdisconnect(true); // kill rogue AP on startup
    WiFi.setHostname(hostName);
    delay(100);
    WiFi.onEvent(onWiFiEvent);
  }
  setWifiSTA();
  // connect to Wifi station
  uint32_t startAttemptTime = millis();
  // Stop trying on failure timeout, will try to reconnect later by ping
  wl_status_t wlStat = WL_NO_SSID_AVAIL;
  if (strlen(ST_SSID)) {
    while (wlStat = WiFi.status(), wlStat != WL_CONNECTED && millis() - startAttemptTime < 5000)  {
      logPrint(".");
      delay(500);
    }
  }
  if (wlStat == WL_NO_SSID_AVAIL || allowAP) setWifiAP(); // AP allowed if no Station SSID eg on first time use 
  if (wlStat != WL_CONNECTED) LOG_WRN("SSID %s not connected %s", ST_SSID, wifiStatusStr(wlStat));
#if CONFIG_IDF_TARGET_ESP32S3
  setupMdnsHost(); // not on ESP32 as uses 6k of heap
#endif
  // show stats of requested SSID
  int numNetworks = WiFi.scanNetworks();
  for (int i=0; i < numNetworks; i++) {
    if (!strcmp(WiFi.SSID(i).c_str(), ST_SSID))
      LOG_INF("Wifi stats for %s - signal strength: %d dBm; Encryption: %s; channel: %u",  ST_SSID, WiFi.RSSI(i), getEncType(i), WiFi.channel(i));
  }
  if (pingHandle == NULL) startPing();
  return wlStat == WL_CONNECTED ? true : false;
}

void resetWatchDog() {
  // use ping task as watchdog in case of freeze
  static bool watchDogStarted = false;
  if (watchDogStarted) esp_task_wdt_reset();
  else {
    // setup watchdog on first call
    esp_task_wdt_deinit(); 
    esp_task_wdt_config_t twdt_config = {
      .timeout_ms = (wifiTimeoutSecs * 1000 * 2),
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true, // panic abort on watchdog alert (contains wdt_isr)
    };
    esp_task_wdt_init(&twdt_config);
    esp_task_wdt_add(NULL);
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      watchDogStarted = true;
      esp_task_wdt_reset();
      LOG_INF("WatchDog started using task: %s", pcTaskGetName(NULL));
    } else LOG_ERR("WatchDog failed to start");
  }
}

static void statusCheck() {
  // regular status checks
  doAppPing();
  if (!timeSynchronized) getLocalNTP();
  if (!dataFilesChecked) dataFilesChecked = checkDataFiles();
#if INCLUDE_MQTT
  if (mqtt_active) startMqttClient();
#endif
}

void resetCrashLoop() {
  crashLoop = 0;
}

static void pingSuccess(esp_ping_handle_t hdl, void *args) {
  //uint32_t elapsed_time;
  //esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
  if (DEBUG_MEM) {
    static uint32_t minStack = UINT32_MAX;
    uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (freeStack < minStack) {
      minStack = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Task ping stack space only: %u", freeStack);
      else LOG_INF("Task ping stack space reduced to: %u", freeStack);
    }
  }
  resetWatchDog();
  if (dataFilesChecked) resetCrashLoop();
  statusCheck();
}

static void pingTimeout(esp_ping_handle_t hdl, void *args) {
  // a ping check is used because esp may maintain a connection to gateway which may be unuseable, which is detected by ping failure
  // but some routers may not respond to ping - https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/221
  // so setting usePing to false ignores ping failure if connection still present
  resetWatchDog();
  if (strlen(ST_SSID)) {
    wl_status_t wStat = WiFi.status();
    if (wStat != WL_NO_SSID_AVAIL && wStat != WL_NO_SHIELD) {
      if (usePing) {
        LOG_WRN("Failed to ping gateway, restart wifi ...");
        startWifi(false);
      } else {
        if (wStat == WL_CONNECTED) statusCheck(); // treat as ok
        else {
          LOG_WRN("Disconnected, restart wifi ...");
          startWifi(false);
        }
      }
    }
  }
}

static void startPing() {
  IPAddress ipAddr = WiFi.gatewayIP();
  ip_addr_t pingDest; 
  IP_ADDR4(&pingDest, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  esp_ping_config_t pingConfig = ESP_PING_DEFAULT_CONFIG();
  pingConfig.target_addr = pingDest;  
  pingConfig.count = ESP_PING_COUNT_INFINITE;
  pingConfig.interval_ms = wifiTimeoutSecs * 1000;
  pingConfig.timeout_ms = 5000;
  pingConfig.task_stack_size = PING_STACK_SIZE;
  pingConfig.task_prio = 1;
  // set ping task callback functions 
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = pingSuccess;
  cbs.on_ping_timeout = pingTimeout;
  cbs.on_ping_end = NULL; 
  cbs.cb_args = NULL;
  esp_ping_new_session(&pingConfig, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  LOG_INF("Started ping monitoring - %s", usePing ? "On" : "Off");
  debugMemory("startPing");
}

void stopPing() {
  if (pingHandle != NULL) {
    esp_ping_stop(pingHandle);
    esp_ping_delete_session(pingHandle);
    pingHandle = NULL;
  }
}

#define EXT_IP_HOST "api.ipify.org"
char extIP[MAX_IP_LEN] = "Not assigned"; // router external IP
bool doGetExtIP = true;

void getExtIP() {
  // Get external IP address
  if (doGetExtIP) { 
    NetworkClientSecure hclient;
    if (remoteServerConnect(hclient, EXT_IP_HOST, HTTPS_PORT, "", GETEXTIP)) {
      HTTPClient https;
      int httpCode = HTTP_CODE_NOT_FOUND;
      if (https.begin(hclient, EXT_IP_HOST, HTTPS_PORT, "/", true)) {
        char newExtIp[MAX_IP_LEN] = "";
        httpCode = https.GET();
        if (httpCode == HTTP_CODE_OK) {
          strncpy(newExtIp, https.getString().c_str(), sizeof(newExtIp) - 1);  
          if (strcmp(newExtIp, extIP)) {
            // external IP changed
            strncpy(extIP, newExtIp, sizeof(extIP) - 1);
            updateStatus("extIP", extIP);
            updateStatus("save", "0");
            externalAlert("External IP changed", extIP);
          } else LOG_INF("External IP: %s", extIP);
        } else LOG_WRN("External IP request failed, error: %s", https.errorToString(httpCode).c_str());    
        if (httpCode != HTTP_CODE_OK) doGetExtIP = false;
        https.end();     
      }
      remoteServerClose(hclient);
    }
  }
}

/************** generic NetworkClientSecure functions ******************/

static uint8_t failCounts[REMFAILCNT] = {0};

void remoteServerClose(NetworkClientSecure& sclient) {
  if (sclient.available()) sclient.clear();
  if (sclient.connected()) sclient.stop();
}

bool remoteServerConnect(NetworkClientSecure& sclient, const char* serverName, uint16_t serverPort, const char* serverCert, uint8_t connIdx) {
  // Connect to server if not already connected or previously disconnected
  if (sclient.connected()) return true;
  else {
    if (failCounts[connIdx] >= MAX_FAIL) {
      if (failCounts[connIdx] == MAX_FAIL) {
        LOG_ERR("Abandon %s connection attempt until next rollover", serverName);
        failCounts[connIdx] = MAX_FAIL + 1;
      }
    } else {
      if (ESP.getFreeHeap() > TLS_HEAP) {
        // not connected, so try for a period of time
        if (useSecure && strlen(serverCert)) sclient.setCACert(serverCert);
        else sclient.setInsecure(); // no cert check
    
        uint32_t startTime = millis();
        while (!sclient.connected()) {
          if (sclient.connect(serverName, serverPort)) break;
          if (millis() - startTime > responseTimeoutSecs * 1000) break;
          delay(2000);
        }
        if (sclient.connected()) {
          failCounts[connIdx] = 0;
          return true;
        }
        else {
          // failed to connect in allocated time
          // 'Memory allocation failed' indicates lack of heap space
          // 'Generic error' can indicate DNS failure
          char errBuf[100] = "Unknown server error";
          int errNum = sclient.lastError(errBuf, sizeof(errBuf));
          LOG_WRN("Timed out connecting to server: %s, Err: %d, %s", serverName, errNum, errBuf);
        }
      } else LOG_WRN("Insufficient heap %s for %s TLS session", fmtSize(ESP.getFreeHeap()), serverName);
      failCounts[connIdx]++;
    }
  }
  return false;
}

void remoteServerReset() {
  // reset fail counts
  for (uint8_t i = 0; i < REMFAILCNT; i++) failCounts[i] = 0;
}

/************************** NTP  **************************/

// Needs to be a time zone string from: https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv
char timezone[FILE_NAME_LEN] = "GMT0";
char ntpServer[MAX_HOST_LEN] = "pool.ntp.org";
uint8_t alarmHour = 1;

time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  // construct filename from date/time
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}

static void showLocalTime(const char* timeSrc) {
  time_t currEpoch = getEpoch();
  char timeFormat[20];
  strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
  LOG_INF("Got current time from %s: %s with tz: %s", timeSrc, timeFormat, timezone);
  timeSynchronized = true;
}

bool getLocalNTP() {
  // get current time from NTP server and apply to ESP32
  LOG_INF("Using NTP server: %s", ntpServer);
  configTzTime(timezone, ntpServer);
  if (getEpoch() > 10000) {
    showLocalTime("NTP");    
    return true;
  }
  else {
    LOG_WRN("Not yet synced with NTP");
    return false;
  }
}

void syncToBrowser(uint32_t browserUTC) {
  // Synchronize to browser clock if out of sync
  if (!timeSynchronized) {
    struct timeval tv;
    tv.tv_sec = browserUTC;
    settimeofday(&tv, NULL);
    setenv("TZ", timezone, 1);
    tzset();
    showLocalTime("browser");
  }
}

void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays) {
  // elapsed time that app has been running
  uint32_t secs = timeVal / 1000; //convert milliseconds to seconds
  uint32_t mins = secs / 60; //convert seconds to minutes
  uint32_t hours = mins / 60; //convert minutes to hours
  uint32_t days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the converted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the converted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the converted hours to days in order to display 23 hours max
  if (noDays) sprintf(timeStr, "%02lu:%02lu:%02lu", hours, mins, secs);
  else sprintf(timeStr, "%lu-%02lu:%02lu:%02lu", days, hours, mins, secs);
}

static time_t setAlarm(uint8_t alarmHour) {
  // calculate future alarm datetime based on current datetime
  // ensure relevant timezone identified (default GMT0)
  time_t currEpoch = getEpoch();
  struct tm* timeinfo = localtime(&currEpoch);
  // set alarm date & time for next given hour
  int nextDay = 0; // try same day then next day
  do {
    timeinfo->tm_mday += nextDay;
    timeinfo->tm_hour = alarmHour;
    timeinfo->tm_min = 0;
    timeinfo->tm_sec = 0;
    nextDay = 1;
  } while (mktime(timeinfo) < getEpoch());
  char inBuff[30];
  strftime(inBuff, sizeof(inBuff), "%d/%m/%Y %H:%M:%S", timeinfo);
  LOG_INF("Alarm scheduled at %s", inBuff);
  // return future alarm time as epoch seconds
  return mktime(timeinfo);
}

bool checkAlarm() {
  // call from appPing() to check if daily alarm time at given hour has occurred
  static time_t rolloverEpoch = 0;
  if (timeSynchronized && getEpoch() >= rolloverEpoch) {
    // alarm time reached
    rolloverEpoch = setAlarm(alarmHour); // set next alarm time
    return true;
  }
  return false;
}

/********************** misc functions ************************/

bool changeExtension(char* fileName, const char* newExt) {
  // replace original file extension with supplied extension (buffer must be large enough)
  size_t inNamePtr = strlen(fileName);
  // find '.' before extension text
  while (inNamePtr > 0 && fileName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(fileName + inNamePtr, newExt, extLen);
  fileName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

void showProgress(const char* marker) {
  // show progess as dots 
  static uint8_t dotCnt = 0;
  logPrint(marker); // progress marker
  if (++dotCnt >= DOT_MAX) {
    dotCnt = 0;
    logLine();
  }
}

bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress) {
  // calculate percentage progress, only report back on percentReport boundary
  uint8_t percentage = (progressVal * 100) / totalVal;
  if (percentage >= pcProgress + percentReport) {
    pcProgress = percentage;
    return true;
  } else return false;
}

bool urlEncode(const char* inVal, char* encoded, size_t maxSize) {
  int encodedLen = 0;
  char hexTable[] = "0123456789ABCDEF";
  while (*inVal) {
    if (isalnum(*inVal) || strchr("$-_.+!*'(),:@~#", *inVal)) *encoded++ = *inVal;
    else {
      encodedLen += 3; 
      if (encodedLen >= maxSize) return false;  // Buffer overflow
      *encoded++ = '%';
      *encoded++ = hexTable[(*inVal) >> 4];
      *encoded++ = hexTable[*inVal & 0xf];
    }
    inVal++;
  }
  *encoded = 0;
  return true;
}

void urlDecode(char* inVal) {
  // replace url encoded characters
  std::string decodeVal(inVal); 
  std::string replaceVal = decodeVal;
  std::smatch match; 
  while (regex_search(decodeVal, match, std::regex("(%)([0-9A-Fa-f]{2})"))) {
    std::string s(1, static_cast<char>(std::strtoul(match.str(2).c_str(),nullptr,16))); // hex to ascii 
    replaceVal = std::regex_replace(replaceVal, std::regex(match.str(0)), s);
    decodeVal = match.suffix().str();
  }
  strcpy(inVal, replaceVal.c_str());
}

void listBuff (const uint8_t* b, size_t len) {
  // output buffer content as hex, 16 bytes per line
  if (!len || !b) LOG_WRN("Nothing to print");
  else {
    for (size_t i = 0; i < len; i += 16) {
      int linelen = (len - i) < 16 ? (len - i) : 16;
      for (size_t k = 0; k < linelen; k++) logPrint(" %02x", b[i+k]);
      puts(" ");
    }
  }
}

size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) {
  // find a subarray (needle) in another array (haystack)
  size_t h = 0, n = 0; // Two pointers to traverse the arrays
  // Traverse both arrays simultaneously
  while (h < hSize && n < nSize) {
    // If element matches, increment both pointers
    if (haystack[h] == needle[n]) {
      h++;
      n++;
      // If needle is completely traversed
      if (n == nSize) return h; // position of end of needle
    } else {
      // if not, increment h and reset n
      h = h - n + 1;
      n = 0;
    }
  }
  return 0; // not found
}

void removeChar(char* s, char c) {
  // remove specified character from string
  int writer = 0, reader = 0;
  while (s[reader]) {
    if (s[reader] != c) s[writer++] = s[reader];
    reader++;       
  }
  s[writer] = 0;
}

void replaceChar(char* s, char c, char r) {
  // replace specified character in string
  int reader = 0;
  while (s[reader]) {
    if (s[reader] == c) s[reader] = r;
    reader++;       
  }
}

char* fmtSize (uint64_t sizeVal) {
  // format size according to magnitude
  // only one call per format string
  static char returnStr[20];
  if (sizeVal < 50 * 1024) sprintf(returnStr, "%llu bytes", sizeVal);
  else if (sizeVal < ONEMEG) sprintf(returnStr, "%lluKB", sizeVal / 1024);
  else if (sizeVal < ONEMEG * 1024) sprintf(returnStr, "%0.1fMB", (double)(sizeVal) / ONEMEG);
  else sprintf(returnStr, "%0.1fGB", (double)(sizeVal) / (ONEMEG * 1024));
  return returnStr;
}


void doRestart(const char* restartStr) {
  LOG_ALT("Controlled restart: %s", restartStr);
#ifdef ISCAM
  appShutdown();
#endif
#if INCLUDE_MQTT
  if (mqtt_active) stopMqttClient();
#endif  
  resetCrashLoop();
  flush_log(true);
  delay(2000);
  ESP.restart();
}

static void boardInfo() {
  LOG_INF("Chip %s, %d cores @ %dMhz, rev %d", ESP.getChipModel(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getChipRevision() / 100);
  FlashMode_t ideMode = ESP.getFlashChipMode();
  LOG_INF("Flash %s, mode %s @ %dMhz", fmtSize(ESP.getFlashChipSize()), (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"), ESP.getFlashChipSpeed() / OneMHz);

#if defined(CONFIG_SPIRAM_MODE_OCT)
  const char* psramMode = "OPI";
#else 
  const char* psramMode = "QSPI";
#endif
  char memInfo[100] = "none";
#if !CONFIG_IDF_TARGET_ESP32C3
  if (psramFound()) sprintf(memInfo, "%s, mode %s @ %dMhz", fmtSize(ESP.getPsramSize()), psramMode, CONFIG_SPIRAM_SPEED);
#endif
  LOG_INF("PSRAM %s", memInfo);
}

uint16_t smoothAnalog(int analogPin, int samples) {
  // get averaged analog pin value 
  uint32_t level = 0; 
  if (analogPin > 0) {
    for (int j = 0; j < samples; j++) level += analogRead(analogPin); 
    level /= samples;
  }
  return level;
}

void setupADC() {
  analogSetAttenuation(ADC_ATTEN);
  analogReadResolution(ADC_BITS);
}

float smoothSensor(float latestVal, float smoothedVal, float alpha) {
  // simple Exponential Moving Average filter 
  // where alpha between 0.0 (max smooth) and 1.0 (no smooth)
  return (latestVal * alpha) + smoothedVal * (1.0 - alpha);
}

// onboard chip temperature sensor
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t temp_sensor = NULL;
#endif

static void prepInternalTemp() {
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  // setup internal sensor
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  temperature_sensor_install(&temp_sensor_config, &temp_sensor);
  temperature_sensor_enable(temp_sensor);
#endif
}

float readInternalTemp() {
  float intTemp = NULL_TEMP;
#if CONFIG_IDF_TARGET_ESP32
  // convert on chip raw temperature in F to Celsius degrees
  intTemp = (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
    temperature_sensor_get_celsius(temp_sensor, &intTemp); 
#endif
  return intTemp;
}
/*********************** Remote loggging ***********************/
/*
 * Log mode selection in user interface: 
 * false : log to serial / web monitor only
 * true  : also saves log on SD card. To download the log generated, either:
 *  - To view the log, press Show Log button on the browser
 * - To clear the log file contents, on log web page press Clear Log link
 */
 
#define MAX_OUT 200
static va_list arglist;
static char fmtBuf[MAX_OUT];
static char outBuf[MAX_OUT];
char alertMsg[MAX_OUT];
TaskHandle_t logHandle = NULL;
static SemaphoreHandle_t logSemaphore = NULL;
static SemaphoreHandle_t logMutex = NULL;
static int logWait = 100; // ms
bool useLogColors = false;  // true to colorise log messages (eg if using idf.py, but not arduino)
bool wsLog = false;

#define WRITE_CACHE_CYCLE 5

bool sdLog = false; // log to SD
int logType = 0; // which log contents to display (0 : ram, 1 : sd, 2 : ws)
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;

// RAM memory based logging in RTC slow memory (cannot init)
RTC_NOINIT_ATTR char messageLog[RAM_LOG_LEN];
RTC_NOINIT_ATTR uint16_t mlogEnd;

static void ramLogClear() {
  mlogEnd = 0;
  memset(messageLog, 0, RAM_LOG_LEN);
}
  
static void ramLogStore(size_t msgLen) {
  // save log entry in ram buffer
  if (mlogEnd + msgLen >= RAM_LOG_LEN) {
    // log needs to roll around cyclic buffer
    uint16_t firstPart = RAM_LOG_LEN - mlogEnd;
    memcpy(messageLog + mlogEnd, outBuf, firstPart);
    msgLen -= firstPart;
    memcpy(messageLog, outBuf + firstPart, msgLen);
    mlogEnd = 0;
  } else memcpy(messageLog + mlogEnd, outBuf, msgLen);
  mlogEnd += msgLen;
}

void flush_log(bool andClose) {
  if (log_remote_fp != NULL) {
    fsync(fileno(log_remote_fp));  
    fflush(log_remote_fp);
    if (andClose) {
      LOG_INF("Closed SD file for logging");
      fclose(log_remote_fp);
      log_remote_fp = NULL;
    } else delay(1000);
  }  
}

static void remote_log_init_SD() {
#if !CONFIG_IDF_TARGET_ESP32C3
  STORAGE.mkdir(DATA_DIR);
  // Open remote file
  log_remote_fp = NULL;
  log_remote_fp = fopen("/sdcard" LOG_FILE_PATH, "a");
  if (log_remote_fp == NULL) {LOG_WRN("Failed to open SD log file %s", LOG_FILE_PATH);}
  else {
    logPrint(" \n");
    LOG_INF("Opened SD file for logging");
  }
#endif
}

void reset_log() {
  if (logType == 0) ramLogClear();
  if (logType == 2) {
    if (log_remote_fp != NULL) flush_log(true); // Close log file
    STORAGE.remove(LOG_FILE_PATH);
    remote_log_init_SD();
  }
  if (logType != 1) LOG_INF("Cleared %s log file", logType == 0 ? "RAM" : "SD"); 
}

void remote_log_init() {
  // setup required log mode
  if (sdLog) {
    flush_log(false);
    remote_log_init_SD(); // store log on sd card
  } else flush_log(true);
}

static void logTask(void *arg) {
  // separate task to reduce stack size in other tasks
  while(true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vsnprintf(outBuf, MAX_OUT, fmtBuf, arglist);
    va_end(arglist);
    xSemaphoreGive(logSemaphore);
  }
}

void logPrint(const char *format, ...) {
  // feeds logTask to format message, then outputs as required
  if (logMutex == NULL) logSetup();
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(logWait)) == pdTRUE) {
    strncpy(fmtBuf, format, MAX_OUT);
    va_start(arglist, format); 
    vTaskPrioritySet(logHandle, uxTaskPriorityGet(NULL) + 1);
    xTaskNotifyGive(logHandle);
    outBuf[MAX_OUT - 2] = '\n'; 
    outBuf[MAX_OUT - 1] = 0; // ensure always have ending newline
    xSemaphoreTake(logSemaphore, portMAX_DELAY); // wait for logTask to complete        
    // output to monitor console if attached
    size_t msgLen = strlen(outBuf);
    if (outBuf[msgLen - 2] == '~') {
      // set up alert message for browser
      outBuf[msgLen - 2] = ' ';
      strncpy(alertMsg, outBuf, MAX_OUT - 1);
      alertMsg[msgLen - 2] = 0;
    }
    ramLogStore(msgLen); // store in rtc ram 
    if (monitorOpen) Serial.print(outBuf); 
    else delay(10); // allow time for other tasks
    if (sdLog) {
      if (log_remote_fp != NULL) {
        // output to SD if file opened
        fwrite(outBuf, sizeof(char), msgLen, log_remote_fp); // log.txt
        // periodic sync to SD
        if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
      } 
    }
    // output to web socket if open
    if (msgLen > 1) {
      outBuf[msgLen - 1] = 0; // lose final '/n'
      if (wsLog) wsAsyncSendText(outBuf);
    }
    xSemaphoreGive(logMutex);
  } 
}

void logLine() {
  logPrint(" \n");
}

void logSetup() {
  // prep logging environment
  Serial.begin(115200);
  Serial.setDebugOutput(DBG_ON);
  printf("\n\n");
  if (DEBUG_MEM) printf("init > Free: heap %lu\n", ESP.getFreeHeap()); 
  if (!DBG_ON) esp_log_level_set("*", ESP_LOG_NONE); // suppress ESP_LOG_ERROR messages
  if (crashLoop == MAGIC_NUM) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Crash loop detected, check log %s", (brownoutStatus == 'B' || brownoutStatus == 'R') ? "(brownout)" : " ");
  crashLoop = MAGIC_NUM;
  logSemaphore = xSemaphoreCreateBinary(); // flag that log message formatted
  logMutex = xSemaphoreCreateMutex(); // control access to log formatter
  xSemaphoreGive(logSemaphore);
  xSemaphoreGive(logMutex);
  xTaskCreate(logTask, "logTask", LOG_STACK_SIZE, NULL, LOG_PRI, &logHandle);
  if (mlogEnd >= RAM_LOG_LEN) ramLogClear(); // init
  LOG_INF("Setup RAM based log, size %u, starting from %u\n\n", RAM_LOG_LEN, mlogEnd);
  LOG_INF("=============== %s %s ===============", APP_NAME, APP_VER);
  initBrownout();
  prepInternalTemp();
  boardInfo();
  LOG_INF("Compiled with arduino-esp32 v%s", ESP_ARDUINO_VERSION_STR);
  wakeupResetReason();
  if (alertBuffer == NULL) alertBuffer = psramFound() ? (byte*)ps_malloc(maxAlertBuffSize) : (byte*)malloc(maxAlertBuffSize); 
  if (jsonBuff == NULL) jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN); 
  debugMemory("logSetup");
}

void formatHex(const char* inData, size_t inLen) {
  // format data as hex bytes for output
  char formatted[(inLen * 3) + 1];
  for (int i=0; i<inLen; i++) sprintf(formatted + (i*3), "%02x ", inData[i]);
  formatted[(inLen * 3)] = 0; // terminator
  LOG_INF("Hex: %s", formatted);
}

const char* espErrMsg(esp_err_t errCode) {
  // convert esp error code to text
  static char errText[100];
  esp_err_to_name_r(errCode, errText, 100);
  return errText;
}

void forceCrash() {
  // force crash for testing purposes
  delay(5000);
#pragma GCC diagnostic ignored "-Wdiv-by-zero"
  printf("%u\n", 1/0);
#pragma GCC diagnostic warning "-Wdiv-by-zero"
}

/****************** base 64 ******************/

#define BASE64 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

const uint8_t* encode64chunk(const uint8_t* inp, int rem) {
  // receive 3 byte input buffer and return 4 byte base64 buffer
  rem = 3 - rem; // last chunk may be less than 3 bytes 
  uint32_t buff = 0; // hold 3 bytes as shifted 24 bits
  static uint8_t b64[4];
  // shift input into buffer
  for (int i = 0; i < 3 - rem; i++) buff |= inp[i] << (8*(2-i)); 
  // shift 6 bit output from buffer and encode
  for (int i = 0; i < 4 - rem; i++) b64[i] = BASE64[buff >> (6*(3-i)) & 0x3F]; 
  // filler for last chunk if less than 3 bytes
  for (int i = 0; i < rem; i++) b64[3-i] = '='; 
  return b64;
}

const char* encode64(const char* inp) {
  // helper to base64 encode strings up to 90 chars long
  static char encoded[121]; // space for 4/3 expansion + terminator
  encoded[0] = 0;
  int len = strlen(inp);
  if (len > 90) {
    LOG_WRN("Input string too long: %u chars", len);
    len = 90;
  }
  for (int i = 0; i < len; i += 3) 
    strncat(encoded, (char*)encode64chunk((uint8_t*)inp + i, min(len - i, 3)), 4);
  return encoded;
}


/************** task monitoring ***************/

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 1, 0)

static const char* getTaskStateString(eTaskState state) {
  // 
  switch (state) { 
    case eRunning: return "Running"; 
    case eReady: return "Ready"; 
    case eBlocked: return "Blocked"; 
    case eSuspended: return "Suspended"; 
    case eDeleted: return "Deleted"; 
    case eInvalid: return "Invalid"; 
    default: return "Unknown";
  }
}

static void statsTask(void *arg) { 
  // Output real time task stats periodically
  #define STATS_TASK_PRIO     10
  #define STATS_INTERVAL      30000 // ms
  #define ARRAY_SIZE_OFFSET   40   // Increase this if ESP_ERR_INVALID_SIZE

  static configRUN_TIME_COUNTER_TYPE prevRunCounter = 0;
  esp_err_t ret = ESP_OK;  
  TaskStatus_t *statsArray = NULL;
  UBaseType_t statsArraySize;
  configRUN_TIME_COUNTER_TYPE runCounter;
      
  while (true) {
    delay(STATS_INTERVAL);

    do { // fake loop for breaks
      // Allocate array to store current task states
      statsArraySize = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
      statsArray = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * statsArraySize);
      if (statsArray == NULL) {
        ret = ESP_ERR_NO_MEM;
        break;
      }

      // Get current task states
      statsArraySize = uxTaskGetSystemState(statsArray, statsArraySize, &runCounter);
      if (statsArraySize == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        break;
      }

      // Calculate total_elapsed_time in units of run time stats clock period
      if (runCounter - prevRunCounter == 0) {
        ret = ESP_ERR_INVALID_STATE;
        break;
      }

      printf("\nTask stats interval %lums on %u cores\n", (runCounter - prevRunCounter) / 1000, CONFIG_FREERTOS_NUMBER_OF_CORES);
      printf("\n| %-16s | %-10s | %-3s | %-4s | %-6s |\n", "Task name", "State", "Pri", "Core", "Core%");
      printf("|------------------|------------|-----|------|--------|\n"); 
      // Match each task in start_array to those in the end_array
      for (int i = 0; i < statsArraySize; i++) {
        float percentage_time = ((float)statsArray[i].ulRunTimeCounter * 100.0) / runCounter;
        printf("| %-16s | %-10s | %*u | %*s | %*.1f%% |\n", 
          statsArray[i].pcTaskName, getTaskStateString(statsArray[i].eCurrentState), 3, (int)statsArray[i].uxCurrentPriority, 4, "tbd", 5, percentage_time);
      }
      printf("|------------------|------------|-----|------|--------|\n"); 
    } while (false);

    prevRunCounter = runCounter;
    free(statsArray);
    if (ret != ESP_OK) LOG_WRN("Failed to start task monitoring %s", espErrMsg(ret));
  }
}

void runTaskStats() {
  // invoke task stats monitoring
  // Allow other core to finish initialization
  vTaskDelay(pdMS_TO_TICKS(100));
  // Create and start stats task
  xTaskCreatePinnedToCore(statsTask, "statsTask", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
}
#endif

void checkMemory(const char* source) {
  LOG_INF("%s Free: heap %u, block: %u, min: %u, pSRAM %u", strlen(source) ? source : "Setup", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
  if (ESP.getFreeHeap() < WARN_HEAP) LOG_WRN("Free heap only %u, min %u", ESP.getFreeHeap(), ESP.getMinFreeHeap());
  if (ESP.getMaxAllocHeap() < WARN_ALLOC) LOG_WRN("Max allocatable heap block is only %u", ESP.getMaxAllocHeap());
  if (!strlen(source) && DEBUG_MEM) {
    printGpioInfo();
    runTaskStats();
  }
}

uint32_t checkStackUse(TaskHandle_t thisTask, int taskIdx) {
  // get minimum free stack size for task since started
  // taskIdx used to index minStack[] array
  static uint32_t minStack[20]; 
  uint32_t freeStack = 0;
  if (thisTask != NULL) {
    freeStack = (uint32_t)uxTaskGetStackHighWaterMark(thisTask);
    if (!minStack[taskIdx]) {
      minStack[taskIdx] = freeStack; // initialise
      LOG_INF("Task %s on core %d, initial stack space %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
    if (freeStack < minStack[taskIdx]) {
      minStack[taskIdx] = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Task %s on core %d, stack space only: %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
      else LOG_INF("Task %s on core %d, stack space reduced to %u", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
  }
  return freeStack;
}

void debugMemory(const char* caller) {
  if (DEBUG_MEM) {
    logPrint("%s > Free: heap %u, block: %u, min: %u, pSRAM %u\n", caller, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
    delay(FLUSH_DELAY);
  }
}

#include "esp32-hal-periman.h"
static void printGpioInfo() {
  // from https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/chip-debug-report.cpp
  printf("Assigned GPIO Info:\n");
  for (uint8_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    if (!perimanPinIsValid(i)) continue;  //invalid pin
    peripheral_bus_type_t type = perimanGetPinBusType(i);
    if (type == ESP32_BUS_TYPE_INIT) continue;  //unused pin

#if defined(BOARD_HAS_PIN_REMAP)
    int dpin = gpioNumberToDigitalPin(i);
    if (dpin < 0) continue;  //pin is not exported
    else printf("  D%-3d|%4u : ", dpin, i);
#else
    printf("  %4u : ", i);
#endif
    const char *extra_type = perimanGetPinBusExtraType(i);
    if (extra_type) printf("%s", extra_type);
    else printf("%s", perimanGetTypeName(type));
    int8_t bus_number = perimanGetPinBusNum(i);
    if (bus_number != -1) printf("[%u]", bus_number);

    int8_t bus_channel = perimanGetPinBusChannel(i);
    if (bus_channel != -1) printf("[%u]", bus_channel);
    printf("\n");
  }
  printf("\n");
}


/****************** send device to sleep (light or deep) & watchdog ******************/

#include <esp_wifi.h>
#include <driver/gpio.h>

static esp_sleep_wakeup_cause_t printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : LOG_INF("Wakeup by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : LOG_INF("Wakeup by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : LOG_INF("Wakeup by internal timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : LOG_INF("Wakeup by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : LOG_INF("Wakeup by ULP program"); break;
    case ESP_SLEEP_WAKEUP_GPIO: LOG_INF("Wakeup by GPIO"); break;    
    case ESP_SLEEP_WAKEUP_UART: LOG_INF("Wakeup by UART"); break; 
    default : LOG_INF("Wakeup by reset"); break;
  }
  return wakeup_reason;
}


static esp_reset_reason_t printResetReason() {
  esp_reset_reason_t bootReason = esp_reset_reason();
  switch (bootReason) {
    case ESP_RST_UNKNOWN: LOG_INF("Reset for unknown reason"); break;
    case ESP_RST_POWERON: {
      LOG_INF("Power on reset");
      brownoutStatus = 0;
      messageLog[0] = 0;
      break;
    }
    case ESP_RST_EXT: LOG_INF("Reset from external pin"); break;
    case ESP_RST_SW: LOG_INF("Software reset via esp_restart"); break;
    case ESP_RST_PANIC: LOG_INF("Software reset due to exception/panic"); break;
    case ESP_RST_INT_WDT: LOG_INF("Reset due to interrupt watchdog"); break;
    case ESP_RST_TASK_WDT: LOG_INF("Reset due to task watchdog"); break;
    case ESP_RST_WDT: LOG_INF("Reset due to other watchdogs"); break;
    case ESP_RST_DEEPSLEEP: LOG_INF("Reset after exiting deep sleep mode"); break;
    case ESP_RST_BROWNOUT: LOG_INF("Software reset due to brownout"); break;
    case ESP_RST_SDIO: LOG_INF("Reset over SDIO"); break;
    default: LOG_WRN("Unhandled reset reason"); break;
  }
  return bootReason;
}

esp_sleep_wakeup_cause_t wakeupResetReason() {
  printResetReason();
  esp_sleep_wakeup_cause_t wakeupReason = printWakeupReason();
  return wakeupReason;
}

void goToSleep(int wakeupPin, bool deepSleep) {
#if !CONFIG_IDF_TARGET_ESP32C3
  // if deep sleep, restarts with reset
  // if light sleep, restarts by continuing this function
  LOG_INF("Going into %s sleep", deepSleep ? "deep" : "light");
  delay(100);
  if (deepSleep) { 
    if (wakeupPin >= 0) {
      // wakeup on pin low
      pinMode(wakeupPin, INPUT_PULLUP);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)wakeupPin, 0);
    }
    esp_deep_sleep_start();
  } else {
    // light sleep
    esp_wifi_stop();
    // wakeup on pin high
    if (wakeupPin >= 0) gpio_wakeup_enable((gpio_num_t)wakeupPin, GPIO_INTR_HIGH_LEVEL); 
    esp_light_sleep_start();
  }
  // light sleep restarts here
  LOG_INF("Light sleep wakeup");
  esp_wifi_start();
#else
  LOG_WRN("This function not compatible with ESP32-C3");
#endif
}


// catch software resets due to brownouts
//https://github.com/espressif/esp-idf/blob/master/components/esp_system/port/brownout.c

#include "esp_private/system_internal.h"
#include "esp_private/rtc_ctrl.h"
#include "hal/brownout_ll.h"

#include "soc/rtc_periph.h"
#include "hal/brownout_hal.h"

#define BROWNOUT_DET_LVL 7

IRAM_ATTR static void notifyBrownout(void *arg) {
  esp_cpu_stall(!xPortGetCoreID());  // Stop the other core.
  esp_reset_reason_set_hint(ESP_RST_BROWNOUT);
  brownoutStatus = 'B';
  esp_restart_noos(); // dirty reboot
}

static void initBrownout(void) {
  // brownout warning only output once to prevent bootloop
  if (brownoutStatus == 'R') LOG_WRN("Brownout warning previously notified");
  else if (brownoutStatus == 'B') {
    LOG_WRN("Brownout occurred due to inadequate power supply");
    brownoutStatus = 'R';
  } else {
    brownout_hal_config_t cfg = {
      .threshold = BROWNOUT_DET_LVL,
      .enabled = true,
      .reset_enabled = false,
      .flash_power_down = true,
      .rf_power_down = true,
    };
    brownout_hal_config(&cfg);
    brownout_ll_intr_clear();
    rtc_isr_register(notifyBrownout, NULL, RTC_CNTL_BROWN_OUT_INT_ENA_M, RTC_INTR_FLAG_IRAM);
    brownout_ll_intr_enable(true);
    brownoutStatus = 0; 
  }
}
