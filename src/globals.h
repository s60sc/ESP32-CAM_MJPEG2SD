// Global generic declarations
//
// s60sc 2021, 2022, 2026

#pragma once

#include "esp_arduino_version.h"

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 1, 1)
#error Must be compiled with arduino-esp32 core v3.1.1 or higher
#endif

#if __has_include("../devUtilities.cpp")
#define DEV_ONLY
#endif
#ifdef DEV_ONLY
// to compile with -Wall -Werror=all -Wextra
#pragma GCC diagnostic error "-Wformat=2"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wformat-y2k"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
//#pragma GCC diagnostic ignored "-Wunused-variable"
//#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
//#pragma GCC diagnostic ignored "-Wignored-qualifiers"
//#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wvolatile"
#endif

/******************** Libraries *******************/

#include "Arduino.h"
#include <ESPmDNS.h> 
#include "lwip/sockets.h"
#include <vector>
#include "ping/ping_sock.h"
#include <Preferences.h>
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
#include <SD_MMC.h>
#endif
#include <LittleFS.h>
#include <FFat.h>
#include <sstream>
#include <Update.h>
#include <WiFi.h>
#include <ETH.h>
#ifdef APP_BT_ENABLED
#include <esp_bt.h>
#endif
#include <HTTPClient.h>
#include <NetworkClient.h> 
#include <NetworkClientSecure.h> 
#include <WiFiClientSecure.h>
#include <esp_http_server.h>
#include <esp_https_server.h>
#include "esp_sntp.h"
#include "driver/uart.h"

// ADC
#define ADC_ATTEN ADC_11db
#define ADC_SAMPLES 16
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
#define ADC_BITS 13
#define MAX_ADC 8191 // maximum ADC value at given resolution
#else
#define ADC_BITS 12
#define MAX_ADC 4095 // maximum ADC value at given resolution
#endif
#define CENTER_ADC (MAX_ADC / 2) 

// data folder defs
#define DATA_DIR "/data"
#define HTML_EXT ".htm"
#define TEXT_EXT ".txt"
#define JS_EXT ".js"
#define CSS_EXT ".css"
#define ICO_EXT ".ico"
#define SVG_EXT ".svg"
#define JPG_EXT ".jpg"
#define CONFIG_FILE_PATH DATA_DIR "/configs" TEXT_EXT
#define LOG_FILE_PATH DATA_DIR "/log" TEXT_EXT
#define OTA_FILE_PATH DATA_DIR "/OTA" HTML_EXT
#define COMMON_JS_PATH DATA_DIR "/common" JS_EXT 
#define WEBDAV "/webdav"
#define GITHUB_HOST "raw.githubusercontent.com"
#define FILLSTAR "****************************************************************"
#define DELIM '~'

// general defs
#define ONEMEG (1024 * 1024)
#define MAX_PWD_LEN 64
#define MAX_HOST_LEN 32
#define MAX_IP_LEN 16
#define BOUNDARY_VAL "123456789000000000000987654321"
#define SF_LEN 128
#define WAV_HDR_LEN 44
#define RAM_LOG_LEN (512 * 13) // size of system message log in bytes stored in slow RTC ram (max 8KB - vars)
#define MIN_STACK_FREE 512
#define STARTUP_FAIL "Startup Failure: "
#define MAX_PAYLOAD_LEN 672 // set bigger than any incoming websocket payload (20ms audio)
#define NULL_TEMP -127
#define OneMHz 1000000
#define USECS 1000000
#define MAGIC_NUM 987654321
#define MAX_FAIL 5
#define PANIC_DELAY 5 // seconds before restart after panic
#define DAY_LENGTH (24 * 60 * 60) // in secs
#define LOG_MAX_ARGS 8   // max arguments per log message
#define LOG_STR_AREA 128 // store %s arguments for one message

enum RemoteFail {SETASSIST, GETEXTIP, TGRAMCONN, FSFTP, EMAILCONN, EXTERNALHB, BLOCKLIST, GETEXTMSL, GETEXTMAG, GETEXTNOCT, REMFAILCNT}; // REMFAILCNT always last
enum class Brightness : uint8_t  { Off = 0, Low, Medium, High, Full, Count }; //  Perceived brightness 25/50/75/100%
enum class LedType : uint8_t { Mono = 0, WS2812, RGB3Pin }; // Status LED type

// global mandatory app specific functions, in appSpecific.cpp 
bool appDataFiles();
bool appSetup(); 
bool appSpecificSMTP();
esp_err_t appSpecificSustainHandler(httpd_req_t* req);
esp_err_t appSpecificWebHandler(httpd_req_t *req, const char* variable, const char* value);
void appSpecificWsBinHandler(uint8_t* wsMsg, size_t wsMsgLen);
void appSpecificWsHandler(const char* wsMsg);
void appSpecificTelegramTask(void* p);
char* buildAppJsonString(bool filter);
bool updateAppStatus(const char* variable, const char* value, bool fromUser = true);

// global general utility functions in utils.cpp / utilsFS.cpp / peripherals.cpp etc
void buildJsonString(uint8_t filter);
bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress);
bool changeExtension(char* fileName, const char* newExt);
bool checkAlarm();
bool checkAppReady();
bool checkAuth(httpd_req_t* req);
void checkDataFiles();
bool checkFreeStorage();
bool checkI2Cdevice(const char* devName);
void checkMemory(const char* source = "");
uint32_t checkStackUse(TaskHandle_t thisTask, int taskIdx);
void debugMemory(const char* caller);
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);
void deleteFolderOrFile(const char* deleteThis);
void devCheck();
void devSetup();
void doAppPing(bool timeSynced);
void doRestart(const char* restartStr);
esp_err_t downloadFile(File& df, httpd_req_t* req);
void emailAlert(const char* _subject, const char* _message);
const char* encode64(const char* inp);
const uint8_t* encode64chunk(const uint8_t* inp, int rem);
const char* espErrMsg(esp_err_t errCode);
void externalAlert(const char* subject, const char* message);
bool externalPeripheral(byte pinNum, uint32_t outputData = 0);
esp_err_t extractHeaderVal(httpd_req_t *req, const char* variable, char* value, size_t valueSize);
esp_err_t extractQueryKeyVal(httpd_req_t *req, char* variable, char* value, size_t valueSize);
esp_err_t fileHandler(httpd_req_t* req, bool download = false);
void flush_log(bool andClose = false);
char* fmtSize (uint64_t sizeVal);
void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays = false);
void formatHex(const char* inData, size_t inLen);
const char* formatIPstr(bool getAP = false);
bool formatSDcard();
bool fsStartTransfer(const char* fileFolder);
const char* getEncType(int ssidIndex);
void getExtIP();
time_t getEpoch();
size_t getFreeStorage();
uint32_t getFrequency();
int getLogStartPtr(int startPtr);
float getNTCcelsius(uint16_t resistance, float oldTemp);
void goToSleep(bool deepSleep);
bool handleWebDav(httpd_req_t* rreq);
void initStatus(int cfgGroup, int delayVal);
bool isPathTraversal(const char* path);
void killSocket(int skt = -99);
bool getJsonValue(const char* json, const char* key, char* value, const char* nestedKey = nullptr, int occurrence = 1);
void listBuff(const uint8_t* b, size_t len); 
bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension);
void loadCerts();
bool loadConfig();
bool logFlush(uint32_t timeout_ms);
void logLine();
bool logSetup();
void OTAprereq();
bool parseJson(int rxSize);
bool prepFreq(int maxFreq, int sampleInterval);
bool prepI2C();
void prepPeripherals();
void prepSMTP();
bool prepTelegram();
void prepTemperature();
void prepUpload();
void printAllTasksInfo();
void reloadConfigs();
float readInternalTemp();
float readTemperature(bool isCelsius, bool onlyDS18 = false);
float readVoltage();
void remote_log_init();
void remoteServerClose(Client& client);
bool remoteServerConnect(Client& client, const char* host, uint16_t port, uint8_t idx);
bool remoteServerConnect(NetworkClientSecure& client, const char* host, uint16_t port, uint8_t idx);
bool remoteServerConnect(NetworkClientSecure& client, const char* host, uint16_t port, const char* cert, uint8_t idx);
void remoteServerReset();
void removeChar(char* s, char c);
void replaceChar(char* s, char c, char r);
void resetCrashLoop();
void reset_log();
void resetWatchDog(int wdIndex, uint32_t wdTimeout = 1);
bool retrieveConfigVal(const char* variable, char* value);
void runStatusCheck();
void runTaskStats(bool _onceOnly = false);
void saveRamLog(const char* ramLogName);
esp_err_t sendChunks(File df, httpd_req_t *req, bool endChunking = true);
void sendSSE(const char* eventType, const char* eventData);
void setFolderName(const char* fname, char* fileName, size_t fileNameSize);
void setLamp(uint8_t lampVal);
void setPeripheralResponse(const byte pinNum, const uint32_t responseData);
void setupADC();
void showHWM(const char* msg);
void showProgress(const char* marker = ".");
void showSys();
uint16_t smoothAnalog(int analogPin, int samples = ADC_SAMPLES);
float smoothSensor(float latestVal, float smoothedVal, float alpha);
void startOTAtask();
void startSecTimer(bool startTimer);
bool startStorage();
bool startWebServer();
void stopPing();
void syncToBrowser(uint32_t browserUTC);
char* toCase(char *s, bool toLower = true);
char* trim(char* str);
void updateAuthCache();
bool updateConfigVect(const char* variable, int value);
bool updateConfigVect(const char* variable, const char* value);
void updateStatus(const char* variable, const char* _value, bool fromUser = true);
esp_err_t uploadHandler(httpd_req_t *req);
void urlDecode(char* inVal);
bool urlEncode(const char* inVal, char* encoded, size_t maxSize);
bool updateGNSS();
uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData);
bool utilsStartup();
esp_sleep_wakeup_cause_t wakeupResetReason();
void wsAsyncSendBinary(uint8_t* data, size_t len);
bool wsAsyncSendJson(const char* dataType, const char* wsData);
bool wsAsyncSendText(const char* wsData);
// unified networking helpers (WiFi or Ethernet)
bool startNetwork(bool firstcall = true);
const char* netMacAddress();
int netRSSI();
bool netIsConnected();
// mqtt.cpp
void startMqttClient();  
void stopMqttClient();  
void mqttPublish(const char* payload);
void mqttPublishPath(const char* suffix, const char* payload, const char *device = "sensor");
// telegram.cpp
bool getTgramUpdate(char* response);
bool sendTgramMessage(const char* info, const char* item, const char* parseMode);
bool sendTgramPhoto(uint8_t* photoData, size_t photoSize, const char* caption);
bool sendTgramFile(const char* fileName, const char* contentType, const char* caption);
void tgramAlert(const char* subject, const char* message);
// externalHeartbeat.cpp
void sendExternalHeartbeat();

/******************** Global utility declarations *******************/

extern char AP_SSID[];
extern char AP_Pass[];
extern char AP_ip[];
extern char AP_sn[];
extern char AP_gw[];

extern int netMode; // 0=WiFi, 1=Ethernet, 2=Eth+AP
extern char hostName[]; //Host name for ddns
extern char ST_SSID[]; //Router ssid
extern char ST_Pass[]; //Router passd
extern bool useHttps;
extern bool useSecure;
extern bool useFtps;

extern char ST_ip[]; //Leave blank for dhcp
extern char ST_sn[];
extern char ST_gw[];
extern char ST_ns1[];
extern char ST_ns2[];
extern char extIP[];

extern int ethCS;   // chip select
extern int ethInt;  // interrupt
extern int ethRst;  // reset
extern int ethSclk; // SPI clock
extern int ethMiso; // SPI data pin
extern int ethMosi; // SPI data pin

extern char Auth_Name[]; 
extern char Auth_Pass[];

extern int responseTimeoutSecs; // how long to wait for remote server in secs
extern bool allowAP; // set to true to allow AP to startup if cannot reconnect to STA (router)
extern uint32_t wifiTimeoutSecs; // how often to check wifi status
extern uint8_t percentLoaded;
extern int refreshVal;
extern bool dataFilesChecked;
extern char ipExtAddr[];
extern bool doGetExtIP;
extern bool usePing; // set to false if problems related to this issue occur: https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/221
extern uint16_t sustainId;
extern bool heartBeatDone;
extern TaskHandle_t heartBeatHandle;
extern TaskHandle_t checkDataHandle;
extern TaskHandle_t statusCheckHandle;
extern char portFwd[];
extern float latLon[];
extern uint32_t deepSleepTimer;

// remote file server
extern char fsServer[];
extern char ftpUser[];
extern uint16_t fsPort;
extern char FS_Pass[];
extern char fsWd[];
extern bool autoUpload;
extern bool deleteAfter;
extern bool fsUse;
extern char inFileName[];

//  SMTP server
extern char smtp_login[];
extern char SMTP_Pass[];
extern char smtp_email[];
extern char smtp_server[];
extern uint16_t smtp_port;
extern bool smtpUse; // whether or not to use smtp
extern int emailCount;

// Mqtt broker
extern bool mqtt_active;
extern char mqtt_broker[];
extern char mqtt_port[];
extern char mqtt_user[];
extern char mqtt_user_Pass[];
extern char mqtt_topic_prefix[];  

// control sending alerts 
extern size_t alertBufferSize;
extern size_t maxAlertBuffSize;
extern byte* alertBuffer;

// Telegram
extern bool tgramUse;
extern char tgramToken[];
extern char tgramChatId[];
extern char tgramHdr[];

// web serve
extern char timezone[];
extern char ntpServer[];
extern const char* setupPage_html;
extern const char* otaPage_html;
extern const char* failPageS_html;
extern const char* failPageE_html;
extern char startupFailure[];
extern char* serverCerts[];

// app status
extern uint8_t alarmHour;
extern char* jsonBuff; 
extern bool sdLog;
extern int logType;
extern char messageLog[];
extern uint16_t mlogEnd;
extern bool timeSynchronized;
extern bool monitorOpen;
extern time_t currEpoch;
extern bool RCactive;
extern int wakePin;
extern int wakeLevel;
extern UBaseType_t uxHighWaterMarkArr[];
extern UBaseType_t STACK_MEM;
extern bool appSetupDone;

// SD storage
extern int sdMinCardFreeSpace; // Minimum amount of card free Megabytes before freeSpaceMode action is enabled
extern int sdFreeSpaceMode; // 0 - No Check, 1 - Delete oldest dir, 2 - Upload to ftp and then delete folder on SD 
extern bool formatIfMountFailed ; // Auto format the file system if mount failed. Set to false to not auto format.
extern const char* storageType;

// I2C pins
extern int I2C0sda;
extern int I2C0scl;
extern int I2C1sda;
extern int I2C1scl;

#define HTTP_METHOD_STRING(method) \
  (method == HTTP_DELETE) ? "DELETE" : \
  (method == HTTP_GET) ? "GET" : \
  (method == HTTP_HEAD) ? "HEAD" : \
  (method == HTTP_POST) ? "POST" : \
  (method == HTTP_PUT) ? "PUT" : \
  (method == HTTP_CONNECT) ? "CONNECT" : \
  (method == HTTP_OPTIONS) ? "OPTIONS" : \
  (method == HTTP_TRACE) ? "TRACE" : \
  (method == HTTP_COPY) ? "COPY" : \
  (method == HTTP_LOCK) ? "LOCK" : \
  (method == HTTP_MKCOL) ? "MKCOL" : \
  (method == HTTP_MOVE) ? "MOVE" : \
  (method == HTTP_PROPFIND) ? "PROPFIND" : \
  (method == HTTP_PROPPATCH) ? "PROPPATCH" : \
  (method == HTTP_SEARCH) ? "SEARCH" : \
  (method == HTTP_UNLOCK) ? "UNLOCK" : \
  (method == HTTP_BIND) ? "BIND" : \
  (method == HTTP_REBIND) ? "REBIND" : \
  (method == HTTP_UNBIND) ? "UNBIND" : \
  (method == HTTP_ACL) ? "ACL" : \
  (method == HTTP_REPORT) ? "REPORT" : \
  (method == HTTP_MKACTIVITY) ? "MKACTIVITY" : \
  (method == HTTP_CHECKOUT) ? "CHECKOUT" : \
  (method == HTTP_MERGE) ? "MERGE" : \
  (method == HTTP_MSEARCH) ? "MSEARCH" : \
  (method == HTTP_NOTIFY) ? "NOTIFY" : \
  (method == HTTP_SUBSCRIBE) ? "SUBSCRIBE" : \
  (method == HTTP_UNSUBSCRIBE) ? "UNSUBSCRIBE" : \
  (method == HTTP_PATCH) ? "PATCH" : \
  (method == HTTP_PURGE) ? "PURGE" : \
  (method == HTTP_MKCALENDAR) ? "MKCALENDAR" : \
  (method == HTTP_LINK) ? "LINK" : \
  (method == HTTP_UNLINK) ? "UNLINK" : \
  "UNKNOWN"

/*********************** Log formatting ************************/

#include "esp_debug_helpers.h"

// log levels
enum logLevel_t : uint8_t {
  LOG_LEVEL_RAW = 0,
  LOG_LEVEL_COMMAND,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ALERT,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_VERBOSE
};

// queued data types
enum logArgType_t : uint8_t {
  LOG_ARG_INT,
  LOG_ARG_UINT,
  LOG_ARG_DOUBLE,
  LOG_ARG_STR,      // v.str_off = offset into entry strs[]
  LOG_ARG_PTR
};

struct logArg_t {
  logArgType_t type;
  uint8_t size;     // sizeof original integer type (for %x/%u of negatives)
  union {
    int64_t   i;
    uint64_t  u;
    double    d;
    uintptr_t p;
    uint16_t  str_off;
  } v;
};

struct logEntry_t {
  const char* fmt;   // static lifetime
  const char* file;             // __FILE__, static lifetime (path stripped in the log task)
  const char* func;             // __FUNCTION__, static lifetime
  uint16_t line;                // __LINE__
  const char* timestamp;
  uint8_t level;
  uint8_t nargs;
  uint8_t raw = 0;    // 1 = line captured from esp_log: already fully decorated by IDF, 2 = verbatim (LOG_RAW. LOG_CMD) 
  uint16_t str_used;
  logArg_t args[LOG_MAX_ARGS];
  char strs[LOG_STR_AREA + 1]; // last byte is always NUL (target of "empty" strings)
};

extern bool dbgVerbose;
extern volatile uint8_t g_logLevel;

void logCopyStr(logEntry_t* e, logArg_t &a, const char* s);
logEntry_t* logAcquire(logLevel_t level, const char* timestamp, const char* file, int line, const char* func, const char* fmt);
void logPost(logEntry_t* e);
void logSetLevel(logLevel_t level);
static inline void logFormatCheck(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static inline void logFormatCheck(const char *, ...) {}

template <class> struct logAlways_false : std::false_type {};

// Stores one argument with its type; no formatting happens here.
template <typename T>
inline void logPush(logEntry_t* e, T value) {
  logArg_t &a = e->args[e->nargs++];
  using D = std::remove_cv_t<T>;
  a.size = 8;
  if constexpr (std::is_floating_point_v<D>) {
    a.type = LOG_ARG_DOUBLE;
    a.v.d = (double)value;
  } else if constexpr (std::is_same_v<D, const char *> || std::is_same_v<D, char *>) {
    a.type = LOG_ARG_STR;
    logCopyStr(e, a, value);
  } else if constexpr (std::is_pointer_v<D>) {
    a.type = LOG_ARG_PTR;
    a.v.p = reinterpret_cast<uintptr_t>(value);
  } else if constexpr (std::is_enum_v<D>) {
    a.type = LOG_ARG_INT;
    a.v.i = static_cast<int64_t>(value);
  } else if constexpr (std::is_integral_v<D>) {
    a.size = (uint8_t)sizeof(D);
    if constexpr (std::is_signed_v<D>) { a.type = LOG_ARG_INT;  a.v.i = (int64_t)value; }
    else                               { a.type = LOG_ARG_UINT; a.v.u = (uint64_t)value; }
  } else {
    static_assert(logAlways_false<D>::value, "LOG: unsupported argument type (use numbers, enums, pointers or C strings)");
  }
}

template <typename... Args>
static inline void logSubmit(logLevel_t level, const char* timestamp, const char* file, int line, const char* func, const char* fmt, Args... args) {
  static_assert(sizeof...(Args) <= LOG_MAX_ARGS, "ERR: too many args, increase LOG_MAX_ARGS");
  logEntry_t* e = logAcquire(level, timestamp, file, line, func, fmt);
  if (!e) return; // pool empty or not initialised: drop, never block
  if (!timestamp[0]) e->raw = 2;
  (logPush(e, args), ...); // left-to-right
  logPost(e);
}

//  macros
#define LOG_SUBMIT(level, fmt, ...)                        \
  do {                                                     \
    if ((uint8_t)(level) <= g_logLevel) {                  \
      if (false) { logFormatCheck((fmt), ##__VA_ARGS__); } \
      if ((level == LOG_LEVEL_RAW) || (level == LOG_LEVEL_COMMAND)) logSubmit((level), "", NULL, 0, NULL, (fmt), ##__VA_ARGS__); \
      else logSubmit((level), esp_log_system_timestamp(), __FILE__, __LINE__, __FUNCTION__, (fmt), ##__VA_ARGS__); \
    }                                                      \
  } while (0)

#define LOG_RAW(fmt, ...) LOG_SUBMIT(LOG_LEVEL_RAW, fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) LOG_SUBMIT(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) LOG_SUBMIT(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...) LOG_SUBMIT(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_ALT(fmt, ...) LOG_SUBMIT(LOG_LEVEL_ALERT, fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) LOG_SUBMIT(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_VRB(fmt, ...) LOG_SUBMIT(LOG_LEVEL_VERBOSE, fmt, ##__VA_ARGS__)
#define LOG_CMD(fmt, ...) LOG_SUBMIT(LOG_LEVEL_COMMAND, fmt, ##__VA_ARGS__)
#define LOG_PRT(buff, bufflen) log_print_buf((const uint8_t*)buff, bufflen)

// function call backtrace for decoding
#define LOG_TRC(frames) \
  do { \
  Serial.setDebugOutput(true);  \
  esp_backtrace_print(frames);  \
  Serial.setDebugOutput(false); \
  } while (0)

