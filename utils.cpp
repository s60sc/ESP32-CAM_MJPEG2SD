
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
char startupFailure[50] = {0};

/************************** Wifi **************************/

char hostName[32] = ""; // Default Host name
char ST_SSID[32]  = ""; //Default router ssid
char ST_Pass[MAX_PWD_LEN] = ""; //Default router passd

// leave following blank for dhcp
char ST_ip[16]  = ""; // Static IP
char ST_sn[16]  = ""; // subnet normally 255.255.255.0
char ST_gw[16]  = ""; // gateway to internet, normally router IP
char ST_ns1[16] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[16] = ""; // alternative DNS Server, can be blank

// Access point Config Portal SSID and Pass
char AP_SSID[32] = "";
char AP_Pass[MAX_PWD_LEN] = "";
char AP_ip[16]  = ""; //Leave blank to use 192.168.4.1
char AP_sn[16]  = "";
char AP_gw[16]  = "";

// basic HTTP Authentication access to web page
char Auth_Name[16] = ""; 
char Auth_Pass[MAX_PWD_LEN] = "";

int responseTimeoutSecs = 10; // time to wait for FTP or SMTP response
bool allowAP = true;  // set to true to allow AP to startup if cannot connect to STA (router)
int wifiTimeoutSecs = 30; // how often to check wifi status
static bool APstarted = false;
static esp_ping_handle_t pingHandle = NULL;
static void startPing();

static void setupMdnsHost() {  
  // set up MDNS service 
  char mdnsName[15]; // max mdns host name length
  snprintf(mdnsName, 15, hostName);
  if (MDNS.begin(mdnsName)) {
    // Add service to MDNS
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "udp", 83);
    // MDNS.addService("ftp", "tcp", 21);    
    LOG_INF("mDNS service: http://%s.local", mdnsName);
  } else LOG_ERR("mDNS host: %s Failed", mdnsName);
  debugMemory("setupMdnsHost");
}

const char* getEncType(int ssidIndex) {
  switch (WiFi.encryptionType(ssidIndex)) {
    case (WIFI_AUTH_OPEN):
      return "Open";
    case (WIFI_AUTH_WEP):
      return "WEP";
    case (WIFI_AUTH_WPA_PSK):
      return "WPA_PSK";
    case (WIFI_AUTH_WPA2_PSK):
      return "WPA2_PSK";
    case (WIFI_AUTH_WPA_WPA2_PSK):
      return "WPA_WPA2_PSK";
    case (WIFI_AUTH_WPA2_ENTERPRISE):
      return "WPA2_ENTERPRISE";
    case (WIFI_AUTH_MAX):
      return "AUTH_MAX";
    default:
      return "Not listed";
  }
  return "n/a";
}

static void onWiFiEvent(WiFiEvent_t event) {
  // callback to report on wifi events
  if (event == ARDUINO_EVENT_WIFI_READY);
  else if (event == ARDUINO_EVENT_WIFI_SCAN_DONE);  
  else if (event == ARDUINO_EVENT_WIFI_STA_START) LOG_INF("Wifi Station started, connecting to: %s", ST_SSID);
  else if (event == ARDUINO_EVENT_WIFI_STA_STOP) LOG_INF("Wifi Station stopped %s", ST_SSID);
  else if (event == ARDUINO_EVENT_WIFI_AP_START) {
    if (!strcmp(WiFi.softAPSSID().c_str(), AP_SSID) || !strlen(AP_SSID)) {
      LOG_INF("Wifi AP SSID: %s started, use 'http://%s' to connect", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
      APstarted = true;
    }
  }
  else if (event == ARDUINO_EVENT_WIFI_AP_STOP) {
    if (!strcmp(WiFi.softAPSSID().c_str(), AP_SSID)) {
      LOG_INF("Wifi AP stopped: %s", AP_SSID);
      APstarted = false;
    }
  }
  else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) LOG_INF("Wifi Station IP, use 'http://%s' to connect", WiFi.localIP().toString().c_str()); 
  else if (event == ARDUINO_EVENT_WIFI_STA_LOST_IP) LOG_INF("Wifi Station lost IP");
  else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);
  else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) LOG_INF("WiFi Station connection to %s, using hostname: %s", ST_SSID, hostName);
  else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) LOG_INF("WiFi Station disconnected");
  else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) LOG_INF("WiFi AP client connection");
  else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) LOG_INF("WiFi AP client disconnection");
  else LOG_WRN("WiFi Unhandled event %d", event);
}

static bool setWifiAP() {
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
  }
  return true;
}

static bool setWifiSTA() {
  // set station with static ip if provided
  if (strlen(ST_SSID)) { 
    if (strlen(ST_ip) > 1) {
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      if (!_ip.fromString(ST_ip)) LOG_ERR("Failed to parse IP: %s", ST_ip);
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
    WiFi.begin(ST_SSID, ST_Pass);
    return true;
  } else LOG_WRN("No Station SSID provided, use AP");
  return false;
}

bool startWifi(bool firstcall) {
  // start wifi station (and wifi AP if allowed or station not defined)
  if (firstcall) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false); // prevent the flash storage WiFi credentials
    WiFi.setAutoReconnect(false); // Set whether module will attempt to reconnect to an access point in case it is disconnected
    WiFi.softAPdisconnect(false); // kill rogue AP on startup
    WiFi.disconnect(true);
    WiFi.setHostname(hostName);
    WiFi.onEvent(onWiFiEvent);
  }
  bool station = setWifiSTA();
  debugMemory("setWifiSTA");
  if (!station || allowAP) setWifiAP(); // AP allowed if no Station SSID eg on first time use
  debugMemory("setWifiAP");
  if (station) {
    // connect to Wifi station
    uint32_t startAttemptTime = millis();
    // Stop trying on failure timeout, will try to reconnect later by ping
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000)  {
      Serial.print(".");
      delay(500);
      Serial.flush();
    }
    if (pingHandle == NULL) startPing();
    debugMemory("startPing");
  }
#if CONFIG_IDF_TARGET_ESP32S3
  setupMdnsHost(); // not on ESP32 as uses 6k of heap
#endif
  // show stats of requested SSID
  int numNetworks = WiFi.scanNetworks();
  for (int i=0; i < numNetworks; i++) {
    if (!strcmp(WiFi.SSID(i).c_str(), ST_SSID))
      LOG_INF("Wifi stats for %s - signal strength: %d dBm; Encryption: %s; channel: %u",  ST_SSID, WiFi.RSSI(i), getEncType(i), WiFi.channel(i));
  }
  return WiFi.status() == WL_CONNECTED ? true : false;
}

static void pingSuccess(esp_ping_handle_t hdl, void *args) {
  if (!timeSynchronized) getLocalNTP();
  if (!dataFilesChecked) dataFilesChecked = checkDataFiles();
#ifdef INCLUDE_MQTT
  if (mqtt_active) startMqttClient();
#endif
  doAppPing();
}

static void pingTimeout(esp_ping_handle_t hdl, void *args) {
  LOG_WRN("Failed to ping gateway, restart wifi ...");
  startWifi(false);
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
#if CONFIG_IDF_TARGET_ESP32S3
  pingConfig.task_stack_size = 1024 * 6;
#else
  pingConfig.task_stack_size = 1024 * 4;
#endif
  pingConfig.task_prio = 1;
  // set ping task callback functions 
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = pingSuccess;
  cbs.on_ping_timeout = pingTimeout;
  cbs.on_ping_end = NULL; 
  cbs.cb_args = NULL;
  esp_ping_new_session(&pingConfig, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  LOG_INF("Started ping monitoring");
}

void stopPing() {
  if (pingHandle != NULL) {
    esp_ping_stop(pingHandle);
    esp_ping_delete_session(pingHandle);
    pingHandle = NULL;
  }
}

const char* extIpHost = "checkip.dyndns.org";
const int ipAddrLen = 16;
char ipExtAddr[ipAddrLen] = {"Not assigned"};

void getExtIP() {
  // Get external IP address
  WiFiClient hclient;
  if (hclient.connect(extIpHost, 80)) {
    // send the request to the server
    hclient.print("GET / HTTP/1.0\r\n Host: ");
    hclient.print(extIpHost);
    hclient.print("\r\nConnection: close\r\n\r\n");
    // Read all the lines of the reply from server
    uint32_t startAttemptTime = millis();
    while (!hclient.available() && millis() - startAttemptTime < 5000) delay(500);
    if (hclient.available()) {
      String newExtIp = "";
      while (hclient.available()) newExtIp += hclient.readStringUntil('\r');
      if (newExtIp.length()) {
        if (strstr(newExtIp.c_str(), "200 OK") != NULL) {
          int startPt = newExtIp.lastIndexOf("Address: ") + String("Address: ").length();
          int endPt =  newExtIp.lastIndexOf("</body>");
          newExtIp = newExtIp.substring(startPt, endPt).c_str();
          if (strcmp(newExtIp.c_str(), ipExtAddr)) {
            // external IP changed
            strncpy(ipExtAddr, newExtIp.c_str(), ipAddrLen-1);
            LOG_INF("External IP changed: %s", ipExtAddr); 
          }
        } else LOG_ERR("Bad request response");
      } else LOG_ERR("External IP not retrieved");
    } else LOG_ERR("External IP no response");
    hclient.stop();
  } else LOG_ERR("ExtIP connection failed");
  LOG_INF("Current External IP: %s", ipExtAddr);
}


/************************** NTP  **************************/

// Needs to be a time zone string from: https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv
char timezone[64] = "GMT0";
char ntpServer[64] = "pool.ntp.org";

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
  struct timeval tv;
  tv.tv_sec = browserUTC;
  settimeofday(&tv, NULL);
  setenv("TZ", timezone, 1);
  tzset();
  showLocalTime("browser");
}

void formatElapsedTime(char* timeStr, uint32_t timeVal) {
  uint32_t secs = timeVal / 1000; //convert milliseconds to seconds
  uint32_t mins = secs / 60; //convert seconds to minutes
  uint32_t hours = mins / 60; //convert minutes to hours
  uint32_t days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the converted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the converted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the converted hours to days in order to display 23 hours max
  sprintf(timeStr, "%u-%02u:%02u:%02u", days, hours, mins, secs);
}

/********************** misc functions ************************/

bool changeExtension(char* outName, const char* inName, const char* newExt) {
  // replace original file extension with supplied extension
  size_t inNamePtr = strlen(inName);
  // find '.' before extension text
  while (inNamePtr > 0 && inName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(outName, inName, inNamePtr);
  memcpy(outName + inNamePtr, newExt, extLen);
  outName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

void showProgress() {
  // show progess as dots 
  static uint8_t dotCnt = 0;
  logPrint("."); // progress marker
  if (++dotCnt >= 50) {
    dotCnt = 0;
    logPrint("\n");
  }
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

void checkMemory() {
  LOG_INF("Free: heap %u, block: %u, pSRAM %u", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
}


uint32_t checkStackUse(TaskHandle_t thisTask) {
  // get minimum free stack size for task since started
  uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(thisTask);
  LOG_INF("Task %s min stack space: %u\n", pcTaskGetTaskName(thisTask), freeStack);
  return freeStack;
}

void debugMemory(const char* caller) {
  if (CHECK_MEM) {
    delay(FLUSH_DELAY);
    logPrint("%s > Free: heap %u, block: %u, pSRAM %u\n", caller, ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
  }
}

void doRestart(const char* restartStr) {
  flush_log(true);
  LOG_ALT("Controlled restart: %s", restartStr);
  delay(2000);
  ESP.restart();
}

uint16_t smoothAnalog(int analogPin) {
  // get averaged analog pin value 
  uint32_t level = 0; 
  if (analogPin > 0) {
    for (int j = 0; j < ADC_SAMPLES; j++) level += analogRead(analogPin); 
    level /= ADC_SAMPLES;
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
static TaskHandle_t logHandle = NULL;
static SemaphoreHandle_t logSemaphore = NULL;
static SemaphoreHandle_t logMutex = NULL;
static int logWait = 100; // ms
bool useLogColors = false;  // true to colorise log messages (eg if using idf.py, but not arduino)

#define WRITE_CACHE_CYCLE 5
bool logMode = false; // 
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;

// RAM memory based logging
char* messageLog; // used to hold system message log
uint16_t mlogEnd = 0;
uint16_t mlogLen = 0;
bool mlogCycle = false; // if cycled thru log end

static void ramLogClear() {
  if (mlogLen) {
    mlogEnd = 0;
    mlogCycle = false; 
    messageLog[0] = '\0';
    LOG_INF("Setup RAM based log");
  }
}


void ramLogPrep() {
  logMode = true;
  mlogLen = RAM_LOG_LEN;
  messageLog = (char*)malloc(mlogLen);
  ramLogClear();
}

static void ramLogStore(size_t msgLen) {
  // save log entry in ram buffer
  if (mlogEnd + msgLen > RAM_LOG_LEN - 2) {
    // log needs to roll over cyclic buffer, before saving message
    mlogEnd = 0;
    mlogCycle = true;
    strcpy(messageLog, outBuf);
    messageLog[RAM_LOG_LEN-1] = '\n'; // so that newline at end of final whitespace
    messageLog[RAM_LOG_LEN-2] = '\0'; // ensure there is always a terminator
  } else strcat(messageLog, outBuf);
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
  SD_MMC.mkdir(DATA_DIR);
  // Open remote file
  log_remote_fp = NULL;
  log_remote_fp = fopen("/sdcard" LOG_FILE_PATH, "a");
  if (log_remote_fp == NULL) {LOG_ERR("Failed to open SD log file %s", LOG_FILE_PATH);}
  else {LOG_INF("Opened SD file for logging");}
#endif
}

void reset_log() {
  ramLogClear();
#if !CONFIG_IDF_TARGET_ESP32C3
  if (log_remote_fp != NULL) {
    flush_log(true); // Close log file
    SD_MMC.remove(LOG_FILE_PATH);
    LOG_INF("Cleared log file");
    if (logMode) remote_log_init_SD();   
  }
#endif
}

void remote_log_init() {
  // setup required log mode
  if (logMode) {
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
  if (xSemaphoreTake(logMutex, logWait / portTICK_PERIOD_MS) == pdTRUE) {
    strncpy(fmtBuf, format, MAX_OUT);
    fmtBuf[MAX_OUT - 1] = 0;
    va_start(arglist, format); 
    vTaskPrioritySet(logHandle, uxTaskPriorityGet(NULL) + 1);
    xTaskNotifyGive(logHandle);
    xSemaphoreTake(logSemaphore, portMAX_DELAY); // wait for logTask to complete        
    // output to monitor console if attached
    size_t msgLen = strlen(outBuf);
    if (outBuf[msgLen - 2] == '~') {
      // set up alert message for browser
      outBuf[msgLen - 2] = ' ';
      strncpy(alertMsg, outBuf, MAX_OUT - 1);
      alertMsg[msgLen - 2] = 0;
    }
    if (monitorOpen) Serial.print(outBuf); 
    else delay(10); // allow time for other tasks
    if (logMode) {
      if (log_remote_fp != NULL) {
        // output to SD if file opened
        fwrite(outBuf, sizeof(char), msgLen, log_remote_fp); // log.txt
        // periodic sync to SD
        if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
      } else ramLogStore(msgLen); // store in ram instead
    }
    // output to web socket if open
    outBuf[msgLen - 1] = 0; // lose final '/n'
    wsAsyncSend(outBuf);
    delay(FLUSH_DELAY);
    xSemaphoreGive(logMutex);
  } 
}

void logSetup() {
  // prep logging environment
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println(); 
  logSemaphore = xSemaphoreCreateBinary(); // flag that log message formatted
  logMutex = xSemaphoreCreateMutex(); // control access to log formatter
  xSemaphoreGive(logSemaphore);
  xSemaphoreGive(logMutex);
  xTaskCreate(logTask, "logTask", 1024 * 2, NULL, 1, &logHandle); 
  print_wakeup_reason();
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


/****************** send device to sleep (light or deep) ******************/

#include <esp_wifi.h>

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : LOG_INF("Wakeup by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : LOG_INF("Wakeup by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : 
      // expected wakeup reason from deep sleep
      LOG_INF("Wakeup by internal timer"); 
    break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : LOG_INF("Wakeup by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : LOG_INF("Wakeup by ULP program"); break;
    case ESP_SLEEP_WAKEUP_GPIO: LOG_INF("Wakeup by GPIO"); break;    
    case ESP_SLEEP_WAKEUP_UART: LOG_INF("Wakeup by UART"); break; 
    default : LOG_INF("Wakeup by reset"); break;
  }
}

void goToSleep(int wakeupPin, bool deepSleep) {
#if !CONFIG_IDF_TARGET_ESP32C3
  // if deep sleep, restarts with reset
  // if light sleep, restarts by continuing this function
  LOG_INF("Going into %s sleep", deepSleep ? "deep" : "light");
  delay(100);
  if (deepSleep) { 
    if (wakeupPin >= 0) {
      // wakeup on pin high
      pinMode(wakeupPin, INPUT_PULLDOWN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)wakeupPin, 1); 
    }
    esp_deep_sleep_start();
  } else {
    // light sleep
    esp_wifi_stop();
    if (wakeupPin >= 0) gpio_wakeup_enable((gpio_num_t)wakeupPin, GPIO_INTR_HIGH_LEVEL); // wakeup on pin high
    esp_light_sleep_start();
  }
  // light sleep restarts here
  LOG_INF("Light sleep wakeup");
  esp_wifi_start();
#else
  LOG_WRN("This function not compatible with ESP32-C3");
#endif
}
