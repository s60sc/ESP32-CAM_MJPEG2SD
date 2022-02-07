/*
* Capture ESP32 Cam JPEG images into a MJPEG file and store on SD
* minimises the file writes and matches them to the SD card sector size.
* MJPEG files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020
*/

#include "myConfig.h"

// user parameters set from web
bool useMotion  = true; // whether to use camera for motion detection (with motionDetect.cpp)
bool dbgMotion  = false;
bool forceRecord = false; //Recording enabled by rec button

// status & control fields
uint8_t FPS;
bool nightTime = false;
bool autoUpload = false;  // Automatically upload every created file to remote ftp server    
uint16_t insufficient = 0;  
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not        

// stream separator
static const size_t boundaryLen = strlen(JPEG_BOUNDARY);
static char hdrBuf[HDR_BUF_LEN];

// header and reporting info
static uint32_t vidSize; // total video size
static uint16_t frameCnt;
static uint32_t startMjpeg; // total overall time
static uint32_t dTimeTot; // total frame decode/monitor time
static uint32_t fTimeTot; // total frame buffering time
static uint32_t wTimeTot; // total SD write time
static uint32_t oTime; // file opening time
static uint32_t cTime; // file closing time
static uint32_t sTime; // file streaming time
static uint32_t vidDuration; // duration in secs of recorded file

uint8_t ftypePtr; // index to frameData[] for ftp
uint8_t frameDataRows = 14;                         
static uint16_t frameInterval; // units of 0.1ms between frames

// SD card storage
#define MAX_JPEG ONEMEG/2 // UXGA jpeg frame buffer at highest quality 375kB rounded up
uint8_t* SDbuffer; // has to be dynamically allocated due to size
uint8_t iSDbuffer[RAMSIZE];
static size_t highPoint;
static File mjpegFile;
static char mjpegFileName[FILE_NAME_LEN];


// SD playback
static File playbackFile;
static char partName[100];
static size_t readLen;
static const char* needle = JPEG_BOUNDARY;
static bool firstCallPlay = true;
static uint8_t recFPS;
static uint32_t recDuration;
static uint8_t saveFPS = 99;
bool doPlayback = false;

// task control
static TaskHandle_t captureHandle = NULL;
static TaskHandle_t playbackHandle = NULL;
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t playbackSemaphore;
SemaphoreHandle_t frameMutex = NULL;
SemaphoreHandle_t motionMutex = NULL;
static volatile bool isPlaying = false;
bool isCapturing = false;
bool stopPlayback = false;
bool timeLapseOn = false;
static camera_fb_t* fb;

static void readSD();

/**************** timers & ISRs ************************/

static void IRAM_ATTR frameISR() {
  // interrupt at current frame rate
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(captureHandle, &xHigherPriorityTaskWoken); // wake capture task to process frame
  if (isPlaying) xSemaphoreGiveFromISR (playbackSemaphore, &xHigherPriorityTaskWoken ); // notify playback to send frame
  if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

void controlFrameTimer(bool restartTimer) {
  // frame timer control, timer3 so dont conflict with cam
  static hw_timer_t* timer3 = NULL;
  // stop current timer
  if (timer3) {
    timerAlarmDisable(timer3);   
    timerDetachInterrupt(timer3); 
    timerEnd(timer3);
  }
  if (restartTimer) {
    // (re)start timer 3 interrupt per required framerate
    timer3 = timerBegin(3, 8000, true); // 0.1ms tick
    frameInterval = 10000 / FPS; // in units of 0.1ms 
    LOG_DBG("Frame timer interval %ums for FPS %u", frameInterval/10, FPS); 
    timerAlarmWrite(timer3, frameInterval, true); 
    timerAlarmEnable(timer3);
    timerAttachInterrupt(timer3, &frameISR, true);
  }
}

/**************** capture MJPEG  ************************/

static void openMjpeg() {
  // derive filename from date & time, store in date folder
  // time to open a new file on SD increases with the number of files already present
  oTime = millis();
  dateFormat(partName, sizeof(partName), true);
  SD_MMC.mkdir(partName); // make date folder if not present
  dateFormat(partName, sizeof(partName), false);
  // open mjpeg file with temporary name
  mjpegFile  = SD_MMC.open(partName, FILE_WRITE);
  oTime = millis() - oTime;
  LOG_DBG("File opening time: %ums", oTime);
  startAudio();
  // initialisation of counters
  startMjpeg = millis();
  frameCnt = fTimeTot = wTimeTot = dTimeTot = highPoint = vidSize = 0;
}

static inline bool doMonitor(bool capturing) {
  // monitor incoming frames for motion 
  static uint8_t motionCnt = 0;
  // ratio for monitoring stop during capture / movement prior to capture
  uint8_t checkRate = (capturing) ? FPS*MOVE_STOP_SECS : FPS/MOVE_START_CHECKS;
  if (!checkRate) checkRate = 1;
  if (++motionCnt/checkRate) motionCnt = 0; // time to check for motion
  return !(bool)motionCnt;
}  

static inline void freeFrame() {
  // free frame buffer and give semaphore to allow camera and any streaming to continue
  if (fb) esp_camera_fb_return(fb);
  fb = NULL;
  xSemaphoreGive(frameMutex);
  delay(1);
}

static void timeLapse() {
  // record a time lapse mjpeg. 
  // Note that if FPS changed during time lapse recording, 
  //  the time lapse counters wont be modified
  static int frameCnt, requiredFrames, intervalCnt = 0;
  static int intervalMark = SECS_BETWEEN_FRAMES * saveFPS;
  static File tlFile;
  if (timeSynchronized) {
    if (!frameCnt) {
      // initialise time lapse mjpeg
      requiredFrames = MINUTES_DURATION * 60 / SECS_BETWEEN_FRAMES;
      dateFormat(partName, sizeof(partName), true);
      SD_MMC.mkdir(partName); // make date folder if not present
      dateFormat(partName, sizeof(partName), false);
      snprintf(mjpegFileName, sizeof(mjpegFileName)-1, "%s_%s_%u_TL_%u.%s", 
        partName, frameData[fsizePtr].frameSizeStr, PLAYBACK_FPS, requiredFrames, FILE_EXT);
      tlFile = SD_MMC.open(mjpegFileName, FILE_WRITE);
      LOG_INF("Started time lapse file %s, interval %u, for %u frames", mjpegFileName, intervalMark, requiredFrames);
      frameCnt++;
    }
    if (intervalCnt > intervalMark) {
      // save this frame to time lapse mjpeg
      tlFile.write((uint8_t*)JPEG_BOUNDARY, boundaryLen);
      uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003; // align end of jpeg on 4 byte boundary for subsequent AVI
      size_t jpegTypeLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, fb->len + filler);
      tlFile.write((uint8_t*)hdrBuf, jpegTypeLen); // jpeg frame details
      tlFile.write(fb->buf, fb->len + filler);
      frameCnt++;
      intervalCnt = 0;   
      intervalMark = SECS_BETWEEN_FRAMES * saveFPS;  // recalc in case FPS changed                                       
    }
    intervalCnt++;
    if (frameCnt > requiredFrames) {
      tlFile.write((uint8_t*)JPEG_BOUNDARY, boundaryLen);
      tlFile.close();
      frameCnt = intervalCnt = 0;
      LOG_DBG("Finished time lapse");
    }
  }
}

static void saveFrame() {
  // build frame boundary for jpeg 
  uint32_t fTime = millis();
  // add boundary to buffer
  memcpy(SDbuffer+highPoint, JPEG_BOUNDARY, boundaryLen);
  highPoint += boundaryLen;
  size_t jpegSize = fb->len; 
  uint16_t filler = (4 - (jpegSize & 0x00000003)) & 0x00000003; // align end of jpeg on 4 byte boundary for subsequent AVI
  jpegSize += filler;
  size_t jpegTypeLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpegSize);
  memcpy(SDbuffer+highPoint, hdrBuf, HDR_BUF_LEN); // marker at start of each mjpeg frame
  highPoint += jpegTypeLen;
  memcpy(SDbuffer+highPoint, fb->buf, jpegSize);
  highPoint += jpegSize;  
  freeFrame(); 
  // only write to SD when at least RAMSIZE is available in buffer
  uint32_t wTime = millis();
  if (highPoint/RAMSIZE) {  
    size_t remainder = highPoint % RAMSIZE;
    size_t transferSize = highPoint-remainder;
    for (int i=0; i<transferSize/RAMSIZE; i++) {
      // copy psram to interim dram before writing
      memcpy(iSDbuffer, SDbuffer+RAMSIZE*i, RAMSIZE);
      mjpegFile.write(iSDbuffer, RAMSIZE);
    }
    // push remainder to buffer start, should not overlap
    memcpy(SDbuffer, SDbuffer+transferSize, remainder);
    highPoint = remainder;
  } 

  wTime = millis() - wTime;
  wTimeTot += wTime;
  vidSize += jpegTypeLen + jpegSize + filler + boundaryLen;
  LOG_DBG("SD storage time %u ms", wTime);
  frameCnt++;
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  LOG_DBG("Frame processing time %u ms", fTime);
}

static bool closeMjpeg() {
  // closes and renames the file
  uint32_t captureTime = frameCnt / FPS;
  Serial.println("");
  LOG_INF("Capture time %u, min seconds: %u ", captureTime, minSeconds);
  if (captureTime >= minSeconds) {
    cTime = millis();
    // add final boundary to buffer
    memcpy(SDbuffer + highPoint, JPEG_BOUNDARY, boundaryLen);
    highPoint += boundaryLen;
    // write remaining frame content to SD
    mjpegFile.write(SDbuffer, highPoint); 
    LOG_DBG("Final SD storage time %lu ms", millis() - cTime); 

    // finalise file on SD
    uint32_t hTime = millis();
    vidDuration = millis() - startMjpeg;
    float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
    mjpegFile.close();
    // rename file to include actual FPS, duration, and frame count, plus file extension
    snprintf(mjpegFileName, sizeof(mjpegFileName)-1, "%s_%s_%lu_%lu_%u.%s", 
      partName, frameData[fsizePtr].frameSizeStr, lround(actualFPS), lround(vidDuration/1000.0), frameCnt, FILE_EXT);
    SD_MMC.rename(partName, mjpegFileName);
    finishAudio(mjpegFileName, true);
    LOG_DBG("MJPEG close/rename time %lu ms", millis() - hTime); 
    cTime = millis() - cTime;
    insufficient = 0;

    // MJPEG stats
    LOG_INF("******** MJPEG recording stats ********");
    LOG_INF("Recorded %s", mjpegFileName);
    LOG_INF("MJPEG duration: %0.1f secs", (float)vidDuration / 1000.0);
    LOG_INF("Number of frames: %u", frameCnt);
    LOG_INF("Required FPS: %u", FPS);
    LOG_INF("Actual FPS: %0.1f", actualFPS);
    LOG_INF("File size: %0.2f MB", (float)vidSize / ONEMEG);
    if (frameCnt) {
      LOG_INF("Average frame length: %u bytes", vidSize / frameCnt);
      LOG_INF("Average frame monitoring time: %u ms", dTimeTot / frameCnt);
      LOG_INF("Average frame buffering time: %u ms", fTimeTot / frameCnt);
      LOG_INF("Average frame storage time: %u ms", wTimeTot / frameCnt);
    }
    LOG_INF("Average SD write speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    LOG_INF("File open / completion times: %u ms / %u ms", oTime, cTime);
    LOG_INF("Busy: %u%%", std::min(100 * (wTimeTot + fTimeTot + dTimeTot + oTime + cTime) / vidDuration, (uint32_t)100));
    checkMemory();
    LOG_INF("*************************************");

    if (autoUpload) ftpFileOrFolder(mjpegFileName); // Upload it to remote ftp server if requested
    checkFreeSpace();
    return true; 
  } else {
    // delete too small files if exist
    mjpegFile.close();
    SD_MMC.remove(partName);
    finishAudio(partName, false);
    LOG_DBG("Insufficient capture duration: %u secs", captureTime);
    insufficient++;                  
    return false;
  }
}

static boolean processFrame() {
  // get camera frame
  static bool wasCapturing = false;
  static bool wasRecording = false;                                 
  static bool captureMotion = false;
  bool capturePIR = false;
  bool res = true;
  uint32_t dTime = millis();
  bool finishRecording = false;
  bool savedFrame = false;
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  fb = esp_camera_fb_get();
  if (fb) {
    if (timeLapseOn) timeLapse();
    // determine if time to monitor, then get motion capture status
    if (!forceRecord && useMotion) { 
      if (dbgMotion) checkMotion(fb, false); // check each frame for debug
      else if (doMonitor(isCapturing)) captureMotion = checkMotion(fb, isCapturing); // check 1 in N frames
    }
    if (USE_PIR) {
      capturePIR = digitalRead(PIR_PIN);
      if (!capturePIR && !isCapturing && !useMotion) checkMotion(fb, isCapturing); // to update light level
    }
    
    // either active PIR, Motion, or force start button will start capture, neither active will stop capture
    isCapturing = forceRecord | captureMotion | capturePIR;
    if (forceRecord || wasRecording || doRecording) {
      if(forceRecord && !wasRecording){
        wasRecording = true;
      }else if(!forceRecord && wasRecording){
        wasRecording = false;
      }
      
      if (isCapturing && !wasCapturing) {
        // movement has occurred, start recording, and switch on lamp if night time
        if (AUTO_LAMP && nightTime) controlLamp(true); // switch on lamp
        stopPlaying(); // terminate any playback
        stopPlayback  = true; // stop any subsequent playback
        LOG_INF("Capture started by %s%s%s", captureMotion ? "Motion " : "", capturePIR ? "PIR" : "",forceRecord ? "Button" : "");
        openMjpeg();
        wasCapturing = true;
      }
      if (isCapturing && wasCapturing) {
        // capture is ongoing
        dTimeTot += millis() - dTime;
        saveFrame();
        savedFrame = true;
        showProgress();
        if (frameCnt >= MAX_FRAMES) {
          Serial.println("");
          LOG_INF("Auto closed recording after %u frames", MAX_FRAMES);
          finishRecording = true;
        }
      }
      if (!isCapturing && wasCapturing) {
        // movement stopped
        finishRecording = true;
        if (AUTO_LAMP) controlLamp(false); // switch off lamp
      }
      wasCapturing = isCapturing;
      LOG_DBG("============================");
    }
  } else {
    LOG_ERR("Failed to get frame");
    res = false;
  }
  if (!savedFrame) freeFrame(); // to prevent mutex being given when already given
  if (finishRecording) {
    // cleanly finish recording (normal or forced)
    if (stopPlayback) closeMjpeg();
    finishRecording = isCapturing = wasCapturing = stopPlayback = false; // allow for playbacks
  }
  return res;
}

static void captureTask(void* parameter) {
  // woken by frame timer when time to capture frame
  uint32_t ulNotifiedValue;
  while (true) {
    ulNotifiedValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (ulNotifiedValue > 5) ulNotifiedValue = 5; // prevent too big queue if FPS excessive
    // may be more than one isr outstanding if the task delayed by SD write or jpeg decode
    while (ulNotifiedValue-- > 0) processFrame();
  }
  vTaskDelete(NULL);
}

uint8_t setFPS(uint8_t val) {
  // change or retrieve FPS value
  if (val) {
    FPS = val;
    // change frame timer which drives the task
    controlFrameTimer(true);
    saveFPS = FPS; // used to reset FPS after playback
  }
  return FPS;
}

uint8_t setFPSlookup(uint8_t val) {
  // set FPS from framesize lookup
  fsizePtr = val;
  return setFPS(frameData[fsizePtr].defaultFPS);
}

/********************** plackback MJPEG ***********************/

int* extractMeta(const char* fname) {
  // extract frame type, FPS, duration, and frame count from filename with assumed format
  int i = 0, _recFPS = 0, _frameCnt = 0, _recDuration = 0, _ftypePtr = 0;
  char* fdup = strdup(fname);
  char* token = strtok (fdup, "_.");
  while (token != NULL) {
    if (i == 2) { // frame type string
      // search for frame type string in frameData
      for (int j = 0; j < frameDataRows; j++) {
        if (strcmp(token, frameData[j].frameSizeStr) == 0) _ftypePtr = j; // set index to relevant frameData row
      }
    }
    if (i == 3) _recFPS = atoi(token);
    if (i == 4) _recDuration = atoi(token);
    if (i == 5 && strcmp(token, FILE_EXT) != 0) _frameCnt = atoi(token);
    i++;
    token = strtok (NULL, "_.");
  }
  free(fdup);

  static int meta[4];
  meta[0] = _ftypePtr;
  meta[1] = _recFPS;
  meta[2] = _recDuration;
  meta[3] = _frameCnt;
  return meta;
}

static void playbackFPS(const char* fname) {
  // extract meta data from filename to commence playback
  int* meta = extractMeta(fname);
  recFPS = meta[1];
  recDuration = meta[2];
  // temp change framerate to recorded framerate
  FPS = recFPS;
  controlFrameTimer(true); // set frametimer
}

void openSDfile(const char* streamFile) {
  // open selected file on SD for streaming
  if (stopPlayback) {
    LOG_WRN("Playback refused - capture in progress");
  }  else {
    stopPlaying(); // in case already running
    strcpy(mjpegFileName, streamFile);
    LOG_INF("Playing %s", mjpegFileName);
    playbackFile = SD_MMC.open(mjpegFileName, FILE_READ);
    vidSize = playbackFile.size();
    playbackFPS(mjpegFileName);
    // load first cluster ready for first request
    firstCallPlay = true;
    isPlaying = true; // task control
    doPlayback = true; // browser control
    readSD(); // prime playback task
  }
}

static void readSD() {
  // read next cluster from SD for playback
  uint32_t rTime = millis();
  readLen = 0;
  if (!stopPlayback) {
    // read to interim dram before copying to psram
    readLen = playbackFile.read(iSDbuffer, RAMSIZE);
    memcpy(SDbuffer+RAMSIZE*2, iSDbuffer, RAMSIZE);
  }
  LOG_DBG("SD read time %lu ms", millis() - rTime);
  wTimeTot += millis() - rTime;
  xSemaphoreGive(readSemaphore); // signal that ready     
  delay(10);                     
}

size_t* getNextFrame() {
  // get next cluster on demand when ready for opened mjpeg
  static bool remaining;
  static size_t streamOffset;
  static size_t boundary;
  static size_t imgPtrs[2];
  static uint32_t hTimeTot;
  static uint32_t tTimeTot;
  static uint32_t hTime;
  static size_t buffLen;
  static uint16_t skipOver;
  if (firstCallPlay) {
    sTime = millis();
    hTime = millis();
    firstCallPlay = false;
    remaining = false;
    frameCnt = boundary = streamOffset = 0;
    wTimeTot = fTimeTot = hTimeTot = tTimeTot = 0;
    skipOver = 200; // skip over first boundary
    buffLen = readLen;
  }  
  
  LOG_DBG("http send time %lu ms", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (buffLen && !stopPlayback) {
    if (!remaining) {
      mTime = millis();
      xSemaphoreTake(readSemaphore, portMAX_DELAY); // wait for read from SD card completed
      LOG_DBG("SD wait time %lu ms", millis()-mTime);
      wTimeTot += millis()-mTime;
      mTime = millis();                                 
      memcpy(SDbuffer, SDbuffer+RAMSIZE*2, readLen); // load new cluster from double buffer
      LOG_DBG("memcpy took %lu ms for %u bytes", millis()-mTime, readLen);
      buffLen = readLen;
      fTimeTot += millis() - mTime;
      remaining = true;
      xTaskNotifyGive(playbackHandle); // wake up task to get next cluster - sets readLen
    }
    mTime = millis();
    if (streamOffset + skipOver > buffLen) {
      LOG_ERR("streamOffset %u + skipOver %u > buffLen %u", streamOffset, skipOver, buffLen);
      stopPlayback = true;

    } else {
      // search for next boundary in buffer
      boundary = isSubArray(SDbuffer + streamOffset + skipOver, (uint8_t*)needle, buffLen - streamOffset - skipOver, boundaryLen);
      skipOver = 0;
      LOG_DBG("frame search time %lu ms", millis()-mTime);
      fTimeTot += millis()-mTime;
      if (boundary) {
        // found image boundary
        mTime = millis();
        // wait on playbackSemaphore for rate control
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        LOG_DBG("frame timer wait %lu ms", millis()-mTime);
        tTimeTot += millis()-mTime;
        frameCnt++;
        showProgress();
        imgPtrs[0] = boundary; // amount to send
        imgPtrs[1] = streamOffset; // from here
        streamOffset += boundary;
        if (streamOffset >= buffLen) remaining = false;
      } else {
        // no (more) complete images in buffer
        imgPtrs[0] = buffLen - streamOffset; // remainder of buffer
        imgPtrs[1] = streamOffset;
        remaining = false;
      }
    }
    if (!remaining) streamOffset = 0; // load next cluster from SD in parallel
  }

  if ((!buffLen && !remaining) || stopPlayback) {
    // finished, close SD file used for streaming
    playbackFile.close();
    imgPtrs[0] = 0;
    imgPtrs[1] = 0;
    if (stopPlayback){
      LOG_INF("Force close playback");
    } else {
      uint32_t playDuration = (millis() - sTime) / 1000;
      uint32_t totBusy = wTimeTot + fTimeTot + hTimeTot;
      LOG_INF("******** MJPEG playback stats ********");
      LOG_INF("Playback %s", mjpegFileName);
      LOG_INF("Recorded FPS %u, duration %u secs", recFPS, recDuration);
      LOG_INF("Playback FPS %0.1f, duration %u secs", (float)frameCnt / playDuration, playDuration);
      LOG_INF("Number of frames: %u", frameCnt);
      LOG_INF("Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
      if (frameCnt) {
        LOG_INF("Average frame SD read time: %u ms", wTimeTot / frameCnt);
        LOG_INF("Average frame processing time: %u ms", fTimeTot / frameCnt);
        LOG_INF("Average frame delay time: %u ms", tTimeTot / frameCnt);
        LOG_INF("Average http send time: %u ms", hTimeTot / frameCnt);
        LOG_INF("Busy: %u%%", std::min(100 * totBusy / (totBusy + tTimeTot), (uint32_t)100));
      }
      checkMemory();      
      LOG_INF("*************************************\n");
    }
    setFPS(saveFPS); // realign with browser
    stopPlayback = false;
    isPlaying = false;
  }
  hTime = millis();
  return imgPtrs;
}

void stopPlaying() {
  if (isPlaying) {
    // force stop any currently running playback
    stopPlayback = true;
    // wait till stopped cleanly, but prevent infinite loop
    uint32_t timeOut = millis();
    while (isPlaying && millis() - timeOut < 2000) delay(10);
    if (isPlaying) {
      Serial.println("");
      LOG_WRN("Force closed playback");
      doPlayback = false;
      setFPS(saveFPS);
      xSemaphoreGive(playbackSemaphore);
      xSemaphoreGive(readSemaphore);
      delay(200);
    }
    stopPlayback = false;
    isPlaying = false;
  }
}

static void playbackTask(void* parameter) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    readSD();
  }
  vTaskDelete(NULL);
}

/******************* Startup ********************/

bool prepMjpeg() {
  // initialisation & prep for MJPEG capture
  pinMode(4, OUTPUT);
  digitalWrite(4, 0); // set pin 4 fully off as sd_mmc library still initialises pin 4 in 1 line mode
  SDbuffer = (uint8_t*)ps_malloc(MAX_JPEG); // buffer frame to store in SD
  if (USE_PIR) pinMode(PIR_PIN, INPUT_PULLDOWN); // pulled high for active
  if (USE_LAMP) pinMode(LAMP_PIN, OUTPUT);
  readSemaphore = xSemaphoreCreateBinary();
  playbackSemaphore = xSemaphoreCreateBinary();
  frameMutex = xSemaphoreCreateMutex();
  motionMutex = xSemaphoreCreateMutex();
  if (!esp_camera_fb_get()) return false; // test & prime camera
  LOG_INF("To record new MJPEG, do one of:");
  if (USE_PIR) LOG_INF("- attach PIR to pin %u", PIR_PIN);
  if (USE_PIR) LOG_INF("- raise pin %u to 3.3V", PIR_PIN);
  if (useMotion) LOG_INF("- move in front of camera");
  Serial.println();
  return true;
}

void startSDtasks() {
  // tasks to manage SD card operation
  xTaskCreate(&captureTask, "captureTask", 4096, NULL, 5, &captureHandle);
  if (xTaskCreate(&playbackTask, "playbackTask", 4096, NULL, 4, &playbackHandle) != pdPASS)
    LOG_ERR("Insufficient memory to create playbackTask");
  sensor_t * s = esp_camera_sensor_get();
  fsizePtr = s->status.framesize; 
  setFPS(frameData[fsizePtr].defaultFPS); // initial frames per second  
}

static void deleteTask(TaskHandle_t thisTaskHandle) {
  if (thisTaskHandle != NULL) vTaskDelete(thisTaskHandle);
  thisTaskHandle = NULL;
}

void endTasks() {
  deleteTask(captureHandle);
  deleteTask(playbackHandle);
  deleteTask(getDS18Handle);
}

void OTAprereq() {
  // stop timer isrs, and free up heap space, or crashes esp32
  controlFrameTimer(false);
  endTasks();
  esp_camera_deinit();
  delay(100);
}
