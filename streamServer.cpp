// streamServer handles streaming, playback, file downloads
// each sustained activity uses a separate task if available
// - web streaming, playback, file downloads use task 0
// - video streaming uses task 1
// - audio streaming uses task 2
// - subtitle streaming uses task 3
//
// s60sc 2022 - 2024
//

#include "appGlobals.h"

#define AUX_STRUCT_SIZE 2048 // size of http request aux data - sizeof(struct httpd_req_aux) = 1108 in esp_http_server
// stream separator
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" BOUNDARY_VAL
#define JPEG_BOUNDARY "\r\n--" BOUNDARY_VAL "\r\n"
#define JPEG_TYPE "Content-Type: image/jpeg\r\nContent-Length: %10u\r\n\r\n"
#define HDR_BUF_LEN 64
#define END_WAIT 100

static fs::FS fpv = STORAGE;
bool forcePlayback = false; // browser playback status
bool streamNvr = false;
bool streamSnd = false;
bool streamSrt = false;
static bool isStreaming[MAX_STREAMS] = {false};
size_t streamBufferSize[MAX_STREAMS] = {0};
byte* streamBuffer[MAX_STREAMS] = {NULL}; // buffer for stream frame
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN];
uint16_t sustainId = 0;
uint8_t numStreams = 1;
uint8_t vidStreams = 1;
int srtInterval = 1; // subtitle interval in secs


TaskHandle_t sustainHandle[MAX_STREAMS]; 
struct httpd_sustain_req_t {
  httpd_req_t* req = NULL;
  uint8_t taskNum; 
  char activity[16];
  bool inUse = false; 
};
httpd_sustain_req_t sustainReq[MAX_STREAMS];


static void showPlayback(httpd_req_t* req) {
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
          LOG_VRB("Playback aborted due to error: %s", espErrMsg(res));
          stopPlaying();
        }
      }
    }
    if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
    sustainId = currEpoch;
  }
}

static void showStream(httpd_req_t* req, uint8_t taskNum) {
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
    if (xSemaphoreTake(frameSemaphore[taskNum], pdMS_TO_TICKS(MAX_FRAME_WAIT)) == pdFAIL) {
      // failed to take semaphore, allow retry
      streamBufferSize[taskNum] = 0;
      continue;
    }
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
      LOG_VRB("Streaming aborted due to error: %s", espErrMsg(res));
      isStreaming[taskNum] = false;
    }     
  }
  if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
  uint32_t mjpegTime = millis() - startTime;
  float mjpegTimeF = float(mjpegTime) / 1000; // secs
  LOG_INF("MJPEG: %u frames, total %s in %0.1fs @ %0.1ffps", frameCnt, fmtSize(mjpegLen), mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
}

static void audioStream(httpd_req_t* req, uint8_t taskNum) {
  // output WAV audio stream to remote NVR
#if INCLUDE_AUDIO
  if (micGain) {
    esp_err_t res = ESP_OK;
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    isStreaming[taskNum] = true;
    uint32_t totalSamples = 0;
    audioBytes = WAV_HDR_LEN;
    updateWavHeader();
    while (isStreaming[taskNum]) {
      if (audioBytes) {
        res = httpd_resp_send_chunk(req, (const char*)audioBuffer, audioBytes); 
        audioBytes = 0;
      } else delay(20); // allow time for buffer to load
      if (res != ESP_OK) isStreaming[taskNum] = false; // client connection closed
      else totalSamples += audioBytes / 2; // 16 bit samples
    }
    audioBytes = 1; // stop loading of buffer
    if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
    LOG_INF("WAV: sent %lu samples", totalSamples);
  } else LOG_WRN("No ESP mic defined or mic is off");
#else 
  httpd_resp_sendstr(req, NULL);
#endif
}

static void srtStream(httpd_req_t* req, uint8_t taskNum) {
  // generate subtitle entries for streaming, consisting of timestamp
  // plus telemetry data if telemetry enabled
  esp_err_t res = ESP_OK;
  isStreaming[taskNum] = true;
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*"); 
  int srtSeqNo = 0;
  uint32_t srtTime = 0;
  const uint32_t sampleInterval = 1000 * (srtInterval < 1 ? 1 : srtInterval);
  char srtHdr[100];
  char timeStr[10];
  while (isStreaming[taskNum]) {
    srtSeqNo++;
    uint32_t startTime = millis();
    formatElapsedTime(timeStr, srtTime, true);
    size_t srtPtr = sprintf(srtHdr, "%d\n%s --> ", srtSeqNo, timeStr);
    srtTime += sampleInterval;
    formatElapsedTime(timeStr, srtTime, true);
    srtPtr += sprintf(srtHdr + srtPtr, "%s\n", timeStr);
    time_t currEpoch = getEpoch();
    srtPtr += strftime(srtHdr + srtPtr, 12, "%H:%M:%S  ", localtime(&currEpoch));
    httpd_resp_send_chunk(req, (const char*)srtHdr, srtPtr);
#if INCLUDE_TELEM
    // add telemetry data 
    if (teleUse) {
      storeSensorData(true);
      if (srtBytes) res = httpd_resp_send_chunk(req, (const char*)srtBuffer, srtBytes);
      srtBytes = 0;
    }
#endif
    if (res == ESP_OK) res = httpd_resp_sendstr_chunk(req, "\n\n");
    if (res != ESP_OK) isStreaming[taskNum] = false; // client connection closed
    else while (isStreaming[taskNum] && millis() - sampleInterval < startTime) delay(50);
  }
  if (res == ESP_OK) httpd_resp_sendstr_chunk(req, NULL);
  LOG_INF("SRT: sent %d subtitles", srtSeqNo);
}

void stopSustainTask(int taskId) {
  isStreaming[taskId] = false;
}

static void sustainTask(void* p) {
  // process sustained requests as a separate task 
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint8_t i = *(uint8_t*)p; // identify task number
    if (i == 0) {
      if (!strcmp(sustainReq[i].activity, "download")) fileHandler(sustainReq[i].req, true); 
      else if (!strcmp(sustainReq[i].activity, "playback")) showPlayback(sustainReq[i].req);
      else if (!strcmp(sustainReq[i].activity, "stream")) showStream(sustainReq[i].req, i);
    } 
    else if (i == 1) showStream(sustainReq[i].req, i);
    else if (i == 2) audioStream(sustainReq[i].req, i);
    else if (i == 3) srtStream(sustainReq[i].req, i);

    // cleanup as request now complete on return
    killSocket(httpd_req_to_sockfd(sustainReq[i].req));
    delay(END_WAIT);
    free(sustainReq[i].req->aux);
    sustainReq[i].req->~httpd_req_t();
    free(sustainReq[i].req);
    sustainReq[i].req = NULL;
    sustainReq[i].inUse = false; 
  }
  vTaskDelete(NULL);
}

void startSustainTasks() {
  // start httpd sustain tasks
  if (streamNvr) numStreams = vidStreams = 2;
  if (streamSnd) numStreams = 3;
  if (streamSrt) numStreams = 4;
  if (numStreams > MAX_STREAMS) {
    LOG_WRN("numStreams %d exceeds MAX_STREAMS %d", numStreams, MAX_STREAMS);
    numStreams = MAX_STREAMS;
  }
  if (MAX_JPEG * (vidStreams + 1) > ESP.getFreePsram()) {
    LOG_WRN("Insufficient PSRAM for NVR streams");
    vidStreams = 1;
    streamNvr = streamSnd = streamSrt = false;
  }
  for (int i = 0; i < vidStreams; i++)
    if (streamBuffer[i] == NULL) streamBuffer[i] = (byte*)ps_malloc(MAX_JPEG); 

  for (int i = 0; i < numStreams; i++) {
    sustainReq[i].taskNum = i; // so task knows its number
    xTaskCreate(sustainTask, "sustainTask", SUSTAIN_STACK_SIZE, &sustainReq[i].taskNum, SUSTAIN_PRI, &sustainHandle[i]); 
  } 
  LOG_INF("Started %d %s sustain tasks", numStreams, useHttps ? "HTTPS" : "HTTP");
  debugMemory("startSustainTasks");
}

esp_err_t appSpecificSustainHandler(httpd_req_t* req) {
  // first check if authentication is required & passed
  esp_err_t res = ESP_FAIL;
  if (checkAuth(req)) { 
    // handle long running request as separate task
    // obtain details from query string
    if (extractQueryKeyVal(req, variable, value) == ESP_OK) {
      // playback, download, web streaming uses task 0
      // remote streaming eg video uses task 1, audio task 2, srt task 3
      uint8_t taskNum = 99;
      if (!strcmp(variable, "download")) taskNum = 0;
      else if (!strcmp(variable, "playback")) taskNum = 0;
      else if (!strcmp(variable, "stream")) taskNum = 0;
      else if (!strcmp(variable, "video")) taskNum = 1;
      else if (!strcmp(variable, "audio")) taskNum = 2;
      else if (!strcmp(variable, "srt")) taskNum = 3;

      if (taskNum < numStreams) {
        if (taskNum == 0) {
          if (req->method == HTTP_HEAD) { 
            // task check request from app web page
            if (sustainReq[taskNum].inUse) {
              // task not free, try stopping it for new stream
              if (!strcmp(variable, "stream")) {
                isStreaming[taskNum] = false;
                if (!taskNum) doPlayback = false; // only for task 0
                delay(END_WAIT + 100);
              }
            } 
            if (sustainReq[taskNum].inUse) {
              LOG_WRN("Task %d not free", taskNum);
              httpd_resp_set_status(req, "500 No free task");
            }
            else {
              sustainId = currEpoch; // task available
              res = ESP_OK;
            }
            httpd_resp_sendstr(req, NULL);
            return res;
          }
        } else {
          // stop remote streaming if currently active
          if (sustainReq[taskNum].inUse) {
            isStreaming[taskNum] = false;
            delay(END_WAIT + 100);
          }
        }
            
        // action request if task available
        if (!sustainReq[taskNum].inUse) {
          // make copy of request data and pass request to task indexed by request
          uint8_t i = taskNum;
          sustainReq[i].inUse = true;
          sustainReq[i].req = static_cast<httpd_req_t*>(malloc(sizeof(httpd_req_t)));
          new (sustainReq[i].req) httpd_req_t(*req);
          sustainReq[i].req->aux = psramFound() ? ps_malloc(AUX_STRUCT_SIZE) : malloc(AUX_STRUCT_SIZE); 
          memcpy(sustainReq[i].req->aux, req->aux, AUX_STRUCT_SIZE);
          strncpy(sustainReq[i].activity, variable, sizeof(sustainReq[i].activity) - 1); 
          // activate relevant task
          xTaskNotifyGive(sustainHandle[i]);
          return ESP_OK;
        } else httpd_resp_set_status(req, "500 No free task");
      } else {
        if (taskNum < MAX_STREAMS) LOG_WRN("Task not created for stream: %s, numStreams %d", variable, numStreams);
        else LOG_WRN("Invalid task id: %s", variable);
        httpd_resp_set_status(req, "400 Invalid url");
      }
    } else httpd_resp_set_status(req, "400 Bad URL");
    httpd_resp_sendstr(req, NULL);
  } 
  return res;
}
