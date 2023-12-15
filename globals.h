// Global generic declarations
//
// s60sc 2021, 2022

#pragma once
// to compile with -Wall -Werror=all -Wextra
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
//#pragma GCC diagnostic ignored "-Wunused-variable"
//#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
//#pragma GCC diagnostic ignored "-Wignored-qualifiers"
//#pragma GCC diagnostic ignored "-Wclass-memaccess"

/******************** Libraries *******************/

#include "Arduino.h"
#include <driver/i2s.h>
#include <ESPmDNS.h> 
#include "lwip/sockets.h"
#include <vector>
#include "ping/ping_sock.h"
#include <Preferences.h>
#include <regex>
#include <SD_MMC.h>
#include <LittleFS.h>
#include <sstream>
#include <Update.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_http_server.h>
#include <esp_https_server.h>

// ADC
#define ADC_ATTEN ADC_11db
#define ADC_SAMPLES 16
#if CONFIG_IDF_TARGET_ESP32S3
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
#define CONFIG_FILE_PATH DATA_DIR "/configs" TEXT_EXT
#define LOG_FILE_PATH DATA_DIR "/log" TEXT_EXT
#define OTA_FILE_PATH DATA_DIR "/OTA" HTML_EXT
#define COMMON_JS_PATH DATA_DIR "/common" JS_EXT 
#define GITHUB_HOST "raw.githubusercontent.com"

#define FILLSTAR "****************************************************************"
#define DELIM '~'
#define ONEMEG (1024 * 1024)
#define MAX_PWD_LEN 64
#define MAX_HOST_LEN 32
#define MAX_IP_LEN 16
#define BOUNDARY_VAL "123456789000000000000987654321"
#define SF_LEN 100
#define RAM_LOG_LEN (1024 * 7) // size of system message log in bytes stored in slow RTC ram (max 8KB - vars)
#define MIN_STACK_FREE 512

// global mandatory app specific functions, in appSpecific.cpp 
bool appDataFiles();
esp_err_t appSpecificHeaderHandler(httpd_req_t *req);
void appSpecificTelegramTask(void* p);
esp_err_t appSpecificSustainHandler(httpd_req_t* req);
esp_err_t appSpecificWebHandler(httpd_req_t *req, const char* variable, const char* value);
void appSpecificWsHandler(const char* wsMsg);
void buildAppJsonString(bool filter);
bool updateAppStatus(const char* variable, const char* value);

// global general utility functions in utils.cpp / utilsFS.cpp / peripherals.cpp    
void buildJsonString(uint8_t filter);
bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress);
bool changeExtension(char* fileName, const char* newExt);
bool checkAlarm();
bool checkDataFiles();
bool checkFreeStorage();
void checkMemory(const char* source = "");
uint32_t checkStackUse(TaskHandle_t thisTask, int taskIdx);
void debugMemory(const char* caller);
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);
void deleteFolderOrFile(const char* deleteThis);
void devSetup();
void doAppPing();
void doRestart(const char* restartStr);
esp_err_t downloadFile(File& df, httpd_req_t* req);
void emailAlert(const char* _subject, const char* _message);
const char* encode64(const char* inp);
const uint8_t* encode64chunk(const uint8_t* inp, int rem);
const char* espErrMsg(esp_err_t errCode);
void externalAlert(const char* subject, const char* message);
bool externalPeripheral(byte pinNum, uint32_t outputData = 0);
esp_err_t extractHeaderVal(httpd_req_t *req, const char* variable, char* value);
esp_err_t extractQueryKeyVal(httpd_req_t *req, char* variable, char* value);
esp_err_t fileHandler(httpd_req_t* req, bool download = false);
void flush_log(bool andClose = false);
char* fmtSize (uint64_t sizeVal);
void forceCrash();
void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays = false);
void formatHex(const char* inData, size_t inLen);
bool fsFileOrFolder(const char* fileFolder);
const char* getEncType(int ssidIndex);
void getExtIP();
time_t getEpoch();
size_t getFreeStorage();
bool getLocalNTP();
float getNTCcelsius(uint16_t resistance, float oldTemp);
void getOldestDir(char* oldestDir);
void goToSleep(int wakeupPin, bool deepSleep);
void initStatus(int cfgGroup, int delayVal);
void killWebSocket();
void listBuff(const uint8_t* b, size_t len); 
bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension);
bool loadConfig();
void logLine();
void logPrint(const char *fmtStr, ...);
void logSetup();
void OTAprereq();
bool parseJson(int rxSize);
void prepPeripherals();
void prepSMTP();
bool prepTelegram();
void prepTemperature();
void prepUart();
void prepUpload();
void reloadConfigs();
float readTemperature(bool isCelsius);
float readVoltage();
void remote_log_init();
void remoteServerClose(WiFiClientSecure& sclient);
bool remoteServerConnect(WiFiClientSecure& sclient, const char* serverName, uint16_t serverPort, const char* serverCert);
void removeChar(char* s, char c);
void replaceChar(char* s, char c, char r);
void reset_log();
void resetWatchDog();
bool retrieveConfigVal(const char* variable, char* value);
esp_err_t sendChunks(File df, httpd_req_t *req, bool endChunking = true);
void setFolderName(const char* fname, char* fileName);
void setPeripheralResponse(const byte pinNum, const uint32_t responseData);
void setupADC();
void showProgress(const char* marker = ".");
uint16_t smoothAnalog(int analogPin, int samples = ADC_SAMPLES);
float smoothSensor(float latestVal, float smoothedVal, float alpha);
void startOTAtask();
void startSecTimer(bool startTimer);
bool startStorage();
void startWebServer();
bool startWifi(bool firstcall = true);
void stopPing();
void syncToBrowser(uint32_t browserUTC);
bool updateConfigVect(const char* variable, const char* value);
void updateStatus(const char* variable, const char* _value);
void urlDecode(char* inVal);
uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData);
esp_sleep_wakeup_cause_t wakeupResetReason();
void wsAsyncSend(const char* wsData);
// mqtt.cpp
void startMqttClient();  
void stopMqttClient();  
void mqttPublish(const char* payload);
// telegram.cpp
bool getTgramUpdate(char* response);
bool sendTgramMessage(const char* info, const char* item, const char* parseMode);
bool sendTgramPhoto(uint8_t* photoData, size_t photoSize, const char* caption);
bool sendTgramFile(const char* fileName, const char* contentType, const char* caption);
void tgramAlert(const char* subject, const char* message);

/******************** Global utility declarations *******************/

extern char AP_SSID[];
extern char AP_Pass[];
extern char AP_ip[];
extern char AP_sn[];
extern char AP_gw[];

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

extern char Auth_Name[]; 
extern char Auth_Pass[];

extern int responseTimeoutSecs; // how long to wait for remote server in secs
extern bool allowAP; // set to true to allow AP to startup if cannot reconnect to STA (router)
extern int wifiTimeoutSecs; // how often to check wifi status
extern uint8_t percentLoaded;
extern int refreshVal;
extern bool configLoaded;
extern bool dataFilesChecked;
extern char ipExtAddr[];
extern bool doGetExtIP;
extern bool usePing; // set to false if problems related to this issue occur: https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/221
extern bool wsLog;
extern uint32_t sustainId;

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
extern byte* alertBuffer;

// Telegram
extern bool tgramUse;
extern char tgramToken[];
extern char tgramChatId[];
extern char tgramHdr[];

// certificates
extern const char* git_rootCACertificate;
extern const char* ftps_rootCACertificate;
extern const char* smtp_rootCACertificate;
extern const char* mqtt_rootCACertificate;
extern const char* telegram_rootCACertificate;
extern const char* hfs_rootCACertificate;
extern const char* prvtkey_pem; // app https server private key
extern const char* cacert_pem; // app https server public certificate

// app status
extern char timezone[];
extern char ntpServer[];
extern uint8_t alarmHour;
extern char* jsonBuff; 
extern bool dbgVerbose;
extern bool sdLog;
extern char alertMsg[];
extern bool ramLog;
extern int logType;
extern char messageLog[];
extern uint16_t mlogEnd;
extern bool timeSynchronized;
extern bool monitorOpen; 
extern const char* setupPage_html;
extern const char* otaPage_html;
extern SemaphoreHandle_t wsSendMutex;
extern char startupFailure[];
extern time_t currEpoch;

extern UBaseType_t uxHighWaterMarkArr[];

// SD storage
extern int sdMinCardFreeSpace; // Minimum amount of card free Megabytes before freeSpaceMode action is enabled
extern int sdFreeSpaceMode; // 0 - No Check, 1 - Delete oldest dir, 2 - Upload to ftp and then delete folder on SD 
extern bool formatIfMountFailed ; // Auto format the file system if mount failed. Set to false to not auto format.

/*********************** Log formatting ************************/

//#define USE_LOG_COLORS  // uncomment to colorise log messages (eg if using idf.py, but not arduino)
#ifdef USE_LOG_COLORS
#define LOG_COLOR_ERR  "\033[0;31m" // red
#define LOG_COLOR_WRN  "\033[0;33m" // yellow
#define LOG_COLOR_DBG  "\033[0;36m" // cyan
#define LOG_NO_COLOR   "\033[0m"
#else
#define LOG_COLOR_ERR
#define LOG_COLOR_WRN
#define LOG_COLOR_DBG
#define LOG_NO_COLOR
#endif 

#define INF_FORMAT(format) "[%s %s] " format "\n", esp_log_system_timestamp(), __FUNCTION__
#define LOG_INF(format, ...) logPrint(INF_FORMAT(format), ##__VA_ARGS__)
#define LOG_ALT(format, ...) logPrint(INF_FORMAT(format "~"), ##__VA_ARGS__)
#define WRN_FORMAT(format) LOG_COLOR_WRN "[%s WARN %s] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), __FUNCTION__
#define LOG_WRN(format, ...) logPrint(WRN_FORMAT(format "~"), ##__VA_ARGS__)
#define ERR_FORMAT(format) LOG_COLOR_ERR "[%s ERROR @ %s:%u] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), pathToFileName(__FILE__), __LINE__
#define LOG_ERR(format, ...) logPrint(ERR_FORMAT(format "~"), ##__VA_ARGS__)
#define DBG_FORMAT(format) LOG_COLOR_DBG "[%s DEBUG @ %s:%u] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), pathToFileName(__FILE__), __LINE__
#define LOG_DBG(format, ...) if (dbgVerbose) logPrint(DBG_FORMAT(format), ##__VA_ARGS__)
#define CHK_FORMAT(format) LOG_COLOR_ERR "[###### CHECK @ %s:%u] " format LOG_NO_COLOR "\n", pathToFileName(__FILE__), __LINE__
#define LOG_CHK(format, ...) do { logPrint(CHK_FORMAT(format), ##__VA_ARGS__); delay(FLUSH_DELAY); } while (0)
#define LOG_PRT(buff, bufflen) log_print_buf((const uint8_t*)buff, bufflen)
