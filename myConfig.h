// Global declarations

#pragma once

/******************** User modifiable defines *******************/

// Select camera model by uncommenting one only
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

#define USE_PIR false // true to use PIR for motion detection
#define USE_OTA true  // true to enable OTA updates
//#define USE_DS18B20 // uncomment to include DS18B20
#define ONELINE true // MMC 1 line mode
#define minCardFreeSpace 50 // Minimum amount of card free Megabytes before freeSpaceMode action is enabled
#define freeSpaceMode 1 // 0 - No Check, 1 - Delete oldest dir, 2 - Move to ftp and then delete folder   

#define XCLK_MHZ 20 // fastest camera clock rate 

//#define USE_LOG_COLORS  // uncomment to colorise log messages (eg if using idf.py, but not arduino)

/********************* global defines *******************/
#ifdef USE_LOG_COLORS
#define LOG_COLOR_ERR  "\033[0;31m" // red
#define LOG_COLOR_WRN  "\033[0;33m" // yellow
#define LOG_COLOR_DBG  "\033[0;95m" // purple
#define LOG_COLOR_TME  "\033[0;36m" // cyan
#define LOG_NO_COLOR   "\033[0m"
#else
#define LOG_COLOR_ERR
#define LOG_COLOR_WRN
#define LOG_COLOR_DBG
#define LOG_COLOR_TME
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
#define TME_FORMAT(format) LOG_COLOR_TME "[%s] " format LOG_NO_COLOR "\n", esp_log_system_timestamp()
#define LOG_TME(format, ...) logPrint(TME_FORMAT(format), ##__VA_ARGS__)
#define LOG_PRT(buff, bufflen) if (dbgVerbose) log_print_buf((const uint8_t*)buff, bufflen)
 
#define APP_NAME "ESP32-CAM_MJPEG"
#define APP_VER "4.1b"

#define LOG_DIR "/Log"
#define LOG_FILE_NAME LOG_DIR "/log.txt"

/******************** Libraries *******************/

#include "Arduino.h"
#include <stdio.h>
#include <string.h>
#include <bits/stdc++.h> 
#include <DNSServer.h>
#include <driver/i2s.h>
#include "esp_adc_cal.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_jpg_decode.h"
#include <esp_system.h>
#include "esp_timer.h"
#include <ESPmDNS.h>
#include <FS.h>
#include "lwip/err.h"
#include <lwip/netdb.h>  
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <Preferences.h>
#include <regex>
#include <SD_MMC.h>
#include "SPIFFS.h" 
#include <sys/time.h>
#include "time.h"
#include <Update.h>
#include <vector>  // Dynamic string array
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WiFiUdp.h>


/******************** Function declarations *******************/

float battVoltage();
bool checkMotion(camera_fb_t* fb, bool captureStatus);
void controlLamp(bool lampVal);
void createUploadTask(const char* val, bool move = false);
void createScheduledUploadTask(const char* val);
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);
void deleteFolderOrFile(const char* val);
int* extractMeta(const char* fname);
bool fetchMoveMap(uint8_t **out, size_t *out_len);
void finishAudio(const char* mjpegName, bool isvalid);
void flush_log(bool andClose = false);
void reset_log();
void getLocalNTP();
size_t* getNextFrame();
bool isAVI(File &fh);
bool isNight(uint8_t nightSwitch);
void listDir(const char* fname, char* htmlBuff);
bool loadConfig();
void logPrint(const char *fmtStr, ...);
void openSDfile();
bool OTAlistener();
void OTAprereq();
void OTAsetup();
void prepDS18B20();
bool prepMjpeg();
void prepMic();
bool prepSD_MMC();
size_t readClientBuf(File &fh, byte* &clientBuf, size_t buffSize);
float readDS18B20temp(bool isCelsius);
void remote_log_init();
bool resetConfig();
bool saveConfig();
uint8_t setFPS(uint8_t val);
uint8_t setFPSlookup(uint8_t val);
void setupADC();
void showProgress(); 
void startAudio();
void startCameraServer();
void startSDtasks();
bool startWifi();
void stopPlaying();
void syncToBrowser(char *val);
void tryDS18B20();
String upTime();


/******************** Variable declarations *******************/

extern String AP_SSID;
extern char   AP_Pass[];
extern char   AP_ip[];
extern char   AP_sn[];
extern char   AP_gw[];

extern char hostName[]; //Host name for ddns
extern char ST_SSID[]; //Router ssid
extern char ST_Pass[]; //Router passd

extern char ST_ip[]; //Leave blank for dhcp
extern char ST_sn[];
extern char ST_gw[];
extern char ST_ns1[];
extern char ST_ns2[];

extern char timezone[64];

// status & control fields 
extern bool aviOn;
extern bool autoUpload;
extern bool dbgMotion;
extern bool dbgVerbose;
extern bool doPlayback;
extern bool doRecording;// = true; // whether to capture to SD or not
extern bool forceRecord; //Recording enabled by rec button
extern uint8_t FPS;
extern uint8_t fsizePtr; // index to frameData[] for record
extern uint16_t insufficient;
extern char* htmlBuff; 
extern bool isCapturing;
extern bool lampOn;
extern bool lampVal;
extern uint8_t lightLevel;  
extern byte logMode;
extern int micGain;
extern uint8_t minSeconds; // default min video length (includes MOVE_STOP_SECS time)
extern float motionVal;  // motion sensitivity setting - min percentage of changed pixels that constitute a movement
extern uint8_t nightSwitch; // initial white level % for night/day switching
extern bool nightTime; 
extern uint8_t* SDbuffer;
extern bool stopCheck;
extern bool stopPlayback;
extern bool useMotion; // whether to use camera for motion detection (with motionDetect.cpp)  

// stream separator
extern const char* _STREAM_BOUNDARY;
extern const char* _STREAM_PART;

// audio
extern const uint32_t SAMPLE_RATE; // audio sample rate
extern const uint32_t WAV_HEADER_LEN;
extern const uint32_t RAMSIZE;
extern const uint32_t SAMPLE_RATE; 
extern const uint8_t BUFFER_WIDTH;

// ftp server
extern char ftp_server[];
extern char ftp_user[];
extern char ftp_port[];
extern char ftp_pass[];
extern char ftp_wd[];

// task handling
extern TaskHandle_t getDS18Handle;
extern SemaphoreHandle_t frameMutex;
extern SemaphoreHandle_t motionMutex;

struct frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor; // (0..3)
  const uint8_t sampleRate; // (1..N)
};

// indexed by frame size - needs to be consistent with sensor.h framesize_t enum
const frameStruct frameData[] = {
  {"96X96", 96, 96, 30, 1, 1}, 
  {"QQVGA", 160, 120, 30, 1, 1},
  {"QCIF", 176, 144, 30, 1, 1}, 
  {"HQVGA", 240, 176, 30, 2, 1}, 
  {"240X240", 240, 240, 30, 2, 1}, 
  {"QVGA", 320, 240, 30, 2, 1}, 
  {"CIF", 400, 296, 30, 2, 1},  
  {"HVGA", 480, 320, 30, 2, 1}, 
  {"VGA", 640, 480, 20, 3, 1}, 
  {"SVGA", 800, 600, 20, 3, 1}, 
  {"XGA", 1024, 768, 5, 3, 1},   
  {"HD", 1280, 720, 5, 3, 1}, 
  {"SXGA", 1280, 1024, 5, 3, 1}, 
  {"UXGA", 1600, 1200, 5, 3, 1}  
};
