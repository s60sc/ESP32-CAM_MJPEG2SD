// Global generic declarations
//
// s60sc 2021, 2022

#pragma once
// to compile with -Wall -Werror=all -Wextra
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
//#pragma GCC diagnostic ignored "-Wunused-variable"
//#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

/******************** Libraries *******************/

#include "Arduino.h"
#include <driver/i2s.h>
#include "esp_http_server.h"
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
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// global mandatory app specific functions, in appSpecific.cpp 
bool appDataFiles();
void buildAppJsonString(bool filter);
bool updateAppStatus(const char* variable, const char* value);
esp_err_t webAppSpecificHandler(httpd_req_t *req, const char* variable, const char* value);
void wsAppSpecificHandler(const char* wsMsg);

// global general utility functions in utils.cpp / utilsSD.cpp / peripherals.cpp    
void buildJsonString(uint8_t filter);
bool checkDataFiles();
bool checkFreeSpace();
void checkMemory();
void debugMemory(const char* caller);
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);
void deleteFolderOrFile(const char* deleteThis);
void devSetup();
void doRestart(String restartStr);
void emailAlert(const char* _subject, const char* _message);
const char* encode64(const char* inp);
const uint8_t* encode64chunk(const uint8_t* inp, int rem);
bool externalPeripheral(byte pinNum, uint32_t outputData = 0);
void flush_log(bool andClose = false);
void formatElapsedTime(char* timeStr, uint32_t timeVal);
void formatHex(const char* inData, size_t inLen);
bool ftpFileOrFolder(const char* fileFolder, bool _deleteAfter = false);
const char* getEncType(int ssidIndex);
time_t getEpoch();
bool getLocalNTP();
void getOldestDir(char* oldestDir);
void goToSleep(int wakeupPin, bool deepSleep);
void initStatus(int cfgGroup, int delayVal);
void listBuff(const uint8_t* b, size_t len); 
bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension);
bool loadConfig();
void logPrint(const char *fmtStr, ...);
void logSetup();
void OTAprereq();
bool parseJson(int rxSize);
void prepPeripherals();
void prepSMTP();
void prepUart();
void remote_log_init();
void removeChar(char *s, char c);
void reset_log();
void setPeripheralResponse(const byte pinNum, const uint32_t responseData);
void showProgress();
float smooth(float latestVal, float smoothedVal, float alpha);
void startFTPtask();
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
void wsAsyncSend(const char* wsData);

/******************** Global utility declarations *******************/

extern char AP_SSID[];
extern char AP_Pass[];
extern char AP_ip[];
extern char AP_sn[];
extern char AP_gw[];

extern char hostName[]; //Host name for ddns
extern char ST_SSID[]; //Router ssid
extern char ST_Pass[]; //Router passd

extern char ST_ip[]; //Leave blank for dhcp
extern char ST_sn[];
extern char ST_gw[];
extern char ST_ns1[];
extern char ST_ns2[];

extern char Auth_Name[]; 
extern char Auth_Pass[];

extern int responseTimeoutSecs; // time to wait for FTP or SMTP response
extern bool allowAP; // set to true to allow AP to startup if cannot reconnect to STA (router)
extern int wifiTimeoutSecs; // how often to check wifi status
extern uint8_t percentLoaded;
extern int refreshVal;
extern bool configLoaded;
extern bool dataFilesChecked;
extern bool allowSpaces;// set set true to allow whitespace in configs.txt key values
  
// ftp server
extern char ftp_server[];
extern char ftp_user[];
extern uint16_t ftp_port;
extern char FTP_Pass[];
extern char ftp_wd[];

//  SMTP server
extern char smtp_login[];
extern char SMTP_Pass[];
extern char smtp_email[];
extern char smtp_server[];
extern uint16_t smtp_port;

// control sending emails 
extern size_t smtpBufferSize;
extern byte* SMTPbuffer;
extern bool smtpUse; // whether or not to send email alerts
extern int smtpFrame; // which captured frame number to use for email image
extern int smtpMaxEmails; // too many could cause account suspension

extern char timezone[];
extern char ntpServer[];
extern char* jsonBuff; 
extern bool dbgVerbose;
extern bool logMode;
extern bool timeSynchronized;
extern bool monitorOpen; 
extern const char* defaultPage_html;
extern const char* otaPage_html;
extern SemaphoreHandle_t wsSendMutex;
extern char startupFailure[];

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
#define WRN_FORMAT(format) LOG_COLOR_WRN "[%s WARN %s] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), __FUNCTION__
#define LOG_WRN(format, ...) logPrint(WRN_FORMAT(format), ##__VA_ARGS__)
#define ERR_FORMAT(format) LOG_COLOR_ERR "[%s ERROR @ %s:%u] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), pathToFileName(__FILE__), __LINE__
#define LOG_ERR(format, ...) logPrint(ERR_FORMAT(format), ##__VA_ARGS__)
#define DBG_FORMAT(format) LOG_COLOR_DBG "[%s DEBUG @ %s:%u] " format LOG_NO_COLOR "\n", esp_log_system_timestamp(), pathToFileName(__FILE__), __LINE__
#define LOG_DBG(format, ...) if (dbgVerbose) logPrint(DBG_FORMAT(format), ##__VA_ARGS__)
#define CHK_FORMAT(format) LOG_COLOR_ERR "[######### CHECK @ %s:%u] " format LOG_NO_COLOR "\n", pathToFileName(__FILE__), __LINE__
#define LOG_CHK(format, ...) logPrint(CHK_FORMAT(format), ##__VA_ARGS__)
#define LOG_PRT(buff, bufflen) log_print_buf((const uint8_t*)buff, bufflen)
