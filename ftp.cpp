#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WiFiUdp.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h"
#include <vector>  // Dynamic string array
#include <regex>

//extern bool debug;
bool dbg=1;

//Defined in custom config file myConfig.h
extern const char* ftp_server;
extern const char* ftp_user;
extern const char* ftp_port;
extern const char* ftp_pass;
extern const char* ftp_wd;

unsigned int hiPort; //Data connection port
extern bool doRecording;
extern bool stopPlayback;
static bool doRecordingSave = false;

//FTP buffers
char outBuf[128];
char outCount;
#define BUFF_EXT 100
#define BUFF_SIZE (32 * 1024)+BUFF_EXT // Upload data buffer size
//WiFi Clients
WiFiClient client;
WiFiClient dclient;

bool isAVI(File &fh);
size_t readClientBuf(File &fh, byte* &clientBuf, size_t buffSize);
                       

void efail(){
  byte thisByte = 0;
  client.println(F("QUIT"));
  while (!client.available()) delay(1);
  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);
  }
  client.stop();
  if(dbg) Serial.println("Ftp command disconnected");
}

byte eRcv(bool bFail=true){
  byte respCode;
  byte thisByte;

  while (!client.available()) delay(1);
 
  respCode = client.peek();
  outCount = 0;
  while (client.available()) {
    thisByte = client.read();
    if(dbg) Serial.write(thisByte);
    if (outCount < 127)  {
      outBuf[outCount] = thisByte;
      outCount++;
      outBuf[outCount] = 0;
    }
  }
  //Serial.printf("respCode: %i\n", respCode);
  if (respCode >= '4') {
    if(bFail) efail();
    return 0;
  }
  return 1;
}

//Connect to ftp and change to root dir
bool ftpConnect(){
  //Connect
  if (client.connect(ftp_server, String(ftp_port).toInt()) ) {
    if(dbg) Serial.println(F("Ftp command connected"));
  } else {
    Serial.printf("Error opening ftp connection to %s %s\n", ftp_server,ftp_port);    
    return 0;
  }
  if (!eRcv()) return 0;

  client.print("USER ");
  client.println(ftp_user);
  if (!eRcv()) return 0;

  client.print("PASS ");
  client.println(ftp_pass);
  if (!eRcv()) return 0;

  client.println("SYST");
  if (!eRcv()) return 0;

  client.println("Type I");
  if (!eRcv()) return 0;

  client.println("PASV");
  if (!eRcv()) return 0;

  char *tStr = strtok(outBuf, "(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL, "(,");
    array_pasv[i] = atoi(tStr);
    if (tStr == NULL)
    {
      Serial.println(F("Bad PASV Answer"));
      return 0;
    }
  }
  unsigned int loPort;
  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;
  if(dbg) Serial.print(F("Data port: "));
  hiPort = hiPort | loPort;
  if(dbg) Serial.println(hiPort);
  
  if(dbg) Serial.printf("Change to root dir: %s\n", ftp_wd);
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
  if (!eRcv()) return 0;

  client.stop();
  if(dbg) Serial.println(F("Ftp command disconnected"));

  return 1;
}

//Check if it is to create remote directory and change to this dir
bool ftpCheckDirPath(String filePath, String &fileName){
  int lv=0;
  std::vector<String> dirPaths;  // declare dir tree paths
  for (char* token = strtok((char *)filePath.c_str(), "/"); token; token = strtok(NULL, "/")){
      //Serial.printf("token=%s\n", token);
      dirPaths.push_back(token);
      lv++;
  }
  
  //Create sub dirs
  for(int i=lv-2; i>=0; --i){
      if(dbg) Serial.printf("Checking sub dir[%i]: %s\n",i,dirPaths[i].c_str() );      
      client.print("CWD ");
      client.println(dirPaths[i].c_str());
      eRcv(false); //Don't stop if dir not exists

      //Create dir if not exists
      char *tStr = strtok(outBuf, " ");
      //Serial.printf("Res: %s\n",tStr);
      if(strcmp(tStr,"550")==0){
        if(dbg) Serial.printf("Create dir: %s\n", dirPaths[i].c_str());
        client.print("MKD ");        
        client.println(dirPaths[i].c_str());
        if (!eRcv()){
          client.stop();
          return 0;
       } 
       //Change to new dir
       if(dbg) Serial.printf("Change to dir: %s\n", dirPaths[i].c_str());
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

//Store sdfile to current ftp dir
bool ftpStoreFile(String file, File &fh){
  // determine if file is suitable for conversion to AVI
  std::string sfile(file.c_str());
  if (isAVI(fh)) {
    sfile = std::regex_replace(sfile, std::regex("mjpeg"), "avi");
    file = String(sfile.data());
  }
  if(dbg) Serial.printf("Ftp store file: %s\n", file.c_str());
  
  //Connect to data port
  if (dclient.connect(ftp_server, hiPort)) {
    if(dbg) Serial.println("Ftp data connected");
  } else{
    Serial.println("Ftp data connection failed");   
    return 0;
  }
  client.print("STOR ");
  client.println(file);
  if (!eRcv()){
    dclient.stop();
    return 0;
  }
  
  if(dbg) Serial.println(F("Uploading.."));
  //byte clientBuf[BUFF_SIZE];
  byte *clientBuf = (byte*)ps_malloc(BUFF_SIZE * sizeof(byte)); 
  if(clientBuf==NULL){
    Serial.println(F("Memory allocation failed .."));
    dclient.stop();
    return 0;
  }
  
  unsigned int buffCount=0;
  unsigned long uploadStart = millis();
  size_t readLen,writeLen = 0;
  while (readLen>0){
    readLen = readClientBuf(fh, clientBuf, BUFF_SIZE-BUFF_EXT); // obtain modified data to send    
    ////readLen = fh.read(clientBuf, BUFF_SIZE);
    if(readLen) writeLen = dclient.write((const uint8_t *)clientBuf, readLen);
    if(readLen>0 && writeLen==0){
        Serial.println(F("Write buffer failed .."));
        dclient.stop();
        return 0;
    }
    if(dbg && (++buffCount)%80==0) Serial.println("."); else Serial.print(F("."));
    //Serial.printf("Wifi client write %u Kb\n", (writeLen / 1024));
  }
  float uploadDur =  (millis() - uploadStart)/1000;  
  free(clientBuf);
  Serial.printf("\nDone Uploaded in %3.1f sec\n",uploadDur); 
  dclient.stop();
  return 1; 
}

//Upload a single file or whole directory to ftp 
void uploadFolderOrFileFtp(String sdName, const bool removeAfterUpload, uint8_t levels){
  if(dbg) Serial.printf("Ftp upload name: %s\n", sdName.c_str());
  if(sdName=="/"){
      Serial.printf("Root is not allowed %s\n",sdName.c_str());  
      return;  
  }
  String ftpName = "";
  
  //Ftp connect
  if(!ftpConnect()){
    return; 
  }

  File root = SD_MMC.open(sdName);
  if (!root) {
      Serial.printf("Failed to open: %s\n", sdName.c_str());
      ftpDisconnect();
      return;
  }  
    
  if (!root.isDirectory()) { //Upload a single file
      if(dbg) Serial.printf("Uploading file: %s\n", sdName.c_str());    
 
      if(!ftpCheckDirPath(sdName, ftpName)){
          if(dbg) Serial.printf("Create ftp dir path %s failed\n", sdName.c_str());
          ftpDisconnect();
          return;
      }
      if(!ftpStoreFile(ftpName, root)){
        Serial.printf("Store file %s to ftp failed\n", ftpName.c_str());
        ftpDisconnect();
        return;
      }
      root.close();
      if(dbg) Serial.println(F("File closed"));
      
      if(removeAfterUpload){
        if(dbg) Serial.printf("Removing file %s\n", sdName.c_str()); 
        SD_MMC.remove(sdName.c_str());
      }    
  }else{  //Upload a whole directory
      if(dbg) Serial.printf("Uploading directory: %s\n", sdName.c_str()); 
      File fh = root.openNextFile();      
      sdName = fh.name();
      
      if(!ftpCheckDirPath(sdName, ftpName)){
          Serial.printf("Create ftp dir path %s failed\n", sdName.c_str());
          ftpDisconnect();
          return;
      }
        
      bool bUploadOK=false;
      while (fh) {
          sdName = fh.name();
          bUploadOK = false;
          if (fh.isDirectory()) {
              Serial.printf("Sub directory: %s, Not uploading\n", sdName.c_str());
              /*if (levels) {
                std::string strfile(file.name());
                const char* f="TEST";
                doUploadFtp(f, removeAfterUpload, levels – 1);
              }*/
          } else {        
              byte bPos = sdName.lastIndexOf("/");
              String ftpName = sdName.substring(bPos+1);      
              if(dbg) Serial.printf("Uploading sub sd file %s \n", sdName.c_str()); 
              bUploadOK = ftpStoreFile(ftpName, fh);
              if(!bUploadOK){
                Serial.printf("Store file %s to ftp failed\n", ftpName.c_str());                
              }
          }
          //Remove if ok
          if(removeAfterUpload && bUploadOK){
            if(dbg) Serial.printf("Removing file %s\n", sdName.c_str()); 
            SD_MMC.remove(sdName.c_str());
          }
          fh = root.openNextFile();
      }      
  }
  //Disconnect from ftp
  ftpDisconnect();  
}

static void taskUpload(void * parameter){
    // prevent SD access by other tasks
    String fname((char*) parameter);
    stopPlayback = true; 
    doRecordingSave = doRecording;
    doRecording = false;
    delay(1000); // allow other tasks to finish off
    
    Serial.printf("Entering upload task with %s\n",fname.c_str());
    uploadFolderOrFileFtp(fname,false,0);
    Serial.println("Ending uploadTask");
    doRecording = doRecordingSave; 
    vTaskDelete( NULL );
  
}

void createUploadTask(const char* val){
    static char fname[100];
    strcpy(fname, val); // else wont persist
    Serial.printf("Starting upload task with %s\n",val);
    xTaskCreate(
        &taskUpload,      /* Task function. */
        "taskUpload",     /* String with name of task. */
        4096*2,           /* Stack size in bytes. */
        (void *)fname,    /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */

}
