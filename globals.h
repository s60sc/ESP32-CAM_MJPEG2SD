// Global declarations
//
// s60sc 2021, 2022

#pragma once

/******************** User modifiable defines *******************/
extern const char* git_rootCACertificate;

//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
#define XCLK_MHZ 20 // fastest camera clock rate 

#define ALLOW_SPACES false // set set true to allow whitespace in configs.txt key values

//#define USE_LOG_COLORS  // uncomment to colorise log messages (eg if using idf.py, but not arduino)


/** Do not change anything below here unless you know what you are doing **/

/********************* fixed defines leave as is *******************/ 
 
#define APP_NAME "ESP-CAM_MJPEG" // max 15 chars
#define APP_VER "7.0"

#define DATA_DIR "/data"
#define HTML_EXT ".htm"
#define TEXT_EXT ".txt"
#define JS_EXT ".js"
#define CSS_EXT ".css"
#define ICO_EXT ".ico"
#define INDEX_PAGE_PATH DATA_DIR "/MJPEG2SD" HTML_EXT
#define CONFIG_FILE_PATH DATA_DIR "/configs" TEXT_EXT
#define LOG_FILE_PATH DATA_DIR "/log" TEXT_EXT
#define FILE_NAME_LEN 64
#define ONEMEG (1024 * 1024)
#define MAX_PWD_LEN 64
#define JSON_BUFF_LEN (32 * 1024) // set big enough to hold all file names in a folder
#define GITHUB_URL "https://raw.githubusercontent.com/s60sc/ESP32-CAM_MJPEG2SD/master"

#define FILE_EXT "avi"
#define BOUNDARY_VAL "123456789000000000000987654321"
#define AVI_HEADER_LEN 310 // AVI header length
#define CHUNK_HDR 8 // bytes per jpeg hdr in AVI 
#define WAVTEMP "/current.wav"
#define AVITEMP "/current.avi"
#define TLTEMP "/current.tl"

#define FILLSTAR "****************************************************************"
#define STORAGE SD_MMC // use of SPIFFS or SD_MMC 
#define RAMSIZE (1024 * 8) // set this to multiple of SD card sector size (512 or 1024 bytes)
#define CHUNKSIZE (1024 * 4)
#define FLUSH_DELAY 0 // for debugging crashes
#define MAX_CONFIGS 150 
#define INCLUDE_FTP 
#define INCLUDE_SMTP
//#define DEV_ONLY // leave commented out

#define IS_IO_EXTENDER false // must be false for client
#define EXTPIN 100
// which web assets to download
#define USE_LOG true
#define USE_WSL true
#define USE_JQUERY true
#define USE_COMMON false
#define USE_CONFIG true


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
#include <SPIFFS.h>
#include <sstream>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "esp_camera.h"

/******************** Function declarations *******************/

struct mjpegStruct {
  size_t buffLen;
  size_t buffOffset;
  size_t jpegSize;
};

struct fnameStruct {
  uint8_t recFPS;
  uint32_t recDuration;
  uint16_t frameCnt;
};

// global app specific functions
void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL = false);
void buildAviIdx(size_t dataSize, bool isVid = true, bool isTL = false);
bool checkMotion(camera_fb_t* fb, bool motionStatus);
bool checkSDFiles();
bool fetchMoveMap(uint8_t **out, size_t *out_len);
void finalizeAviIndex(uint16_t frameCnt, bool isTL = false);
void finishAudio(bool isValid);
mjpegStruct getNextFrame(bool firstCall = false);
bool getPIRval();
bool haveWavFile(bool isTL = false);
void openSDfile(const char* streamFile);
void prepAviIndex(bool isTL = false);
bool prepRecording();
void prepMic();
float readDS18B20temp(bool isCelsius);
void setCamPan(int panVal);
void setCamTilt(int tiltVal);
uint8_t setFPS(uint8_t val);
uint8_t setFPSlookup(uint8_t val);
void setLamp(bool lampVal);
void setupADC();
void startAudio();
void startSDtasks();
void startStreamServer();
void stopPlaying();
size_t writeAviIndex(byte* clientBuf, size_t buffSize, bool isTL = false);
size_t writeWavFile(byte* clientBuf, size_t buffSize);

// global general utility functions in utils.cpp / utilsSD.cpp / peripherals.cpp
void buildAppJsonString(bool filter);
void buildJsonString(uint8_t filter);
bool checkDataFiles();
bool checkFreeSpace();
void checkMemory();
void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder);
void deleteFolderOrFile(const char* deleteThis);
void devSetup();
void doRestart(String restartStr);
void emailAlert(const char* _subject, const char* _message);
const char* encode64(const char* inp);
const uint8_t* encode64chunk(const uint8_t* inp, int rem);
bool externalPeripheral(byte pinNum, uint32_t outputData = 0);
void flush_log(bool andClose = false);
void formatHex(const char* inData, size_t inLen);
bool ftpFileOrFolder(const char* fileFolder);
bool getLocalNTP();
void getOldestDir(char* oldestDir);
void getUpTime(char* timeVal);
void goToSleep(int wakeupPin, bool deepSleep);
void listBuff(const uint8_t* b, size_t len); 
bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension);
bool loadConfig();
void logPrint(const char *fmtStr, ...);
void OTAprereq();
void prepPeripherals();
bool prepSD_MMC();
void prepSMTP();
void prepUart();
void processAppWSmsg(uint8_t* wsMsg);
void remote_log_init();
void removeChar(char *s, char c);
void reset_log();
void setPeripheralResponse(const byte pinNum, const uint32_t responseData);

void showProgress();
void startOTAtask();
void startSecTimer(bool startTimer);
bool startSpiffs(bool deleteAll = false);
void startWebServer();
bool startWifi();
void syncToBrowser(const char *val);
bool updateAppStatus(const char* variable, const char* value);
bool updateConfigVect(const char* variable, const char* value);
bool updateStatus(const char* variable, const char* _value);
void urlDecode(char* inVal);
uint32_t usePeripheral(const byte pinNum, const uint32_t receivedData);
esp_err_t webAppSpecificHandler(httpd_req_t *req, const char* variable, const char* value);
void wsAsyncSend(char* wsData);

/******************** Global utility declarations *******************/

extern String AP_SSID;
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

extern char Auth_User[]; 
extern char Auth_Pass[];

extern int responseTimeoutSecs; // time to wait for FTP or SMTP response
extern bool allowAP; // set to true to allow AP to startup if cannot reconnect to STA (router)
extern int wifiTimeoutSecs; // how often to check wifi status
extern uint8_t percentLoaded;
extern int refreshVal;

#ifdef INCLUDE_FTP    
// ftp server
extern char ftp_server[];
extern char ftp_user[];
extern uint16_t ftp_port;
extern char ftp_pass[];
extern char ftp_wd[];
#endif 

#ifdef INCLUDE_SMTP
//  SMTP server
extern char smtp_login[];
extern char smtp_pass[];
extern char smtp_email[];
extern char smtp_server[];
extern uint16_t smtp_port;
#endif
extern size_t smtpBufferSize;
extern byte* SMTPbuffer;

// control sending emails 
extern bool smtpUse; // whether or not to send email alerts
extern int smtpFrame; // which captured frame number to use for email image
extern int smtpMaxEmails; // too many could cause account suspension

extern char timezone[];
extern char* jsonBuff; 
extern bool dbgVerbose;
extern bool logMode;
extern bool timeSynchronized;
extern bool monitorOpen; 
extern const char* defaultPage_html;

// SD storage
extern int sdMinCardFreeSpace; // Minimum amount of card free Megabytes before freeSpaceMode action is enabled
extern int sdFreeSpaceMode; // 0 - No Check, 1 - Delete oldest dir, 2 - Upload to ftp and then delete folder on SD 
extern bool sdFormatIfMountFailed ; // Auto format the sd card if mount failed. Set to false to not auto format.

// peripheral stored values
extern bool useIOextender; // true to use IO Extender, otherwise false
extern float currentVoltage; 
extern float dsTemp;
extern bool pirVal;

/******************** Global app declarations *******************/

// motion detection parameters
extern int moveStartChecks; // checks per second for start motion
extern int moveStopSecs; // secs between each check for stop, also determines post motion time
extern int maxFrames; // maximum number of frames in video before auto close 

// motion recording parameters
extern int detectMotionFrames; // min sequence of changed frames to confirm motion 
extern int detectNightFrames; // frames of sequential darkness to avoid spurious day / night switching
extern int detectNumBands;
extern int detectStartBand;
extern int detectEndBand; // inclusive
extern int detectChangeThreshold; // min difference in pixel comparison to indicate a change

// record timelapse avi independently of motion capture, file name has same format as avi except ends with T
extern int tlSecsBetweenFrames; // too short interval will interfere with other activities
extern int tlDurationMins; // a new file starts when previous ends
extern int tlPlaybackFPS;  // rate to playback the timelapse, min 1 

// status & control fields 
extern bool autoUpload;
extern bool dbgMotion;
extern bool doPlayback;
extern bool doRecording; // whether to capture to SD or not
extern bool forceRecord; //Recording enabled by rec button
extern uint8_t FPS;
extern uint8_t fsizePtr; // index to frameData[] for record
extern bool isCapturing;
extern uint8_t lightLevel;  
extern int micGain;
extern uint8_t minSeconds; // default min video length (includes moveStopSecs time)
extern float motionVal;  // motion sensitivity setting - min percentage of changed pixels that constitute a movement
extern uint8_t nightSwitch; // initial white level % for night/day switching
extern bool nightTime; 
extern bool stopPlayback;
extern bool useMotion; // whether to use camera for motion detection (with motionDetect.cpp)  
extern bool timeLapseOn; // enable time lapse recording
extern int maxFrames;
extern char inFileName[];

// buffers
extern uint8_t iSDbuffer[];
extern byte chunk[];
extern uint8_t aviHeader[];
extern const uint8_t dcBuf[]; // 00dc
extern const uint8_t wbBuf[]; // 01wb
extern byte* uartData;

// peripherals

// IO Extender use
extern bool useIOextender; // true to use IO Extender, otherwise false
extern bool useUART0;
extern int uartTxdPin;
extern int uartRxdPin;
// peripherals used
extern bool pirUse; // true to use PIR for motion detection
extern bool lampUse; // true to use lamp
extern bool lampAuto; // if true in conjunction with usePir & useLamp, switch on lamp when PIR activated
extern bool servoUse; // true to use pan / tilt servo control
// microphone cannot be used on IO Extender
extern bool micUse; // true to use external I2S microphone 

// sensors 
extern int pirPin; // if usePir is true
extern int lampPin; // if useLamp is true
// Pan / Tilt Servos 
extern int servoPanPin; // if useServos is true
extern int servoTiltPin;
// ambient / module temperature reading 
extern int ds18b20Pin; // if INCLUDE_DS18B20 uncommented
// batt monitoring 
extern int voltPin; // if INCLUDE_VOLTAGE uncommented

// microphone recording
extern int micSckPin; // I2S SCK
extern int micWsPin;  // I2S WS
extern int micSdPin;  // I2S SD

// configure for specific servo model, eg for SG90
extern int servoDelay;
extern int servoMinAngle; // degrees
extern int servoMaxAngle;
extern int servoMinPulseWidth; // usecs
extern int servoMaxPulseWidth;

// battery monitor
extern int voltDivider;
extern int voltLow;
extern int voltInterval;

// audio
extern const uint32_t SAMPLE_RATE; // audio sample rate
extern const uint32_t WAV_HEADER_LEN;
extern const uint32_t SAMPLE_RATE; 
extern const uint8_t BUFFER_WIDTH;

// task handling
extern TaskHandle_t playbackHandle;
extern TaskHandle_t DS18B20handle;
extern TaskHandle_t servoHandle;
extern TaskHandle_t uartClientHandle;
extern TaskHandle_t emailHandle;
extern TaskHandle_t ftpHandle;
extern SemaphoreHandle_t frameMutex;
extern SemaphoreHandle_t motionMutex;


/************************** structures ********************************/

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

/*********************** Log formatting ************************/

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
#define LOG_PRT(buff, bufflen) log_print_buf((const uint8_t*)buff, bufflen)
  
