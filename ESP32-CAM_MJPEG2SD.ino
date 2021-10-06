#include "esp_camera.h"

// built using arduino-esp32 stable release v2.0.0
//
// WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
//            or another board which has PSRAM enabled
//
// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

static const char* TAG = "ESP32-CAM";

#include "camera_pins.h"
#include "myConfig.h"

const char* appVersion = "2.5";
#define XCLK_MHZ 20 // fastest clock rate

//External functions
void startCameraServer();
bool prepMjpeg();
void startSDtasks();
bool prepSD_MMC();
bool prepDS18();
void OTAsetup();
bool OTAlistener();
bool startWifi();
void checkConnection();  

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  //ESP_LOG will not work if not set verbose
  esp_log_level_set("*", ESP_LOG_VERBOSE);
  ESP_LOGI(TAG, "=============== Starting ===============");
  
  if(!prepSD_MMC()){
    ESP_LOGE(TAG, "SD card initialization failed!!, Will restart after 10 secs");    
    delay(10000);
    ESP.restart();
  }
  
  //Remove old log file
  if(SD_MMC.exists("/Log/log.txt")) SD_MMC.remove("/Log/log.txt");

  // configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = XCLK_MHZ*1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 4;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = ESP_FAIL;
  uint8_t retries = 2;
  while (retries && err != ESP_OK) {
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG,"Camera init failed with error 0x%x", err);
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, 0); // power cycle the camera (OV2640)
      retries--;
    }
  } 
  if (err != ESP_OK) ESP.restart();
  else ESP_LOGI(TAG, "Camera init OK");

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the brightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_SVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif
  
  //Load config and connect wifi or start config AP if fail
  if(!startWifi()){
    ESP_LOGE(TAG, "Failed to start wifi, restart after 10 secs");
    delay(10000);
    ESP.restart();
  }
  
  //Telnet debug will need internet conection first
  if(dbgMode!=2){ //Non telnet mode.
    //Call remote log init to debug wifi connection on startup
    //View the file from the access point http://192.168.4.1/file?log.txt
    remote_log_init();  
  }
  setupADC(); 
  
  //Disable telnet init without wifi
  if(dbgMode==2) dbgMode =0;                                    
  
  if (!prepMjpeg()) {
    ESP_LOGE(TAG, "Unable to continue, MJPEG capture fail, restart after 10 secs");    
    delay(10000);
    ESP.restart();
  }
        
  //Start httpd
  startCameraServer();
  OTAsetup();
  startSDtasks();
  if (prepDS18()) {ESP_LOGI(TAG, "DS18B20 device available");}
  else ESP_LOGI(TAG, "DS18B20 device not present");

  String wifiIP = (WiFi.status() == WL_CONNECTED && WiFi.getMode() != WIFI_AP) ? WiFi.localIP().toString(): WiFi.softAPIP().toString();
  ESP_LOGI(TAG, "Use 'http://%s' to connect", wifiIP.c_str());  
  ESP_LOGI(TAG, "Camera Ready @ %uMHz, version %s.", XCLK_MHZ, appVersion);  
}

void loop() {
  //Check connection
  checkConnection();
  //USE_OTA
  if (!OTAlistener()) delay(100000);  
  //else delay(2000);
}
