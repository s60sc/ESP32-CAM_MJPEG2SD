/*
* Capture ESP32 Cam JPEG images into a AVI file and store on SD
* matches file writes to the SD card sector size.
* AVI files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020, 2022
*/

#include "appGlobals.h"

// user parameters set from web
bool useMotion  = true; // whether to use camera for motion detection (with motionDetect.cpp)
bool dbgMotion  = false;
bool forceRecord = false; // Recording enabled by rec button

// motion detection parameters
int moveStartChecks = 5; // checks per second for start motion
int moveStopSecs = 2; // secs between each check for stop, also determines post motion time
int maxFrames = 20000; // maximum number of frames in video before auto close 

// record timelapse avi independently of motion capture, file name has same format as avi except ends with T
int tlSecsBetweenFrames; // too short interval will interfere with other activities
int tlDurationMins; // a new file starts when previous ends
int tlPlaybackFPS;  // rate to playback the timelapse, min 1 

// status & control fields
uint8_t FPS;
bool nightTime = false;
bool autoUpload = false;  // Automatically upload every created file to remote ftp server
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not 
uint8_t xclkMhz = 20; // camera clock rate MHz

// header and reporting info
static uint32_t vidSize; // total video size
static uint16_t frameCnt;
static uint32_t startTime; // total overall time
static uint32_t dTimeTot; // total frame decode/monitor time
static uint32_t fTimeTot; // total frame buffering time
static uint32_t wTimeTot; // total SD write time
static uint32_t oTime; // file opening time
static uint32_t cTime; // file closing time
static uint32_t sTime; // file streaming time

uint8_t frameDataRows = 14; 
static uint16_t frameInterval; // units of 0.1ms between frames

// SD card storage
#define MAX_JPEG ONEMEG/2 // UXGA jpeg frame buffer at highest quality 375kB rounded up
uint8_t iSDbuffer[(RAMSIZE + CHUNK_HDR) * 2];
static size_t highPoint;
static File aviFile;
static char aviFileName[FILE_NAME_LEN];

// SD playback
static File playbackFile;
static char partName[FILE_NAME_LEN];
static size_t readLen;
static uint8_t recFPS;
static uint32_t recDuration;
static uint8_t saveFPS = 99;
bool doPlayback = false;

// task control
TaskHandle_t captureHandle = NULL;
TaskHandle_t playbackHandle = NULL;
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t playbackSemaphore;
SemaphoreHandle_t motionMutex = NULL;
SemaphoreHandle_t aviMutex = NULL;
static volatile bool isPlaying = false;
bool isCapturing = false;
bool stopPlayback = false;
bool timeLapseOn = false;

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

/**************** capture AVI  ************************/

static void openAvi() {
  // derive filename from date & time, store in date folder
  // time to open a new file on SD increases with the number of files already present
  oTime = millis();
  dateFormat(partName, sizeof(partName), true);
  SD_MMC.mkdir(partName); // make date folder if not present
  dateFormat(partName, sizeof(partName), false);
  // open avi file with temporary name 
  aviFile  = SD_MMC.open(AVITEMP, FILE_WRITE);
  oTime = millis() - oTime;
  LOG_DBG("File opening time: %ums", oTime);
  startAudio();
  // initialisation of counters
  startTime = millis();
  frameCnt = fTimeTot = wTimeTot = dTimeTot = vidSize = 0;
  highPoint = AVI_HEADER_LEN; // allot space for AVI header
  prepAviIndex();
}

static inline bool doMonitor(bool capturing) {
  // monitor incoming frames for motion 
  static uint8_t motionCnt = 0;
  // ratio for monitoring stop during capture / movement prior to capture
  uint8_t checkRate = (capturing) ? FPS*moveStopSecs : FPS/moveStartChecks;
  if (!checkRate) checkRate = 1;
  if (++motionCnt/checkRate) motionCnt = 0; // time to check for motion
  return !(bool)motionCnt;
}  

static void timeLapse(camera_fb_t* fb) {
  // record a time lapse avi
  // Note that if FPS changed during time lapse recording, 
  //  the time lapse counters wont be modified
  static int frameCntTL, requiredFrames, intervalCnt = 0;
  static int intervalMark = tlSecsBetweenFrames * saveFPS;
  static File tlFile;
  static char TLname[FILE_NAME_LEN];
  if (timeLapseOn) {
    if (timeSynchronized) {
      if (!frameCntTL) {
        // initialise time lapse avi
        requiredFrames = tlDurationMins * 60 / tlSecsBetweenFrames;
        dateFormat(partName, sizeof(partName), true);
        SD_MMC.mkdir(partName); // make date folder if not present
        dateFormat(partName, sizeof(partName), false);
        int tlen = snprintf(TLname, FILE_NAME_LEN - 1, "%s_%s_%u_%u_%u_T.%s", 
          partName, frameData[fsizePtr].frameSizeStr, tlPlaybackFPS, tlDurationMins, requiredFrames, FILE_EXT);
        if (tlen > FILE_NAME_LEN - 1) LOG_WRN("file name truncated");
        if (SD_MMC.exists(TLTEMP)) SD_MMC.remove(TLTEMP);
        tlFile = SD_MMC.open(TLTEMP, FILE_WRITE);
        tlFile.write(aviHeader, AVI_HEADER_LEN); // space for header
        prepAviIndex(true);
        LOG_INF("Started time lapse file %s, duration %u mins, for %u frames",  TLname, tlDurationMins, requiredFrames);
        frameCntTL++; // to stop re-entering
      }
      if (intervalCnt > intervalMark) {
        // save this frame to time lapse avi
        uint8_t hdrBuff[CHUNK_HDR];
        memcpy(hdrBuff, dcBuf, 4); 
        // align end of jpeg on 4 byte boundary for AVI
        uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003; 
        uint32_t jpegSize = fb->len + filler;
        memcpy(hdrBuff+4, &jpegSize, 4);
        tlFile.write(hdrBuff, CHUNK_HDR); // jpeg frame details
        tlFile.write(fb->buf, jpegSize);
        buildAviIdx(jpegSize, true, true); // save avi index for frame
        frameCntTL++;
        intervalCnt = 0;   
        intervalMark = tlSecsBetweenFrames * saveFPS;  // recalc in case FPS changed 
      }
      intervalCnt++;
      if (frameCntTL > requiredFrames) {
        // finish timelapse recording
        xSemaphoreTake(aviMutex, portMAX_DELAY);
        buildAviHdr(tlPlaybackFPS, fsizePtr, --frameCntTL, true);
        xSemaphoreGive(aviMutex);
        // add index
        finalizeAviIndex(frameCntTL, true);
        size_t idxLen = 0;
        do {
          idxLen = writeAviIndex(iSDbuffer, RAMSIZE, true);
          tlFile.write(iSDbuffer, idxLen);
        } while (idxLen > 0);
        // add header
        tlFile.seek(0, SeekSet); // start of file
        tlFile.write(aviHeader, AVI_HEADER_LEN);
        tlFile.close(); 
        SD_MMC.rename(TLTEMP, TLname);
        frameCntTL = intervalCnt = 0;
        LOG_DBG("Finished time lapse");
        if (autoUpload) ftpFileOrFolder(TLname); // Upload it to remote ftp server if requested
      }
    }
  } else frameCntTL = intervalCnt = 0;
}

static void saveFrame(camera_fb_t* fb) {
  // save frame on SD card
  uint32_t fTime = millis();
  // align end of jpeg on 4 byte boundary for AVI
  uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003; 
  size_t jpegSize = fb->len + filler;
  // add avi frame header
  memcpy(iSDbuffer+highPoint, dcBuf, 4); 
  memcpy(iSDbuffer+highPoint+4, &jpegSize, 4);
  highPoint += CHUNK_HDR;
  if (highPoint >= RAMSIZE) {
    // marker overflows buffer
    highPoint -= RAMSIZE;
    aviFile.write(iSDbuffer, RAMSIZE);
    // push overflow to buffer start
    memcpy(iSDbuffer, iSDbuffer+RAMSIZE, highPoint);
  }
  // add frame content
  size_t jpegRemain = jpegSize;
  uint32_t wTime = millis();
  while (jpegRemain >= RAMSIZE - highPoint) {
    // write to SD when RAMSIZE is filled in buffer
    memcpy(iSDbuffer+highPoint, fb->buf + jpegSize - jpegRemain, RAMSIZE - highPoint);
    aviFile.write(iSDbuffer, RAMSIZE);
    jpegRemain -= RAMSIZE - highPoint;
    highPoint = 0;
  } 
  wTime = millis() - wTime;
  wTimeTot += wTime;
  LOG_DBG("SD storage time %u ms", wTime);
  // whats left or small frame
  memcpy(iSDbuffer+highPoint, fb->buf + jpegSize - jpegRemain, jpegRemain);
  highPoint += jpegRemain;
  
  if (smtpUse) {
    if (frameCnt == smtpFrame) {
      // save required frame for email alert
      smtpBufferSize = fb->len;
      if (fb->len < MAX_JPEG && SMTPbuffer != NULL) memcpy(SMTPbuffer, fb->buf, fb->len);
    }
  }
  buildAviIdx(jpegSize); // save avi index for frame
  vidSize += jpegSize + CHUNK_HDR;
  frameCnt++; 
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  LOG_DBG("Frame processing time %u ms", fTime);
}

static bool closeAvi() {
  // closes the recorded file
  uint32_t vidDuration = millis() - startTime;
  uint32_t vidDurationSecs = lround(vidDuration/1000.0);
  Serial.println("");
  LOG_DBG("Capture time %u, min seconds: %u ", vidDurationSecs, minSeconds);

  cTime = millis();
  // write remaining frame content to SD
  aviFile.write(iSDbuffer, highPoint); 
  size_t readLen = 0;
  // add wav file if exists
  finishAudio(true);
  bool haveWav = haveWavFile();
  if (haveWav) {
    do {
      readLen = writeWavFile(iSDbuffer, RAMSIZE);
      aviFile.write(iSDbuffer, readLen);
    } while (readLen > 0);
  }
  // save avi index
  finalizeAviIndex(frameCnt);
  do {
    readLen = writeAviIndex(iSDbuffer, RAMSIZE);
    if (readLen) aviFile.write(iSDbuffer, readLen);
  } while (readLen > 0);
  // save avi header at start of file
  float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
  uint8_t actualFPSint = (uint8_t)(lround(actualFPS));  
  xSemaphoreTake(aviMutex, portMAX_DELAY);
  buildAviHdr(actualFPSint, fsizePtr, frameCnt);
  xSemaphoreGive(aviMutex); 
  aviFile.seek(0, SeekSet); // start of file
  aviFile.write(aviHeader, AVI_HEADER_LEN); 
  aviFile.close();
  LOG_DBG("Final SD storage time %lu ms", millis() - cTime);
  uint32_t hTime = millis(); 
  if (vidDurationSecs >= minSeconds) {
    // name file to include actual dateTime, FPS, duration, and frame count
    int alen = snprintf(aviFileName, FILE_NAME_LEN - 1, "%s_%s_%u_%u_%u%s.%s", 
      partName, frameData[fsizePtr].frameSizeStr, actualFPSint, vidDurationSecs, frameCnt, haveWav ? "_S" : "", FILE_EXT);
    if (alen > FILE_NAME_LEN - 1) LOG_WRN("file name truncated");
    SD_MMC.rename(AVITEMP, aviFileName);
    LOG_DBG("AVI close time %lu ms", millis() - hTime); 
    cTime = millis() - cTime;
    // AVI stats
    LOG_INF("******** AVI recording stats ********");
    LOG_ALT("Recorded %s", aviFileName);
    LOG_INF("AVI duration: %u secs", vidDurationSecs);
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
    if (mqtt_active) {
      sprintf(jsonBuff, "{\"RECORD\":\"OFF\", \"TIME\":\"%s\"}", esp_log_system_timestamp());
      mqttPublish(jsonBuff);
    }
    if (autoUpload) ftpFileOrFolder(aviFileName); // Upload it to remote ftp server if requested
    checkFreeSpace();
    char subjectMsg[50];
    sprintf(subjectMsg, "Frame %u attached", smtpFrame);
    emailAlert("Motion Alert", subjectMsg);
    return true; 
  } else {
    // delete too small files if exist
    SD_MMC.remove(AVITEMP);
    LOG_INF("Insufficient capture duration: %u secs", vidDurationSecs); 
    return false;
  }
}

static boolean processFrame() {
  // get camera frame
  static bool wasCapturing = false;
  static bool wasRecording = false;
  static bool captureMotion = false;
  bool res = true;
  uint32_t dTime = millis();
  bool finishRecording = false;
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == NULL) return false;
  timeLapse(fb);
  // determine if time to monitor, then get motion capture status
  if (!forceRecord && useMotion) { 
    if (dbgMotion) checkMotion(fb, false); // check each frame for debug
    else if (doMonitor(isCapturing)) captureMotion = checkMotion(fb, isCapturing); // check 1 in N frames
  }
  if (pirUse) {
    pirVal = getPIRval();
    if (!pirVal && !isCapturing && !useMotion) checkMotion(fb, isCapturing); // to update light level
  }
  
  // either active PIR, Motion, or force start button will start capture, neither active will stop capture
  isCapturing = forceRecord | captureMotion | pirVal;
  if (forceRecord || wasRecording || doRecording) {
    if (forceRecord && !wasRecording) wasRecording = true;
    else if (!forceRecord && wasRecording) wasRecording = false;
    
    if (isCapturing && !wasCapturing) {
      // movement has occurred, start recording, and switch on lamp if night time
      if (lampAuto && nightTime) setLamp(lampLevel); // switch on lamp
      stopPlaying(); // terminate any playback
      stopPlayback = true; // stop any subsequent playback
      LOG_ALT("Capture started by %s%s%s", captureMotion ? "Motion " : "", pirVal ? "PIR" : "",forceRecord ? "Button" : "");
      if (mqtt_active) {
        sprintf(jsonBuff, "{\"RECORD\":\"ON\", \"TIME\":\"%s\"}", esp_log_system_timestamp());
        mqttPublish(jsonBuff);
      }
      openAvi();
      wasCapturing = true;
    }
    if (isCapturing && wasCapturing) {
      // capture is ongoing
      dTimeTot += millis() - dTime;
      saveFrame(fb);
      showProgress();
      if (frameCnt >= maxFrames) {
        Serial.println("");
        LOG_INF("Auto closed recording after %u frames", maxFrames);
        forceRecord = false;
      }
    }
    if (!isCapturing && wasCapturing) {
      // movement stopped
      finishRecording = true;
      if (lampAuto) setLamp(0); // switch off lamp
    }
    wasCapturing = isCapturing;
    LOG_DBG("============================");
  }
  if (fb != NULL) esp_camera_fb_return(fb);
  fb = NULL; 
  if (finishRecording) {
    // cleanly finish recording (normal or forced)
    if (stopPlayback) closeAvi();
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

/********************** plackback AVI as MJPEG ***********************/

static fnameStruct extractMeta(const char* fname) {
  // extract FPS, duration, and frame count from avi filename
  fnameStruct fnameMeta;
  char fnameStr[FILE_NAME_LEN];
  strcpy(fnameStr, fname);
  // replace all '_' with space for sscanf
  for (int i = 0; i <= strlen(fnameStr); i++) 
    if (fnameStr[i] == '_') fnameStr[i] = ' ';
  int items = sscanf(fnameStr, "%*s %*s %*s %hhu %u %hu", &fnameMeta.recFPS, &fnameMeta.recDuration, &fnameMeta.frameCnt);
  if (items != 3) LOG_ERR("failed to parse %s, items %u", fname, items);
  return fnameMeta;
}

static void playbackFPS(const char* fname) {
  // extract meta data from filename to commence playback
  fnameStruct fnameMeta = extractMeta(fname);
  recFPS = fnameMeta.recFPS;
  if (recFPS < 1) recFPS = 1;
  recDuration = fnameMeta.recDuration;
  // temp change framerate to recorded framerate
  FPS = recFPS;
  controlFrameTimer(true); // set frametimer
}

static void readSD() {
  // read next cluster from SD for playback
  uint32_t rTime = millis();
  // read to interim dram before copying to psram
  readLen = 0;
  if (!stopPlayback) {
    readLen = playbackFile.read(iSDbuffer+RAMSIZE+CHUNK_HDR, RAMSIZE);
    LOG_DBG("SD read time %lu ms", millis() - rTime);
  }
  wTimeTot += millis() - rTime;
  xSemaphoreGive(readSemaphore); // signal that ready     
  delay(10);
}


void openSDfile(const char* streamFile) {
  // open selected file on SD for streaming
  if (stopPlayback) LOG_WRN("Playback refused - capture in progress");
  else {
    stopPlaying(); // in case already running
    strcpy(aviFileName, streamFile);
    LOG_INF("Playing %s", aviFileName);
    playbackFile = SD_MMC.open(aviFileName, FILE_READ);
    playbackFile.seek(AVI_HEADER_LEN, SeekSet); // skip over header
    playbackFPS(aviFileName);
    isPlaying = true; // task control
    doPlayback = true; // browser control
    readSD(); // prime playback task
  }
}

mjpegStruct getNextFrame(bool firstCall) {
  // get next cluster on demand when ready for opened avi
  mjpegStruct mjpegData;
  static bool remainingBuff;
  static bool completedPlayback;
  static size_t buffOffset;
  static uint32_t hTimeTot;
  static uint32_t tTimeTot;
  static uint32_t hTime;
  static size_t remainingFrame;
  static size_t buffLen;
  const uint32_t dcVal = 0x63643030; // value of 00dc marker
  if (firstCall) {
    sTime = millis();
    hTime = millis();  
    remainingBuff = completedPlayback = false;
    frameCnt = remainingFrame = vidSize =  buffOffset = 0;
    wTimeTot = fTimeTot = hTimeTot = tTimeTot = 0;
  }  
  
  LOG_DBG("http send time %lu ms", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (!stopPlayback) {
    // continue sending out frames
    if (!remainingBuff) {
      // load more data from SD
      mTime = millis();
      // move final bytes to buffer start in case jpeg marker at end of buffer
      memcpy(iSDbuffer, iSDbuffer+RAMSIZE, CHUNK_HDR);
      xSemaphoreTake(readSemaphore, portMAX_DELAY); // wait for read from SD card completed
      buffLen = readLen;
      LOG_DBG("SD wait time %lu ms", millis()-mTime);
      wTimeTot += millis()-mTime;
      mTime = millis();  
      // overlap buffer by CHUNK_HDR to prevent jpeg marker being split between buffers
      memcpy(iSDbuffer+CHUNK_HDR, iSDbuffer+RAMSIZE+CHUNK_HDR, buffLen); // load new cluster from double buffer

      LOG_DBG("memcpy took %lu ms for %u bytes", millis()-mTime, buffLen);
      fTimeTot += millis() - mTime;
      remainingBuff = true;
      if (buffOffset > RAMSIZE) buffOffset = 4; // special case, marker overlaps end of buffer 
      else buffOffset = frameCnt ? 0 : CHUNK_HDR; // only before 1st frame
      xTaskNotifyGive(playbackHandle); // wake up task to get next cluster - sets readLen
    }
    mTime = millis();
    if (!remainingFrame) {
      // at start of jpeg frame marker
      uint32_t inVal;
      memcpy(&inVal, iSDbuffer + buffOffset, 4);
      if (inVal != dcVal) {
        // reached end of frames to stream
        mjpegData.buffLen = buffOffset; // remainder of final jpeg
        mjpegData.buffOffset = 0; // from start of buff
        mjpegData.jpegSize = 0; 
        stopPlayback = completedPlayback = true;
        return mjpegData;
      } else {
        // get jpeg frame size
        uint32_t jpegSize;
        memcpy(&jpegSize, iSDbuffer + buffOffset + 4, 4);
        remainingFrame = jpegSize;
        vidSize += jpegSize;
        buffOffset += CHUNK_HDR; // skip over marker 
        mjpegData.jpegSize = jpegSize; // signal start of jpeg to webServer
        mTime = millis();
        // wait on playbackSemaphore for rate control
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        LOG_DBG("frame timer wait %lu ms", millis()-mTime);
        tTimeTot += millis()-mTime;
        frameCnt++;
        showProgress();
      }
    } else mjpegData.jpegSize = 0; // within frame,    
    // determine amount of data to send to webServer
    if (buffOffset > RAMSIZE) mjpegData.buffLen = 0; // special case 
    else mjpegData.buffLen = (remainingFrame > buffLen - buffOffset) ? buffLen - buffOffset : remainingFrame;
    mjpegData.buffOffset = buffOffset; // from here    
    remainingFrame -= mjpegData.buffLen;
    buffOffset += mjpegData.buffLen;
    if (buffOffset >= buffLen) remainingBuff = false;
    
  } else {
    // finished, close SD file used for streaming
    playbackFile.close();
    logPrint("\n");
    if (!completedPlayback) LOG_INF("Force close playback");
    uint32_t playDuration = (millis() - sTime) / 1000;
    uint32_t totBusy = wTimeTot + fTimeTot + hTimeTot;
    LOG_INF("******** AVI playback stats ********");
    LOG_INF("Playback %s", aviFileName);
    LOG_INF("Recorded FPS %u, duration %u secs", recFPS, recDuration);
    LOG_INF("Playback FPS %0.1f, duration %u secs", (float)frameCnt / playDuration, playDuration);
    LOG_INF("Number of frames: %u", frameCnt);
    if (frameCnt) {
      LOG_INF("Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
      LOG_INF("Average frame SD read time: %u ms", wTimeTot / frameCnt);
      LOG_INF("Average frame processing time: %u ms", fTimeTot / frameCnt);
      LOG_INF("Average frame delay time: %u ms", tTimeTot / frameCnt);
      LOG_INF("Average http send time: %u ms", hTimeTot / frameCnt);
      LOG_INF("Busy: %u%%", min(100 * totBusy / (totBusy + tTimeTot), (uint32_t)100));
    }
    checkMemory();
    LOG_INF("*************************************\n");
    setFPS(saveFPS); // realign with browser
    stopPlayback = isPlaying = false;
    mjpegData.buffLen = mjpegData.buffOffset = 0; // signal end of jpeg
  }
  hTime = millis();
  delay(1);
  return mjpegData;
}

void stopPlaying() {
  isStreaming = false; // if on another browser instance
  if (isPlaying) {
    // force stop any currently running playback
    stopPlayback = true;
    // wait till stopped cleanly, but prevent infinite loop
    uint32_t timeOut = millis();
    while (isPlaying && millis() - timeOut < 2000) delay(10);
    if (isPlaying) {
      Serial.println("");
      LOG_WRN("Force closed playback");
      doPlayback = false; // stop webserver playback
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

static void startSDtasks() {
  // tasks to manage SD card operation
  xTaskCreate(&captureTask, "captureTask", 1024 * 4, NULL, 5, &captureHandle);
  xTaskCreate(&playbackTask, "playbackTask", 1024 * 4, NULL, 4, &playbackHandle);
  sensor_t * s = esp_camera_sensor_get();
  fsizePtr = s->status.framesize; 
  setFPS(frameData[fsizePtr].defaultFPS); // initial frames per second  
  debugMemory("startSDtasks");
}

bool prepRecording() {
  // initialisation & prep for AVI capture
  readSemaphore = xSemaphoreCreateBinary();
  playbackSemaphore = xSemaphoreCreateBinary();
  aviMutex = xSemaphoreCreateMutex();
  motionMutex = xSemaphoreCreateMutex();
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb == NULL) LOG_WRN("failed to get camera frame");
  else {
    esp_camera_fb_return(fb);
    fb = NULL;
  }
  startSDtasks();
  LOG_INF("To record new AVI, do one of:");
  if (pirUse) {
    String extStr = (pirPin >= EXTPIN) ? "IO extender" : "";
    LOG_INF("- attach PIR to %s pin %u", extStr, pirPin);
    LOG_INF("- raise %s pin %u to 3.3V", extStr, pirPin);
  }
  if (useMotion) LOG_INF("- move in front of camera");
  Serial.println();
  debugMemory("prepRecording");
  return true;
}

static void deleteTask(TaskHandle_t thisTaskHandle) {
  if (thisTaskHandle != NULL) vTaskDelete(thisTaskHandle);
  thisTaskHandle = NULL;
}

void endTasks() {
  deleteTask(captureHandle);
  deleteTask(playbackHandle);
  deleteTask(DS18B20handle);
  deleteTask(servoHandle);
  deleteTask(emailHandle);
  deleteTask(ftpHandle);
  deleteTask(uartClientHandle);
}

void OTAprereq() {
  // stop timer isrs, and free up heap space, or crashes esp32
  controlFrameTimer(false);
  stopPing();
  endTasks();
  esp_camera_deinit();
  delay(100);
}
