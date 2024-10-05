// General purpose SD card and flash storage utilities
//
// s60sc 2021, 2022 

#include "appGlobals.h"

// Storage settings
int sdMinCardFreeSpace = 100; // Minimum amount of card free Megabytes before sdFreeSpaceMode action is enabled
int sdFreeSpaceMode = 1; // 0 - No Check, 1 - Delete oldest dir, 2 - Upload oldest dir to FTP/HFS and then delete on SD 
bool formatIfMountFailed = true; // Auto format the file system if mount failed. Set to false to not auto format.
static fs::FS fp = STORAGE;

// hold sorted list of filenames/folders names in order of newest first
static std::vector<std::string> fileVec;
static auto currentDir = "/~current";
static auto previousDir = "/~previous";
static char fsType[10] = {0};

static void infoSD() {
#if !(CONFIG_IDF_TARGET_ESP32C3)
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) LOG_WRN("No SD card attached");
  else {
    char typeStr[8] = "UNKNOWN";
    if (cardType == CARD_MMC) strcpy(typeStr, "MMC");
    else if (cardType == CARD_SD) strcpy(typeStr, "SDSC");
    else if (cardType == CARD_SDHC) strcpy(typeStr, "SDHC");
    LOG_INF("SD card type %s, Size: %s", typeStr, fmtSize(SD_MMC.cardSize()));
  }
#endif
}

static bool prepSD_MMC() {
  bool res = false;
#if !(CONFIG_IDF_TARGET_ESP32C3)
  /* open SD card in MMC 1 bit mode
     MMC4  MMC1  ESP32 ESP32S3
      D2          12
      D3    ..    13
      CMD  CMD    15    38
      CLK  CLK    14    39
      D0   D0     2     40
      D1          4
  */
  if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); // small number to force vector into psram
  fileVec.reserve(1000);
  if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
#if CONFIG_IDF_TARGET_ESP32S3
#if !defined(SD_MMC_CLK)
  LOG_WRN("SD card pins not defined");
  return false;
#else
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
#endif
#endif
  
  res = SD_MMC.begin("/sdcard", true, formatIfMountFailed);
#if defined(CAMERA_MODEL_AI_THINKER)
  pinMode(4, OUTPUT);
  digitalWrite(4, 0); // set lamp pin fully off as sd_mmc library still initialises pin 4 in 1 line mode
#endif 
  if (res) {
    fp.mkdir(DATA_DIR);
    infoSD();
    res = true;
  } else {
    LOG_WRN("SD card mount failed");
    res = false;
  }
#endif
  return res;
}

static void listFolder(const char* rootDir) { 
  // list contents of folder
  LOG_INF("Sketch size %s", fmtSize(ESP.getSketchSize()));    
  File root = fp.open(rootDir);
  File file = root.openNextFile();
  while (file) {
    LOG_INF("File: %s, size: %s", file.path(), fmtSize(file.size()));
    file = root.openNextFile();
  }
  char totalBytes[20];
  strcpy(totalBytes, fmtSize(STORAGE.totalBytes()));
  LOG_INF("%s: %s used of %s", fsType, fmtSize(STORAGE.usedBytes()), totalBytes);
}

bool startStorage() {
  // start required storage device (SD card or flash file system)
  bool res = false;
#if !(CONFIG_IDF_TARGET_ESP32C3)
  if ((fs::SDMMCFS*)&STORAGE == &SD_MMC) {
    strcpy(fsType, "SD_MMC");
    res = prepSD_MMC();
    if (res) listFolder(DATA_DIR);
    else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Check SD card inserted");
    debugMemory("startStorage");
    return res; 
  }
#endif
  // One of SPIFFS or LittleFS
  if (!strlen(fsType)) {
#ifdef _SPIFFS_H_
    if ((fs::SPIFFSFS*)&STORAGE == &SPIFFS) {
      strcpy(fsType, "SPIFFS");
      res = SPIFFS.begin(formatIfMountFailed);
    }
#endif
#ifdef _LITTLEFS_H_
    if ((fs::LittleFSFS*)&STORAGE == &LittleFS) {
      strcpy(fsType, "LittleFS");
      res = LittleFS.begin(formatIfMountFailed);
      // create data folder if not present
      if (res) LittleFS.mkdir(DATA_DIR);
    }
#endif
    if (res) {  
      // list details of files on file system
      const char* rootDir = !strcmp(fsType, "LittleFS") ? DATA_DIR : "/";
      listFolder(rootDir);
    }
  } else {
    snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Failed to mount %s", fsType);  
    dataFilesChecked = true; // disable setupAssist as no file system
  }
  debugMemory("startStorage");
  return res;
}

static void getOldestDir(char* oldestDir) {
  // get oldest folder by its date name
  File root = fp.open("/");
  File file = root.openNextFile();
  if (file) strcpy(oldestDir, file.path()); // initialise oldestDir
  while (file) {
    if (file.isDirectory() && strstr(file.name(), "System") == NULL // ignore Sys Vol Info
        && strstr(DATA_DIR, file.name()) == NULL) { // ignore data folder
      if (strcmp(oldestDir, file.path()) > 0) strcpy(oldestDir, file.path()); 
    }
    file = root.openNextFile();
  }
}

void inline getFileDate(File& file, char* fileDate) {
  // get last write date of file as string
  time_t writeTime = file.getLastWrite();
  struct tm lt;
  localtime_r(&writeTime, &lt);
  strftime(fileDate, sizeof(fileDate), "%Y-%m-%d %H:%M:%S", &lt);
}

bool checkFreeStorage() { 
  // Check for sufficient space on storage
  bool res = false;
  size_t freeSize = (size_t)((STORAGE.totalBytes() - STORAGE.usedBytes()) / ONEMEG);
  if (!sdFreeSpaceMode && freeSize < sdMinCardFreeSpace) 
    LOG_WRN("Space left %uMB is less than minimum %uMB", freeSize, sdMinCardFreeSpace);
  else {
    // delete to make space
    while (freeSize < sdMinCardFreeSpace) {
      char oldestDir[FILE_NAME_LEN];
      getOldestDir(oldestDir);
      LOG_WRN("Deleting oldest folder: %s %s", oldestDir, sdFreeSpaceMode == 2 ? "after uploading" : "");
#if INCLUDE_FTP_HFS
      if (sdFreeSpaceMode == 2) fsStartTransfer(oldestDir); // transfer and then delete oldest folder
#endif
      deleteFolderOrFile(oldestDir);
      freeSize = (size_t)((STORAGE.totalBytes() - STORAGE.usedBytes()) / ONEMEG);
    }
    LOG_INF("Storage free space: %s", fmtSize(STORAGE.totalBytes() - STORAGE.usedBytes()));
    res = true;
  }
  return res;
} 

void setFolderName(const char* fname, char* fileName) {
  // set current or previous folder 
  char partName[FILE_NAME_LEN];
  if (strchr(fname, '~') != NULL) {
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
    } else strcpy(fileName, ""); 
  } else strcpy(fileName, fname);
}

bool listDir(const char* fname, char* jsonBuff, size_t jsonBuffLen, const char* extension) {
  // either list day folders in root, or files in a day folder
  bool hasExtension = false;
  char partJson[200]; // used to build SD page json buffer
  bool noEntries = true;
  char fileName[FILE_NAME_LEN];
  setFolderName(fname, fileName);

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
    File root = fp.open(fileName);
    if (strlen(fileName)) {
      if (!root) LOG_WRN("Failed to open directory %s", fileName);
      else if (!root.isDirectory()) LOG_WRN("Not a directory %s", fileName);
      LOG_VRB("Retrieving %s in %s", returnDirs ? "folders" : "files", fileName);
    }
    
    // build relevant option list
    strcpy(jsonBuff, returnDirs ? "{" : "{\"/\":\".. [ Up ]\",");            
    File file = root.openNextFile();
    if (psramFound()) heap_caps_malloc_extmem_enable(MIN_RAM); // small number to force vector into psram
    while (file) {
      if (returnDirs && file.isDirectory() && strstr(DATA_DIR, file.name()) == NULL) {  
        // build folder list, ignore data folder
        sprintf(partJson, "\"%s\":\"%s\",", file.path(), file.name());
        fileVec.push_back(std::string(partJson));
        noEntries = false;
      }
      if (!returnDirs && !file.isDirectory()) {
        // build file list
        if (strstr(file.name(), extension) != NULL) {
          sprintf(partJson, "\"%s\":\"%s %s\",", file.path(), file.name(), fmtSize(file.size()));
          fileVec.push_back(std::string(partJson));
          noEntries = false;
        }
      }
      file = root.openNextFile();
    }
    if (psramFound()) heap_caps_malloc_extmem_enable(MAX_RAM);
  }
  
  if (noEntries && !hasExtension) sprintf(jsonBuff, "{\"/\":\"List folders\",\"%s\":\"Go to current (today)\",\"%s\":\"Go to previous (yesterday)\"}", currentDir, previousDir);
  else {
    // build json string content
    sort(fileVec.begin(), fileVec.end(), std::greater<std::string>());
    for (auto fileInfo : fileVec) {
      if (strlen(jsonBuff) + strlen(fileInfo.c_str()) < jsonBuffLen) strcat(jsonBuff, fileInfo.c_str());
      else {
        LOG_WRN("Too many folders/files to list %u+%u in %u bytes", strlen(jsonBuff), strlen(partJson), jsonBuffLen);
        break;
      }
    }
    jsonBuff[strlen(jsonBuff)-1] = '}'; // lose trailing comma 
  }
  fileVec.clear();
  return hasExtension;
}

static void deleteOthers(const char* baseFile) {
#ifdef ISCAM
  // delete corresponding csv and srt files if exist
  char otherDeleteName[FILE_NAME_LEN];
  strcpy(otherDeleteName, baseFile);
  changeExtension(otherDeleteName, CSV_EXT);
  if (STORAGE.remove(otherDeleteName)) LOG_INF("File %s deleted", otherDeleteName);
  changeExtension(otherDeleteName, SRT_EXT);
  if (STORAGE.remove(otherDeleteName)) LOG_INF("File %s deleted", otherDeleteName);
#endif  
}

void deleteFolderOrFile(const char* deleteThis) {
  // delete supplied file or folder, unless it is a reserved folder
  char fileName[FILE_NAME_LEN];
  setFolderName(deleteThis, fileName);
  File df = fp.open(fileName);
  if (!df) {
    LOG_WRN("Failed to open %s", fileName);
    return;
  }
  if (df.isDirectory() && (strstr(fileName, "System") != NULL 
      || strstr("/", fileName) != NULL)) {
    df.close();   
    LOG_WRN("Deletion of %s not permitted", fileName);
    delay(1000); // reduce thrashing on same error
    return;
  }  
  LOG_INF("Deleting : %s", fileName);
  // Empty named folder first
  if (df.isDirectory() || ((!strcmp(fsType, "SPIFFS")) && strstr("/", fileName) != NULL)) {
    LOG_INF("Folder %s contents", fileName);
    File file = df.openNextFile();
    while (file) {
      char filepath[FILE_NAME_LEN];
      strcpy(filepath, file.path()); 
      if (file.isDirectory()) LOG_INF("  DIR : %s", filepath);
      else {
        size_t fSize = file.size();
        file.close();
        LOG_INF("  FILE : %s Size : %s %sdeleted", filepath, fmtSize(fSize), STORAGE.remove(filepath) ? "" : "not ");
        deleteOthers(filepath);
      }
      file = df.openNextFile();
    }
    // Remove the folder
    if (df.isDirectory()) LOG_ALT("Folder %s %sdeleted", fileName, STORAGE.rmdir(fileName) ? "" : "not ");
    else df.close();
  } else {
    // delete individual file
    df.close();
    LOG_ALT("File %s %sdeleted", deleteThis, STORAGE.remove(deleteThis) ? "" : "not ");  //Remove the file
    deleteOthers(deleteThis);
  }
}

/************** uncompressed tarball **************/

#define BLOCKSIZE 512

static esp_err_t writeHeader(File& inFile, httpd_req_t* req) {  
  char tarHeader[BLOCKSIZE] = {0}; // 512 bytes tar header
  strncpy(tarHeader, inFile.name(), 99); // name of file
  sprintf(tarHeader + 100, "0000666"); // file permissions stored as ascii octal number
  sprintf(tarHeader + 124, "%011o", inFile.size()); // length of file in bytes as 6 digit ascii octal number
  memcpy(tarHeader + 148, "        ", 8); // init as 8 spaces to calc checksum
  tarHeader[156] = '0'; // type of entry - 0 for ordinary file
  strcpy(tarHeader + 257, "ustar"); // magic
  memcpy(tarHeader + 263, "00", 2); // version as two 0 digits

  // Calculate and set the checksum
  uint32_t checksum = 0;
  for (const auto& ch : tarHeader) checksum += ch;
  sprintf(tarHeader + 148, "%06lo", checksum); // six digit octal number with leading zeroes followed by a NUL and then a space.
  return httpd_resp_send_chunk(req, tarHeader, BLOCKSIZE);
}

esp_err_t downloadFile(File& df, httpd_req_t* req) {
  // download file as attachment, required file name in inFileName
  // setup download header, create zip file if required, and download file
  esp_err_t res = ESP_OK;
  bool needZip = false;
  char downloadName[FILE_NAME_LEN];
  strcpy(downloadName, df.name());
  size_t downloadSize = df.size();
  char fsSavePath[FILE_NAME_LEN];
  strcpy(fsSavePath, inFileName);
#ifdef ISCAM
  changeExtension(fsSavePath, CSV_EXT);
  
  // check if ancillary files present
  needZip = STORAGE.exists(fsSavePath);
  const char* extensions[3] = {AVI_EXT, CSV_EXT, SRT_EXT};
  if (needZip) {
    // ancillary files, calculate total size for http header
    downloadSize = 0;
    for (const auto& ext : extensions) {
      changeExtension(fsSavePath, ext);
      File inFile = STORAGE.open(fsSavePath, FILE_READ);
      if (inFile) {
        // round up file size to 512 byte boundary and add header size
        downloadSize += (((inFile.size() + BLOCKSIZE - 1) / BLOCKSIZE) * BLOCKSIZE) + BLOCKSIZE;
        strcpy(downloadName, inFile.name());
        inFile.close();
      }
    }
    downloadSize += BLOCKSIZE * 2; // end of tarball marker
    changeExtension(downloadName, "zip"); 
  } 
#endif 

  // create http header
  LOG_INF("Download file: %s, size: %s", downloadName, fmtSize(downloadSize));
  httpd_resp_set_type(req, "application/octet-stream");
  // header field values must remain valid until first send
  char contentDisp[IN_FILE_NAME_LEN + 50];
  snprintf(contentDisp, sizeof(contentDisp) - 1, "attachment; filename=%s", downloadName);
  httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
  char contentLength[10];
  snprintf(contentLength, sizeof(contentLength) - 1, "%i", downloadSize);
  httpd_resp_set_hdr(req, "Content-Length", contentLength);

  if (needZip) {
#ifdef ISCAM
    // package avi file and ancillary files into uncompressed tarball
    for (const auto& ext : extensions) {
      changeExtension(fsSavePath, ext);
      File inFile = STORAGE.open(fsSavePath, FILE_READ);
      if (inFile) {
        res = writeHeader(inFile, req);
        if (res == ESP_OK) res = sendChunks(inFile, req, false);
        if (res == ESP_OK) {
          // write end of file filler
          size_t remainingBytes = inFile.size() % BLOCKSIZE;
          if (remainingBytes) {
            char zeroBlock[BLOCKSIZE - remainingBytes] = {};
            res = httpd_resp_send_chunk(req, zeroBlock, sizeof(zeroBlock));
          }
          inFile.close();
        }
      }
    }

    // Write two blocks filled with zeros to mark the end of the archive
    char zeroBlock[BLOCKSIZE] = {};
    res = httpd_resp_send_chunk(req, zeroBlock, BLOCKSIZE);
    res = httpd_resp_send_chunk(req, zeroBlock, BLOCKSIZE);
    res = httpd_resp_sendstr_chunk(req, NULL);
#endif
  } else res = sendChunks(df, req); // send AVI
  return res;
}
