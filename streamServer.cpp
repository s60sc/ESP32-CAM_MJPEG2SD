// mjpeg2sd specific web functions
// streamServer handles streaming and stills
//
// s60sc 2022
//

#include "appGlobals.h"

// stream separator
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" BOUNDARY_VAL
#define JPEG_BOUNDARY "\r\n--" BOUNDARY_VAL "\r\n"
#define JPEG_TYPE "Content-Type: image/jpeg\r\nContent-Length: %10u\r\n\r\n"
#define HDR_BUF_LEN 64

static const size_t boundaryLen = strlen(JPEG_BOUNDARY);
static char hdrBuf[HDR_BUF_LEN];
static fs::FS fpv = STORAGE;
bool forcePlayback = false;
bool isStreaming = false;
static httpd_handle_t streamServer = NULL; 

esp_err_t webAppSpecificHandler(httpd_req_t *req, const char* variable, const char* value) {
  // update handling requiring response specific to mjpeg2sd
  if (!strcmp(variable, "sfile")) {
    // get folders / files on SD, save received filename if has required extension
    strcpy(inFileName, value);
    if (!forceRecord) doPlayback = listDir(inFileName, jsonBuff, JSON_BUFF_LEN, FILE_EXT); // browser control
    else strcpy(jsonBuff, "{}");                      
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  else if (!strcmp(variable, "updateFPS")) {
    // requires response with updated default fps
    sprintf(jsonBuff, "{\"fps\":\"%u\"}", setFPSlookup(fsizePtr));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  return ESP_OK;
}

static esp_err_t streamHandler(httpd_req_t* req) {
  // send mjpeg stream or single frame
  esp_err_t res = ESP_OK;
  char variable[FILE_NAME_LEN]; 
  char value[FILE_NAME_LEN];
  bool singleFrame = false;                                       
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegKB = 0;
  mjpegStruct mjpegData;

  // obtain key from query string
  extractQueryKey(req, variable);
  strcpy(value, variable + strlen(variable) + 1); // value is now second part of string
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  if (!strcmp(variable, "random")) singleFrame = true;
  doPlayback = false;
  if (!strcmp(variable, "source") && !strcmp(value, "file")) {
    forcePlayback = true;
    if (fpv.exists(inFileName)) {
      if (stopPlayback) LOG_WRN("Playback refused - capture in progress");
      else {
        LOG_INF("Playback enabled (SD file selected)");
        doPlayback = true;
      }
    } else LOG_WRN("File %s doesn't exist when Playback requested", inFileName);
  }

  // output header if streaming request
  if (!singleFrame) httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  if (doPlayback) {
    // playback mjpeg from SD
    openSDfile(inFileName);
    mjpegData = getNextFrame(true);
    while (doPlayback) {
      jpgLen = mjpegData.buffLen;
      size_t buffOffset = mjpegData.buffOffset;
      if (!jpgLen && !buffOffset) {
        // complete mjpeg streaming
        res = httpd_resp_send(req, JPEG_BOUNDARY, boundaryLen);
        doPlayback = false; 
      } else {
        if (jpgLen) {
          if (mjpegData.jpegSize) { // start of frame
            // send mjpeg header 
            res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, boundaryLen);
            size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, mjpegData.jpegSize);
            res = httpd_resp_send_chunk(req, hdrBuf, hdrLen);   
            frameCnt++;
          } 
          // send buffer 
          res = httpd_resp_send_chunk(req, (const char*)iSDbuffer+buffOffset, jpgLen);
        }
        mjpegData = getNextFrame(); 
      }
    }
  } else { 
    // live images
    do {
      isStreaming = true;
      camera_fb_t* fb;
      if (dbgMotion) {
        // motion tracking stream, wait for new move mapping image
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpgBuf, &jpgLen);
        if (!jpgLen) res = ESP_FAIL;
      } else {
        // stream from camera
        fb = esp_camera_fb_get();
        if (fb == NULL) return ESP_FAIL;
        jpgLen = fb->len;
        jpgBuf = fb->buf;
      }
      if (res == ESP_OK) {
        if (singleFrame) {
           httpd_resp_set_type(req, "image/jpeg");
           httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
           // send single jpeg to browser
           res = httpd_resp_send(req, (const char*)jpgBuf, jpgLen); 
        } else {
          // send next frame in stream
          res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, boundaryLen);    
          size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, hdrBuf, hdrLen);
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
        }
        frameCnt++;
      }
      xSemaphoreGive(motionMutex);
      if (fb != NULL) esp_camera_fb_return(fb);
      fb = NULL;  
      mjpegKB += jpgLen / 1024;
      if (res != ESP_OK) break;
    } while (!singleFrame && isStreaming);
    uint32_t mjpegTime = millis() - startTime;
    float mjpegTimeF = float(mjpegTime) / 1000; // secs
    if (singleFrame) LOG_INF("JPEG: %uB in %ums", jpgLen, mjpegTime);
    else LOG_INF("MJPEG: %u frames, total %ukB in %0.1fs @ %0.1ffps", frameCnt, mjpegKB, mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
  }
  return res;
}

void startStreamServer() {
if (psramFound()) heap_caps_malloc_extmem_enable(0); 
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32S3
  config.stack_size = 1024 * 8;
#endif
  httpd_uri_t streamUri = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = NULL};
  config.server_port = STREAM_PORT;
  config.ctrl_port = STREAM_PORT; // not used
  config.lru_purge_enable = true;
  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &streamUri);
    LOG_INF("Starting streaming server on port: %u", config.server_port);
  } else LOG_ERR("Failed to start streaming server");
  if (psramFound()) heap_caps_malloc_extmem_enable(4096); 
  debugMemory("startStreamserver");
}

 
