/*
* Extension to capture ESP32 Cam JPEG images into a MJPEG file and store on SD
* Writing to the SD card is the bottleneck so this library
* minimises the file writes and matches them to the SD card cluster size.
* MJPEG files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020
*/

// user definable environmental setup
#define CLUSTERSIZE 32768 // set this to match the SD card cluster size
#define MAX_FRAMES 20000 // maximum number of frames before auto close
#define ONELINE true // MMC 1 line mode
#define TIMEZONE "GMT0BST,M3.5.0/01,M10.5.0/02" // set to local timezone 

#include "arduino.h"
#include <SD_MMC.h>
#include <regex>
#include <sys/time.h> 
#include "time.h"
#include "esp_camera.h"

// user parameters
uint8_t FPS;
uint8_t minFrames = 10;
bool debug = false;

// stream separator
extern const char* _STREAM_BOUNDARY; 
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
static char* part_buf[64];

// header and reporting info
static uint32_t vidSize; // total video size
static uint16_t frameCnt;
static uint32_t startMjpeg; // total overall time
static uint32_t fTimeTot; // total frame processing time
static uint32_t wTimeTot; // total SD write time
static uint32_t oTime; // file opening time
static uint32_t cTime; // file closing time 
static uint32_t sTime; // file streaming time
static uint32_t vidDuration; // duration in secs of recorded file

struct frameSizeStruct {
  const char* frameSizeStr;
  const uint16_t frameWidth;
  const uint16_t frameHeight;
  const uint16_t defaultFPS;
};
// indexed by frame size - needs to be consistent with sensor.h enum
static const frameSizeStruct frameSizeData[] = {
  {"QQVGA", 160, 120, 25},
  {"n/a", 0, 0, 0}, 
  {"n/a", 0, 0, 0}, 
  {"HQVGA", 240, 176, 25}, 
  {"QVGA", 320, 240, 25}, 
  {"CIF", 400, 296, 25},
  {"VGA", 640, 480, 15}, 
  {"SVGA", 800, 600, 10}, 
  {"XGA", 1024, 768, 5}, 
  {"SXGA", 1280, 1024, 3}, 
  {"UXGA", 1600, 1200, 2}  
};
static uint8_t fsizePtr; // index to frameSizeData[]
static uint16_t frameInterval; // units of 0.1ms between frames

// SD card storage
#define ONEMEG (1024*1024)
#define MAX_JPEG ONEMEG/2 // UXGA jpeg frame buffer at highest quality 375kB rounded up
uint8_t* SDbuffer; // has to be dynamically allocated due to size
static size_t highPoint;
static File mjpegFile;
static char mjpegName[100];

// SD streaming
static File streamFile;
static char partName[90];
static char optionHtml[200]; // used to build SD page html buffer
static size_t readLen;
static const char* needle = _STREAM_BOUNDARY;
static uint8_t recFPS;
static bool firstCall = true;
uint8_t saveFPS;

// task control
static SemaphoreHandle_t mjpegSemaphore;
static SemaphoreHandle_t readSemaphore;
static SemaphoreHandle_t streamSemaphore;
SemaphoreHandle_t frameMutex;
volatile bool isStreaming = false;
bool stopPlayback = false;
static volatile uint8_t mjpegStatus = 9; // invalid
static volatile uint8_t PIRpin;

#define LAMP_PIN 4

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

void getNewFPS(uint8_t frameSize, char* htmlBuff) {
  saveFPS = FPS = frameSizeData[frameSize].defaultFPS;
  sprintf(htmlBuff, "{\"fps\":\"%u\"}", FPS);
}

void controlLamp(uint8_t lampVal) {
  // switch lamp on / off
  pinMode(LAMP_PIN, OUTPUT);
  digitalWrite(LAMP_PIN, lampVal);
}

/**************** timers & ISRs ************************/

static void IRAM_ATTR frameISR() {
  // time to get frame
  if (isStreaming) xSemaphoreGive(streamSemaphore);
  else xSemaphoreGive(mjpegSemaphore);
}

static void IRAM_ATTR PIR_ISR(void* arg) {
  // PIR activated - normally high (using internal pullup)
  // indicates motion by pulling to gnd
  mjpegStatus = !digitalRead(PIRpin);
  xSemaphoreGive(mjpegSemaphore);
}

static void setupPIRinterrupt() {
  // do this way due to camera use of interrupts
  pinMode(PIRpin, INPUT_PULLUP); // pulled high for inactive
  showInfo("To record new MJPEG, attach PIR to pin %u or simulate by grounding pin %u", PIRpin, PIRpin);
  esp_err_t err = gpio_isr_handler_add((gpio_num_t)PIRpin, &PIR_ISR, NULL);
  if (err != ESP_OK) showError("gpio_isr_handler_add failed (%x)", err);
  gpio_set_intr_type((gpio_num_t)PIRpin, GPIO_INTR_ANYEDGE);
}

static void controlFrameTimer(bool timerOn) {
  // frame timer control, timer3 so dont conflict wih cam
  static hw_timer_t* timer3 = NULL;
  if (timerOn) {
    // start timer 3 interrupt per required framerate
    timer3 = timerBegin(3, 8000, true); // 0.1ms tick
    frameInterval = (FPS) ? 10000 / FPS : 200; // in units of 0.1ms 
    showDebug("Frame timer interval %ums for FPS %u", frameInterval/10, FPS);
    timerAlarmWrite(timer3, frameInterval, true); // 
    timerAlarmEnable(timer3);
    timerAttachInterrupt(timer3, &frameISR, true);
  } else {
    // off
    if (timer3) timerEnd(timer3);
    timer3 = NULL;
  }
}

/**************** MJPEG processing ************************/

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
    frameSizeData[fsizePtr].frameSizeStr, frameSizeData[fsizePtr].frameWidth, frameSizeData[fsizePtr].frameHeight, FPS);
  oTime = millis() - oTime;
  showDebug("File opening time: %ums", oTime);
  // initialisation of counters
  startMjpeg = millis();
  frameCnt =  fTimeTot = wTimeTot = 0;
  memcpy(SDbuffer, " ", 1); // for VLC
  highPoint = vidSize = 1;
} 

static void processFrame() {
  // build frame boundary for jpeg then write to SD when cluster size reached
  uint32_t fTime = millis();
  // add boundary to buffer
  memcpy(SDbuffer+highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
  highPoint += streamBoundaryLen;
  // get jpeg from camera 
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  camera_fb_t * fb = esp_camera_fb_get();
  size_t jpegSize = fb->len; 
  size_t streamPartLen = snprintf((char*)part_buf, 63, _STREAM_PART, jpegSize);
  memcpy(SDbuffer+highPoint, part_buf, streamPartLen); // marker at start of each mjpeg frame
  highPoint += streamPartLen;
  // add jpeg to buffer
  memcpy(SDbuffer+highPoint, fb->buf, jpegSize);
  highPoint += jpegSize;
  esp_camera_fb_return(fb);
  xSemaphoreGive(frameMutex);
    
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
  showDebug("SD storage time %u ms", wTime);
  
  frameCnt++;
  vidSize += streamPartLen+jpegSize+streamBoundaryLen;
  mjpegStatus = (digitalRead(PIRpin)) ? 0 : 2; // in case PIR off missed
  if (frameCnt >= MAX_FRAMES) {
    showInfo("Auto closed recording after %u frames", MAX_FRAMES);
    mjpegStatus = 0; // auto close on limit
  }
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  showDebug("Frame processing time %u ms", fTime);
}

static bool closeMjpeg() {
  // closes and renames the file
  if (frameCnt > minFrames) { 
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
      partName, frameSizeData[fsizePtr].frameSizeStr, lround(actualFPS), lround(vidDuration/1000));
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
      showInfo("Average frame process time: %u ms", fTimeTot / frameCnt);
      showInfo("Average frame storage time: %u ms", wTimeTot / frameCnt);
    }
    showInfo("Average SD write speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    showInfo("File open / completion times: %u ms / %u ms", oTime, cTime);
    showInfo("Busy: %u%%", std::min(100 * (wTimeTot+fTimeTot+oTime+cTime) / vidDuration, (uint32_t)100));
    showInfo("Free heap %u bytes, largest free block %u bytes", xPortGetFreeHeapSize(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    showInfo("***************************\n");
    return true;
  } else {
    // delete too small file
    mjpegFile.close();
    SD_MMC.remove(partName);
    showError("Insufficient frames captured: %u", frameCnt);
    return false;
  }
}  

static void mjpegControllerTask(void* parameter) {
  // interrupt driven mjpeg control loop
  static bool isCapturing = false;
  while (true) {
    // wait on semaphore
    xSemaphoreTake(mjpegSemaphore, portMAX_DELAY);
    // semaphore given
    switch(mjpegStatus) {
      case 0:
        // stop capture and finalise mjpeg
        if (isCapturing) {
          // only close if capturing
          controlFrameTimer(false); // stop frame timer
          closeMjpeg();
          delay(1000); // debounce
          isCapturing = false;
        } 
      break;
      case 1:  
        // record new mjpeg if not already doing so
        if (!isCapturing) {
          if (isStreaming) {
            stopPlayback = true;
            while (stopPlayback) delay(10); // wait for playback to stop
          }
          isCapturing = true;
          // determine frame interval from selected frame size
          sensor_t * s = esp_camera_sensor_get();
          fsizePtr = s->status.framesize;
          openMjpeg();  
          processFrame(); // get first frame 
          controlFrameTimer(true); // start frame timer
        } 
      break;
      case 2:
        // write next frame to SD, if capturing
        if (isCapturing) {
          processFrame();
          showProgress();
        }
      break;
      case 3:
        // read next cluster from SD, if streaming
        if (isStreaming) { 
          uint32_t rTime = millis();      
          readLen = streamFile.read(SDbuffer+CLUSTERSIZE*2, CLUSTERSIZE);
          showDebug("SD read time %u ms", millis() - rTime);
          wTimeTot += millis() - rTime;
          xSemaphoreGive(readSemaphore); // signal that ready
        }
      break;
      default:
        showError("Invalid mjpeg control status %u", mjpegStatus);
      break;
    }
  }
  vTaskDelete(NULL);
}

/********************** streaming ***********************/

static void extractFPS(const char* fname) {
  // extract FPS and duration from filename with assumed format 
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
  FPS = recFPS;
}

static void openSDfile() {
  // open selected file on SD for streaming
  if (isStreaming) {
    // in case this file opened during another playback
    stopPlayback = true; 
    while (stopPlayback) delay(10);
  }
  showInfo("Streaming %s", mjpegName);
  streamFile = SD_MMC.open(mjpegName, FILE_READ);
  vidSize = streamFile.size();
  extractFPS(mjpegName);  
  // load first cluster ready for first request
  mjpegStatus = 3; 
  firstCall = true;
  isStreaming = true;
  xSemaphoreGive(mjpegSemaphore);
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
  }
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
        strcat(htmlBuff, optionHtml);
        noEntries = false;
      }
    }
    if (!file.isDirectory() && !returnDirs) {
      // update existing html with file details
      std::string str(file.name());
      if (str.find(".mjpeg") != std::string::npos) {
        sprintf(optionHtml, "\"%s\":\"%s %0.1fMB\",", file.name(), file.name(), (float)file.size()/ONEMEG);
        strcat(htmlBuff, optionHtml);
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
  if (firstCall) {
    sTime = millis();
    hTime = millis();
    firstCall = false;
    remaining = false;
    frameCnt = boundary = streamOffset = 0;
    wTimeTot = fTimeTot = hTimeTot = tTimeTot = 0;
    skipOver = 200; // skip over first boundary
    buffLen = readLen;
    controlFrameTimer(true); // start frame timer
  }  
  
  showDebug("http send time %u ms", millis() - hTime);
  hTimeTot += millis() - hTime;
  uint32_t mTime = millis();
  if (buffLen && !stopPlayback) {
    if (!remaining) {
      mTime = millis(); 
      xSemaphoreTake(readSemaphore, portMAX_DELAY);
      showDebug("SD wait time %u ms", millis()-mTime);
      wTimeTot += millis()-mTime;
      mTime = millis();                                 
      memcpy(SDbuffer, SDbuffer+CLUSTERSIZE*2, readLen); // load new cluster from double buffer
      showDebug("memcpy took %u ms for %u bytes", millis()-mTime, readLen);
      buffLen = readLen;
      fTimeTot += millis()-mTime;                                 
      remaining = true; 
      xSemaphoreGive(mjpegSemaphore); // signal for next cluster - sets readLen
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
        // delay on streamSemaphore for rate control
        xSemaphoreTake(streamSemaphore, portMAX_DELAY);
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
    if (!remaining) {
      // load next cluster from SD in parallel
      streamOffset = 0;
    }
  } 
  if ((!buffLen && !remaining) || stopPlayback) {     
    // finished, close SD file used for streaming   
    streamFile.close(); 
    controlFrameTimer(false); // stop frame timer
    isStreaming = false;
    imgPtrs[0] = 0; 
    imgPtrs[1] = 0;
    if (stopPlayback) {
     showInfo("Force close playback");
     stopPlayback = false;
    } else {
      uint32_t playDuration = (millis()-sTime)/1000;
      uint32_t totBusy = wTimeTot+fTimeTot+hTimeTot;
      showInfo("\n******** MJPEG playback stats ********");
      showInfo("Playback %s", mjpegName);
      showInfo("Recorded FPS %u, duration %u secs", recFPS, vidDuration);
      showInfo("Required playback FPS %u", FPS);
      showInfo("Actual playback FPS %0.1f, duration %u secs", (float)frameCnt/playDuration, playDuration);
      showInfo("Number of frames: %u", frameCnt);
      if (frameCnt) {
        showInfo("Average frame SD read time: %u ms", wTimeTot / frameCnt);
        showInfo("Average SD read speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
        showInfo("Average frame processing time: %u ms", fTimeTot / frameCnt);
        showInfo("Average frame delay time: %u ms", tTimeTot / frameCnt);
        showInfo("Average http send time: %u ms", hTimeTot / frameCnt);
        showInfo("Busy: %u%%", std::min(100 * totBusy/(totBusy+tTimeTot),(uint32_t)100));
      }
      showInfo("Free heap %u bytes, largest free block %u bytes", xPortGetFreeHeapSize(), heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
      showInfo("***************************\n");
    }
    FPS = saveFPS; // realign with browser
  }
  hTime = millis();
  return imgPtrs;
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
      if (ONELINE) controlLamp(0); // set lamp fully off as sd_mmc library still initialises pin 4
      getLocalNTP(); // get time from NTP
      SDbuffer = (uint8_t*)ps_malloc(MAX_JPEG); // buffer frame to store in SD
      PIRpin = (ONELINE) ? 12 : 33;
      setupPIRinterrupt(); 
      mjpegSemaphore = xSemaphoreCreateBinary();
      readSemaphore = xSemaphoreCreateBinary();
      streamSemaphore = xSemaphoreCreateBinary();
      frameMutex = xSemaphoreCreateMutex();
      esp_camera_fb_get(); // prime camera
      sensor_t * s = esp_camera_sensor_get();
      saveFPS = FPS = frameSizeData[s->status.framesize].defaultFPS; // initial frames per second     
      xTaskCreate(&mjpegControllerTask, "mjpegControllerTask", 4096*4, NULL, 5, NULL);
      showDebug("mjpegControllerTask on core %u, Free heap %u bytes", xPortGetCoreID(), xPortGetFreeHeapSize());
      return true;
    } else return false;
  } else {
    showError("pSRAM must be enabled");
    return false;
  }
}
