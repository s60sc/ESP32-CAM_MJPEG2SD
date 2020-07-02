#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WiFiUdp.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h"
#include <vector>  // Dynamic string array

//extern bool debug;
bool dbg=1;

//Defined in custom config file myConfig.h
extern const char* ftp_server;
extern const char* ftp_user;
extern const char* ftp_port;
extern const char* ftp_pass;
extern const char* ftp_wd;

unsigned int hiPort; //Data connection port

//FTP buffers
char outBuf[128];
char outCount;

#define BUFF_SIZE 16 * 1024 // Upload data buffer size
//WiFi Clients
WiFiClient client;
WiFiClient dclient;

void efail(){
  byte thisByte = 0;
  client.println(F("QUIT"));
  while (!client.available()) delay(1);
  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);
  }
  client.stop();
  if(dbg) Serial.println(F("Ftp command disconnected"));
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

byte ftpDisconnect(){
  dclient.stop();
  if(dbg) Serial.println(F("Ftp data disconnected"));
  client.println();
  if (!eRcv()) return 0;

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
  unsigned int clientCount = 0;
  unsigned int buffCount=0;
  unsigned long uploadStart = millis();
  while (fh.available()){
    clientBuf[clientCount] = fh.read();
    clientCount++;
    if (clientCount >= BUFF_SIZE){
      unsigned int bWrite = dclient.write((const uint8_t *)clientBuf, BUFF_SIZE);
      if(bWrite==0){
          Serial.println(F("Write buffer failed .."));
          dclient.stop();
          return 0;
      }
      clientCount = 0;      
      if(dbg && (++buffCount)%80==0) Serial.println("."); else Serial.print(F("."));
      //Serial.printf("Wifi client write %u\n", bWrite);
    }    
  }
  if (clientCount > 0) dclient.write((const uint8_t *)clientBuf, clientCount);
  float uploadDur =  (millis() - uploadStart)/1024;  
  free(clientBuf);
  Serial.printf("\nDone Uploaded in %3.1f sec\n",uploadDur); 
  dclient.stop();
  return 1; 
}

//Upload a single file or whole dir ftp ftp
void uploadFolderOrFileFtp(String sdName, const bool removeAfterUpload, uint8_t levels){
  if(dbg) Serial.printf("Ftp upload name: %s\n", sdName.c_str());
  String ftpName = "";
  
  //Ftp connect
  if(!ftpConnect()){
    return; 
  }

  File root = SD_MMC.open(sdName);
  if (!root) {
      Serial.printf("Failed to open: %s\n", sdName);
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
              if(dbg) Serial.printf("Uploading sub sd file %s to %s\n", sdName.c_str(),ftpName.c_str()); 
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
    Serial.printf("Entering upload task with %s\n",parameter);
    String fname = (char *)parameter;
    uploadFolderOrFileFtp(fname,false,0);
    Serial.println("Ending uploadTask");
    vTaskDelete( NULL );
  
}
void createUploadTask(const char* val){
    Serial.printf("Starting upload task with %s\n",val);
    xTaskCreate(
        &taskUpload,       /* Task function. */
        "taskUpload",     /* String with name of task. */
        4096*2,           /* Stack size in bytes. */
        (void *)val,      /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */

}
