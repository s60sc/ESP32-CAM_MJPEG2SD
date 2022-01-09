// Store SD card content on a remote server using FTP
// gemi254

#include "myConfig.h"

//Ftp server default params
char ftp_server[32] = "test.ftp.com";
char ftp_port[6]    = "21";
char ftp_user[32]   = "test";
char ftp_pass[32]   = "test";
char ftp_wd[64]     = "/home/user/";

//FTP buffers
char rspBuf[255]; //Ftp response buffer
uint8_t rspCount;
#define BUFF_EXT 100
#define BUFF_SIZE (32 * 1024)+BUFF_EXT // Upload data buffer size
#define RESPONSE_TIMEOUT 10000                             
unsigned int hiPort; //Data connection port
static File root;

//WiFi Clients
WiFiClient client;
WiFiClient dclient;


void efail(){
  byte thisByte = 0;
  client.println(F("QUIT"));
  
  unsigned long start = millis();
  while (!client.available() && millis() < start + RESPONSE_TIMEOUT) delay(1);  

  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);
  }
  client.stop();
  LOG_DBG("Command disconnected");  
}
byte eRcv(bool bFail=true){
  byte respCode;
  byte thisByte;

  unsigned long start = millis();
  while (!client.available() && millis() < start + RESPONSE_TIMEOUT) delay(1);
 
  respCode = client.peek();
  rspCount = 0;
  while (client.available()) {
    thisByte = client.read();
    if (rspCount < sizeof(rspBuf)) {       // if (rspCount < 127)  {
      rspBuf[rspCount] = thisByte;
      rspCount++;
      rspBuf[rspCount] = 0;
    }
  }
  //Skip line feed at end
  rspBuf[rspCount-1]=0;
  LOG_DBG("Ftp resp: %s",rspBuf);  
  if (respCode >= '4' || respCode >= '5' ) {
    if(bFail) efail();
    return 0;
  }
  return 1;
}

//Connect to ftp and change to root dir
bool ftpConnect(){
  //Connect
  if (client.connect(ftp_server, String(ftp_port).toInt()) ) {
    LOG_DBG("Command connected at %s:%s", ftp_server,ftp_port);
  } else {
    LOG_ERR("Error opening ftp connection to %s:%s", ftp_server, ftp_port);
    return 0;
  }
  LOG_DBG("Connected to ftp!");
  if (!eRcv()) return 0;

  client.print("USER ");
  client.println(ftp_user);
  if (!eRcv()) return 0;

  LOG_DBG("Ftp pass:%s", ftp_pass);                                          
  client.print("PASS ");
  client.println(ftp_pass);
  if (!eRcv()) return 0;

  client.println("SYST");
  if (!eRcv()) return 0;

  client.println("Type I");
  if (!eRcv()) return 0;

  client.println("PASV");
  if (!eRcv()) return 0;

  char *tStr = strtok(rspBuf, "(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL, "(,");
    array_pasv[i] = atoi(tStr);
    if (tStr == NULL)
    {
      LOG_ERR("Bad PASV Answer");
      return 0;
    }
  }
  unsigned int loPort;
  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;
  hiPort = hiPort | loPort;
  LOG_DBG("Data port: %i", hiPort);
  LOG_DBG("Change to root dir: %s", ftp_wd);
  client.print("CWD ");
  client.println(ftp_wd);
  if (!eRcv()){
    dclient.stop();
    return 0;
  }
  return 1;
}

//Properly disconnect from ftp
byte ftpDisconnect(){
  client.println("QUIT");
  bool retVal = eRcv() ? 1 : 0;
  root.close();
  dclient.stop();
  client.stop();
  if (retVal) LOG_DBG("Command disconnected");
  return retVal;
}

//Check if it is to create remote directory and change to this dir
bool ftpCheckDirPath(String filePath, String &fileName){
  int lv=0;
  std::vector<String> dirPaths;  // declare dir tree paths
  for (char* token = strtok((char *)filePath.c_str(), "/"); token; token = strtok(NULL, "/")){
      dirPaths.push_back(token);
      lv++;
  }
  
  //Create sub dirs
  for(int i=lv-2; i>=0; --i){
      LOG_DBG("Searching for sub dir[%i]: %s",i,dirPaths[i].c_str() );      
      client.print("CWD ");
      client.println(dirPaths[i].c_str());
      eRcv(false); //Don't stop if dir not exists

      //Create dir if not exists
      char *tStr = strtok(rspBuf, " ");
      //Serial.printf("Res: %s\n",tStr);
      if(strcmp(tStr,"550")==0){
        LOG_DBG("Create dir: %s", dirPaths[i].c_str());
        client.print("MKD ");        
        client.println(dirPaths[i].c_str());
        if (!eRcv()){
          client.stop();
          return 0;
       } 
       //Change to new dir
       LOG_DBG("Change to dir: %s", dirPaths[i].c_str());
       client.print("CWD ");
       client.println(dirPaths[i].c_str());
       if (!eRcv()){
         client.stop();
         return 0;
       }    
     }     
  }
  //Store filename without path 
  fileName = dirPaths[lv-1];  
  return 1;
}

//Upload sdfile to current ftp dir, ignore already existing files ?
bool ftpStoreFile(String file, File &fh, bool notExistingOnly=false){
   
   uint32_t fileSize = fh.size();   
  
  // determine if file is suitable for conversion to AVI
  std::string sfile(file.c_str());
  if (isAVI(fh)) {
    sfile = std::regex_replace(sfile, std::regex("mjpeg"), "avi");
    file = String(sfile.data());
    LOG_INF("Store renamed file: %s size: %0.1fMB", file.c_str(),(float)(fileSize/(1024*1024)));    
  }else{
    LOG_INF("Store file: %s size: %0.1fMB", file.c_str(),(float)(fileSize/(1024*1024)));
  }

  //Check if file already exists 
  if(notExistingOnly){
     LOG_DBG("Check local file: %s size: %u B", file.c_str(), fileSize);
     client.print("SIZE ");        
     client.println(file.c_str());
     eRcv(false);
     char *tStr = strtok(rspBuf, " ");
     //Serial.printf("Size Res: %s\n", tStr);
     if(strcmp(tStr,"213")==0){
        tStr = strtok(NULL,"\n");
        uint32_t remoteSize = atol(tStr);
        uint32_t diff = abs((int64_t)fileSize - (int64_t)remoteSize);
        LOG_DBG("Server file: %s real local size: %u, server size: %u, (diff: %u ", file.c_str(), fileSize, remoteSize, diff);
        if (diff < 10000){ //File exists and have same size
            LOG_DBG("File already exists!");
            dclient.stop();
            return true;
        }
     }else{
        LOG_DBG("File: %s was not found on server", file.c_str());
     }
  }
 
  //Connect to data port
  if (dclient.connect(ftp_server, hiPort)) {
    LOG_DBG("Data connected");
  } else{
    LOG_ERR("Data connection failed");   
    return false;
  }
  
  client.print("STOR ");
  client.println(file);
  if (!eRcv()){
    dclient.stop();
    return false;
  }
  
  LOG_INF("Uploading..");
  //byte clientBuf[BUFF_SIZE];
  byte *clientBuf = (byte*)ps_malloc(BUFF_SIZE * sizeof(byte)); 
  if(clientBuf==NULL){
    LOG_ERR("Memory allocation failed ..");
    dclient.stop();
    return false;
  }
  
  unsigned int buffCount=0;
  uint32_t writeBytes=0;                       
  unsigned long uploadStart = millis();
  size_t readLen, writeLen = 0;
  do {
    readLen = readClientBuf(fh, clientBuf, BUFF_SIZE-BUFF_EXT); // obtain modified data to send 
    if(readLen) writeLen = dclient.write((const uint8_t *)clientBuf, readLen);
    if(readLen>0 && writeLen==0){
        LOG_ERR("Write buffer failed ..");
        dclient.stop();
        return false;
    }
    writeBytes += writeLen;
    if(buffCount%5==0){
      LOG_INF("Uploaded %0.0f%%", ( (double)writeBytes / fileSize)*100.0f );      
    }
    ++buffCount;
  } while (readLen);
  if(readLen<BUFF_SIZE) LOG_INF("Uploaded 100%%");
  float uploadDur =  (millis() - uploadStart)/1024;  
  free(clientBuf);
  LOG_INF("Done Uploaded in %3.1f sec",uploadDur); 
  dclient.stop();
  return true; 
}

//Upload a single file or whole directory to ftp 
//On directory incredimental upload
bool uploadFolderOrFileFtp(String sdName, const bool removeAfterUpload, uint8_t levels){
  LOG_INF("Ftp upload name: %s", sdName.c_str());
  if(sdName=="" || sdName=="/"){
    LOG_DBG("Root or null is not allowed %s",sdName.c_str());  
    return true;  
  }
  String ftpName = "";
  //Ftp connect
  if(!ftpConnect()){
    return false; 
  }

  root = SD_MMC.open(sdName);
  if (!root) {
      LOG_ERR("Failed to open: %s", sdName.c_str());
      ftpDisconnect();
      return false;
  }  
    
  if (!root.isDirectory()) { //Upload a single file
      LOG_INF("Uploading file: %s", sdName.c_str());    
 
      if(!ftpCheckDirPath(sdName, ftpName)){
          LOG_ERR("Create ftp dir path %s failed", sdName.c_str());
          ftpDisconnect();
          return false;
      }
      if(!ftpStoreFile(ftpName, root)){
        LOG_ERR("Store file %s to ftp failed", ftpName.c_str());
        ftpDisconnect();
        return false;
      }
      root.close();
      LOG_DBG("File closed");
      
      if(removeAfterUpload){
        LOG_INF("Removing file %s", sdName.c_str()); 
        SD_MMC.remove(sdName.c_str());
      }    
  }else{  //Upload a whole directory
      LOG_INF("Uploading directory: %s", sdName.c_str()); 
      File fh = root.openNextFile();      
      sdName = fh.path();

      if(!ftpCheckDirPath(sdName, ftpName)){
          LOG_ERR("Create ftp dir path %s failed", sdName.c_str());
          ftpDisconnect();
          return false;
      }
          
      bool bUploadOK=false;
      while (fh) {
          sdName = fh.path();

          bUploadOK = false;
          if (fh.isDirectory()) {
              LOG_INF("Sub directory: %s, Not uploading", sdName.c_str());
              /*if (levels) {
                std::string strfile(file.name());
                const char* f="TEST";
                doUploadFtp(f, removeAfterUpload, levels – 1);
              }*/
          } else { 
              byte bPos = sdName.lastIndexOf(".");
              if (sdName.substring(bPos+1) == "mjpeg") {
                bPos = sdName.lastIndexOf("/");
                String ftpName = sdName.substring(bPos+1);      
                LOG_INF("Uploading sub sd file %s to %s", sdName.c_str(),ftpName.c_str()); 
                //
                bUploadOK = ftpStoreFile(ftpName, fh, true);
                if(!bUploadOK){
                  LOG_ERR("Store file %s to ftp failed", ftpName.c_str());
                }
              } 
            }
            //Remove if ok
            if(removeAfterUpload && bUploadOK){
              LOG_INF("Removing file %s", sdName.c_str()); 
              SD_MMC.remove(sdName.c_str());
            } 
            delay(500); // allow other tasks to finish off
            fh = root.openNextFile();
       }
  }
  //Disconnect from ftp
  ftpDisconnect(); 
  return true; 
}

static bool removeAfterUpload=false; //Delete file if successfully uploaded
static void taskUpload(void * parameter){
    // prevent SD access by other tasks
    String fname((char*) parameter);
    delay(1000); // allow other tasks to finish off
    
    LOG_DBG("Entering upload task fname: %s",fname.c_str());    
    uploadFolderOrFileFtp(fname, removeAfterUpload, 0);
    LOG_DBG("Ending uploadTask");
    stopCheck = false;
    vTaskDelete( NULL );
}

void createUploadTask(const char* val, bool move){
    if (ESP.getFreeHeap() < 50000) {
      LOG_ERR("Insufficient free heap for FTP: %u", ESP.getFreeHeap());
    } else {
      stopCheck = true; // prevent ram space contention
      delay(100);
      doPlayback = false;
      static char fname[100];
      strcpy(fname, val); // else wont persist
      removeAfterUpload = move;
      LOG_DBG("Starting upload task with param: %s and delete on upload flag: %i",val,removeAfterUpload);
      xTaskCreate(
          &taskUpload,       /* Task function. */
          "taskUpload",     /* String with name of task. */
          4096*2,           /* Stack size in bytes. */
          (void *)fname,      /* Parameter passed as input of the task */
          1,                /* Priority of the task. */
          NULL);            /* Task handle. */
    }
}
static void taskScheduledUpload(void * parameter) {
  // prevent SD access by other tasks
    String fname((char*) parameter);
    delay(1000); // allow other tasks to finish off

    for(;;) {
      LOG_DBG("Entering upload task with param: %s",fname.c_str());    
      if(uploadFolderOrFileFtp(fname,false,0)) {
        LOG_DBG("Upload completed");
        break;
      }else {
        vTaskDelay(1*60*60*1000 / portTICK_PERIOD_MS);
      }
    }
    LOG_DBG("Ending uploadTask");
    vTaskDelete( NULL );
}

void createScheduledUploadTask(const char* val) {
   static char fname[100];
    strcpy(fname, val); // else wont persist
    LOG_DBG("Starting upload task with val: %s",val);
    xTaskCreate(
        &taskScheduledUpload,       /* Task function. */
        "scheduledTaskUpload",     /* String with name of task. */
        4096*2,           /* Stack size in bytes. */
        (void *)fname,      /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */
}
