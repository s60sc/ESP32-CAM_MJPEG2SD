#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WiFiUdp.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h"
#include <vector>  // Dynamic string array

//Defined in custom config file myConfig.h
extern const char* ftp_server;
extern const char* ftp_user;
extern const char* ftp_port;
extern const char* ftp_pass;
extern const char* ftp_wd;


//FTP buffers
char outBuf[128];
char outCount;
#define BUFF_SIZE 1024 //Upload data buffer size
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
  Serial.println(F("Command disconnected"));
}

byte eRcv(bool bFail=true){
  byte respCode;
  byte thisByte;

  while (!client.available()) delay(1);
 
  respCode = client.peek();
  outCount = 0;
  while (client.available()) {
    thisByte = client.read();
    Serial.write(thisByte);
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

//byte uploadFileFtp(String fileName){
byte uploadFileFtp(File &fh){
  
  String fileName = fh.name();
  /*
  File fh = SD_MMC.open(fileName, "r");
  if (!fh) {
    Serial.println("file open failed");
    return 0;
  }*/
  //Connect
  if (client.connect(ftp_server, String(ftp_port).toInt()) ) {
    Serial.println(F("Command connected"));
  }
  else {
    fh.close();
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

  unsigned int hiPort, loPort;
  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;
  Serial.print(F("Data port: "));
  hiPort = hiPort | loPort;
  Serial.println(hiPort);
  if (dclient.connect(ftp_server, hiPort)) {
    Serial.println("Data connected");
  }
  else {
    Serial.println("Data connection failed");
    client.stop();
    fh.close();
    return 0;
  }
  
  Serial.printf("Change root dir: %s\n", ftp_wd);
  client.print("CWD ");
  client.println(ftp_wd);
  if (!eRcv()){
    dclient.stop();
    return 0;
  }
  int lv=0;
  std::vector<String> dirPaths;  // declare dir tree paths
  for (char* token = strtok((char *)fileName.c_str(), "/"); token; token = strtok(NULL, "/")){
      //Serial.printf("token=%s\n", token);
      dirPaths.push_back(token);
      lv++;
  }
  
  //Create sub dirs
  for(int i=lv-2; i>=0; --i){
      Serial.printf("Checking sub dir[%i]: %s\n",i,dirPaths[i].c_str() );      
      client.print("CWD ");
      client.println(dirPaths[i].c_str());
      eRcv(false); //Don't stop if dir not exists

      //Create dir if not exists
      char *tStr = strtok(outBuf, " ");
      //Serial.printf("Res: %s\n",tStr);
      if(strcmp(tStr,"550")==0){
        Serial.printf("Create dir: %s\n", dirPaths[i].c_str());
        client.print("MKD ");        
        client.println(dirPaths[i].c_str());
        if (!eRcv()){
          dclient.stop();
          return 0;
       } 
       //Change to new dir
       Serial.printf("Change to dir: %s\n", dirPaths[i].c_str());
       client.print("CWD ");
       client.println(dirPaths[i].c_str());
       if (!eRcv()){
         dclient.stop();
         return 0;
       }    
     }     
  }

  String file = dirPaths[lv-1];  
  Serial.printf("Ftp store file: %s\n", file.c_str());
  client.print("STOR ");
  client.println(file);
  if (!eRcv()){
    dclient.stop();
    return 0;
  }
  Serial.println(F("Uploading.."));
  
  byte clientBuf[BUFF_SIZE];
  int clientCount = 0;
  unsigned int buffCount=0;
  while (fh.available()){
    clientBuf[clientCount] = fh.read();
    clientCount++;
    if (clientCount >= BUFF_SIZE){
      dclient.write((const uint8_t *)clientBuf, BUFF_SIZE);
      clientCount = 0;      
      if((++buffCount)%80==0) Serial.println("."); else Serial.print(F("."));
    }    
  }
  if (clientCount > 0) dclient.write((const uint8_t *)clientBuf, clientCount);

  Serial.println("\nDone");
  dclient.stop();
  Serial.println(F("Data disconnected"));
  client.println();
  if (!eRcv()) return 0;

  client.println("QUIT");
  if (!eRcv()) return 0;

  client.stop();
  Serial.println(F("Command disconnected"));

  fh.close();
  Serial.println(F("File closed"));
  return 1;
}

void uploadFolderOrFileFtp(String sdName, const bool removeAfterUpload, uint8_t levels){
  Serial.printf("Ftp upload name: %s\n", sdName.c_str());
  File root = SD_MMC.open(sdName);
  if (!root) {
      Serial.printf("Failed to open: %s\n", sdName);    
      return;
  }

  //Upload a single file
  if (!root.isDirectory()) {
      Serial.printf("Uploading file: %s\n", sdName.c_str());    
      bool bUploadOK = uploadFileFtp(root);
      if(removeAfterUpload && bUploadOK){
        Serial.printf("Removing file %s\n", sdName.c_str()); 
        SD_MMC.remove(sdName.c_str());
      }
      return;
  }
  
  //Upload a directory
  Serial.printf("Uploading directory: %s\n", sdName.c_str()); 

   //Upload a directory
  File file = root.openNextFile();
  bool bUploadOK=false;
  while (file) {
      String fileName = file.name();
      bUploadOK = false;
      if (file.isDirectory()) {
          Serial.printf("Sub directory: %s, Not uploading\n", fileName.c_str());
          /*if (levels) {
            std::string strfile(file.name());
            const char* f="TEST";
            doUploadFtp(f, removeAfterUpload, levels – 1);
          }*/
      } else {
          String fname = file.name();
          Serial.printf("Uploading sub file %s\n", fname.c_str()); 
          bUploadOK = uploadFileFtp(file);
      }
      //Remove if ok
      if(removeAfterUpload && bUploadOK){
        Serial.printf("Removing file %s\n", sdName.c_str()); 
        SD_MMC.remove(sdName.c_str());
      }
      file = root.openNextFile();
  }
     
}
