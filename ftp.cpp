// Upload SD card or SPIFFS content to a remote server using FTP or HTTPS
// 
// s60sc 2022, 2023

#include "appGlobals.h"

#if INCLUDE_FTP_HFS
#if (!INCLUDE_CERTS)
const char* hfs_rootCACertificate = "";
const char* ftps_rootCACertificate = "";
#endif

// File server params (FTP or HTTPS), setup via web page
char fsServer[MAX_HOST_LEN];
uint16_t fsPort = 21;
char FS_Pass[MAX_PWD_LEN]; // FTP password or HTTPS passcode
char fsWd[FILE_NAME_LEN]; 

static bool uploadInProgress = false;
uint8_t percentLoaded = 0;
TaskHandle_t fsHandle = NULL;
static char storedPathName[FILE_NAME_LEN];
static char folderPath[FILE_NAME_LEN];
static byte* fsChunk;
bool deleteAfter = false; // auto delete after upload
bool autoUpload = false;  // Automatically upload every created file to remote file server
bool fsUse = false; // FTP if false, HTTPS if true


/******************** HTTPS ********************/

// Upload file of folder of files from local storage to remote HTTPS file server
// Requires significant heap space due to TLS.
// Each file POST has following format, where the following values are derived 
// from the web page:
//   Host: FS Server
//   port: FS port
//   passcode: FS password
//   pathname: FS root dir + selected day folder/file
/*
POST /upload HTTP/1.1
Host: 192.168.1.135
Content-Length: 2412358
Content-Type: multipart/form-data; boundary=123456789000000000000987654321

--123456789000000000000987654321
Content-disposition: form-data; name="json"
Content-Type: "application/json"

{"pathname":"/FS/root/dir/20231119/20231119_140513_SVGA_20_6_120.avi","passcode":"abcd1234"}
--123456789000000000000987654321
Content-disposition: form-data; name="file"; filename="20231119_140513_SVGA_20_6_120.avi"
Content-Type: "application/octet-stream"

<file content>
--123456789000000000000987654321
*/

#define CONTENT_TYPE "Content-Type: \"%s\"\r\n\r\n"
#define POST_HDR "POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Length: %u\r\n" CONTENT_TYPE
#define MULTI_TYPE "multipart/form-data; boundary=" BOUNDARY_VAL
#define JSON_TYPE "application/json"
#define BIN_TYPE "application/octet-stream"
#define FORM_DATA "--" BOUNDARY_VAL "\r\nContent-disposition: form-data; name=\"%s%s\"\r\n" CONTENT_TYPE 
#define END_BOUNDARY "\r\n--" BOUNDARY_VAL "--\r\n"
#define FILE_NAME "file\"; filename=\""
#define JSON_DATA "{\"pathname\":\"%s%s/%s\",\"passcode\":\"%s\"}"
#define FORM_OFFSET 256 // offset in fsBuff to prepare form data

NetworkClientSecure hclient;
char* fsBuff;

static void postHeader(const char* tmethod, const char* contentType, bool isFile, 
  size_t fileSize, const char* fileName) {
  // create http post header
  char* p = fsBuff + FORM_OFFSET; // leave space for http request data
  if (isFile) {
    p += sprintf(p, FORM_DATA, "json", "", JSON_TYPE);
    // fsBuff initially contains folder name
    p += sprintf(p, JSON_DATA, fsWd, folderPath, fileName, FS_Pass);
    p += sprintf(p, "\r\n" FORM_DATA, FILE_NAME, fileName, BIN_TYPE); 
  } // else JSON data already loaded by hfsCreateFolder()
  size_t formLen = strlen(fsBuff + FORM_OFFSET);
  // create http request header
  p = fsBuff;
  if (isFile) fileSize += formLen + strlen(END_BOUNDARY);
  p += sprintf(p, POST_HDR, tmethod, fsServer, fileSize, isFile ? MULTI_TYPE : JSON_TYPE);
  size_t reqLen = strlen(fsBuff);
  // concatenate request and form data
  if (formLen) {
    memmove(fsChunk + reqLen, fsChunk + FORM_OFFSET, formLen);
    fsChunk[reqLen + formLen] = 0;
  }
  hclient.print(fsBuff); // http header
}

static bool hfsStoreFile(File &fh) {
  // Upload individual file to HTTPS server
  // reject if folder or not valid file type
#ifdef ISCAM
  if (!strstr(fh.name(), AVI_EXT) && !strstr(fh.name(), CSV_EXT) && !strstr(fh.name(), SRT_EXT)) return false; 
#else
  if (!strstr(fh.name(), FILE_EXT)) return false; 
#endif
  LOG_INF("Upload file: %s, size: %s", fh.name(), fmtSize(fh.size()));    

  // prep POST header and send file to HTTPS server
  postHeader("upload", BIN_TYPE, true, fh.size(), fh.name());
  // upload file content in chunks
  uint8_t percentLoaded = 0;
  size_t chunksize = 0, totalSent = 0;
  while ((chunksize = fh.read((uint8_t*)fsChunk, CHUNKSIZE))) {
    hclient.write((uint8_t*)fsChunk, chunksize);
    totalSent += chunksize;
    if (calcProgress(totalSent, fh.size(), 5, percentLoaded)) LOG_INF("Uploaded %u%%", percentLoaded); 
  }
  percentLoaded = 100;
  hclient.println(END_BOUNDARY);
  return true;
}

/******************** FTP ********************/

// FTP control
bool useFtps = false;
char ftpUser[MAX_HOST_LEN];
static char rspBuf[256]; // Ftp response buffer
static char respCodeRx[4]; // ftp response code
static fs::FS fp = STORAGE;
#define NO_CHECK "999"

// WiFi Clients
NetworkClient rclient;
NetworkClient dclient;

static bool sendFtpCommand(const char* cmd, const char* param, const char* respCode, const char* respCode2 = NO_CHECK) {
  // build and send ftp command
  if (strlen(cmd)) {
    rclient.print(cmd);
    rclient.println(param);
  }
  LOG_VRB("Sent cmd: %s%s", cmd, param);
  
  // wait for ftp server response
  uint32_t start = millis();
  while (!rclient.available() && millis() < start + (responseTimeoutSecs * 1000)) delay(1);
  if (!rclient.available()) {
    LOG_WRN("FTP server response timeout");
    return false;
  }
  // read in response code and message
  rclient.read((uint8_t*)respCodeRx, 3); 
  respCodeRx[3] = 0; // terminator
  int readLen = rclient.read((uint8_t*)rspBuf, 255);
  rspBuf[readLen] = 0;
  while (rclient.available()) rclient.read(); // bin the rest of response

  // check response code with expected
  LOG_VRB("Rx code: %s, resp: %s", respCodeRx, rspBuf);
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

static bool ftpConnect() {
  // Connect to ftp or ftps
  if (rclient.connect(fsServer, fsPort)) {LOG_VRB("FTP connected at %s:%u", fsServer, fsPort);}
  else {
    LOG_WRN("Error opening ftp connection to %s:%u", fsServer, fsPort);
    return false;
  }
  if (!sendFtpCommand("", "", "220")) return false;
  if (useFtps) {
    if (sendFtpCommand("AUTH ", "TLS", "234")) {
      /* NOT IMPLEMENTED */
    } else LOG_WRN("FTPS not available");
  }
  if (!sendFtpCommand("USER ", ftpUser, "331")) return false;
  if (!sendFtpCommand("PASS ", FS_Pass, "230")) return false;
  // change to supplied folder
  if (!sendFtpCommand("CWD ", fsWd, "250")) return false;
  if (!sendFtpCommand("Type I", "", "200")) return false;
  return true;
}

static void ftpDisconnect() {
  // Disconnect from ftp server
  rclient.println("QUIT");
  dclient.stop();
  rclient.stop();
}

static bool ftpCreateFolder(const char* folderName) {
  // create folder if non existent then change to it
  LOG_VRB("Check for folder %s", folderName);
  sendFtpCommand("CWD ", folderName, NO_CHECK); 
  if (strcmp(respCodeRx, "550") == 0) {
    // non existent folder, create it
    if (!sendFtpCommand("MKD ", folderName, "257")) return false;
    //sendFtpCommand("SITE CHMOD 755 ", folderName, "200", "550"); // unix only
    if (!sendFtpCommand("CWD ", folderName, "250")) return false;         
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
  LOG_VRB("Data port: %i", dataPort);
  if (!dclient.connect(fsServer, dataPort)) {
    LOG_WRN("Data connection failed");   
    return false;
  }
  return true;
}

static bool ftpStoreFile(File &fh) {
  // Upload individual file to current folder, overwrite any existing file 
  // reject if folder, or not valid file type    
#ifdef ISCAM
  if (!strstr(fh.name(), AVI_EXT) && !strstr(fh.name(), CSV_EXT) && !strstr(fh.name(), SRT_EXT)) return false; 
#else
  if (!strstr(fh.name(), FILE_EXT)) return false; 
#endif
  char ftpSaveName[FILE_NAME_LEN];
  strcpy(ftpSaveName, fh.name());
  size_t fileSize = fh.size();
  LOG_INF("Upload file: %s, size: %s", ftpSaveName, fmtSize(fileSize));    

  // open data connection
  openDataPort();
  uint32_t writeBytes = 0; 
  uint32_t uploadStart = millis();
  size_t readLen, writeLen;
  if (!sendFtpCommand("STOR ", ftpSaveName, "150", "125")) return false;
  do {
    // upload file in chunks
    readLen = fh.read(fsChunk, CHUNKSIZE);  
    if (readLen) {
      writeLen = dclient.write((const uint8_t*)fsChunk, readLen);
      writeBytes += writeLen;
      if (writeLen == 0) {
        LOG_WRN("Upload file to ftp failed");
        return false;
      }
      if (calcProgress(writeBytes, fileSize, 5, percentLoaded)) LOG_INF("Uploaded %u%%", percentLoaded); 
    }
  } while (readLen > 0);
  dclient.stop();
  percentLoaded = 100;
  bool res = sendFtpCommand("", "", "226");
  if (res) {
    LOG_ALT("Uploaded %s in %u sec", fmtSize(writeBytes), (millis() - uploadStart) / 1000);
    //sendFtpCommand("SITE CHMOD 644 ", ftpSaveName, "200", "550"); // unix only
  } else LOG_WRN("File transfer not successful");
  return res;
}


/******************** Common ********************/

static bool getFolderName(const char* folderName) {
  // extract folder names from path name
  strcpy(folderPath, folderName); 
  int pos = 1; // skip 1st '/'
  // get each folder name in sequence
  bool res = true;
  for (char* p = strchr(folderPath, '/'); (p = strchr(++p, '/')) != NULL; pos = p + 1 - folderPath) {
    *p = 0; // terminator
    if (!fsUse) res = ftpCreateFolder(folderPath + pos);
  }
  return res;
}

static bool uploadFolderOrFileFs(const char* fileOrFolder) {
  // Upload a single file or whole folder using FTP or HTTPS server
  // folder is uploaded file by file
  fsBuff = (char*)fsChunk;
  bool res = fsUse ? remoteServerConnect(hclient, fsServer, fsPort, hfs_rootCACertificate, FSFTP) : ftpConnect();

  if (!res) {
    LOG_WRN("Unable to connect to %s server", fsUse ? "HTTPS" : "FTP");
    return false;
  }
  res = false;
  const int saveRefreshVal = refreshVal;
  refreshVal = 1;
  File root = fp.open(fileOrFolder);
  if (!root.isDirectory()) {
    // Upload a single file 
    char fsSaveName[FILE_NAME_LEN];
    strcpy(fsSaveName, root.path());
    if (getFolderName(root.path())) res = fsUse ? hfsStoreFile(root) : ftpStoreFile(root); 
#ifdef ISCAM
    // upload corresponding csv and srt files if exist
    if (res) {
      changeExtension(fsSaveName, CSV_EXT);
      if (fp.exists(fsSaveName)) {
        File csv = fp.open(fsSaveName);
        res = fsUse ? hfsStoreFile(csv) : ftpStoreFile(csv);
        csv.close();
      }
      changeExtension(fsSaveName, SRT_EXT);
      if (fp.exists(fsSaveName)) {
        File srt = fp.open(fsSaveName);
        res = fsUse ? hfsStoreFile(srt) : ftpStoreFile(srt);
        srt.close();
      }
    }
    if (!res) LOG_WRN("Failed to upload: %s", fsSaveName);
#endif
  } else {  
    // Upload a whole folder, file by file
    LOG_INF("Uploading folder: ", root.name()); 
    strncpy(folderPath, root.name(), FILE_NAME_LEN - 1);
    res = fsUse ? true : ftpCreateFolder(root.name());
    if (!res) {
      refreshVal = saveRefreshVal;
      return false;
    }
    File fh = root.openNextFile();
    while (fh) {
      res = fsUse ? hfsStoreFile(fh) : ftpStoreFile(fh);
      if (!res) break; // abandon rest of files
      fh.close();
      fh = root.openNextFile();
    }
    if (fh) fh.close();
  }
  refreshVal = saveRefreshVal;
  root.close();
  fsUse ? remoteServerClose(hclient) : ftpDisconnect(); 
  return res;
}

static void fileServerTask(void* parameter) {
  // process an FTP or HTTPS request
#ifdef ISCAM
  doPlayback = false; // close any current playback
#endif
  fsChunk = psramFound() ? (byte*)ps_malloc(CHUNKSIZE) : (byte*)malloc(CHUNKSIZE); 
  if (strlen(storedPathName) >= 2) {
    File root = fp.open(storedPathName);
    if (!root) LOG_WRN("Failed to open: %s", storedPathName);
    else { 
      bool res = uploadFolderOrFileFs(storedPathName);
      if (res && deleteAfter) deleteFolderOrFile(storedPathName);
    }
  } else LOG_VRB("Root or null is not allowed %s", storedPathName);  
  uploadInProgress = false;
  free(fsChunk);
  fsHandle = NULL;
  vTaskDelete(NULL);
}

bool fsStartTransfer(const char* fileFolder) {
  // called from other functions to commence transfer of file or folder to file server
  setFolderName(fileFolder, storedPathName);
  if (!uploadInProgress) {
    uploadInProgress = true;
    if (fsHandle == NULL) xTaskCreate(&fileServerTask, "fileServerTask", FS_STACK_SIZE, NULL, FTP_PRI, &fsHandle);    
    debugMemory("fsStartTransfer");
    return true;
  } else LOG_WRN("Unable to transfer %s as another transfer in progress", storedPathName);
  return false;
}

void prepUpload() {
  LOG_INF("File uploads will use %s server", fsUse ? "HTTPS" : "FTP");
}
#endif
