// Global MJPEG2SD declarations
//
// s60sc 2021, 2022, 2024

#pragma once
#include "globals.h"

/**************************************************************************
 Copy & Paste one of the camera models below into the following #define block
 Selecting wrong model may crash your device due to pin conflict
***************************************************************************/
/*
 * ESP32 models
CAMERA_MODEL_AI_THINKER 
CAMERA_MODEL_WROVER_KIT 
CAMERA_MODEL_ESP_EYE 
CAMERA_MODEL_M5STACK_PSRAM 
CAMERA_MODEL_M5STACK_V2_PSRAM 
CAMERA_MODEL_M5STACK_WIDE 
CAMERA_MODEL_M5STACK_ESP32CAM
CAMERA_MODEL_M5STACK_UNITCAM
CAMERA_MODEL_TTGO_T_JOURNAL 
CAMERA_MODEL_ESP32_CAM_BOARD
CAMERA_MODEL_TTGO_T_CAMERA_PLUS

* ESP32S3 models
CAMERA_MODEL_XIAO_ESP32S3 
CAMERA_MODEL_FREENOVE_ESP32S3_CAM
CAMERA_MODEL_ESP32S3_EYE 
CAMERA_MODEL_ESP32S3_CAM_LCD
*/

// User's ESP32 cam board
#if defined(CONFIG_IDF_TARGET_ESP32)
#define CAMERA_MODEL_AI_THINKER 

// User's ESP32S3 cam board
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CAMERA_MODEL_FREENOVE_ESP32S3_CAM
//#define CAMERA_MODEL_XIAO_ESP32S3 
#endif

/***************************************************************
  To reduce code size by removing unwanted features,
  set relevant defines below to false and optionally delete associated file
***************************************************************/
#define INCLUDE_FTP_HFS true // ftp.cpp (file upload)
#define INCLUDE_SMTP true    // smtp.cpp (email)
#define INCLUDE_MQTT true    // mqtt.cpp 
#define INCLUDE_TGRAM true   // telegram.cpp
#define INCLUDE_CERTS true   // certificates.cpp (https and server certificate checking)
#define INCLUDE_TELEM true   // telemetry.cpp
#define INCLUDE_AUDIO true   // audio.cpp (microphone)
#define INCLUDE_UART true    // uart.cpp (use another esp32 as IO extender)
#define INCLUDE_WEBDAV true  // webDav.cpp (WebDAV protocol)
#define INCLUDE_EXTHB true   // externalHeartbeat.cpp (heartbeat to remote server)
#define INCLUDE_PGRAM true   // photogram.cpp (photogrammetry feature)

#define INCLUDE_TINYML false  // if true, requires relevant Edge Impulse TinyML Arduino library to be installed
#define INCLUDE_DS18B20 false // if true, requires additional libraries: OneWire and DallasTemperature

/**************************************************************************/

#define ALLOW_SPACES false  // set true to allow whitespace in configs.txt key values

// web server ports 
#define HTTP_PORT 80 // insecure app access
#define HTTPS_PORT 443 // secure app access

/*********************** Fixed defines leave as is ***********************/ 
/** Do not change anything below here unless you know what you are doing **/

//#define DEV_ONLY // leave commented out
//#define SIDE_ALARM // leave commented out
#define STATIC_IP_OCTAL "133" // dev only
#define DEBUG_MEM false // leave as false
#define FLUSH_DELAY 0 // for debugging crashes
#define DBG_ON false // esp debug output
#define DOT_MAX 50
#define HOSTNAME_GRP 99
//#define REPORT_IDLE // core processor idle time monitoring
#define USE_IP6 true
 
#define APP_NAME "ESP-CAM_MJPEG" // max 15 chars
#define APP_VER "9.9.2"

#define HTTP_CLIENTS 2 // http(s), ws(s)
#define MAX_STREAMS 4 // (web stream, playback, download), NVR, audio, subtitle
#define INDEX_PAGE_PATH DATA_DIR "/MJPEG2SD" HTML_EXT
#define FILE_NAME_LEN 64
#define IN_FILE_NAME_LEN (FILE_NAME_LEN * 2)
#define JSON_BUFF_LEN (32 * 1024) // set big enough to hold all file names in a folder
#define MAX_CONFIGS 190 // must be > number of entries in configs.txt
#define MAX_JPEG (ONEMEG / 2) // UXGA jpeg frame buffer at highest quality 375kB rounded up
#define MIN_RAM 8 // min object size stored in ram instead of PSRAM default is 4096
#define MAX_RAM 4096 // max object size stored in ram instead of PSRAM default is 4096
#define TLS_HEAP (64 * 1024) // min free heap for TLS session
#define WARN_HEAP (32 * 1024) // low free heap warning
#define WARN_ALLOC (16 * 1024) // low free max allocatable free heap block
#define MAX_FRAME_WAIT 1200
#define RGB888_BYTES 3 // number of bytes per pixel
#define GRAYSCALE_BYTES 1 // number of bytes per pixel 
#define MAX_ALERT MAX_JPEG

#ifdef SIDE_ALARM
#define STORAGE LittleFS
#else
#define STORAGE SD_MMC
#endif
#define GITHUB_PATH "/s60sc/ESP32-CAM_MJPEG2SD/master"
#define RAMSIZE (1024 * 8) // set this to multiple of SD card sector size (512 or 1024 bytes)
#define CHUNKSIZE (1024 * 4)
#define ISCAM // cam specific code in generics

#define IS_IO_EXTENDER false // must be false except for IO_Extender
#define EXTPIN 100

// to determine if newer data files need to be loaded
#define CFG_VER 16

#define AVI_EXT "avi"
#define CSV_EXT "csv"
#define SRT_EXT "srt"
#define AVI_HEADER_LEN 310 // AVI header length
#define CHUNK_HDR 8 // bytes per jpeg hdr in AVI 
#define WAVTEMP "/current.wav"
#define AVITEMP "/current.avi"
#define TLTEMP "/current.tl"
#define TELETEMP "/current.csv"
#define SRTTEMP "/current.srt"

#define DMA_BUFF_LEN 1024 // used for I2S buffer size
#define DMA_BUFF_CNT 4
#define MIC_GAIN_CENTER 3 // mid point

#ifdef CONFIG_IDF_TARGET_ESP32S3 
#define SERVER_STACK_SIZE (1024 * 8)
#define DS18B20_STACK_SIZE (1024 * 2)
#define STICK_STACK_SIZE (1024 * 4)
#else
#define SERVER_STACK_SIZE (1024 * 4)
#define DS18B20_STACK_SIZE (1024)
#define STICK_STACK_SIZE (1024 * 2)
#endif
#define BATT_STACK_SIZE (1024 * 2)
#define CAPTURE_STACK_SIZE (1024 * 4)
#define EMAIL_STACK_SIZE (1024 * 6)
#define FS_STACK_SIZE (1024 * 4)
#define LOG_STACK_SIZE (1024 * 3)
#define AUDIO_STACK_SIZE (1024 * 4)
#define MICREM_STACK_SIZE (1024 * 2)
#define MQTT_STACK_SIZE (1024 * 4)
#define PING_STACK_SIZE (1024 * 5)
#define PLAYBACK_STACK_SIZE (1024 * 2)
#define SERVO_STACK_SIZE (1024)
#define SUSTAIN_STACK_SIZE (1024 * 4)
#define TGRAM_STACK_SIZE (1024 * 6)
#define TELEM_STACK_SIZE (1024 * 4)
#define UART_STACK_SIZE (1024 * 2)

// task priorities
#define CAPTURE_PRI 6
#define SUSTAIN_PRI 5
#define HTTP_PRI 5
#define MICREM_PRI 5
#define STICK_PRI 5
#define PLAY_PRI 4
#define TELEM_PRI 3
#define AUDIO_PRI 2
#define TGRAM_PRI 1
#define EMAIL_PRI 1
#define FTP_PRI 1
#define LOG_PRI 1
#define MQTT_PRI 1
#define LED_PRI 1
#define SERVO_PRI 1
#define UART_PRI 1
#define DS18B20_PRI 1
#define BATT_PRI 1
#define IDLEMON_PRI 5

/******************** Libraries *******************/

#include "esp_camera.h"
#include "camera_pins.h"

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

enum audioAction {NO_ACTION, UPDATE_CONFIG, RECORD_ACTION, PLAY_ACTION, PASS_ACTION, WAV_ACTION, STOP_ACTION};

// global app specific functions

void applyFilters();
void applyVolume();
void appShutdown();
void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL = false);
void buildAviIdx(size_t dataSize, bool isVid = true, bool isTL = false);
size_t buildSubtitle(int srtSeqNo, uint32_t sampleInterval);
void buzzerAlert(bool buzzerOn);
bool checkMotion(camera_fb_t* fb, bool motionStatus);
int8_t checkPotVol(int8_t adjVol);
bool checkSDFiles();
void currentStackUsage();
void displayAudioLed(int16_t audioSample);
void doIOExtPing();
void finalizeAviIndex(uint16_t frameCnt, bool isTL = false);
void finishAudio(bool isValid);
mjpegStruct getNextFrame(bool firstCall = false);
size_t getAudioBuffer(bool endStream);
bool getPIRval();
bool haveWavFile(bool isTL = false);
bool isNight(uint8_t nightSwitch);
void keepFrame(camera_fb_t* fb);
void micTaskStatus();
void motorSpeed(int speedVal, bool leftMotor = true);
void openSDfile(const char* streamFile);
bool prepAudio();
void prepAviIndex(bool isTL = false);
bool prepCam();
bool prepRecording();
void prepTelemetry();
void prepMic();
void remoteMicHandler(uint8_t* wsMsg, size_t wsMsgLen);
void setCamPan(int panVal);
void setCamTilt(int tiltVal);
uint8_t setFPS(uint8_t val);
uint8_t setFPSlookup(uint8_t val);
void setLamp(uint8_t lampVal);
void setLights(bool lightsOn);
void setSteering(int steerVal);
void setStepperPin(uint8_t pinNum, uint8_t pinPos);
void setStickTimer(bool restartTimer, uint32_t interval = 0);
void startAudio();
void startSustainTasks();
bool startTelemetry();
void stepperDone();
void stepperRun(float RPM, float revFraction, bool _clockwise);
void stopPlaying();
void stopSustainTask(int taskId);
void stopTelemetry(const char* fileName);
void storeSensorData(bool fromStream);
void takePhotos(bool startPhotos);
void trackSteeering(int controlVal, bool steering);
size_t writeAviIndex(byte* clientBuf, size_t buffSize, bool isTL = false);
size_t writeWavFile(byte* clientBuf, size_t buffSize);

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
extern bool mlUse; // whether to use ML for motion detection, requires INCLUDE_TINYML to be true
extern float mlProbability; // minimum probability (0.0 - 1.0) for positive classification

// record timelapse avi independently of motion capture, file name has same format as avi except ends with T
extern int tlSecsBetweenFrames; // too short interval will interfere with other activities
extern int tlDurationMins; // a new file starts when previous ends
extern int tlPlaybackFPS;  // rate to playback the timelapse, min 1 

// status & control fields 
extern const char* appConfig;
extern bool autoUpload;
extern bool dbgMotion;
extern bool doPlayback;
extern bool doRecording; // whether to capture to SD or not
extern bool forceRecord; // Recording enabled by rec button
extern bool forcePlayback; // playback enabled by user
extern uint8_t FPS;
extern uint8_t fsizePtr; // index to frameData[] for record
extern bool isCapturing;
extern uint8_t lightLevel;  
extern uint8_t lampLevel;  
extern int micGain;
extern uint8_t minSeconds; // default min video length (includes moveStopSecs time)
extern float motionVal;  // motion sensitivity setting - min percentage of changed pixels that constitute a movement
extern uint8_t nightSwitch; // initial white level % for night/day switching
extern bool nightTime; 
extern bool stopPlayback;
extern bool useMotion; // whether to use camera for motion detection (with motionDetect.cpp)  
extern uint8_t colorDepth;
extern bool timeLapseOn; // enable time lapse recording
extern int maxFrames;
extern uint8_t xclkMhz;
extern char camModel[];
extern bool doKeepFrame;
extern int alertMax; // too many could cause account suspension (daily emails)
extern bool streamNvr;
extern bool streamSnd;
extern bool streamSrt;
extern uint8_t numStreams;
extern uint8_t vidStreams;

// buffers
extern uint8_t iSDbuffer[];
extern uint8_t aviHeader[];
extern const uint8_t dcBuf[]; // 00dc
extern const uint8_t wbBuf[]; // 01wb
extern byte* uartData;
extern size_t streamBufferSize[];
extern byte* streamBuffer[]; // buffer for stream frame
extern size_t motionJpegLen;
extern uint8_t* motionJpeg;
extern uint8_t* audioBuffer;
extern char srtBuffer[];
extern size_t srtBytes;

// peripherals

// IO Extender use
extern bool useIOextender; // true to use IO Extender, otherwise false
extern bool useUART0;
extern int uartTxdPin;
extern int uartRxdPin;
// peripherals used
extern bool pirUse; // true to use PIR or radar sensor (RCWL-0516) for motion detection
extern bool lampUse; // true to use lamp
extern bool lampAuto; // if true in conjunction with usePir & useLamp, switch on lamp when PIR activated
extern bool lampNight;
extern int lampType;
extern bool servoUse; // true to use pan / tilt servo control
extern bool voltUse; // true to report on ADC pin eg for for battery
// microphone cannot be used on IO Extender
extern bool micUse; // true to use external I2S microphone 
extern bool wakeUse;
extern bool buzzerUse; // true to use active buzzer
extern int buzzerPin; 
extern int buzzerDuration; 

// sensors 
extern int pirPin; // if usePir is true
extern bool pirVal;
extern int lampPin; // if useLamp is true
extern int wakePin; // if wakeUse is true
extern int lightsPin;
extern bool teleUse;
extern int srtInterval;

// Pan / Tilt Servos 
extern int servoPanPin; // if useServos is true
extern int servoTiltPin;
// ambient / module temperature reading 
extern int ds18b20Pin; // if INCLUDE_DS18B20 true
// batt monitoring 
extern int voltPin; 

// microphone recording
extern int micSckPin; // I2S SCK 
extern int micSWsPin;  // I2S WS / PDM CLK
extern int micSdPin;  // I2S SD / PDM DAT
extern int mampBckIo; 
extern int mampSwsIo;
extern int mampSdIo;
extern volatile bool stopAudio;
extern volatile audioAction THIS_ACTION;
extern TaskHandle_t audioHandle;

// configure for specific servo model, eg for SG90
extern int servoDelay;
extern int servoMinAngle; // degrees
extern int servoMaxAngle;
extern int servoMinPulseWidth; // usecs
extern int servoMaxPulseWidth;
extern int servoCenter;

// battery monitor
extern int voltDivider;
extern float voltLow;
extern int voltInterval;

// audio
extern uint32_t SAMPLE_RATE; // audio sample rate
extern bool micRem;
extern bool mampUse;
extern uint8_t PREAMP_GAIN; // microphone preamplification factor
extern int8_t AMP_VOL; // amplifier volume factor

// stepper motor
extern bool stepperUse;
extern uint8_t stepINpins[];

// RC
extern bool RCactive;
extern int motorRevPin;
extern int motorFwdPin;
extern int motorRevPinR;
extern int motorFwdPinR;
extern bool trackSteer;
extern int servoSteerPin;
extern int lightsRCpin;
extern int pwmFreq;
extern int maxSteerAngle;
extern int maxTurnSpeed;
extern int maxDutyCycle;
extern int minDutyCycle;
extern bool allowReverse;
extern bool autoControl;
extern int waitTime;
extern bool stickUse;
extern int stickzPushPin;
extern int stickXpin;
extern int stickYpin;

// External Heartbeat
extern bool external_heartbeat_active;
extern char external_heartbeat_domain[]; //External Heartbeat domain/IP  
extern char external_heartbeat_uri[];    //External Heartbeat uri (i.e. /myesp32-cam-hub/index.php)
extern int external_heartbeat_port;      //External Heartbeat server port to connect.  
extern char external_heartbeat_token[];  //External Heartbeat server auth token.  

// photogrammetry
extern bool PGactive; 
extern bool clockWise;
extern uint8_t timeForFocus; // in secs
extern uint8_t timeForPhoto; // in secs
extern int pinShutter;
extern int pinFocus;
extern uint8_t photosDone;
extern float gearing;
extern uint8_t numberOfPhotos;
extern float tRPM;
extern bool extCam;

// task handling
extern TaskHandle_t battHandle;
extern TaskHandle_t captureHandle;
extern TaskHandle_t DS18B20handle;
extern TaskHandle_t emailHandle;
extern TaskHandle_t fsHandle;
extern TaskHandle_t logHandle;
extern TaskHandle_t audioHandle;
extern TaskHandle_t mqttTaskHandle;
extern TaskHandle_t playbackHandle;
extern esp_ping_handle_t pingHandle;
extern TaskHandle_t servoHandle;
extern TaskHandle_t stickHandle;
extern TaskHandle_t sustainHandle[];
extern TaskHandle_t telegramHandle;
extern TaskHandle_t telemetryHandle;
extern TaskHandle_t uartClientHandle;
extern SemaphoreHandle_t frameSemaphore[];
extern SemaphoreHandle_t motionSemaphore;


/************************** structures ********************************/

struct frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor; // (0..4)
  const uint8_t sampleRate; // (1..N)
};

// indexed by frame size - needs to be consistent with sensor.h framesize_t enum
const frameStruct frameData[] = {
  {"96X96", 96, 96, 30, 1, 1},   // 2MP sensors
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
  {"UXGA", 1600, 1200, 5, 4, 1},  
  {"FHD", 920, 1080, 5, 3, 1},    // 3MP Sensors
  {"P_HD", 720, 1280, 5, 3, 1},
  {"P_3MP", 864, 1536, 5, 3, 1},
  {"QXGA", 2048, 1536, 5, 4, 1},
  {"QHD", 2560, 1440, 5, 4, 1},   // 5MP Sensors
  {"WQXGA", 2560, 1600, 5, 4, 1},
  {"P_FHD", 1080, 1920, 5, 4, 1},
  {"QSXGA", 2560, 1920, 4, 4, 1}
};
