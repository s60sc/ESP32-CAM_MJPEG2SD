
/* 
 Extension to detect movement in sequential images using background subtraction.
 
 Very small bitmaps are used both to provide image smoothing to reduce spurious motion changes 
 and to enable rapid processing

 The amount of change between images will depend on the frame rate.
 A faster frame rate will need a higher sensitivity

 When frame size is changed the OV2640 outputs a few glitched frames whilst it 
 makes the transition. These could be interpreted as spurious motion.
 
 s60sc 2020

*/
#include "esp_camera.h"
#include "esp_jpg_decode.h"
#include <SD_MMC.h>
#include <bits/stdc++.h> 
using namespace std;

// user configuration parameters for calibrating motion detection
#define MOTION_SEQUENCE 5 // min sequence of changed frames to confirm motion 
#define NIGHT_SEQUENCE 10 // frames of sequential darkness to avoid spurious day / night switching
// define region of interest, ie exclude top and bottom of image from movement detection if required
// divide image into NUM_BANDS horizontal bands, define start and end bands of interest, 1 = top
#define START_BAND 3
#define END_BAND 8 // inclusive
#define NUM_BANDS 10
#define CHANGE_THRESHOLD 15 // min difference in pixel comparison to indicate a change

#define RGB888_BYTES 3 // number of bytes per pixel

#include "remote_log.h"
/*
// auto newline printf
#define showInfo(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define showError(format, ...) Serial.printf("ERROR: " format "\n", ##__VA_ARGS__)
#define showDebug(format, ...) if (dbgVerbose) Serial.printf("DEBUG: " format "\n", ##__VA_ARGS__)
*/
static const char* TAG = "motionDetect";
//Use ESP_LOG that can hanlde both, serial,file,telnet logging
#define showInfo(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
#define showError(format, ...) ESP_LOGE(TAG, format, ##__VA_ARGS__)
#define showDebug(format, ...) if (dbgVerbose) ESP_LOGD(TAG, format, ##__VA_ARGS__)

/********* the following must be declared and initialised elsewhere **********/
extern bool dbgVerbose;
extern bool dbgMotion;
extern uint8_t fsizePtr;
extern uint8_t lightLevel; // Current ambient light level 
extern SemaphoreHandle_t motionMutex;
extern SemaphoreHandle_t frameMutex;
extern float motionVal; // motion sensitivity setting - min percentage of changed pixels that constitute a movement
extern uint16_t insufficient;

struct frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor;
  const uint8_t sampleRate;
};
extern const frameStruct frameData[];

static uint8_t* jpgImg = NULL;
static size_t jpgImgSize = 0;

/**********************************************************************************/

static bool jpg2rgb(const uint8_t *src, size_t src_len, uint8_t ** out, uint8_t scale);

bool checkMotion(camera_fb_t * fb, bool motionStatus) {
  // check difference between current and previous image (subtract background)
  // convert image from JPEG to downscaled RGB888 bitmap to 8 bit grayscale
  uint32_t dTime = millis();
  uint32_t lux = 0;
  static uint32_t motionCnt = 0;
  uint8_t* rgb_buf = NULL;
  uint8_t* jpg_buf = NULL;
  size_t jpg_len = 0;

  // calculate parameters for sample size
  int scaling = frameData[fsizePtr].scaleFactor; 
  uint16_t reducer = frameData[fsizePtr].sampleRate;
  uint8_t downsize = pow(2, scaling) * reducer;
  int sampleWidth = frameData[fsizePtr].frameWidth / downsize;
  int sampleHeight = frameData[fsizePtr].frameHeight / downsize;
  int num_pixels = sampleWidth * sampleHeight;

  if (!jpg2rgb((uint8_t*)fb->buf, fb->len, &rgb_buf, scaling))
    showError("motionDetect: fmt2rgb() failed");

/*
  if (reducer > 1) 
    // further reduce size of bitmap 
    for (int r=0; r<sampleHeight; r++) 
      for (int c=0; c<sampleWidth; c++)      
        rgb_buf[c+(r*sampleWidth)] = rgb_buf[(c+(r*sampleWidth))*reducer]; 
*/
  showDebug("JPEG to greyscale conversion %u bytes in %lums", num_pixels, millis() - dTime);
  dTime = millis();

  // allocate buffer space on heap
  int maxSize = 32*1024; // max size downscaled UXGA 30k
  static uint8_t* changeMap = (uint8_t*)ps_malloc(maxSize);
  static uint8_t* prev_buf = (uint8_t*)ps_malloc(maxSize);
  static uint8_t* _jpgImg = (uint8_t*)ps_malloc(maxSize);
  jpgImg = _jpgImg;

  // compare each pixel in current frame with previous frame 
  int changeCount = 0;
  // set horizontal region of interest in image 
  uint16_t startPixel = num_pixels*(START_BAND-1)/NUM_BANDS;
  uint16_t endPixel = num_pixels*(END_BAND)/NUM_BANDS;
  int moveThreshold = (endPixel-startPixel) * (11-motionVal)/100; // number of changed pixels that constitute a movement
  for (int i=0; i<num_pixels; i++) {
    if (abs(rgb_buf[i] - prev_buf[i]) > CHANGE_THRESHOLD) {
      if (i > startPixel && i < endPixel) changeCount++; // number of changed pixels
      if (dbgMotion) changeMap[i] = 192; // populate changeMap image as with with changed pixels in gray
    } else if (dbgMotion) changeMap[i] =  255; // set white 
    lux += rgb_buf[i]; // for calculating light level
  }
  lightLevel = (lux*100)/(num_pixels*255); // light value as a %
  memcpy(prev_buf, rgb_buf, num_pixels); // save image for next comparison 
  // esp32-cam issue #126
  if (rgb_buf == NULL) showError("Memory leak, heap now: %u, pSRAM now: %u", ESP.getFreeHeap(), ESP.getFreePsram());
  free(rgb_buf); 
  rgb_buf = NULL;
  showDebug("Detected %u changes, threshold %u, light level %u, in %lums", changeCount, moveThreshold, lightLevel, millis() - dTime);
  dTime = millis();

  if (changeCount > moveThreshold) {
    showDebug("### Change detected");
    motionCnt++; // number of consecutive changes
    // need minimum sequence of changes to signal valid movement
    if (!motionStatus && motionCnt >= MOTION_SEQUENCE) {
      showDebug("***** Motion - START");
      motionStatus = true; // motion started
    } 
    if (dbgMotion)
      // to highlight movement detected in changeMap image, set all gray in region of interest to black
      for (int i=0; i<num_pixels; i++) 
         if (i > startPixel && i < endPixel && changeMap[i] < 255) changeMap[i] = 0;
  } else {
    // insufficient change
    if (motionStatus) {
      showDebug("***** Motion - STOP after %u frames", motionCnt);
      motionCnt = 0;
      motionStatus = false; // motion stopped
    }
  }
  if (motionStatus) showDebug("*** Motion - ongoing %u frames", motionCnt);

  if (dbgMotion) { 
    // build jpeg of changeMap for debug streaming
    dTime = millis();
    if (!fmt2jpg(changeMap, num_pixels, sampleWidth, sampleHeight, PIXFORMAT_GRAYSCALE, 80, &jpg_buf, &jpg_len))
      showError("motionDetect: fmt2jpg() failed");
    // prevent streaming from accessing jpeg while it is being updated
    xSemaphoreTake(motionMutex, portMAX_DELAY); 
    memcpy(jpgImg, jpg_buf, jpg_len);
    jpgImgSize = jpg_len; 
    xSemaphoreGive(motionMutex);
    free(jpg_buf);
    jpg_buf = NULL;
    showDebug("Created changeMap JPEG %d bytes in %lums", jpg_len, millis() - dTime);
  }

  showDebug("Free heap: %u, free pSRAM %u", ESP.getFreeHeap(), ESP.getFreePsram());
  // motionStatus indicates whether motion previously ongoing or not
  return motionStatus;
}

bool fetchMoveMap(uint8_t **out, size_t *out_len) {
  // return change map jpeg for streaming
  *out = jpgImg;
  *out_len = jpgImgSize;
  static size_t lastImgLen = 0;
  if (lastImgLen != jpgImgSize) {
    // image changed
    lastImgLen = jpgImgSize;
    return true;
  } else return false;
}

bool isNight(uint8_t nightSwitch) {
  // check if night time for switching on lamp during recording
  static bool nightTime = false;
  static uint16_t nightCnt = 0;
  if (!nightTime && lightLevel < nightSwitch) {
    // dark image
    nightCnt += 1;
    // only signal night time after given sequence of dark frames
    if (nightCnt > NIGHT_SEQUENCE) {
      nightTime = true;     
      showInfo("Night time"); 
    }
  } 
  if (lightLevel > nightSwitch) {
    nightCnt = 0;
    if (nightTime) {
      nightTime = false;
      showInfo("Day time");
    }
  }  
  return nightTime;
}

/************* copied and modified from esp32-camera/to_bmp.c to access jpg_scale_t *****************/

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t data_offset;
  const uint8_t *input;
  uint8_t *output;
} rgb_jpg_decoder;

static bool _rgb_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  // mpjpeg2sd: mofified to generate 8 bit grayscale
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (!data){
    if (x == 0 && y == 0) {
      // write start
      jpeg->width = w;
      jpeg->height = h;
      // if output is null, this is BMP
      if (!jpeg->output) {
        jpeg->output = (uint8_t*)ps_malloc((w*h)+jpeg->data_offset);
        if (!jpeg->output) return false;
      }
    } 
    return true;
  }

  size_t jw = jpeg->width*RGB888_BYTES;
  size_t t = y * jw;
  size_t b = t + (h * jw);
  size_t l = x * RGB888_BYTES;
  uint8_t *out = jpeg->output+jpeg->data_offset;
  uint8_t *o = out;
  size_t iy, ix;
  w *= RGB888_BYTES;

  for (iy=t; iy<b; iy+=jw) {
    o = out+(iy+l)/RGB888_BYTES;
    for (ix=0; ix<w; ix+=RGB888_BYTES) {
      uint16_t grayscale = (data[ix+2]+data[ix+1]+data[ix])/RGB888_BYTES;
      o[ix/RGB888_BYTES] = (uint8_t)grayscale;
    }
    data+=w;
  }
  return true;
}

static uint32_t _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len) {
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (buf) memcpy(buf, jpeg->input + index, len);
  return len;
}

static bool jpg2rgb(const uint8_t *src, size_t src_len, uint8_t **out, uint8_t scale) {
  rgb_jpg_decoder jpeg;
  jpeg.width = 0;
  jpeg.height = 0;
  jpeg.input = src;
  jpeg.output = NULL; 
  jpeg.data_offset = 0;
  esp_err_t res = esp_jpg_decode(src_len, jpg_scale_t(scale), _jpg_read, _rgb_write, (void*)&jpeg);
  *out = jpeg.output;
  return (res == ESP_OK) ? true : false;
}
