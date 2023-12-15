// streamServer handles streaming, playback, file downloads
// each sustained activity uses a separate task if available
// - web streaming, playback, file downloads use task 0
// - network streaming uses task 1
//
// s60sc 2022, 2023
//

#include "appGlobals.h"

#define AUX_STRUCT_SIZE 2048 // size of http request aux data - sizeof(struct httpd_req_aux) = 1108 in esp_http_server
// stream separator
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" BOUNDARY_VAL
#define JPEG_BOUNDARY "\r\n--" BOUNDARY_VAL "\r\n"
#define JPEG_TYPE "Content-Type: image/jpeg\r\nContent-Length: %10u\r\n\r\n"
#define HDR_BUF_LEN 64


static fs::FS fpv = STORAGE;
bool forcePlayback = false; // browser playback status
bool nvrStream = false;
static bool isStreaming[MAX_STREAMS] = {false};
size_t streamBufferSize[MAX_STREAMS] = {0};
byte* streamBuffer[MAX_STREAMS] = {NULL}; // buffer for stream frame
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN];
uint32_t sustainId = 0;
uint8_t numStreams = 1;

TaskHandle_t sustainHandle[MAX_STREAMS]; 
struct httpd_sustain_req_t {
  httpd_req_t* req;
  uint8_t taskNum; 
  char activity[16];
  bool inUse = false; 
};
httpd_sustain_req_t sustainReq[MAX_STREAMS];


static esp_err_t showPlayback(httpd_req_t* req) {
  // output playback file to browser
  esp_err_t res = ESP_OK; 
  stopPlaying();
  forcePlayback = true;
  if (fpv.exists(inFileName)) {
    if (stopPlayback) LOG_WRN("Playback refused - capture in progress");
    else {
      LOG_INF("Playback enabled (SD file selected)");
      doPlayback = true;
    }
  } else LOG_WRN("File %s doesn't exist when Playback requested", inFileName);

  if (doPlayback) {
    // playback mjpeg from SD
    mjpegStruct mjpegData;
    // output header for playback request
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    char hdrBuf[HDR_BUF_LEN];
    openSDfile(inFileName);
    mjpegData = getNextFrame(true);
    while (doPlayback) {
      size_t jpgLen = mjpegData.buffLen;
      size_t buffOffset = mjpegData.buffOffset;
      if (!jpgLen && !buffOffset) {
        // complete mjpeg playback streaming
        res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);
        doPlayback = false; 
      } else {
        if (jpgLen) {
          if (mjpegData.jpegSize) { // start of frame
            // send mjpeg header 
            if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);
            snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, mjpegData.jpegSize);
            if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, hdrBuf);   
          } 
          // send buffer 
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)iSDbuffer+buffOffset, jpgLen);
        }
        if (res == ESP_OK) mjpegData = getNextFrame(); 
        else {
          // when browser closes playback get send error
          LOG_DBG("Playback aborted due to error: %s", espErrMsg(res));
          break;
        }
      }
    }
    httpd_resp_sendstr_chunk(req, NULL);
    sustainId = currEpoch;
  }
  return res;
}

static esp_err_t showStream(httpd_req_t* req, uint8_t taskNum) {
  // start live streaming to browser
  esp_err_t res = ESP_OK; 
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegLen = 0;
  isStreaming[taskNum] = true;
  streamBufferSize[taskNum] = 0;
  if (!taskNum) motionJpegLen = 0;
  // output header for streaming request
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  char hdrBuf[HDR_BUF_LEN];
  while (isStreaming[taskNum]) {
    // stream from camera at current frame rate
    if (xSemaphoreTake(frameSemaphore[taskNum], pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdFAIL) continue;
    if (dbgMotion && !taskNum) {
      // motion tracking stream on task 0 only, wait for new move mapping image
      if (xSemaphoreTake(motionSemaphore, pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdFAIL) continue;
      // use image created by checkMotion()
      jpgLen = motionJpegLen;
      if (!jpgLen) continue;
      jpgBuf = motionJpeg;
    } else {
      // live stream 
      if (!streamBufferSize[taskNum]) continue;
      jpgLen = streamBufferSize[taskNum];
      // use frame stored by processFrame()
      jpgBuf = streamBuffer[taskNum];
    }
    if (res == ESP_OK) {
      // send next frame in stream
      res = httpd_resp_sendstr_chunk(req, JPEG_BOUNDARY);  
      snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
      if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, hdrBuf);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
      frameCnt++;
    } 
    mjpegLen += jpgLen;
    jpgLen = streamBufferSize[taskNum] = 0;
    if (dbgMotion && !taskNum) motionJpegLen = 0;
    if (res != ESP_OK) {
      // get send error when browser closes stream 
      LOG_DBG("Streaming aborted due to error: %s", espErrMsg(res));
      break;
    }     
  }
  httpd_resp_sendstr_chunk(req, NULL);
  uint32_t mjpegTime = millis() - startTime;
  float mjpegTimeF = float(mjpegTime) / 1000; // secs
  LOG_INF("MJPEG: %u frames, total %s in %0.1fs @ %0.1ffps", frameCnt, fmtSize(mjpegLen), mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
  return res;
}

void stopSustainTask(int taskId) {
  isStreaming[taskId] = false;
}

static void sustainTask(void* p) {
  // process sustained requests as a separate task 
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint8_t i = *(uint8_t*)p; // identify task number
    if (!strcmp(sustainReq[i].activity, "download"))
      fileHandler(sustainReq[i].req, true); // download
    else if (!strcmp(sustainReq[i].activity, "playback")) showPlayback(sustainReq[i].req);
    else if (!strcmp(sustainReq[i].activity, "stream")) showStream(sustainReq[i].req, i);
    else {
      httpd_resp_set_status(sustainReq[i].req, "400 Unknown request");
      httpd_resp_sendstr(sustainReq[i].req, NULL);
      LOG_ERR("Unknown request: %s", sustainReq[i].activity);
    }
    // cleanup as request now complete
    free(sustainReq[i].req->aux);
    sustainReq[i].req->~httpd_req_t();
    free(sustainReq[i].req);
    sustainReq[i].inUse = false; 
  }
  vTaskDelete(NULL);
}

void startSustainTasks() {
  // start httpd sustain tasks
  if (nvrStream) numStreams = MAX_STREAMS;
  for (int i = 0; i < numStreams; i++) {
    if (streamBuffer[i] == NULL) streamBuffer[i] = (byte*)ps_malloc(MAX_JPEG); 
    sustainReq[i].taskNum = i; // so task knows its number
    xTaskCreate(sustainTask, "sustainTask", SUSTAIN_STACK_SIZE, &sustainReq[i].taskNum, 4, &sustainHandle[i]); 
  } 
  LOG_INF("Started %d %s sustain tasks", numStreams, useHttps ? "HTTPS" : "HTTP");
  debugMemory("startSustainTasks");
}

esp_err_t appSpecificSustainHandler(httpd_req_t* req) {
  // handle long running request as separate task
  esp_err_t res = ESP_FAIL;
  // obtain details from query string
  if (extractQueryKeyVal(req, variable, value) == ESP_OK) {
    // playback, download, web streaming uses task 0
    // remote streaming eg NVR uses task 1
    uint8_t taskNum = !strcmp(variable, "stream") ? atoi(value) : 0;
    if (taskNum < numStreams) {
      if (req->method == HTTP_HEAD) { 
        if (!sustainReq[taskNum].inUse) {
          // task available
          sustainId = currEpoch;
          res = ESP_OK;
        } else {
          // task not free, try stopping it for new stream
          if (!strcmp(variable, "stream")) {
            isStreaming[taskNum] = false;
            if (!taskNum) doPlayback = false; // only for task 0
          }
          httpd_resp_set_status(req, "500 No free task");
        }
      } else {
        // action request
        if (!sustainReq[taskNum].inUse) {
          // make copy of request data and pass request to task indexed in request
          uint8_t i = taskNum;
          sustainReq[i].inUse = true;
          sustainReq[i].req = static_cast<httpd_req_t*>(malloc(sizeof(httpd_req_t)));
          new (sustainReq[i].req) httpd_req_t(*req);
          sustainReq[i].req->aux = psramFound() ? ps_malloc(AUX_STRUCT_SIZE) : malloc(AUX_STRUCT_SIZE); 
          memcpy(sustainReq[i].req->aux, req->aux, AUX_STRUCT_SIZE);
          strncpy(sustainReq[i].activity, variable, sizeof(sustainReq[i].activity) - 1); 
          // activate relevant task
          xTaskNotifyGive(sustainHandle[i]);
          delay(100);
          return ESP_OK;
        } else httpd_resp_set_status(req, "500 No free task");
      }
    } else httpd_resp_set_status(req, "400 Invalid task num");
  } else httpd_resp_set_status(req, "400 Bad URL");
  httpd_resp_sendstr(req, NULL);
  return res;
}
