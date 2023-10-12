// streamServer handles streaming, playback, file downloads
//
// s60sc 2022, 2023
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
bool forcePlayback = false; // browser playback status
bool isStreaming = false; // browser streaming status
static httpd_handle_t streamServer = NULL; 
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN];

size_t streamBufferSize = 0;
byte* streamBuffer = NULL; // buffer for stream frame

esp_err_t appSpecificSustainHandler(httpd_req_t* req, const char* variable) {
  // send mjpeg stream (live or playback)
  esp_err_t res = ESP_OK; 
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegLen = 0;
  mjpegStruct mjpegData;
  stopPlaying();
  if (!strcmp(variable, "playback")) {
    forcePlayback = true;
    if (fpv.exists(inFileName)) {
      if (stopPlayback) LOG_WRN("Playback refused - capture in progress");
      else {
        LOG_INF("Playback enabled (SD file selected)");
        doPlayback = true;
      }
    } else LOG_WRN("File %s doesn't exist when Playback requested", inFileName);
  }
  // output header for streaming request
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  if (doPlayback) {
    // playback mjpeg from SD
    openSDfile(inFileName);
    mjpegData = getNextFrame(true);
    while (doPlayback) {
      jpgLen = mjpegData.buffLen;
      size_t buffOffset = mjpegData.buffOffset;
      if (!jpgLen && !buffOffset) {
        // complete mjpeg playback streaming
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
    httpd_resp_sendstr_chunk(req, NULL);
    
  } else if (!strcmp(variable, "stream")) {
    // start live streaming
    isStreaming = true;
    streamBufferSize = 0;
    while (isStreaming) {
      if (dbgMotion) {
        // motion tracking stream, wait for new move mapping image
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpgBuf, &jpgLen);
        res = jpgLen ? ESP_OK : ESP_FAIL;
      } else {
        // stream from camera at current frame rate
        xSemaphoreTake(frameSemaphore, portMAX_DELAY);
        jpgLen = streamBufferSize;
        res = jpgLen ? ESP_OK : ESP_FAIL;
        // use frame stored by processFrame()
        if (jpgLen) jpgBuf = streamBuffer;
      }
      if (res == ESP_OK) {
        // send next frame in stream
        res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, boundaryLen);    
        size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, hdrBuf, hdrLen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
        frameCnt++;
      } 
      if (dbgMotion) xSemaphoreGive(motionMutex);
      mjpegLen += jpgLen;
      jpgLen = streamBufferSize = 0;
    }
    httpd_resp_sendstr_chunk(req, NULL);
    uint32_t mjpegTime = millis() - startTime;
    float mjpegTimeF = float(mjpegTime) / 1000; // secs
    LOG_INF("MJPEG: %u frames, total %s in %0.1fs @ %0.1ffps", frameCnt, fmtSize(mjpegLen), mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
  } else {
    httpd_resp_sendstr_chunk(req, NULL);
    LOG_ERR("Unknown request: %s", variable);
  }
  return res;
}

 static esp_err_t sustainHandler(httpd_req_t* req) {
  if (extractQueryKeyVal(req, variable, value) != ESP_OK) return ESP_FAIL;
  if (!strcmp(variable, "download")) {
#ifdef ISCAM
    if (whichExt) changeExtension(inFileName, CSV_EXT);
#endif
    fileHandler(req, true); // download
  } else appSpecificSustainHandler(req, variable);
  return ESP_OK;
}

void startStreamServer() {
if (psramFound()) heap_caps_malloc_extmem_enable(0); 
  esp_err_t res = ESP_FAIL;
  size_t prvtkey_len = strlen(prvtkey_pem);
  size_t cacert_len = strlen(cacert_pem);
  if (useHttps && (!cacert_len || !prvtkey_len)) {
    useHttps = false;
    LOG_ALT("HTTPS not available as server keys not defined, using HTTP");
  }
  
  if (useHttps) {
    // HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32S3
    config.httpd.stack_size = 1024 * 8;
#endif  
    config.cacert_pem = (const uint8_t*)cacert_pem;
    config.cacert_len = cacert_len + 1;
    config.prvtkey_pem = (const uint8_t*)prvtkey_pem;
    config.prvtkey_len = prvtkey_len + 1;
    config.httpd.server_port = STREAMS_PORT;
    config.httpd.ctrl_port = STREAMS_PORT;
    config.httpd.lru_purge_enable = true; // close least used socket 
    config.httpd.max_uri_handlers = 2;
    config.httpd.max_open_sockets = SUSTAIN_CLIENTS;
    res = httpd_ssl_start(&streamServer, &config);
  } else {
    // HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32S3
    config.stack_size = 1024 * 8;
#endif  
    config.server_port = STREAM_PORT;
    config.ctrl_port = STREAM_PORT;
    config.lru_purge_enable = true;   
    config.max_uri_handlers = 2;
    config.max_open_sockets = SUSTAIN_CLIENTS;
    res = httpd_start(&streamServer, &config);
  }
  
  httpd_uri_t sustainUri = {.uri = "/sustain", .method = HTTP_GET, .handler = sustainHandler, .user_ctx = NULL};
  if (res == ESP_OK) {
    httpd_register_uri_handler(streamServer, &sustainUri);
    LOG_INF("Starting streaming server on port: %u", useHttps ? STREAMS_PORT : STREAM_PORT);
  } else LOG_ERR("Failed to start streaming server");
  if (psramFound()) heap_caps_malloc_extmem_enable(4096); 
  if (streamBuffer == NULL) streamBuffer = (byte*)ps_malloc(MAX_JPEG); 
  debugMemory("startStreamserver");
}
