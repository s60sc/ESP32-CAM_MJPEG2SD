/*
* Extension to capture ESP32 Cam JPEG images into a MJPEG file and store on SD
* minimises the file writes and matches them to the SD card sector size.
* MJPEG files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020
*/

extern char timezone[]; //Defined in myConfig.h

// user defined environmental setup
#define USE_PIR false // whether to use PIR for motion detection
#define USE_MOTION true // whether to use camera for motion detection (with motionDetect.cpp)
#define MOVE_START_CHECKS 5 // checks per second for start
#define MOVE_STOP_SECS 1 // secs between each check for stop
#define RAMSIZE 16384 // set this to multiple of SD card sector size (512 or 1024 bytes)
#define MAX_FRAMES 20000 // maximum number of frames in video before auto close
#define ONELINE true // MMC 1 line mode
#define minCardFreeSpace 50 // Minimum amount of card free Megabytes before freeSpaceMode action is enabled
#define freeSpaceMode 1 // 0 - No Check, 1 - Delete oldest dir, 2 - Move to ftp and then delete folder                                                                                                               


#include <SD_MMC.h>
#include <regex>
#include <sys/time.h>
#include "time.h"
#include "esp_camera.h"

// user parameters
bool debug = false;
bool debugMotion = false;
extern bool doRecording;// = true; // whether to capture to SD or not
extern uint8_t minSeconds;// = 5; // default min video length (includes POST_MOTION_TIME)
extern uint8_t nightSwitch;// = 20; // initial white level % for night/day switching
extern float motionVal;// = 8.0; // initial motion sensitivity setting
bool timeSynchronized = false;
// status & control fields
uint8_t FPS;
bool lampOn = false;
bool nightTime = false;
uint8_t lightLevel; // Current ambient light level     
uint16_t insufficient = 0;  

// stream separator
extern const char* _STREAM_BOUNDARY;
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
#define PART_BUF_LEN 64
static char* part_buf[PART_BUF_LEN];

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
  {"QQVGA", 160, 120, 30, 1, 1},
  {"n/a", 0, 0, 0, 0, 1}, 
  {"n/a", 0, 0, 0, 0, 1}, 
  {"HQVGA", 240, 176, 30, 2, 1}, 
  {"QVGA", 320, 240, 30, 2, 1}, 
  {"CIF", 400, 296, 30, 2, 1},
  {"VGA", 640, 480, 20, 3, 1}, 
  {"SVGA", 800, 600, 20, 3, 1}, 
  {"XGA", 1024, 768, 5, 3, 1}, 
  {"SXGA", 1280, 1024, 5, 3, 1}, 
  {"UXGA", 1600, 1200, 5, 3, 1}  
};
extern uint8_t fsizePtr; // index to frameData[] for record
uint8_t ftypePtr; // index to frameData[] for ftp
uint8_t frameDataRows = 11;
static uint16_t frameInterval; // units of 0.1ms between frames

// SD card storage
#define ONEMEG (1024*1024)
#define MAX_JPEG ONEMEG/2 // UXGA jpeg frame buffer at highest quality 375kB rounded up
#define MJPEGEXT "mjpeg"
uint8_t* SDbuffer; // has to be dynamically allocated due to size
uint8_t iSDbuffer[RAMSIZE];
char* htmlBuff;
static size_t htmlBuffLen = 20000; // set big enough to hold all file names in a folder
static size_t highPoint;
static File mjpegFile;
static char mjpegName[100];
char dayFolder[50];
static const uint8_t zeroBuf[4] = {0x00, 0x00, 0x00, 0x00}; // 0000
bool stopCheck = false;

// SD playback
static File playbackFile;
static char partName[100];
static char optionHtml[200]; // used to build SD page html buffer
static size_t readLen;
static const char* needle = _STREAM_BOUNDARY;
static bool firstCallPlay = true;
static uint8_t recFPS;
static uint32_t recDuration;
static uint8_t saveFPS = 99;
bool doPlayback = false;

// task control
static TaskHandle_t captureHandle = NULL;
static TaskHandle_t playbackHandle = NULL;
#if USE_DS18B20
extern TaskHandle_t getDS18tempHandle;
#endif
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t playbackSemaphore;
SemaphoreHandle_t frameMutex;
SemaphoreHandle_t motionMutex;
static volatile bool isPlaying = false;
bool isCapturing = false;
uint8_t PIRpin;
const uint8_t LAMPpin = 4;
bool stopPlayback = false;
static camera_fb_t* fb;

bool isNight(uint8_t nightSwitch);
bool checkMotion(camera_fb_t* fb, bool captureStatus);
void stopPlaying();
void readSD();
void prepSound();
void startAudio();
void finishAudio(const char* mjpegName, bool isvalid);
bool useMicrophone();
String getOldestDir();
void deleteFolderOrFile(const char* val);
void createUploadTask(const char* val, bool move = false);                      

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
  // set timezone as required
  setenv("TZ", timezone, 1);
  if (getEpoch() > 1000) {
    time_t currEpoch = getEpoch();
    char timeFormat[20];
    strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
    timeSynchronized = true;
    showInfo("Got current time from NTP: %s", timeFormat);
  }
  else showError("Unable to sync with NTP");
}

//Synchronize clock to browser clock on AP connection
void syncToBrowser(char *val) {
  if (timeSynchronized) return;

  //Synchronize to browser time, On access point connections with no internet
  Serial.printf("Sync clock to: %s with tz:%s\n", val, timezone);
  struct tm now;
  getLocalTime(&now, 0);

  int Year, Month, Day, Hour, Minute, Second ;
  sscanf(val, "%d-%d-%dT%d:%d:%d", &Year, &Month, &Day, &Hour, &Minute, &Second);

  struct tm t;
  t.tm_year = Year - 1900;
  t.tm_mon  = Month - 1;    // Month, 0 - jan
  t.tm_mday = Day;          // Day of the month
  t.tm_hour = Hour;
  t.tm_min  = Minute;
  t.tm_sec  = Second;

  time_t t_of_day = mktime(&t);
  timeval epoch = {t_of_day, 0};
  struct timezone utc = {0, 0};
  settimeofday(&epoch, &utc);
  //setenv("TZ", timezone, 1);
  Serial.print(&now, "Before sync: %B %d %Y %H:%M:%S (%A) ");
  getLocalTime(&now, 0);
  Serial.println(&now, "After sync: %B %d %Y %H:%M:%S (%A)");
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
  digitalWrite(LAMPpin, lampVal);
}

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
    showDebug("Frame timer interval %ums for FPS %u", frameInterval/10, FPS);
    timerAlarmWrite(timer3, frameInterval, true); // 
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
  showDebug("File opening time: %ums", oTime);
  startAudio();
  // initialisation of counters
  startMjpeg = millis();
  frameCnt = fTimeTot = wTimeTot = dTimeTot = highPoint = vidSize = 0;
}

static inline bool doMonitor(bool capturing) {
  // monitor incoming frames for motion 
  if (stopCheck) return false; // no checks during FTP upload
  else {
    static uint8_t motionCnt = 0;
    // ratio for monitoring stop during capture / movement prior to capture
    uint8_t checkRate = (capturing) ? FPS*MOVE_STOP_SECS : FPS/MOVE_START_CHECKS;
    if (!checkRate) checkRate = 1;
    if (++motionCnt/checkRate) motionCnt = 0; // time to check for motion
    return !(bool)motionCnt;
  }
}  

static inline void freeFrame() {
  // free frame buffer and give semaphore to allow camera and any streaming to continue
  if (fb) esp_camera_fb_return(fb);
  fb = NULL;
  xSemaphoreGive(frameMutex);
  delay(1);
}

static void saveFrame() {
  // build frame boundary for jpeg 
  uint32_t fTime = millis();
  // add boundary to buffer
  memcpy(SDbuffer+highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
  highPoint += streamBoundaryLen;
  size_t jpegSize = fb->len; 
  uint16_t filler = (4 - (jpegSize & 0x00000003)) & 0x00000003; // align end of jpeg on 4 byte boundary for subsequent AVI
  jpegSize += filler;
  size_t streamPartLen = snprintf((char*)part_buf, PART_BUF_LEN-1, _STREAM_PART, jpegSize);
  memcpy(SDbuffer+highPoint, part_buf, streamPartLen); // marker at start of each mjpeg frame
  highPoint += streamPartLen;
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
  vidSize += streamPartLen + jpegSize + filler + streamBoundaryLen;
  showDebug("SD storage time %u ms", wTime);
  frameCnt++;
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  showDebug("Frame processing time %u ms", fTime);
}

bool checkFreeSpace() { //Check for suficcient space in card
  if (freeSpaceMode < 1) return false;
  unsigned long freeSize = (unsigned long)( (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / 1048576);
  Serial.print("Card free space: "); Serial.println(freeSize);
  if (freeSize < minCardFreeSpace) {
    String oldestDir = getOldestDir();
    Serial.print("Oldest dir to delete: "); Serial.println(oldestDir);
    if (freeSpaceMode == 1) { //Delete oldest folder
      deleteFolderOrFile(oldestDir.c_str());
    } else if (freeSpaceMode == 2) { //Upload and then delete oldest folder
      createUploadTask(oldestDir.c_str(), true);
    }
    return true;
  }
  return false;
}                                                            

static bool closeMjpeg() {
  // closes and renames the file
  uint32_t captureTime = frameCnt / FPS;
  if (captureTime > minSeconds) {
    cTime = millis();
    // add final boundary to buffer
    memcpy(SDbuffer + highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
    highPoint += streamBoundaryLen;
    // write remaining frame content to SD
    mjpegFile.write(SDbuffer, highPoint); 
    showDebug("Final SD storage time %lu ms", millis() - cTime); 

    // finalise file on SD
    uint32_t hTime = millis();
    vidDuration = millis() - startMjpeg;
    float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
    mjpegFile.close();
    // rename file to include actual FPS, duration, and frame count, plus file extension
    snprintf(mjpegName, sizeof(mjpegName)-1, "%s_%s_%lu_%lu_%u.%s", 
      partName, frameData[fsizePtr].frameSizeStr, lround(actualFPS), lround(vidDuration/1000.0), frameCnt, MJPEGEXT);
    SD_MMC.rename(partName, mjpegName);
    finishAudio(mjpegName, true);
    showDebug("MJPEG close/rename time %lu ms", millis() - hTime); 
    cTime = millis() - cTime;
    insufficient = 0;

    // MJPEG stats
    showInfo("\n******** MJPEG recording stats ********");
    showInfo("Recorded %s", mjpegName);
    showInfo("MJPEG duration: %0.1f secs", (float)vidDuration / 1000.0);
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
    showInfo("Busy: %u%%", std::min(100 * (wTimeTot + fTimeTot + dTimeTot + oTime + cTime) / vidDuration, (uint32_t)100));
    showInfo("Free heap: %u, free pSRAM %u", ESP.getFreeHeap(), ESP.getFreePsram());
    showInfo("*************************************\n");
    checkFreeSpace();                     
    return true;
  } else {
    // delete too small files if exist
    mjpegFile.close();
    SD_MMC.remove(partName);
    finishAudio(partName, false);
    showDebug("Insufficient capture duration: %u secs", captureTime);
    insufficient++;
    checkFreeSpace();                     
    return false;
  }
}

static boolean processFrame() {
  // get camera frame
  static bool wasCapturing = false;
  static bool captureMotion = false;
  bool capturePIR = false;
  bool res = true;
  uint32_t dTime = millis();
  bool finishRecording = false;

  xSemaphoreTake(frameMutex, portMAX_DELAY);
  fb = esp_camera_fb_get();
  if (fb) {
    // determine if time to monitor, then get motion capture status
    if (USE_MOTION) {
      if (debugMotion) checkMotion(fb, false); // check each frame for debug
      else if (doMonitor(isCapturing)) captureMotion = checkMotion(fb, isCapturing); // check 1 in N frames
      nightTime = isNight(nightSwitch);
      if (nightTime) {
        // dont record if night time as image shift is spurious
        captureMotion = false;
        if (isCapturing) finishRecording = true;
      }
    }
    if (USE_PIR) capturePIR = digitalRead(PIRpin);
    // either active PIR or Motion will start capture, neither active will stop capture
    isCapturing = captureMotion | capturePIR;
    if (doRecording) {
      if (isCapturing && !wasCapturing) {
        // movement has occurred, start recording, and switch on lamp if night time
        stopPlaying(); // terminate any playback
        stopPlayback  = true; // stop any subsequent playback
        showDebug("Capture started by %s%s", captureMotion ? "Motion " : "", capturePIR ? "PIR" : "");
        openMjpeg();
        wasCapturing = true;
      }
      if (isCapturing && wasCapturing) {
        // capture is ongoing
        dTimeTot += millis() - dTime;
        saveFrame();
        showProgress();
        if (frameCnt >= MAX_FRAMES) {
          showInfo("Auto closed recording after %u frames", MAX_FRAMES);
          finishRecording = true;
        }
      }
      freeFrame();
      if (!isCapturing && wasCapturing) {
        // movement stopped
        finishRecording = true;
      }
      wasCapturing = isCapturing;

    }
    showDebug("============================");
  } else {
    showError("Failed to get frame");
    res = false;
  }
  freeFrame();
  if (finishRecording) {
    // cleanly finish recording (normal or forced)
    if (stopPlayback) {
      closeMjpeg();
      controlLamp(false);
    }
    finishRecording = isCapturing = wasCapturing = stopPlayback = false; // allow for playbacks
  }
  return res;
}

static void captureTask(void* parameter) {
  // woken by frame timer when time to capture frame
  uint32_t ulNotifiedValue;
  while (true) {
    ulNotifiedValue = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
    if (i == 5 && strcmp(token, MJPEGEXT) != 0) _frameCnt = atoi(token);
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

void openSDfile() {
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
    readSD(); // prime playback task
  }
}
String getOldestDir() {
  String oldName = "";
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    std::string str(file.name());
    if ( file.isDirectory() && str.find("/System") == std::string::npos ) { // ignore Sys Vol Info
      //Serial.print("D: "); Serial.println(file.name());
      String Name = file.name();
      if (oldName == "" || oldName > Name) oldName = Name;
    }
    file = root.openNextFile();
  }
  return oldName;
}

String getOldestDir() {
  String oldName = "";
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  while (file) {
    std::string str(file.name());
    if ( file.isDirectory() && str.find("/System") == std::string::npos ) { // ignore Sys Vol Info
      //Serial.print("D: "); Serial.println(file.name());
      String Name = file.name();
      if (oldName == "" || oldName > Name) oldName = Name;
    }
    file = root.openNextFile();
  }
  return oldName;
}

void listDir(const char* fname, char* htmlBuff) {
  // either list day folders in root, or files in a day folder
  std::string decodedName(fname);
  // need to decode encoded slashes
  decodedName = std::regex_replace(decodedName, std::regex("%2F"), "/");
  // check if folder or file
  bool noEntries = true;
  strcpy(htmlBuff, "{");
  if (decodedName.find(MJPEGEXT) != std::string::npos) {
    // mjpeg file selected
    strcpy(mjpegName, decodedName.c_str());
    doPlayback = true; // browser control
    noEntries = true;
    strcpy(htmlBuff, "{}");
  } else {
    strcpy(dayFolder, decodedName.c_str());
    bool returnDirs = (decodedName.compare("/")) ? false : true;

    // open relevant folder to list contents
    File root = SD_MMC.open(decodedName.c_str());
    if (!root) showError("Failed to open directory %s", decodedName.c_str());
    if (!root.isDirectory()) showError("Not a directory %s", decodedName.c_str());
    showDebug("Retrieving %s in %s", returnDirs ? "folders" : "files", decodedName.c_str());

    // build relevant option list
    if (returnDirs) strcpy(htmlBuff, "{");
    else strcpy(htmlBuff, " {\"/\":\".. [ Up ]\",");
    File file = root.openNextFile();
    while (file) {
      if (file.isDirectory() && returnDirs) {
        // build folder names into HTML response
        std::string str(file.name());
        if (str.find("/System") == std::string::npos) { // ignore Sys Vol Info
          sprintf(optionHtml, "\"%s\":\"%s\",", file.name(), file.name());
          if (strlen(htmlBuff) + strlen(optionHtml) < htmlBuffLen) strcat(htmlBuff, optionHtml);
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
        if (str.find(MJPEGEXT) != std::string::npos) {
          sprintf(optionHtml, "\"%s\":\"%s %0.1fMB\",", file.name(), file.name(), (float)file.size() / ONEMEG);
          if (strlen(htmlBuff) + strlen(optionHtml) < htmlBuffLen) strcat(htmlBuff, optionHtml);
          else {
            showError("Too many files to list %u+%u in %u bytes", strlen(htmlBuff), strlen(optionHtml), htmlBuffLen);
            break;
          }
          noEntries = false;
        }
      }
      file = root.openNextFile();
    }
  }
  if (noEntries) strcat(htmlBuff, "\"/\":\"Get Folders\"}");
  else htmlBuff[strlen(htmlBuff) - 1] = '}'; // lose trailing comma
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
      if (n == nSize) return h; // position of end of needle
    } else {
      // if not, increment h and reset n
      h = h - n + 1;
      n = 0;
    }
  }
  return 0; // not found
}

void readSD() {
  // read next cluster from SD for playback
  uint32_t rTime = millis();
  readLen = 0;
  if (!stopPlayback) {
    // read to interim dram before copying to psram
    readLen = playbackFile.read(iSDbuffer, RAMSIZE);
    memcpy(SDbuffer+RAMSIZE*2, iSDbuffer, RAMSIZE);
  }
  showDebug("SD read time %lu ms", millis() - rTime);
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
  
  showDebug("http send time %lu ms", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (buffLen && !stopPlayback) {
    if (!remaining) {
      mTime = millis();
      xSemaphoreTake(readSemaphore, portMAX_DELAY); // wait for read from SD card completed
      showDebug("SD wait time %lu ms", millis()-mTime);
      wTimeTot += millis()-mTime;
      mTime = millis();                                 
      memcpy(SDbuffer, SDbuffer+RAMSIZE*2, readLen); // load new cluster from double buffer
      showDebug("memcpy took %lu ms for %u bytes", millis()-mTime, readLen);
      buffLen = readLen;
      fTimeTot += millis() - mTime;
      remaining = true;
      xTaskNotifyGive(playbackHandle); // wake up task to get next cluster - sets readLen
    }
    mTime = millis();
    if (streamOffset + skipOver > buffLen) {
      showError("streamOffset %u + skipOver %u > buffLen %u", streamOffset, skipOver, buffLen);
      stopPlayback = true;

    } else {
      // search for next boundary in buffer
      boundary = isSubArray(SDbuffer + streamOffset + skipOver, (uint8_t*)needle, buffLen - streamOffset - skipOver, streamBoundaryLen);
      skipOver = 0;
      showDebug("frame search time %lu ms", millis()-mTime);
      fTimeTot += millis()-mTime;
      if (boundary) {
        // found image boundary
        mTime = millis();
        // wait on playbackSemaphore for rate control
        xSemaphoreTake(playbackSemaphore, portMAX_DELAY);
        showDebug("frame timer wait %lu ms", millis()-mTime);
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
      uint32_t playDuration = (millis() - sTime) / 1000;
      uint32_t totBusy = wTimeTot + fTimeTot + hTimeTot;
      showInfo("\n******** MJPEG playback stats ********");
      showInfo("Playback %s", mjpegName);
      showInfo("Recorded FPS %u, duration %u secs", recFPS, recDuration);
      showInfo("Playback FPS %0.1f, duration %u secs", (float)frameCnt / playDuration, playDuration);
      showInfo("Number of frames: %u", frameCnt);
      showInfo("Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
      if (frameCnt) {
        showInfo("Average frame SD read time: %u ms", wTimeTot / frameCnt);
        showInfo("Average frame processing time: %u ms", fTimeTot / frameCnt);
        showInfo("Average frame delay time: %u ms", tTimeTot / frameCnt);
        showInfo("Average http send time: %u ms", hTimeTot / frameCnt);
        showInfo("Busy: %u%%", std::min(100 * totBusy / (totBusy + tTimeTot), (uint32_t)100));
      }
      showInfo("Free heap: %u, free pSRAM %u", ESP.getFreeHeap(), ESP.getFreePsram());
      showInfo("*************************************\n");
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
      Serial.println();
      showInfo("Failed to cleanly close playback");
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
bool sdPrepared = false;
bool prepSD_MMC() {
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
  if (ONELINE) {
    // SD_MMC 1 line mode
    res = SD_MMC.begin("/sdcard", true);
  } else res = SD_MMC.begin(); // SD_MMC 4 line mode
  if (res) {
    showInfo("SD ready in %s mode ", ONELINE ? "1-line" : "4-line");
    infoSD();
    sdPrepared = true;
    return true;
  } else {
    showError("SD mount failed for %s mode", ONELINE ? "1-line" : "4-line");
    sdPrepared = false;
    return false;
  }
}

void deleteFolderOrFile(const char * val) {
  // Function provided by user @gemi254
  showInfo("Deleting : %s", val);
  File f = SD_MMC.open(val);
  if (!f) {
    showError("Failed to open %s", val);
    return;
  }
  stopPlayback = true;
  //Empty directory first
  if (f.isDirectory()) {
    showInfo("Directory %s contents", val);
    File file = f.openNextFile();
    while (file) {
      if (file.isDirectory()) {
        Serial.print("  DIR : ");
        Serial.println(file.name());
      } else {
        Serial.print("  FILE: ");
        Serial.print(file.name());
        Serial.print("  SIZE: ");
        Serial.print(file.size());
        if (SD_MMC.remove(file.name())) {
          Serial.println(" deleted.");
        } else {
          Serial.println(" FAILED.");
        }
      }
      file = f.openNextFile();
    }
    f.close();
    //Remove the dir
    if (SD_MMC.rmdir(val)) {
      showInfo("Dir %s removed", val);
    } else {
      Serial.println("Remove dir failed");
    }

  } else {
    //Remove the file
    if (SD_MMC.remove(val)) {
      showInfo("File %s deleted", val);
    } else {
      Serial.println("Delete failed");
    }
  }
}

/******************* Startup ********************/

bool prepMjpeg() {
  // initialisation & prep for MJPEG capture
  if (psramFound()) {
    if (sdPrepared) {
      if (ONELINE) controlLamp(false); // set lamp fully off as sd_mmc library still initialises pin 4
      getLocalNTP(); // get time from NTP
      SDbuffer = (uint8_t*)ps_malloc(MAX_JPEG); // buffer frame to store in SD
      htmlBuff = (char*)ps_malloc(htmlBuffLen);
      if (USE_PIR) {
        PIRpin = (ONELINE) ? 12 : 33;
        pinMode(PIRpin, INPUT_PULLDOWN); // pulled high for active
      }
      pinMode(LAMPpin, OUTPUT);
      readSemaphore = xSemaphoreCreateBinary();
      playbackSemaphore = xSemaphoreCreateBinary();
      frameMutex = xSemaphoreCreateMutex();
      motionMutex = xSemaphoreCreateMutex();
      if (!esp_camera_fb_get()) return false; // test & prime camera
      showInfo("Free DRAM: %u, free pSRAM %u", ESP.getFreeHeap(), ESP.getFreePsram());
      showInfo("Sound recording is %s", useMicrophone() ? "On" : "Off");
      showInfo("\nTo record new MJPEG, do one of:");
      if (USE_PIR) showInfo("- attach PIR to pin %u", PIRpin);
      if (USE_PIR) showInfo("- raise pin %u to 3.3V", PIRpin);
      if (USE_MOTION) showInfo("- move in front of camera");
      showInfo(" ");
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

void startSDtasks() {
  // tasks to manage SD card operation
  xTaskCreate(&captureTask, "captureTask", 4096, NULL, 5, &captureHandle);
  if (xTaskCreate(&playbackTask, "playbackTask", 4096, NULL, 4, &playbackHandle) != pdPASS)
    showError("Insufficient memory to create playbackTask");
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
#if USE_DS18B20
  deleteTask(getDS18tempHandle);
#endif
}

void OTAprereq() {
  // stop timer isrs, and free up heap space, or crashes esp32
  esp_camera_deinit();
  controlFrameTimer(false);
  endTasks();
  delay(100);
}

# ifndef USE_MOTION
bool fetchMoveMap(uint8_t **out, size_t *out_len) {
  // dummy if motionDetect.cpp not used
  *out_len = 0;
  xSemaphoreGive(motionMutex);
}
#endif

String upTime() {
  String ret = "";
  long days = 0;
  long hours = 0;
  long mins = 0;
  long secs = 0;
  secs = millis() / 1000; //convect milliseconds to seconds
  mins = secs / 60; //convert seconds to minutes
  hours = mins / 60; //convert minutes to hours
  days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the coverted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the coverted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the coverted hours to days in order to display 23 hours max
  if (days > 0) ret += String(days) + "d ";
  if (hours > 0) ret += String(hours) + "h ";
  if (mins >= 0) ret += String(mins) + "m ";
  if (secs >= 0) ret += String(secs) + "s ";
  return ret;
}
