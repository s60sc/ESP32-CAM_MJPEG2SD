/*
  Capture ESP32 Cam JPEG images into a MJPEG file and store on SD
  MJPEG files stored on the SD card can also be selected and streamed to a browser as AVI.

  s60sc 2022, 2021, 2020
*/
// built using arduino-esp32 stable release v2.0.2

#include "myConfig.h"
#include "camera_pins.h"

//#define DEV_ONLY // for app development only, leave commented out
void devSetup();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  LOG_INF("=============== Starting ===============");
  if (!psramFound()) {
    LOG_WRN("Need PSRAM to be enabled");
    delay(10000);
    ESP.restart();
  }

  if (!prepSD_MMC()) {
    LOG_WRN("Insert SD card, will restart after 10 secs");
    delay(10000);
    ESP.restart();
  }

  //Put an empty file on sdcard root so it's possible to force reset the settings..
  if (SD_MMC.exists("/forcereset")) {
    LOG_WRN("Forcing reset config..");
    SD_MMC.remove("/forcereset");
    resetConfig();
    ESP.restart();
  }

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
  config.xclk_freq_hz = XCLK_MHZ * 1000000;
  config.pixel_format = PIXFORMAT_JPEG;
  // init with high specs to pre-allocate larger buffers
  if (psramFound()) {
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
      LOG_ERR("Camera init failed with error 0x%x", err);
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(100);
      digitalWrite(PWDN_GPIO_NUM, 0); // power cycle the camera (OV2640)
      retries--;
    }
  }
  if (err != ESP_OK) ESP.restart();
  else LOG_INF("Camera init OK");

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

  // Load saved user configuration
  loadConfig();

  // connect wifi or start config AP if router details not available
#ifdef DEV_ONLY
  devSetup();
#else
  startWifi();
#endif

  if (!prepMjpeg()) {
    LOG_ERR("Unable to continue, MJPEG capture fail, restart after 10 secs");
    delay(10000);
    ESP.restart();
  }

  // Show wifi AP or STA IP address
  String wifiIP;
  if (WiFi.getMode() == WIFI_STA) {
    // if wifi station then wait for connection to be available
    while (WiFi.status() != WL_CONNECTED) delay(1000);
    Serial.println("");
    wifiIP = WiFi.localIP().toString();
  } else wifiIP = WiFi.softAPIP().toString();
  LOG_INF("Use 'http://%s' to connect to %s", wifiIP.c_str(), WiFi.getMode() == WIFI_STA ? "Station" : "Access Point");

  // Start httpd
  startCameraServer();
  prepMic();
  OTAsetup();
  startSDtasks();
  prepDS18B20();
  setupADC();

  LOG_INF("Camera Ready @ %uMHz, version %s.", XCLK_MHZ, APP_VER);
}

void loop() {
  if (!OTAlistener()) delay(100000);
}

// use with IDF
/*
  extern "C" void app_main() {
    initArduino();
    setup();
    loop();
  }
*/
