
/*
* User modifiable features
*
* s60sc 2026
*/

#pragma once

/**************************************************************************
 Uncomment one only of the ESP32 or ESP32S3 camera models in the block below
 Selecting wrong model may crash your device due to pin conflict
***************************************************************************/

// User's ESP32 cam board
#if defined(CONFIG_IDF_TARGET_ESP32)
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE 
//#define CAMERA_MODEL_M5STACK_PSRAM 
//#define CAMERA_MODEL_M5STACK_V2_PSRAM 
//#define CAMERA_MODEL_M5STACK_WIDE 
//#define CAMERA_MODEL_M5STACK_ESP32CAM
//#define CAMERA_MODEL_M5STACK_UNITCAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_TTGO_T_CAMERA_PLUS
//#define CAMERA_MODEL_UICPAL_ESP32
//#define AUXILIARY

// User's ESP32S3 cam board
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CAMERA_MODEL_ESP32_S3_CAM
//#define CAMERA_MODEL_FREENOVE_ESP32S3_CAM
//#define CAMERA_MODEL_XIAO_ESP32S3
//#define CAMERA_MODEL_NEW_ESPS3_RE1_0
//#define CAMERA_MODEL_M5STACK_CAMS3_UNIT
//#define CAMERA_MODEL_ESP32S3_EYE 
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3
//#define CAMERA_MODEL_XENOIONEX
//#define CAMERA_MODEL_Waveshare_ESP32_S3_ETH
//#define CAMERA_MODEL_DFRobot_ESP32_S3_AI_CAM
//#define AUXILIARY

#endif

/***************************************************************
  Optional features NOT included by default to reduce heap use 
  To include a particular feature, change false to true
***************************************************************/
#define INCLUDE_FTP_HFS false // ftp.cpp (file upload)
#define INCLUDE_TGRAM false   // telegram.cpp (Telegram app interface)
#define INCLUDE_AUDIO false   // audio.cpp (microphones & speakers)
#define INCLUDE_PERIPH false  // peripherals.cpp (servos, PIR, led etc)
#define INCLUDE_SMTP false    // smtp.cpp (email)
#define INCLUDE_MQTT false    // mqtt.cpp (MQTT)
#define INCLUDE_HASIO false   // mqtt.cpp (Send home assistant discovery messages). Needs INCLUDE_MQTT true

#define INCLUDE_CERTS false   // certificates.cpp (https and server certificate checking)
#define INCLUDE_UART false    // uart.cpp (use another esp32 as Auxiliary connected via UART)
#define INCLUDE_TELEM false   // telemetry.cpp (real time data collection). Needs INCLUDE_I2C true
#define INCLUDE_WEBDAV false  // webDav.cpp (WebDAV protocol)
#define INCLUDE_EXTHB false   // externalHeartbeat.cpp (heartbeat to remote server)
#define INCLUDE_PGRAM false   // photogram.cpp (photogrammetry feature). Needs INCLUDE_PERIPH true
#define INCLUDE_MCPWM false   // mcpwm.cpp (BDC motor control). Needs INCLUDE_PERIPH true
#define INCLUDE_RTSP false    // rtsp.cpp (RTSP Streaming). Requires additional library: ESP32-RTSPServer
#define INCLUDE_DS18B20 false // if true, requires INCLUDE_PERIPH and additional libraries: OneWire and DallasTemperature
#define INCLUDE_AF false      // for auto focused equipped OV5640. Requires additional library: OV5640_Auto_Focus_for_ESP32_Camera
#define INCLUDE_NEW_JPG false // true to use esp_new_jpg library, which must be installed first. Faster but uses more memory
#define INCLUDE_I2C false     // periphsI2C.cpp (support for I2C peripherals)

// if INCLUDE_I2C true, set each I2C device used to true and instal additional library if required
#define USE_SSD1306 false  // Needs esp8266-oled-ssd1306 library
#define USE_BMx280 false   // NMP280, BME280. Needs BMx280MI library
#define USE_MPU false      // MPU6050, MPU9250, MPU9255. MPU9250 needs hideakitai MPU9250 library
#define USE_DS3231 false   // Needs Makuna Rtc library
#define USE_LCD1602 false  // none
#define USE_MS6511 false   // tbd

// To include Edge Impulse arduino library for additional motion detect filtering
// Use Edge Impulse Studio to create model:
// - Select target device: Espressif ESP-EYE
// - Select Arduino library deployment
// - Unzip created library into Arduino libraries folder
// To compile app with library:
#define INCLUDE_TINYML false  // set to true 
#define TINY_ML_LIB "your_impulse_edge_library.h" // replace with your lib
// To activate ML, under web page Motion tab, select Use Machine Learning option

/**************************************************************************/

#define ALLOW_SPACES false  // set true to allow whitespace in configs.txt key values

// web server ports
#define HTTP_PORT 80 // insecure app access
#define HTTPS_PORT 443 // secure app access

#define USE_IP6 false // if true use IPv6 when available, else use IPv4

#include "src/appGlobals.h"
