
/* 
 Detect movement in sequential images using background subtraction.
 
 Very small bitmaps are used both to provide image smoothing to reduce spurious motion changes 
 and to enable rapid processing

 The amount of change between images will depend on the frame rate.
 A faster frame rate will need a higher sensitivity

 When frame size is changed the OV2640 outputs a few glitched frames whilst it 
 makes the transition. These could be interpreted as spurious motion.
 
 s60sc 2020
*/

#include "appGlobals.h"

using namespace std;

#define RGB888_BYTES 3 // number of bytes per pixel

// motion recording parameters
int detectMotionFrames = 5; // min sequence of changed frames to confirm motion 
int detectNightFrames = 10; // frames of sequential darkness to avoid spurious day / night switching
// define region of interest, ie exclude top and bottom of image from movement detection if required
// divide image into detectNumBands horizontal bands, define start and end bands of interest, 1 = top
int detectNumBands = 10;
int detectStartBand = 3;
int detectEndBand = 8; // inclusive
int detectChangeThreshold = 15; // min difference in pixel comparison to indicate a change

uint8_t lightLevel; // Current ambient light level 
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting
static uint8_t* jpgImg = NULL;
static size_t jpgImgSize = 0;

/**********************************************************************************/

static bool jpg2rgb(const uint8_t *src, size_t src_len, uint8_t ** out, jpg_scale_t scale);

bool isNight(uint8_t nightSwitch) {
  // check if night time for suspending recording
  // or for switching on lamp if enabled
  static bool nightTime = false;
  static uint16_t nightCnt = 0;
  if (!nightTime && lightLevel < nightSwitch) {
    // dark image
    nightCnt += 1;
    // only signal night time after given sequence of dark frames
    if (nightCnt > detectNightFrames
  ) {
      nightTime = true;     
      LOG_INF("Night time"); 
    }
  } 
  if (lightLevel > nightSwitch) {
    nightCnt = 0;
    if (nightTime) {
      nightTime = false;
      LOG_INF("Day time");
    }
  }  
  return nightTime;
}

bool checkMotion(camera_fb_t* fb, bool motionStatus) {
  // check difference between current and previous image (subtract background)
  // convert image from JPEG to downscaled RGB888 bitmap to 8 bit grayscale
  uint32_t dTime = millis();
  uint32_t lux = 0;
  static uint32_t motionCnt = 0;
  uint8_t* rgb_buf = NULL;
  uint8_t* jpg_buf = NULL;
  size_t jpg_len = 0;

  // calculate parameters for sample size
  uint8_t scaling = frameData[fsizePtr].scaleFactor; 
  uint16_t reducer = frameData[fsizePtr].sampleRate;
  uint8_t downsize = pow(2, scaling) * reducer;
  int sampleWidth = frameData[fsizePtr].frameWidth / downsize;
  int sampleHeight = frameData[fsizePtr].frameHeight / downsize;
  int num_pixels = sampleWidth * sampleHeight;
  if (!jpg2rgb((uint8_t*)fb->buf, fb->len, &rgb_buf, (jpg_scale_t)scaling)) {
    LOG_ERR("motionDetect: jpg2rgb() failed");
    free(rgb_buf);
    rgb_buf = NULL;
    return motionStatus;
  }

/*
  if (reducer > 1) 
    // further reduce size of bitmap 
    for (int r=0; r<sampleHeight; r++) 
      for (int c=0; c<sampleWidth; c++)      
        rgb_buf[c+(r*sampleWidth)] = rgb_buf[(c+(r*sampleWidth))*reducer]; 
*/
  LOG_DBG("JPEG to greyscale conversion %u bytes in %lums", num_pixels, millis() - dTime);
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
  uint16_t startPixel = num_pixels*(detectStartBand-1)/detectNumBands;
  uint16_t endPixel = num_pixels*(detectEndBand)/detectNumBands;
  int moveThreshold = (endPixel-startPixel) * (11-motionVal)/100; // number of changed pixels that constitute a movement
  for (int i=0; i<num_pixels; i++) {
    if (abs((int)rgb_buf[i] - (int)prev_buf[i]) > detectChangeThreshold) {
      if (i > startPixel && i < endPixel) changeCount++; // number of changed pixels
      if (dbgMotion) changeMap[i] = 192; // populate changeMap image with changed pixels in gray
    } else if (dbgMotion) changeMap[i] =  255; // set white 
    lux += rgb_buf[i]; // for calculating light level
  }
  lightLevel = (lux*100)/(num_pixels*255); // light value as a %
  nightTime = isNight(nightSwitch);
  memcpy(prev_buf, rgb_buf, num_pixels); // save image for next comparison 
  // esp32-cam issue #126
  if (rgb_buf == NULL) LOG_ERR("Memory leak, heap now: %u, pSRAM now: %u", ESP.getFreeHeap(), ESP.getFreePsram());
  free(rgb_buf); 
  rgb_buf = NULL;
  LOG_DBG("Detected %u changes, threshold %u, light level %u, in %lums", changeCount, moveThreshold, lightLevel, millis() - dTime);
  dTime = millis();

  if (changeCount > moveThreshold) {
    LOG_DBG("### Change detected");
    motionCnt++; // number of consecutive changes
    // need minimum sequence of changes to signal valid movement
    if (!motionStatus && motionCnt >= detectMotionFrames) {
      LOG_DBG("***** Motion - START");
      motionStatus = true; // motion started
      if (mqtt_active) {
        sprintf(jsonBuff, "{\"MOTION\":\"ON\",\"TIME\":\"%s\"}",esp_log_system_timestamp());
        mqttPublish(jsonBuff);
      }
    } 
    if (dbgMotion)
      // to highlight movement detected in changeMap image, set all gray in region of interest to black
      for (int i=0; i<num_pixels; i++) 
         if (i > startPixel && i < endPixel && changeMap[i] < 255) changeMap[i] = 0;
  } else {
    // insufficient change
    if (motionStatus) {
      LOG_DBG("***** Motion - STOP after %u frames", motionCnt);
      motionCnt = 0;
      motionStatus = false; // motion stopped
      if (mqtt_active) {
        sprintf(jsonBuff, "{\"MOTION\":\"OFF\",\"TIME\":\"%s\"}", esp_log_system_timestamp());
        mqttPublish(jsonBuff);
      }
    }
  }
  if (motionStatus) LOG_DBG("*** Motion - ongoing %u frames", motionCnt);

  if (dbgMotion) { 
    // build jpeg of changeMap for debug streaming
    dTime = millis();
    if (!fmt2jpg(changeMap, num_pixels, sampleWidth, sampleHeight, PIXFORMAT_GRAYSCALE, 80, &jpg_buf, &jpg_len))
      LOG_ERR("motionDetect: fmt2jpg() failed");
    // prevent streaming from accessing jpeg while it is being updated
    xSemaphoreTake(motionMutex, portMAX_DELAY); 
    memcpy(jpgImg, jpg_buf, jpg_len);
    jpgImgSize = jpg_len; 
    xSemaphoreGive(motionMutex);
    free(jpg_buf);
    jpg_buf = NULL;
    LOG_DBG("Created changeMap JPEG %d bytes in %lums", jpg_len, millis() - dTime);
  }

  if (dbgVerbose) checkMemory();  
  // motionStatus indicates whether motion previously ongoing or not
  return nightTime ? false : motionStatus;
}

bool fetchMoveMap(uint8_t **out, size_t *out_len) {
  // return change map jpeg for streaming
  if (useMotion){                    
    *out = jpgImg;
    *out_len = jpgImgSize;
    static size_t lastImgLen = 0;
    if (lastImgLen != jpgImgSize) {
      // image changed
      lastImgLen = jpgImgSize;
      return true;
    } else return false;
  }else{
     // dummy if motionDetect.cpp not used
    *out_len = 0;
    return false;
  }
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

static bool jpg2rgb(const uint8_t *src, size_t src_len, uint8_t **out, jpg_scale_t scale) {
  rgb_jpg_decoder jpeg;
  jpeg.width = 0;
  jpeg.height = 0;
  jpeg.input = src;
  jpeg.output = NULL; 
  jpeg.data_offset = 0;
  esp_err_t res = esp_jpg_decode(src_len, scale, _jpg_read, _rgb_write, (void*)&jpeg);
  *out = jpeg.output;
  return (res == ESP_OK) ? true : false;
}
