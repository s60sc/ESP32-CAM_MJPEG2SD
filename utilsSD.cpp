// General purpose SD card utilities, not specific to this app
//
// s60sc 2021, 2022
//
// contribution from @marekful

#include "globals.h"

// Storage settings
int sdMinCardFreeSpace = 100; // Minimum amount of card free Megabytes before sdFreeSpaceMode action is enabled
int sdFreeSpaceMode = 1; // 0 - No Check, 1 - Delete oldest dir, 2 - Upload to ftp and then delete folder on SD 
bool sdFormatIfMountFailed = false; // Auto format the sd card if mount failed. Set to false to not auto format.

// hold sorted list of filenames/folders names in order of newest first
static std::vector<std::string> fileVec;
static auto currentDir = "/#current";
static auto previousDir = "/#previous";

static bool startSpiffs() {
  if (!SPIFFS.begin(true)) {
    LOG_ERR("SPIFFS not mounted");
    return false;
  } else {    
    // list details of files on SPIFFS
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while(file) {
      LOG_INF("File: %s, size: %u", file.path(), file.size());
      file = root.openNextFile();
    }
    LOG_INF("SPIFFS: Total bytes %d, Used bytes %d", SPIFFS.totalBytes(), SPIFFS.usedBytes());
    LOG_INF("Sketch size %d kB", ESP.getSketchSize() / 1024);
    return true;
  }
}

static void infoSD() {
#ifdef INCLUDE_SD
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) LOG_WRN("No SD card attached");
  else {
    char typeStr[8] = "UNKNOWN";
    if (cardType == CARD_MMC) strcpy(typeStr, "MMC");
    else if (cardType == CARD_SD) strcpy(typeStr, "SDSC");
    else if (cardType == CARD_SDHC) strcpy(typeStr, "SDHC");

    uint64_t cardSize, totBytes, useBytes = 0;
    cardSize = SD_MMC.cardSize() / ONEMEG;
    totBytes = SD_MMC.totalBytes() / ONEMEG;
    useBytes = SD_MMC.usedBytes() / ONEMEG;
    LOG_INF("SD card type %s, Size: %lluMB, Used space: %lluMB, of total: %lluMB",
             typeStr, cardSize, useBytes, totBytes);
  }
#endif
}

static bool prepSD_MMC() {
  /* open SD card in MMC 1 bit mode
     MMC4  MMC1  ESP32
      D2          12
      D3    CS    13
      CMD   MOSI  15
      CLK   SCK   14
      D0    MISO  2
      D1          4
  */
  bool res = false;
#ifdef INCLUDE_SD
  if (psramFound()) heap_caps_malloc_extmem_enable(5); // small number to force vector into psram
  fileVec.reserve(1000);
  if (psramFound()) heap_caps_malloc_extmem_enable(4096);
  
  res = SD_MMC.begin("/sdcard", true, sdFormatIfMountFailed);
  if (res) {
    SD_MMC.mkdir(DATA_DIR);
    infoSD();
    return true;
  } else {
    LOG_ERR("SD card mount failed");
    return false;
  }
#endif
  return res;
}

bool startStorage() {
  // start required storage device
  bool res = true;
  if ((fs::SPIFFSFS*)&STORAGE == &SPIFFS) {
    res = startSpiffs();
    if (!res) LOG_ERR("Failed to start SPIFFS");  
#ifdef INCLUDE_SD  
  } else {
    res = prepSD_MMC();
    if (!res) {
      LOG_WRN("Insert SD card, will restart after 10 secs");    
      delay(10000);
      ESP.restart();
    }
#endif
  }
  return res;
}

void getOldestDir(char* oldestDir) {
  // get oldest folder by its date name
  File root = STORAGE.open("/");
  File file = root.openNextFile();
  if (file) strcpy(oldestDir, file.path()); // initialise oldestDir
  while (file) {
    if (file.isDirectory() && strstr(file.name(), "System") == NULL // ignore Sys Vol Info
        && strstr(DATA_DIR, file.name()) == NULL) { // ignore data folder
      if (strcmp(oldestDir, file.path()) > 0) {
      strcpy(oldestDir, file.path()); 
      }
    }
    file = root.openNextFile();
  }
}

void inline getFileDate(File file, char* fileDate) {
  // get creation date of file as string
  time_t writeTime = file.getLastWrite();
  struct tm lt;
  localtime_r(&writeTime, &lt);
  strftime(fileDate, sizeof(fileDate), "%Y-%m-%d %H:%M:%S", &lt);
}

bool checkFreeSpace() { 
  // Check for sufficient space on SD card
  if (sdFreeSpaceMode < 1) return false;
  size_t freeSize = (size_t)((STORAGE.totalBytes() - STORAGE.usedBytes()) / ONEMEG);
  LOG_INF("Card free space: %uMB", freeSize);
  if (freeSize < sdMinCardFreeSpace) {
    char oldestDir[FILE_NAME_LEN];
    getOldestDir(oldestDir);
    LOG_WRN("Deleting oldest folder: %s %s", oldestDir, sdFreeSpaceMode == 2 ? "after uploading" : "");
    if (sdFreeSpaceMode == 1) deleteFolderOrFile(oldestDir); // Delete oldest folder
    if (sdFreeSpaceMode == 2) {
#ifdef INCLUDE_FTP 
      ftpFileOrFolder(oldestDir); // Upload and then delete oldest folder
#endif
      deleteFolderOrFile(oldestDir);
    }
    return true;
  }
  return false;
} 

bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension) {
  // either list day folders in root, or files in a day folder
  bool hasExtension = false;
  char partJson[200]; // used to build SD page json buffer
  char partName[FILE_NAME_LEN];
  char fileName[FILE_NAME_LEN];
  bool noEntries = true;
  // set current or previous folder
  if (strchr(fname, '#') != NULL) {
    if (!strcmp(fname, currentDir)) {
      dateFormat(partName, sizeof(partName), true);
      strcpy(fileName, partName);
      LOG_INF("Current directory set to %s", fileName);
    }
    else if (!strcmp(fname, previousDir)) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      struct tm* tm = localtime(&tv.tv_sec);
      tm->tm_mday -= 1;
      time_t prev = mktime(tm);
      strftime(partName, sizeof(partName), "/%Y%m%d", localtime(&prev));
      strcpy(fileName, partName);
      LOG_INF("Previous directory set to %s", fileName);
    }
  } else strcpy(fileName, fname);

  // check if folder or file
  if (strstr(fileName, extension) != NULL) {
    // required file type selected
    hasExtension = true;
    noEntries = true; 
    strcpy(jsonBuff, "{}");     
  } else {
    // ignore leading '/' if not the only character
    bool returnDirs = strlen(fileName) > 1 ? (strchr(fileName+1, '/') == NULL ? false : true) : true; 
    // open relevant folder to list contents
    File root = STORAGE.open(fileName);
    if (!root) LOG_ERR("Failed to open directory %s", fileName);
    if (!root.isDirectory()) LOG_ERR("Not a directory %s", fileName);
    LOG_DBG("Retrieving %s in %s", returnDirs ? "folders" : "files", fileName);
    
    // build relevant option list
    strcpy(jsonBuff, returnDirs ? "{" : "{\"/\":\".. [ Up ]\",");            
    File file = root.openNextFile();
    if (psramFound()) heap_caps_malloc_extmem_enable(5); // small number to force vector into psram
    while (file) {
      if (returnDirs && file.isDirectory() 
          && strstr(file.name(), "System") == NULL // ignore Sys Vol Info
          && strstr(DATA_DIR, file.name()) == NULL) { // ignore data folder
        // build folder list
        sprintf(partJson, "\"%s\":\"%s\",", file.path(), file.name());
        fileVec.push_back(std::string(partJson));
        noEntries = false;
      }
      if (!returnDirs && !file.isDirectory()) {
        // build file list
        if (strstr(file.name(), extension) != NULL) {
          sprintf(partJson, "\"%s\":\"%s %0.1fMB\",", file.path(), file.name(), (float)file.size() / ONEMEG);
          fileVec.push_back(std::string(partJson));
          noEntries = false;
        }
      }
      file = root.openNextFile();
    }
    if (psramFound()) heap_caps_malloc_extmem_enable(4096);
  }
  
  if (noEntries && !hasExtension) strcpy(jsonBuff, "{\"/\":\"List folders\",\"/#current\":\"Go to current (today)\",\"/#previous\":\"Go to previous (yesterday)\"}");
  else {
    // build json string content
    sort(fileVec.begin(), fileVec.end(), std::greater<std::string>());
    for (auto fileInfo : fileVec) {
      if (strlen(jsonBuff) + strlen(fileInfo.c_str()) < jsonBuffLen) strcat(jsonBuff, fileInfo.c_str());
      else {
        LOG_ERR("Too many folders/files to list %u+%u in %u bytes", strlen(jsonBuff), strlen(partJson), jsonBuffLen);
        break;
      }
    }
    jsonBuff[strlen(jsonBuff)-1] = '}'; // lose trailing comma 
  }
  fileVec.clear();
  return hasExtension;
}

void deleteFolderOrFile(const char* deleteThis) {
  // delete supplied file or folder, unless it is a reserved folder
  File df = STORAGE.open(deleteThis);
  if (!df) {
    LOG_ERR("Failed to open %s", deleteThis);
    return;
  }
  if (df.isDirectory() && (strstr(deleteThis, "System") != NULL 
      || strstr("/", deleteThis) != NULL)) {
    df.close();   
    LOG_ERR("Deletion of %s not permitted", deleteThis);
    return;
  }  
  LOG_WRN("Deleting : %s", deleteThis);
  // Empty named folder first
  if (df.isDirectory() || (((fs::SPIFFSFS*)&STORAGE == &SPIFFS) && strstr("/", deleteThis) != NULL)) {
    LOG_INF("Folder %s contents", deleteThis);
    File file = df.openNextFile();
    while (file) {
      if (file.isDirectory()) LOG_INF("  DIR : %s", file.path());
      else LOG_INF("  FILE : %s SIZE : %uMB %sdeleted", file.path(), file.size() / ONEMEG, 
        STORAGE.remove(file.path()) ? "" : "not ");
      file = df.openNextFile();
    }
    // Remove the folder
    if (df.isDirectory()) LOG_INF("Folder %s %sdeleted", deleteThis, STORAGE.rmdir(deleteThis) ? "" : "not ");
    else df.close();
  } else LOG_INF("File %s %sdeleted", deleteThis, STORAGE.remove(deleteThis) ? "" : "not ");  //Remove the file
}
