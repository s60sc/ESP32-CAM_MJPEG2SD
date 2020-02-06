/*
* Extension to encode ESP32 Cam JPEG captures into a MJPEG file and store on SD
* Writing to the SD card is the bottleneck so this library
* minimises the file writes and matches them to the SD card clustersize.
* MJPEG files stored on the SD card can also be selected and streamed to a browser.
*
* s60sc 2020
*/

// user definable environmental setup
#define CLUSTERSIZE 32768 // set this to match the SD card cluster size
#define MAX_FRAMES 20000 // maximum number of frames before auto close
#define TIMEZONE "GMT0BST,M3.5.0/01,M10.5.0/02" // set to local timezone 
#define ONELINE true // MMC 1 line mode

#include "arduino.h"
#include <SD_MMC.h>
#include <sys/time.h> 
#include <WiFi.h>
#include "time.h"
#include <regex>
#include "esp_camera.h"
#include "sdbrowser.h"

// user parameters
static uint8_t FPS;
static uint8_t minFrames = 10;
static bool doDebug = false;

// stream separator
extern const char* _STREAM_BOUNDARY; 
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
static char* part_buf[64];

// header and reporting info
static uint32_t vidSize; // total video size
static uint16_t frameCnt;
static uint32_t startMjpeg;
static uint32_t fTimeTot; // total frame processing time
static uint32_t wTimeTot; // total SD write time
static uint32_t oTime; // file opening time
static uint32_t cTime; // file closing time
static uint32_t hTime; 
static uint32_t sTime; // file streaming time

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
uint8_t* SDbuffer; 
static size_t highPoint;
static File mjpegFile;
static char mjpegName[100];

// SD streaming
static File streamFile;
static char partName[90];
static char optionHtml[200]; // used to build SD page html buffer
static size_t readLen;
#define FRAME_RATE_HTML "<tr><td>Frame Rate</td><td><input type=\"text\" id=\"frameRate\" value=\"%u\"></td></tr>"
#define MIN_FRAME_HTML "<tr><td>Min Frames</td><td><input type=\"text\" id=\"minFrames\" value=\"%u\"></td></tr>"
#define DEBUG_HTML "<tr><td>Debug</td><td><input type=\"checkbox\" id=\"debug\" value=\"0\"%s></td></tr>"
#define OPT_HTML_FOLDER "<option value=\"%s\">%s</option>"
#define OPT_HTML_FILE "<option value=\"%s\">%s, %0.1fMB</option>"

// task control
static SemaphoreHandle_t mjpegSemaphore;
static SemaphoreHandle_t readSemaphore;
static volatile bool fileIsOpen = false;
static volatile bool isStreaming = false;
static volatile uint8_t mjpegStatus = 9; // invalid
static volatile uint8_t PIRpin;

// auto newline printf
#define showInfo(format, ...) Serial.printf(format "\n", ##__VA_ARGS__)
#define showError(format, ...) Serial.printf("ERROR: " format "\n", ##__VA_ARGS__)
#define showDebug(format, ...) if (doDebug) Serial.printf("DEBUG: " format "\n", ##__VA_ARGS__)

/**************** timers & ISRs ************************/

static void IRAM_ATTR frameISR() {
  // time to get frame
  xSemaphoreGive(mjpegSemaphore);
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
    frameInterval = (FPS) ? 10000 / FPS : 200; // in 0.1ms units
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
  struct timeval tv;
  gettimeofday(&tv, NULL);
  strftime(partName, sizeof(partName)-1, "/%Y%m%d", localtime(&tv.tv_sec));
  SD_MMC.mkdir(partName); // make date folder if not present
  strftime(partName, sizeof(partName)-1, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&tv.tv_sec));
  mjpegFile = SD_MMC.open(partName, FILE_WRITE);

  // open mjpeg file with temporary name
  showInfo("Create %s mjpeg, width: %u, height: %u, @ %u fps", 
    frameSizeData[fsizePtr].frameSizeStr, frameSizeData[fsizePtr].frameWidth, frameSizeData[fsizePtr].frameHeight, FPS);
  oTime = millis() - oTime;
  showDebug("File opening time: %ums", oTime);
  // initialisation of counters
  startMjpeg = millis();
  frameCnt =  fTimeTot = wTimeTot = 0;
  highPoint = vidSize = 0;
} 

static void processFrame() {
  // build frame boundary for jpeg then write to SD when cluster size reached
  uint32_t fTime = millis();
  // get jpeg from camera 
  camera_fb_t * fb = esp_camera_fb_get();
  size_t jpegSize = fb->len; 
  size_t streamPartLen = snprintf((char*)part_buf, 63, _STREAM_PART, jpegSize);
  memcpy(SDbuffer+highPoint, part_buf, streamPartLen); // marker at start of each mjpeg frame
  highPoint += streamPartLen;
  // add jpeg to buffer
  memcpy(SDbuffer+highPoint, fb->buf, jpegSize);
  highPoint += jpegSize;
  esp_camera_fb_return(fb);
  // add boundary to buffer
  memcpy(SDbuffer+highPoint, _STREAM_BOUNDARY, streamBoundaryLen);
  highPoint += streamBoundaryLen;
    
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
  if (frameCnt >= MAX_FRAMES) mjpegStatus = 0; // auto close on limit
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  showDebug("Frame processing time %u ms", fTime);
}

static bool closeMjpeg() {
  // closes and renames the file
  if (frameCnt > minFrames) { 
    cTime = millis(); 
    // write remaining frame content to SD
    mjpegFile.write(SDbuffer, highPoint); 
    showDebug("Final SD storage time %u ms", millis() - cTime); 

    // finalise file on SD
    uint32_t hTime = millis();
    uint32_t vidDuration = millis() - startMjpeg;
    float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
    mjpegFile.close();
    // rename file to include actual FPS and duration, plus file extension
    snprintf(mjpegName, sizeof(mjpegName)-1, "%s_%s_%u_%u.mjpeg", 
    partName, frameSizeData[fsizePtr].frameSizeStr, lround(actualFPS), lround(vidDuration/1000));
    SD_MMC.rename(partName, mjpegName);
    showDebug("MJPEG close/rename time %u ms", millis() - hTime); 
    cTime = millis() - cTime;

    // MJPEG stats
    showInfo("\n******** MJPEG stats ********");
    showInfo("Created %s with %s frame size", mjpegName, frameSizeData[fsizePtr].frameSizeStr);
    showInfo("MJPEG duration: %0.1f secs", (float)vidDuration / 1000); 
    showInfo("Number of frames: %u", frameCnt);
    showInfo("Required FPS: %u", FPS);    
    showInfo("Actual FPS: %0.1f", actualFPS);
    showInfo("File size: %0.2f MB", (float)vidSize / ONEMEG);
    showInfo("Average frame length: %u bytes", vidSize / frameCnt);
    showInfo("Average frame process time: %u ms", fTimeTot / frameCnt);
    showInfo("Average frame storage time: %u ms", wTimeTot / frameCnt);
    showInfo("Average write speed: %u kB/s", ((vidSize / wTimeTot) * 1000) / 1024);
    showInfo("File open / completion times: %u ms / %u ms", oTime, cTime);
    showInfo("Busy: %u%%", 100 * (wTimeTot+fTimeTot+oTime+cTime) / vidDuration);
    showDebug("Free heap %u bytes", xPortGetFreeHeapSize());
    showInfo("***************************\n");
    return true;
  } else {
    // delete too small file
    mjpegFile.close();
    SD_MMC.remove(mjpegName);
    showError("Insufficient frames captured: %u", frameCnt);
    return false;
  }
}  

static void closeStream() {
  streamFile.close();
  isStreaming = false;
  showDebug("Force close streaming file");
}

static void mjpegControllerTask(void* parameter) {
  // interrupt driven mjpeg control loop
  static uint8_t dotCnt = 0;
  while (true) {
    // wait on semaphore
    xSemaphoreTake(mjpegSemaphore, portMAX_DELAY);
    // semaphore given
    if (isStreaming && mjpegStatus !=3) closeStream(); // if still open
    switch(mjpegStatus) {
      case 0:
        // stop capture and finalise mjpeg
        if (fileIsOpen) {
          // only close if file open
          controlFrameTimer(false); // stop frame timer
          closeMjpeg();
          delay(1000); // debounce
          fileIsOpen = false;
        } 
      break;
      case 1:  
        // create new mjpeg 
        if (!fileIsOpen) {
          // only open if capture not in progress
          fileIsOpen = true;
          // determine frame interval from selected frame size
          sensor_t * s = esp_camera_sensor_get();
          fsizePtr = s->status.framesize;
          openMjpeg();  
          processFrame(); // get first frame 
          controlFrameTimer(true); // start frame timer
        } 
      break;
      case 2:
        // write next frame to SD, if file open
        if (fileIsOpen) {
          processFrame();
          if (!doDebug) {
            Serial.print("."); // progress marker
            if (++dotCnt >= 50) {
              dotCnt = 0;
              Serial.println("");
            }
          }
        }
      break;
      case 3:
        // read next cluster from SD, if streaming
        if (isStreaming) { 
          uint32_t rTime = millis();      
          readLen = streamFile.read(SDbuffer+CLUSTERSIZE, CLUSTERSIZE);
          showDebug("read from sd time %u ms", millis() - rTime);
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

/******************* SD, NTP & init ********************/

static inline time_t getEpochSecs() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

static void getNTP() {
  // get current time from NTP server and apply to ESP32
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 0;  // offset from GMT
  const int daylightOffset_sec = 3600; // daylight savings offset in secs
  int i = 0;
  do {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    delay(1000);
  } while (getEpochSecs() < 1000 && i++ < 5); // try up to 5 times
  // set TIMEZONE as required
  setenv("TZ", TIMEZONE, 1);
  if (getEpochSecs() > 1000) showInfo("Got current time from NTP");
  else showError("Unable to sync with NTP");
}

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

static bool prepSDcard(bool oneLine) {
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
    // set lamp fully off as sd_mmc library still initialises pin 4
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
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

bool prepMjpeg() {
  // initialisation & prep for MJPEG capture
  if (psramFound()) {
    if (prepSDcard(ONELINE)) { 
      // get time from NTP
      getNTP();
      // setup buffer in heap
      SDbuffer = (uint8_t*)ps_malloc(MAX_JPEG); // buffer frame to store in SD
      PIRpin = (ONELINE) ? 12 : 33;
      setupPIRinterrupt(); 
      mjpegSemaphore = xSemaphoreCreateBinary();
      readSemaphore = xSemaphoreCreateBinary();
      esp_camera_fb_get(); // prime camera
      sensor_t * s = esp_camera_sensor_get();
      FPS = frameSizeData[s->status.framesize].defaultFPS; // initial frames per second
      showInfo("To stream recorded MJPEG, browse using 'http://%s/sd'", WiFi.localIP().toString().c_str());
      xTaskCreate(&mjpegControllerTask, "mjpegControllerTask", 4096*4, NULL, 1, NULL);
      showDebug("mjpegControllerTask on core %u, Free heap %u bytes", xPortGetCoreID(), xPortGetFreeHeapSize());
      return true;
    } else return false;
  } else {
    showError("pSRAM must be enabled");
    return false;
  }
}

/********************** streaming ***********************/

static void populateParams(char* htmlBuff) {
  // populate SD page with current user parameters
  strcpy(htmlBuff, sdBrowserHead);
  sprintf(optionHtml, FRAME_RATE_HTML, FPS);
  strcat(htmlBuff, optionHtml);
  sprintf(optionHtml, MIN_FRAME_HTML, minFrames);
  strcat(htmlBuff, optionHtml);
  sprintf(optionHtml, DEBUG_HTML, doDebug ? " checked" : "");
  strcat(htmlBuff, optionHtml);
  strcat(htmlBuff, sdBrowserMid);
}

void setParams(uint8_t _FPS, uint8_t _minFrames, bool _doDebug) {
  // update user suplied params
  FPS = _FPS;
  minFrames = _minFrames;
  doDebug = _doDebug;
}

bool listDir(const char* dirname, bool wantDirs, char* htmlBuff) {
  // either list day folders (wantDirs = true), or files in a day folder
  std::string decodedDir(dirname); 
  // need to decode encoded slashes
  decodedDir = std::regex_replace(decodedDir, std::regex("%2F"), "/");
  File root = SD_MMC.open(decodedDir.c_str());
  if (!root){
    showError("Failed to open directory %s", decodedDir.c_str());
    return false;
  }
  if (!root.isDirectory()){
    showError("Not a directory %s", decodedDir.c_str());
    return false;
  }
  showDebug("Listing %s in %s", wantDirs ? "folders" : "files", decodedDir.c_str());

  File file = root.openNextFile();
  populateParams(htmlBuff);
  while (file) {
    if (file.isDirectory() && wantDirs) {
      // build folder names into HTML response
      std::string str(file.name());
      if (str.find("/System") == std::string::npos) { // ignore Sys Vol Info
        sprintf(optionHtml, OPT_HTML_FOLDER, file.name(), file.name());
        strcat(htmlBuff, optionHtml);
      }
    }
    if (!file.isDirectory() && !wantDirs) {
      // update existing html with file details
      std::string str(file.name());
      if (str.find(".mjpeg") != std::string::npos) {
        sprintf(optionHtml, OPT_HTML_FILE, file.name(), file.name(), (float)file.size()/ONEMEG);
        strcat(htmlBuff, optionHtml);
      }
    }
    file = root.openNextFile();
  }
  strcat(htmlBuff, sdBrowserTail);
  return true;
}

void openSDfile(const char* thisFile) {
  // open selected file on SD for streaming
  sTime = millis();
  std::string decodedFile(thisFile); 
  decodedFile = std::regex_replace(decodedFile, std::regex("%2F"), "/");
  showDebug("Streaming %s", decodedFile.c_str());
  streamFile = SD_MMC.open(decodedFile.c_str(), FILE_READ);
  // load first cluster ready for first request
  mjpegStatus = 3; 
  isStreaming = true;
  xSemaphoreGive(mjpegSemaphore);
  hTime = millis();
}

size_t getSDcluster() {
  // get next cluster on demand when ready for opened mjpeg
  showDebug("http send time %u ms", millis() - hTime);
  xSemaphoreTake(readSemaphore, portMAX_DELAY);
  if (readLen) {
    uint32_t mTime = millis();
    memcpy(SDbuffer, SDbuffer+CLUSTERSIZE, readLen); // double buffering  
    showDebug("memcpy took %u ms for %u bytes", millis()-mTime, readLen);                                      
    xSemaphoreGive(mjpegSemaphore); // signal for next cluster
  } else {     
    // close SD file used for streaming   
    streamFile.close(); 
    isStreaming = false;
    showInfo("Streaming took %u secs", (millis()-sTime)/1000);
  }
  hTime = millis();
  return readLen;
}
