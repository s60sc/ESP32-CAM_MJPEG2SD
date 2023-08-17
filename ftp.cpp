// Store SD card or SPIFFS content on a remote server using FTP
//
// s60sc 2022, based on code contributed by gemi254

#include "appGlobals.h"

// Ftp server params, setup via web page
char ftp_server[32];
uint16_t ftp_port = 21;
char ftp_user[32];
char FTP_Pass[MAX_PWD_LEN];
char ftp_wd[64]; 
uint8_t percentLoaded = 0;

// FTP control
static char rspBuf[256]; // Ftp response buffer
static char respCodeRx[4]; // ftp response code                        
TaskHandle_t ftpHandle = NULL;
static char storedPathName[FILE_NAME_LEN];
static bool uploadInProgress = false;
static bool deleteAfter = false;
static fs::FS fp = STORAGE;
#define NO_CHECK "999"

// WiFi Clients
WiFiClient client;
WiFiClient dclient;

static bool sendFtpCommand(const char* cmd, const char* param, const char* respCode, const char* respCode2 = NO_CHECK) {
  // build and send ftp command
  if (strlen(cmd)) {
    client.print(cmd);
    client.println(param);
  }
  LOG_DBG("Sent cmd: %s%s", cmd, param);
  
  // wait for ftp server response
  uint32_t start = millis();
  while (!client.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!client.available()) {
    LOG_ERR("FTP server response timeout");
    return false;
  }
  // read in response code and message
  client.read((uint8_t*)respCodeRx, 3); 
  respCodeRx[3] = 0; // terminator
  int readLen = client.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (client.available()) client.read(); // bin the rest of response

  // check response code with expected
  LOG_DBG("Rx code: %s, resp: %s", respCodeRx, rspBuf);
  if (strcmp(respCode, NO_CHECK) == 0) return true; // response code not checked
  if (strcmp(respCodeRx, respCode) != 0) {
    if (strcmp(respCodeRx, respCode2) != 0) {
      // incorrect response code
      LOG_ERR("Command %s got wrong response: %s %s", cmd, respCodeRx, rspBuf);
      return false;
    }
  }
  return true;
}

static bool ftpConnect(){
  // Connect to ftp and change to root dir
  if (client.connect(ftp_server, ftp_port)) {
    LOG_DBG("FTP connected at %s:%u", ftp_server, ftp_port);
  } else {
    LOG_ERR("Error opening ftp connection to %s:%u", ftp_server, ftp_port);
    return false;
  }
  if (!sendFtpCommand("", "", "220")) return false;
  if (!sendFtpCommand("USER ", ftp_user, "331")) return false;
  if (!sendFtpCommand("PASS ", FTP_Pass, "230")) return false;
  if (!sendFtpCommand("CWD ", ftp_wd, "250")) return false;
  if (!sendFtpCommand("Type I", "", "200")) return false;
  return true;
}

static bool createFtpFolder(const char* folderName) {
  // create folder if non existent then change to it
  LOG_DBG("Check for folder %s", folderName);
  sendFtpCommand("CWD ", folderName, NO_CHECK); 
  if (strcmp(respCodeRx, "550") == 0) {
    // non existent folder, create it
    if (!sendFtpCommand("MKD ", folderName, "257")) return false;
    if (!sendFtpCommand("SITE CHMOD 755 ", folderName, "200"))
       return false;
    if (!sendFtpCommand("CWD ", folderName, "250")) return false;         
  }
  return true;
}

static bool getFolderName(const char* folderPath) {
  // extract folder names from path name
  char folderName[FILE_NAME_LEN];
  strcpy(folderName, folderPath); 
  int pos = 1; // skip 1st '/'
  // get each folder name in sequence
  for (char* p = strchr(folderName, '/'); (p = strchr(++p, '/')) != NULL; pos = p + 1 - folderName) {
    *p = 0; // terminator
    if (!createFtpFolder(folderName + pos)) return false;
  }
  return true;
}

static bool openDataPort() {
  // set up port for data transfer
  if (!sendFtpCommand("PASV", "", "227")) return false;
  // derive data port number
  char* p = strchr(rspBuf, '('); // skip over initial text
  int p1, p2;   
  int items = sscanf(p, "(%*d,%*d,%*d,%*d,%d,%d)", &p1, &p2);
  if (items != 2) {
    LOG_ERR("Failed to parse data port");
    return false;
  }
  int dataPort = (p1 << 8) + p2;
  
  // Connect to data port
  LOG_DBG("Data port: %i", dataPort);
  if (!dclient.connect(ftp_server, dataPort)) {
    LOG_ERR("Data connection failed");   
    return false;
  }
  return true;
}

static bool ftpStoreFile(File &fh) {
  // Upload individual file to current folder, overwrite any existing file  
  if (strstr(fh.name(), FILE_EXT) == NULL) return false; // folder, or not valid file type    
  char ftpSaveName[FILE_NAME_LEN];
  strcpy(ftpSaveName, fh.name());
  size_t fileSize = fh.size();
  LOG_INF("Upload file: %s, size: %0.1fMB", ftpSaveName, (float)(fileSize)/ONEMEG);    

  // open data connection
  openDataPort();
  uint32_t writeBytes = 0, progCnt = 0; 
  uint32_t uploadStart = millis();
  size_t readLen, writeLen;
  if (!sendFtpCommand("STOR ", ftpSaveName, "150", "125")) return false;
  do {
    // upload file in chunks
    readLen = fh.read(chunk, CHUNKSIZE);  
    if (readLen) {
      writeLen = dclient.write((const uint8_t*)chunk, readLen);
      writeBytes += writeLen;
      if (writeLen == 0) {
        LOG_ERR("Upload file to ftp failed");
        return false;
      }
      progCnt++;
      percentLoaded = writeBytes * 100 / fileSize;
      if (progCnt % 50 == 0) LOG_INF("Uploaded %u%%", percentLoaded); 
    }
  } while (readLen > 0);
  dclient.stop();
  percentLoaded = 100;
  if (sendFtpCommand("", "", "226")) LOG_ALT("Uploaded %0.1fMB in %u sec", (float)(writeBytes) / ONEMEG, (millis() - uploadStart) / 1000); 
  else LOG_ERR("File transfer not successful");
  sendFtpCommand("SITE CHMOD 644 ", ftpSaveName, "200");
  return true;
}

static bool uploadFolderOrFileFtp() {
  // Upload a single file or whole folder using ftp 
  // folder is uploaded file by file
  if (strlen(storedPathName) < 2){
    LOG_DBG("Root or null is not allowed %s", storedPathName);  
    return false;  
  }
  if (!ftpConnect()) {
    LOG_ERR("Unable to make ftp connection");
    return false; 
  }
  File root = fp.open(storedPathName);
  if (!root) {
    LOG_ERR("Failed to open: %s", storedPathName);
    return false;
  }  

  bool res;
  const int saveRefreshVal = refreshVal;
  refreshVal = 1;
  if (!root.isDirectory()) {
    // Upload a single file 
    if (getFolderName(root.path())) res = ftpStoreFile(root); 
  } else {  
    // Upload a whole folder, file by file
    LOG_INF("Uploading folder: ", root.name()); 
    if (!createFtpFolder(root.name())) {
      refreshVal = saveRefreshVal;
      return false;
    }
    File fh = root.openNextFile();            
    while (fh) {
      res = ftpStoreFile(fh);
      if (!res) break; // abandon rest of files
      fh.close();
      fh = root.openNextFile();
    }
    if (fh) fh.close();
  }
  refreshVal = saveRefreshVal;
  root.close();
  return res;
}

static void FTPtask(void* parameter) {
  // process an FTP request
#ifdef ISCAM
  doPlayback = false; // close any current playback
#endif
  bool res = uploadFolderOrFileFtp();
  // Disconnect from ftp server
  client.println("QUIT");
  dclient.stop();
  client.stop();
  if (res && deleteAfter) deleteFolderOrFile(storedPathName);
  uploadInProgress = false;
  vTaskDelete(NULL);
}

bool ftpFileOrFolder(const char* fileFolder, bool _deleteAfter) {
  // called from other functions to commence FTP upload
  setFolderName(fileFolder, storedPathName);
  if (!uploadInProgress) {
    uploadInProgress = true;
    deleteAfter = _deleteAfter;
    xTaskCreate(&FTPtask, "FTPtask", 1024 * 3, NULL, 1, &ftpHandle);    
    return true;
  } else LOG_WRN("Unable to upload %s as another upload in progress", storedPathName);
  return false;
}
