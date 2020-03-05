/*
* Extension to capture ESP32 Cam JPEG images into a MJPEG file and store on SD
* minimises the file writes and matches them to the SD card cluster size.
* MJPEG files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020
*/

// user defined environmental setup
#define USE_PIR true // whether to use PIR for motion detection
#define USE_MOTION false // whether to use camera for motion detection (with motionDetect.cpp)
#define POST_MOTION_TIME 2 // number of secs after motion stopped to complete recording, in case movement restarts
#define CLUSTERSIZE 32768 // set this to match the SD card cluster size
#define MAX_FRAMES 20000 // maximum number of frames in video before auto close
#define ONELINE true // MMC 1 line mode
#define TIMEZONE "GMT0BST,M3.5.0/01,M10.5.0/02" // set to local timezone 

#include <SD_MMC.h>
#include <regex>
#include <sys/time.h> 
#include "time.h"
#include "esp_camera.h"

// user parameters
bool debug = false;
bool doRecording = true; // whether to capture to SD or not
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
uint8_t nightSwitch = 20; // initial white level % for night/day switching
uint8_t motionVal = 10; // initial motion sensitivity setting 

// status & control fields
static uint8_t FPS;
bool lampOn = false;     
bool nightTime = false;
uint8_t lightLevel; // Current ambient light level              

// stream separator
extern const char* _STREAM_BOUNDARY; 
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
static char* part_buf[64];

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

struct frameStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
  const uint8_t scaleFactor; // (0..3)
  const uint8_t sampleRate;  // (1..N)
};
// indexed by frame size - needs to be consistent with sensor.h enum
extern const frameStruct frameData[] = {
  {"QQVGA", 160, 120, 25, 2, 1},
  {"n/a", 0, 0, 0, 0, 1}, 
  {"n/a", 0, 0, 0, 0, 1}, 
  {"HQVGA", 240, 176, 25, 3, 1}, 
  {"QVGA", 320, 240, 25, 3, 1}, 
  {"CIF", 400, 296, 25, 3, 1},
  {"VGA", 640, 480, 15, 3, 2}, 
  {"SVGA", 800, 600, 10, 3, 2}, 
  {"XGA", 1024, 768, 5, 3, 3}, 
  {"SXGA", 1280, 1024, 3, 3, 4}, 
  {"UXGA", 1600, 1200, 2, 3, 5}  
};
uint8_t fsizePtr; // index to frameData[]
static uint16_t frameInterval; // units of 0.1ms between frames

// SD card storage
#define ONEMEG (1024*1024)
#define MAX_JPEG ONEMEG/2 // UXGA jpeg frame buffer at highest quality 375kB rounded up
uint8_t* SDbuffer; // has to be dynamically allocated due to size
char* htmlBuff;
static size_t htmlBuffLen = 20000; // set big enough to hold all file names in a folder
static size_t highPoint;
static File mjpegFile;
static char mjpegName[100];
char dayFolder[50];

// SD playback
static File playbackFile;
static char partName[90];
static char optionHtml[200]; // used to build SD page html buffer
static size_t readLen;
static const char* needle = _STREAM_BOUNDARY;
static bool firstCallPlay = true;
static uint8_t recFPS;
static uint8_t saveFPS = 99;
bool doPlayback = false;

// task control
static TaskHandle_t captureHandle = NULL;
static TaskHandle_t playbackHandle = NULL;
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t playbackSemaphore;
SemaphoreHandle_t frameMutex;
static volatile bool capturePIR = false;
static volatile bool isPlaying = false;
volatile uint8_t PIRpin;
bool stopPlayback = false; 
static camera_fb_t* fb;

#define LAMP_PIN 4

bool isNight(uint8_t nightSwitch);
bool checkMotion(camera_fb_t* fb, bool captureStatus);
void stopPlaying();

// auto newline printf
#define showInfo(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define showError(format, ...) Serial.printf("ERROR: " format "\n", ##__VA_ARGS__)
#define showDebug(format, ...) if (debug) Serial.printf("DEBUG: " format "\n", ##__VA_ARGS__)

/************************** NTP  **************************/

static inline time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  // construct filename from date/time
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}

void getLocalNTP() {
  // get current time from NTP server and apply to ESP32
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 0;  // offset from GMT
  const int daylightOffset_sec = 3600; // daylight savings offset in secs
  int i = 0;
  do {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(1000);
  } while (getEpoch() < 1000 && i++ < 5); // try up to 5 times
  // set TIMEZONE as required
  setenv("TZ", TIMEZONE, 1);
  if (getEpoch() > 1000) showInfo("Got current time from NTP");
  else showError("Unable to sync with NTP");
}

/*********************** Utility functions ****************************/

void showProgress() {
  // show progess if not verbose
  static uint8_t dotCnt = 0;
  if (!debug) {
    Serial.print("."); // progress marker
    if (++dotCnt >= 50) {
      dotCnt = 0;
      Serial.println("");
    }
  }
}
  
void controlLamp(bool lampVal) {
  // switch lamp on / off
  lampOn = lampVal;
  pinMode(LAMP_PIN, OUTPUT);
  digitalWrite(LAMP_PIN, lampVal);
}

/**************** timers & ISRs ************************/

static void IRAM_ATTR frameISR() {
  // interrupt at current frame rate
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(captureHandle, &xHigherPriorityTaskWoken); // wake capture task to process frame
  if (isPlaying) xSemaphoreGiveFromISR (playbackSemaphore, &xHigherPriorityTaskWoken ); // notify playback to send frame
  if (xHigherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

static void IRAM_ATTR PIR_ISR(void* arg) {
  // PIR change - high is active
  capturePIR = digitalRead(PIRpin);
}

static void setupPIRinterrupt() {
  // do this way due to camera use of interrupts
  pinMode(PIRpin, INPUT_PULLDOWN); // pulled high for active
  esp_err_t err = gpio_isr_handler_add((gpio_num_t)PIRpin, &PIR_ISR, NULL);
  if (err != ESP_OK) showError("gpio_isr_handler_add failed (%x)", err);
  gpio_set_intr_type((gpio_num_t)PIRpin, GPIO_INTR_ANYEDGE);
}

void controlFrameTimer() {
  // frame timer control, timer3 so dont conflict with cam
  static hw_timer_t* timer3 = NULL;
  // stop current timer
  if (timer3) timerEnd(timer3);
  // (re)start timer 3 interrupt per required framerate
  timer3 = timerBegin(3, 8000, true); // 0.1ms tick
  frameInterval = 10000 / FPS; // in units of 0.1ms 
  showDebug("Frame timer interval %ums for FPS %u", frameInterval/10, FPS);
  timerAlarmWrite(timer3, frameInterval, true); // 
  timerAlarmEnable(timer3);
  timerAttachInterrupt(timer3, &frameISR, true);
}

/**************** capture MJPEG  ************************/

static bool openMjpeg() {
  // derive filename from date & time, store in date folder
  // time to open a new file on SD increases with the number of files already present
  oTime = millis();
  dateFormat(partName, sizeof(partName), true);
  SD_MMC.mkdir(partName); // make date folder if not present
  dateFormat(partName, sizeof(partName), false);
  mjpegFile = SD_MMC.open(partName, FILE_WRITE);

  // open mjpeg file with temporary name
  showInfo("Record %s mjpeg, width: %u, height: %u, @ %u fps", 
    frameData[fsizePtr].frameSizeStr, frameData[fsizePtr].frameWidth, frameData[fsizePtr].frameHeight, FPS);
  oTime = millis() - oTime;
  showDebug("File opening time: %ums", oTime);
  // initialisation of counters
  startMjpeg = millis();
  frameCnt =  fTimeTot = wTimeTot = dTimeTot = 0;
  memcpy(SDbuffer, " ", 1); // for VLC
  highPoint = vidSize = 1;
} 

static inline bool doMonitor(bool capturing) {
  // monitor incoming frames for motion 
  static uint8_t motionCnt = 0;
  // ratio for monitoring stop during capture / movement prior to capture
  uint8_t checkRate = (capturing) ? 10 : 2;
  if (++motionCnt/checkRate) motionCnt = 0; // time to check for motion
  return !(bool)motionCnt;
}  

static inline void freeFrame() {
  // free frame buffer and give semaphore to allow camera and any streaming to continue
  if (fb) esp_camera_fb_return(fb);
  fb = NULL;
  xSemaphoreGive(frameMutex);
}

static void saveFrame() {
  // build frame boundary for jpeg 
  uint32_t fTime = millis();
  // add boundary to buffer
  memcpy(SDbuffer+highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
  highPoint += streamBoundaryLen;
  size_t streamPartLen = 0;
  size_t jpegSize = 0; 
  jpegSize = fb->len; 
  streamPartLen = snprintf((char*)part_buf, 63, _STREAM_PART, jpegSize);
  memcpy(SDbuffer+highPoint, part_buf, streamPartLen); // marker at start of each mjpeg frame
  highPoint += streamPartLen;
  memcpy(SDbuffer+highPoint, fb->buf, jpegSize);
  highPoint += jpegSize;
  freeFrame(); 
  // only write to SD when at least CLUSTERSIZE is available in buffer
  uint32_t wTime = millis();
  if (highPoint/CLUSTERSIZE) {
    size_t remainder = highPoint % CLUSTERSIZE;
    mjpegFile.write(SDbuffer, highPoint-remainder);
    // push remainder to buffer start, should not overlap
    memcpy(SDbuffer, SDbuffer+highPoint-remainder, remainder);
    highPoint = remainder;
  }
  wTime = millis() - wTime;
  wTimeTot += wTime;
  vidSize += streamPartLen+jpegSize+streamBoundaryLen;
  showDebug("SD storage time %u ms", wTime);
  frameCnt++;
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  showDebug("Frame processing time %u ms", fTime);
}

static bool closeMjpeg() {
  // closes and renames the file
  uint32_t captureTime = frameCnt/FPS;
  if (captureTime > minSeconds) { 
    cTime = millis(); 
    // add final boundary to buffer
    memcpy(SDbuffer+highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
    highPoint += streamBoundaryLen;
    // write remaining frame content to SD
    mjpegFile.write(SDbuffer, highPoint); 
    showDebug("Final SD storage time %u ms", millis() - cTime); 

    // finalise file on SD
    uint32_t hTime = millis();
    vidDuration = millis() - startMjpeg;
    float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
    mjpegFile.close();
    // rename file to include actual FPS and duration, plus file extension
    snprintf(mjpegName, sizeof(mjpegName)-1, "%s_%s_%u_%u.mjpeg", 
      partName, frameData[fsizePtr].frameSizeStr, lround(actualFPS), lround(vidDuration/1000));
    SD_MMC.rename(partName, mjpegName);
    showDebug("MJPEG close/rename time %u ms", millis() - hTime); 
    cTime = millis() - cTime;

    // MJPEG stats
    showInfo("\n******** MJPEG recording stats ********");
    showInfo("Recorded %s", mjpegName);
    showInfo("MJPEG duration: %0.1f secs", (float)vidDuration / 1000); 
    showInfo("Number of frames: %u", frameCnt);
    showInfo("Required FPS: %u", FPS);    
    showInfo("Actual FPS: %0.1f", actualFPS);
    showInfo("File size: %0.2f MB", (float)vidSize / ONEMEG);
    if (frameCnt) {
      showInfo("Average frame length: %u bytes", vidSize / frameCnt);
      showInfo("Average frame monitoring time: %u ms", dTimeTot / frameCnt);
      showInfo("Average frame buffering time: %u ms", fTimeTot / frameCnt);
      showInfo("Average frame storage time: %u ms", wTimeTot / frameCnt);
    }
    showInfo("Average SD write speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    showInfo("File open / completion times: %u ms / %u ms", oTime, cTime);
    showInfo("Busy: %u%%", std::min(100 * (wTimeTot+fTimeTot+dTimeTot+oTime+cTime) / vidDuration, (uint32_t)100));
    showInfo("Free heap %u bytes, largest free block %u bytes", xPortGetFreeHeapSize(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    showInfo("***************************\n");
    return true;
  } else {
    // delete too small file
    mjpegFile.close();
    SD_MMC.remove(partName);
    showInfo("Insufficient capture duration: %u secs", captureTime);
    return false;
  }
}

static boolean processFrame() {
  // get camera frame
  static bool isCapturing = false;
  static bool wasCapturing = false;
  static bool coolDown = false;
  bool captureMotion;
  bool res = true;
  uint32_t dTime = millis();
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  fb = esp_camera_fb_get();
  if (fb) {
    // determine if time to monitor, then get motion capture status
    if (USE_MOTION) {
      if (doMonitor(isCapturing|coolDown)) captureMotion = checkMotion(fb, isCapturing);
      nightTime = isNight(nightSwitch);  
    }   
    if (USE_PIR) capturePIR = digitalRead(PIRpin); 
    // either active PIR or Motion will start capture, neither active will stop capture  
    isCapturing = captureMotion | capturePIR;
    if (doRecording) {
      if (isCapturing) coolDown = false; // cancel cooldown if fresh movement
      if (coolDown) isCapturing = true; // maintain capture during cooldown
      if (isCapturing && !wasCapturing) {
        // movement has occurred, start recording, and switch on lamp if night time
        // unless still in cooldown
        if (USE_MOTION) controlLamp(nightTime); 
        stopPlaying(); // terminate any playback
        stopPlayback = true; // prevent new playback
        showDebug("Capture started by %s%s", captureMotion ? "Motion " : "", capturePIR ? "PIR" : "");
        openMjpeg();  
        wasCapturing = true;
      }
      if (isCapturing && wasCapturing) {
        // capture is ongoing
        dTimeTot += millis()-dTime; 
        saveFrame();
        showProgress();
        if (frameCnt >= MAX_FRAMES) {
          showInfo("Auto closed recording after %u frames", MAX_FRAMES);
          isCapturing = false; // auto close on limit
        }
      }
      freeFrame();
      if (!isCapturing && wasCapturing) {
        // movement stopped 
        coolDown = isCapturing = true;
        cTime = millis();
      }
      wasCapturing = isCapturing;
      
      if (coolDown && millis()-cTime >= POST_MOTION_TIME*1000) {
        // finish recording after cooldown period
        closeMjpeg();
        controlLamp(false);
        coolDown = isCapturing = wasCapturing = stopPlayback = false; // allow for playbacks
      }
    }
    showDebug("============================");
  } else {
    showError("Failed to get frame");
    res = false;
  }
  freeFrame();
  return res;
}

static void captureTask(void* parameter) {
  // woken by frame timer when time to capture frame
  uint32_t ulNotifiedValue;
  while (true) {
    ulNotifiedValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // may be more than one isr outstanding if the task delayed by SD write or jpeg decode
    while(ulNotifiedValue-- > 0) processFrame();
  }
  vTaskDelete(NULL);
}

uint8_t setFPS(uint8_t val) {
  // change or retrieve FPS value
  if (val) {
    FPS = val;
    // change frame timer which drives the task
    controlFrameTimer(); 
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

static void playbackFPS(const char* fname) {
  // extract FPS and duration from filename with assumed format and set frametimer
  int i = 0;
  char* fdup = strdup(fname);
  char* token = strtok (fdup, "_.");
  while (token != NULL) {
    if (i==3) recFPS = atoi(token);
    if (i==4) vidDuration = atoi(token);
    i++;
    token = strtok (NULL, "_.");
  }
  free(fdup);
  // temp change framerate to recorded framerate
  FPS = recFPS;
  controlFrameTimer();
}

static void openSDfile() {
  // open selected file on SD for streaming 
  if (stopPlayback) showError("Playback refused - capture in progress");
  else {
    stopPlaying(); // in case already running
    showInfo("Playing %s", mjpegName);
    playbackFile = SD_MMC.open(mjpegName, FILE_READ);
    vidSize = playbackFile.size();
    playbackFPS(mjpegName);  
    // load first cluster ready for first request
    firstCallPlay = true;
    isPlaying = true; // task control
    doPlayback = true; // browser control
    xTaskNotifyGive(playbackHandle);
  }
}

void listDir(const char* fname, char* htmlBuff) {
  // either list day folders in root, or files in a day folder
  std::string decodedName(fname); 
  // need to decode encoded slashes
  decodedName = std::regex_replace(decodedName, std::regex("%2F"), "/");
  // check if folder or file
  if (decodedName.find(".mjpeg") != std::string::npos) {
    // mjpeg file selected
    strcpy(mjpegName, decodedName.c_str());
    openSDfile();
    decodedName = "/"; // enable day folders to be returned 
  } else strcpy(dayFolder, decodedName.c_str());
  bool returnDirs = (decodedName.compare("/")) ? false : true;

  // open relevant folder to list contents
  File root = SD_MMC.open(decodedName.c_str());
  if (!root) showError("Failed to open directory %s", decodedName.c_str());
  if (!root.isDirectory()) showError("Not a directory %s", decodedName.c_str());
  showDebug("Retrieving %s in %s", returnDirs ? "folders" : "files", decodedName.c_str());

  // build relevant option list
  strcpy(htmlBuff, "{"); 
  File file = root.openNextFile();
  bool noEntries = true;
  while (file) {
    if (file.isDirectory() && returnDirs) {
      // build folder names into HTML response
      std::string str(file.name());
      if (str.find("/System") == std::string::npos) { // ignore Sys Vol Info
        sprintf(optionHtml, "\"%s\":\"%s\",", file.name(), file.name());
        if (strlen(htmlBuff)+strlen(optionHtml) < htmlBuffLen) strcat(htmlBuff, optionHtml);
        else {
          showError("Too many folders to list %u+%u in %u bytes", strlen(htmlBuff), strlen(optionHtml), htmlBuffLen);
          break;
        }
        noEntries = false;
      }
    }
    if (!file.isDirectory() && !returnDirs) {
      // update existing html with file details
      std::string str(file.name());
      if (str.find(".mjpeg") != std::string::npos) {
        sprintf(optionHtml, "\"%s\":\"%s %0.1fMB\",", file.name(), file.name(), (float)file.size()/ONEMEG);
        if (strlen(htmlBuff)+strlen(optionHtml) < htmlBuffLen) strcat(htmlBuff, optionHtml);
        else {
          showError("Too many files to list %u+%u in %u bytes", strlen(htmlBuff), strlen(optionHtml), htmlBuffLen);
          break;
        }
        noEntries = false;
      }
    }
    file = root.openNextFile();
  }
  if (noEntries) strcat(htmlBuff, "\"/\":\"Get Folders\"}");
  else htmlBuff[strlen(htmlBuff)-1] = '}'; // lose trailing comma 
}

size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) { 
  // find a subarray (needle) in another array (haystack)
  size_t h = 0, n = 0; // Two pointers to traverse the arrays 
  // Traverse both arrays simultaneously 
  while (h < hSize && n < nSize) { 
    // If element matches, increment both pointers 
    if (haystack[h] == needle[n]) { 
      h++; 
      n++; 
      // If needle is completely traversed 
      if (n == nSize) return h; 
    } else { 
      // if not, increment h and reset n 
      h = h - n + 1; 
      n = 0; 
    } 
  } 
  return 0; 
} 

void readSD() {
  // read next cluster from SD for playback
  uint32_t rTime = millis();
  readLen = (stopPlayback) ? 0 : playbackFile.read(SDbuffer+CLUSTERSIZE*2, CLUSTERSIZE);
  showDebug("SD read time %u ms", millis() - rTime);
  wTimeTot += millis() - rTime;
  xSemaphoreGive(readSemaphore); // signal that ready
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
  
  showDebug("http send time %u ms", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (buffLen && !stopPlayback) {
    if (!remaining) {
      mTime = millis(); 
      xSemaphoreTake(readSemaphore, portMAX_DELAY); // wait for read from SD card completed
      showDebug("SD wait time %u ms", millis()-mTime);
      wTimeTot += millis()-mTime;
      mTime = millis();                                 
      memcpy(SDbuffer, SDbuffer+CLUSTERSIZE*2, readLen); // load new cluster from double buffer
      showDebug("memcpy took %u ms for %u bytes", millis()-mTime, readLen);
      buffLen = readLen;
      fTimeTot += millis()-mTime;                                 
      remaining = true; 
      xTaskNotifyGive(playbackHandle); // wake up task to get next cluster - sets readLen
    }
    mTime = millis();
    if (streamOffset+skipOver > buffLen) {
      showError("streamOffset %u + skipOver %u > buffLen %u", streamOffset, skipOver, buffLen);
      stopPlayback = true;
    
    } else {
      // search for next boundary in buffer
      boundary = isSubArray(SDbuffer+streamOffset+skipOver, (uint8_t*)needle, buffLen-streamOffset-skipOver, streamBoundaryLen);
      skipOver = 0;
      showDebug("frame search time %u ms", millis()-mTime);
      fTimeTot += millis()-mTime;
      if (boundary) {
        // found image boundary
        mTime = millis();
        // wait on playbackSemaphore for rate control
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        showDebug("frame timer wait %u ms", millis()-mTime);
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
    if (stopPlayback) showInfo("Force close playback");
    else {
      uint32_t playDuration = (millis()-sTime)/1000;
      uint32_t totBusy = wTimeTot+fTimeTot+hTimeTot;
      showInfo("\n******** MJPEG playback stats ********");
      showInfo("Playback %s", mjpegName);
      showInfo("Recorded FPS %u, duration %u secs", recFPS, vidDuration);
      showInfo("Required playback FPS %u", FPS);
      showInfo("Actual playback FPS %0.1f, duration %u secs", (float)frameCnt/playDuration, playDuration);
      showInfo("Number of frames: %u", frameCnt);
      showInfo("Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
      if (frameCnt) {
        showInfo("Average frame SD read time: %u ms", wTimeTot / frameCnt);
        showInfo("Average frame processing time: %u ms", fTimeTot / frameCnt);
        showInfo("Average frame delay time: %u ms", tTimeTot / frameCnt);
        showInfo("Average http send time: %u ms", hTimeTot / frameCnt);
        showInfo("Busy: %u%%", std::min(100 * totBusy/(totBusy+tTimeTot),(uint32_t)100));
      }
      showInfo("Free heap %u bytes, largest free block %u bytes", xPortGetFreeHeapSize(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
      showInfo("***************************\n");
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
    while (isPlaying && millis()-timeOut < 2000) delay(10); 
    if (isPlaying) {
      showError("\nFailed to cleanly close playback");
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

/***************************** SD card ********************************/

static void infoSD() {
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) showError("No SD card attached");
  else {
    char typeStr[8] = "UNKNOWN";
    if (cardType == CARD_MMC) strcpy(typeStr, "MMC");
    else if (cardType == CARD_SD) strcpy(typeStr, "SDSC");
    else if (cardType == CARD_SDHC) strcpy(typeStr, "SDHC");

    uint64_t cardSize, totBytes, useBytes = 0;
    cardSize = SD_MMC.cardSize() / ONEMEG;
    totBytes = SD_MMC.totalBytes() / ONEMEG;
    useBytes = SD_MMC.usedBytes() / ONEMEG;
    showInfo("SD card type %s, Size: %lluMB, Used space: %lluMB, Total space: %lluMB", 
      typeStr, cardSize, useBytes, totBytes);
  } 
}

bool prepSD_MMC(bool oneLine) {
  /* open SD card in required mode: MMC 1 bit (1), MMC 4 bit (4)
     MMC4  MMC1  ESP32  
      D2          12      
      D3    CS    13     
      CMD   MOSI  15       
      CLK   SCK   14    
      D0    MISO  2     
      D1          4
 */
  bool res = false;
  if (oneLine) {
    // SD_MMC 1 line mode
    res = SD_MMC.begin("/sdcard", true);
  } else res = SD_MMC.begin(); // SD_MMC 4 line mode
  if (res){ 
    showInfo("SD ready in %s mode ", oneLine ? "1-line" : "4-line");
    infoSD();
    return true;
  } else {
    showError("SD mount failed for %s mode", oneLine ? "1-line" : "4-line");
    return false;
  }
}

/******************* Startup ********************/

bool prepMjpeg() {
  // initialisation & prep for MJPEG capture
  if (psramFound()) {
    if (prepSD_MMC(ONELINE)) { 
      if (ONELINE) controlLamp(false); // set lamp fully off as sd_mmc library still initialises pin 4
      getLocalNTP(); // get time from NTP
      SDbuffer = (uint8_t*)ps_malloc(MAX_JPEG); // buffer frame to store in SD
      htmlBuff = (char*)ps_malloc(htmlBuffLen); 
      if (USE_PIR) {
        PIRpin = (ONELINE) ? 12 : 33;
        pinMode(PIRpin, INPUT_PULLDOWN); // pulled high for active
        // setupPIRinterrupt(); // not required
      }
      readSemaphore = xSemaphoreCreateBinary();
      playbackSemaphore = xSemaphoreCreateBinary();
      frameMutex = xSemaphoreCreateMutex();
      if (!esp_camera_fb_get()) return false; // test & prime camera
      sensor_t * s = esp_camera_sensor_get();
      fsizePtr = s->status.framesize;
      setFPS(frameData[fsizePtr].defaultFPS); // initial frames per second  
      xTaskCreate(&captureTask, "captureTask", 4096*8, NULL, 5, &captureHandle);
      xTaskCreate(&playbackTask, "playbackTask", 4096*8, NULL, 4, &playbackHandle);   
      showDebug("Free heap %u bytes", xPortGetFreeHeapSize());
      showInfo("\nTo record new MJPEG, do one of:");
      if (USE_PIR) showInfo("- attach PIR to pin %u", PIRpin);
      if (USE_PIR) showInfo("- raise pin %u to 3.3V", PIRpin);
      if (USE_MOTION) showInfo("- move in front of camera");
      return true;
    } else {
      showError("SD storage not available");
      return false;
    }
  } else {
    showError("pSRAM must be enabled");
    return false;
  }
}
