#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClient.h> 
#include <WiFiUdp.h>
#include "FS.h"     // SD Card ESP32
#include "SD_MMC.h"
#include <vector>  // Dynamic string array

#include "esp_log.h"
static const char* TAG = "ftp";

//Defined in custom config file myConfig.h
extern const char* ftp_server;
extern const char* ftp_user;
extern const char* ftp_port;
extern const char* ftp_pass;
extern const char* ftp_wd;

unsigned int hiPort; //Data connection port

//FTP buffers
char rspBuf[255]; //Ftp response buffer
char rspCount;
#define BUFF_SIZE 32 * 1024 // Upload data buffer size
#define RESPONSE_TIMEOUT 10000
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
  ESP_LOGI(TAG, "Ftp command disconnected");  
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
  ESP_LOGI(TAG, "Ftp resp: %s",rspBuf);  
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
    ESP_LOGI(TAG, "Ftp command connected");
  } else {
    ESP_LOGE(TAG, "Error opening ftp connection to %s %s\n", ftp_server, ftp_port);
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

  char *tStr = strtok(rspBuf, "(,");
  int array_pasv[6];
  for ( int i = 0; i < 6; i++) {
    tStr = strtok(NULL, "(,");
    array_pasv[i] = atoi(tStr);
    if (tStr == NULL)
    {
      ESP_LOGE(TAG, "Bad PASV Answer");
      return 0;
    }
  }
  unsigned int loPort;
  hiPort = array_pasv[4] << 8;
  loPort = array_pasv[5] & 255;
  //if(dbg) Serial.print(F("Data port: "));
  hiPort = hiPort | loPort;
  ESP_LOGI(TAG, "Data port: %i",hiPort);
  ESP_LOGI(TAG, "Change to root dir: %s", ftp_wd);
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
  ESP_LOGI(TAG, "Ftp command disconnected");

  return 1;
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
      
      ESP_LOGI(TAG, "Searching for sub dir[%i]: %s",i,dirPaths[i].c_str() );      
      client.print("CWD ");
      client.println(dirPaths[i].c_str());
      eRcv(false); //Don't stop if dir not exists

      //Create dir if not exists
      char *tStr = strtok(rspBuf, " ");
      //Serial.printf("Res: %s\n",tStr);
      if(strcmp(tStr,"550")==0){
        ESP_LOGI(TAG, "Create dir: %s", dirPaths[i].c_str());
        client.print("MKD ");        
        client.println(dirPaths[i].c_str());
        if (!eRcv()){
          client.stop();
          return 0;
       } 
       //Change to new dir
       ESP_LOGI(TAG, "Change to dir: %s", dirPaths[i].c_str());
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
  uint32_t fileSize = fh.size();
  ESP_LOGI(TAG, "Ftp store file: %s size: %0.1fMB", file.c_str(),(float)(fileSize/(1024*1024)));
  
  //Connect to data port
  if (dclient.connect(ftp_server, hiPort)) {
    ESP_LOGI(TAG, "Ftp data connected");
  } else{
    ESP_LOGE(TAG, "Ftp data connection failed");   
    return 0;
  }
  client.print("STOR ");
  client.println(file);
  if (!eRcv()){
    dclient.stop();
    return 0;
  }
  
  ESP_LOGI(TAG, "Uploading..");
  //byte clientBuf[BUFF_SIZE];
  byte *clientBuf = (byte*)ps_malloc(BUFF_SIZE * sizeof(byte)); 
  if(clientBuf==NULL){
    ESP_LOGE(TAG, "Memory allocation failed ..");
    dclient.stop();
    return 0;
  }
  
  unsigned int buffCount=0;
  uint32_t writeBytes=0;
  unsigned long uploadStart = millis();
  size_t readLen,writeLen = 0;
  while (fh.available()){
    readLen = fh.read(clientBuf, BUFF_SIZE);
    if(readLen)  writeLen = dclient.write((const uint8_t *)clientBuf, readLen);
    if(readLen>0 && writeLen==0){
        ESP_LOGE(TAG, "Write buffer failed ..");
        dclient.stop();
        return 0;
    }
    writeBytes += writeLen;
    if(buffCount%5==0){
      ESP_LOGI(TAG, "Uploaded %0.0f%%", ( (double)writeBytes / fileSize)*100.0f );      
    }
    ++buffCount;
  }
  if(readLen<BUFF_SIZE) ESP_LOGI(TAG, "Uploaded 100%%");
  float uploadDur =  (millis() - uploadStart)/1024;  
  free(clientBuf);
  ESP_LOGI(TAG, "Done Uploaded in %3.1f sec",uploadDur); 
  dclient.stop();
  return 1; 
}

//Upload a single file or whole directory to ftp 
void uploadFolderOrFileFtp(String sdName, const bool removeAfterUpload, uint8_t levels){
  ESP_LOGI(TAG, "Ftp upload name: %s", sdName.c_str());
  if(sdName=="/"){
     ESP_LOGE(TAG, "Root is not allowed %s",sdName.c_str());  
     return;  
  }
  String ftpName = "";
  
  //Ftp connect
  if(!ftpConnect()){
    return; 
  }

  File root = SD_MMC.open(sdName);
  if (!root) {
      ESP_LOGE(TAG, "Failed to open: %s", sdName.c_str());
      ftpDisconnect();
      return;
  }  
    
  if (!root.isDirectory()) { //Upload a single file
      ESP_LOGI(TAG, "Uploading file: %s", sdName.c_str());    
 
      if(!ftpCheckDirPath(sdName, ftpName)){
          ESP_LOGE(TAG, "Create ftp dir path %s failed", sdName.c_str());
          ftpDisconnect();
          return;
      }
      if(!ftpStoreFile(ftpName, root)){
        ESP_LOGE(TAG, "Store file %s to ftp failed", ftpName.c_str());
        ftpDisconnect();
        return;
      }
      root.close();
      ESP_LOGV(TAG, "File closed");
      
      if(removeAfterUpload){
        ESP_LOGI(TAG, "Removing file %s", sdName.c_str()); 
        SD_MMC.remove(sdName.c_str());
      }    
  }else{  //Upload a whole directory
      ESP_LOGI(TAG, "Uploading directory: %s", sdName.c_str()); 
      File fh = root.openNextFile();      
      sdName = fh.name();
      
      if(!ftpCheckDirPath(sdName, ftpName)){
          ESP_LOGE(TAG, "Create ftp dir path %s failed", sdName.c_str());
          ftpDisconnect();
          return;
      }
        
      bool bUploadOK=false;
      while (fh) {
          sdName = fh.name();
          bUploadOK = false;
          if (fh.isDirectory()) {
              ESP_LOGI(TAG, "Sub directory: %s, Not uploading", sdName.c_str());
              /*if (levels) {
                std::string strfile(file.name());
                const char* f="TEST";
                doUploadFtp(f, removeAfterUpload, levels – 1);
              }*/
          } else {        
              byte bPos = sdName.lastIndexOf("/");
              String ftpName = sdName.substring(bPos+1);          
              ESP_LOGI(TAG, "Uploading sub sd file %s to %s", sdName.c_str(),ftpName.c_str()); 
              bUploadOK = ftpStoreFile(ftpName, fh);
              if(!bUploadOK){
                ESP_LOGE(TAG, "Store file %s to ftp failed", ftpName.c_str());
              }
          }
          //Remove if ok
          if(removeAfterUpload && bUploadOK){
            ESP_LOGI(TAG, "Removing file %s\n", sdName.c_str()); 
            SD_MMC.remove(sdName.c_str());
          }
          fh = root.openNextFile();
      }      
  }
  //Disconnect from ftp
  ftpDisconnect();  
}

static void taskUpload(void * parameter){
    String fname = (char *)parameter;
    ESP_LOGV(TAG, "Entering upload task with %s\n",fname.c_str());    
    uploadFolderOrFileFtp(fname,false,0);
    ESP_LOGV(TAG, "Ending uploadTask");
    vTaskDelete( NULL );  
}

void createUploadTask(const char* val){
    ESP_LOGV(TAG, "Starting upload task with %s\n",val);
    xTaskCreate(
        &taskUpload,       /* Task function. */
        "taskUpload",     /* String with name of task. */
        4096*2,           /* Stack size in bytes. */
        (void *)val,      /* Parameter passed as input of the task */
        1,                /* Priority of the task. */
        NULL);            /* Task handle. */
}
