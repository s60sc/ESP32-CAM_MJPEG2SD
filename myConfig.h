//#ifndef MY_CONFIG
#define MY_CONFIG
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

#include "esp_log.h"
static const char* TAG = "myConfig";

//Wifi Station parameters
//If no default SSID value defined here will start an access point
char ST_SSID[20]="";  //Router ssid
char ST_Pass[12]="";  //Router passd
char ST_ip[16]  = ""; //Leave blank for dhcp
char ST_sn[16]  = ""; 
char ST_gw[16]  = ""; 
char ST_ns1[16] = "";
char ST_ns2[16] = ""; 

char hostName[20];
#define ESP_getChipId() ((uint32_t)ESP.getEfuseMac())
// SSID and Pass for Access point Config Portal
String AP_SSID = "CAM_" + String(ESP_getChipId(), HEX);
char   AP_Pass[20]="123456789";

//Ftp server default params
char ftp_server[32] = "test.ftp.com";
char ftp_port[6]    = "21";
char ftp_user[32]   = "test";
char ftp_pass[32]   = "test";
char ftp_wd[64]     = "/home/user/";

//mjpeg2sd parameters 
//Time zone
const char* TIMEZONE="GMT0BST,M3.5.0/01,M10.5.0/02"; 
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not
uint8_t setFPS(uint8_t val);
bool lampVal = false;
void controlLamp(bool lampVal);
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting 

char configFileName[] = "/config.json";

bool saveConfig(){    
    ESP_LOGI(TAG, "Saving config file.");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();    
    json["hostName"] = hostName;
    json["ST_SSID"] = ST_SSID;
    json["ST_Pass"] = ST_Pass;
    json["ST_ip"] = ST_ip;
    json["ST_gw"] = ST_gw;    
    json["ST_sn"] = ST_sn;
    json["ST_ns1"] = ST_ns1;
    json["ST_ns2"] = ST_ns2;

    json["framesize"] = fsizePtr;
    json["fps"] = setFPS(0); // get FPS value
    json["minf"] = minSeconds;
    json["record"] = doRecording? "1" : "0";
    json["motion"] = String(motionVal);
    json["lamp"] = lampVal? "1" : "0";
    json["lswitch"] = nightSwitch;

    json["ftp_server"] = ftp_server;    
    json["ftp_port"] = ftp_port;
    json["ftp_user"] = ftp_user;
    json["ftp_pass"] = ftp_pass;
    json["ftp_wd"] = ftp_wd;
 
    File configFile = SD_MMC.open("/config.json", "w");
    if (!configFile) {
      ESP_LOGE(TAG, "Failed to open config file  /config.json for writing.");
      return false;
    }
    json.printTo(Serial);
    Serial.println();
    json.printTo(configFile);
    configFile.close();
    Serial.println("Saved");
    return true;
}

bool loadConfig(){
  strcpy(hostName,AP_SSID.c_str());  
  ESP_LOGI(TAG, "Loading /config.json");
  //SD_MMC.remove("/config.json");
  if (SD_MMC.exists("/config.json")) {
    //file exists, reading and loading
    File configFile = SD_MMC.open("/config.json", "r");
    if (configFile) {
      ESP_LOGI(TAG, "Opened config file.");
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      configFile.close();
      if (json.success()) {
        ESP_LOGI(TAG, "Parsed json");
        json.printTo(Serial);
        Serial.println("");
        strcpy(hostName, json["hostName"]);
        strcpy(ST_SSID, json["ST_SSID"]);
        strcpy(ST_Pass, json["ST_Pass"]);        
        strcpy(ST_ip, json["ST_ip"]);
        strcpy(ST_sn, json["ST_sn"]);
        strcpy(ST_gw, json["ST_gw"]);
        strcpy(ST_ns1, json["ST_ns1"]);
        strcpy(ST_ns2, json["ST_ns2"]);
        
        fsizePtr = String(json["framesize"].asString()).toInt();
        
        //When called before prepMjpeg creates an exeption
        //setFPS(  String(json["fps"].asString()).toInt() );
        
        minSeconds = String(json["minf"].asString()).toInt();
        doRecording = json["record"]=="1" ? true : false;
        motionVal = String(json["motion"].asString()).toFloat();
        nightSwitch = String(json["lswitch"].asString()).toInt();            
        lampVal = json["lamp"]=="1" ? true : false;         
        controlLamp(lampVal);
        
        strcpy(ftp_server, json["ftp_server"]);
        strcpy(ftp_port, json["ftp_port"]);
        strcpy(ftp_user, json["ftp_user"]);
        strcpy(ftp_pass, json["ftp_pass"]);
        strcpy(ftp_wd, json["ftp_wd"]);        
      } else {
        ESP_LOGE(TAG, "Failed to parse json config");
        char buff[2048];
        File configFile = SD_MMC.open("/config.json", "r");
        size_t readLen = configFile.read((uint8_t *)buff, 2048);
        Serial.printf("Readed size %i\n",readLen);
        for(size_t i=0; i<readLen; ++i){
          Serial.print(buff[i]);
        }
        Serial.println();
        return false;
      }
    }
  }else{
    ESP_LOGI(TAG, "No config found in SD card.");
    if(strlen(ST_SSID)>1){
      ESP_LOGI(TAG, "Using hardcoded values %s", ST_SSID);
      return true;
    }
    return false;
  }
  return true;
}

void resetConfig(){    
  ESP_LOGI(TAG, "Reseting config..");
  if (!SD_MMC.remove("/config.json")) {
      ESP_LOGE(TAG, "Removing /config.json FAILED");
  }
  ESP.restart();
  delay(1000);
}
bool setWifiAP(){
 //Set access point if disabled
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP);
  else if (WiFi.getMode() == WIFI_STA) WiFi.mode(WIFI_AP_STA);
  
  /*//set static ip
  if(strlen(ST_ip)>1){
    IPAddress _ip,_gw,_sn,_ns1,_ns2;
    _ip.fromString(ST_ip);
    _gw.fromString(ST_gw);
    _sn.fromString(ST_sn);
    _ns1.fromString(ST_ns1);
    _ns2.fromString(ST_ns2);
    //set static ip
    WiFi.softAPConfig(ip, gateway, subnet);   
  } */
  ESP_LOGI(TAG, "Starting Access point with SSID %s", AP_SSID.c_str()); 
  WiFi.softAP(AP_SSID.c_str(), AP_Pass );     
  ESP_LOGI(TAG, "Done. Connect to SSID: %s to setup", AP_SSID.c_str()); 
  //WiFi.localIP().toString not working
  //ESP_LOGI(TAG, "Done. Connect to SSID: %s and navigate to http://%s", AP_SSID.c_str(), WiFi.localIP().toString()); 
  return true;
}

bool startWifi() {  
  //No config found. Setup AP to create one
  if(!loadConfig()) return setWifiAP();
  ESP_LOGV(TAG, "Starting wifi, mode:" + String(WiFi.getMode()) + "\n");   
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP);
  else if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  ESP_LOGV(TAG, "Setup wifi mode:" + String(WiFi.getMode()) + "\n");

  //Disconnect if already connected
  if(WiFi.status() == WL_CONNECTED){
    ESP_LOGI(TAG, "Disconnecting from ssid: %s\n",String(WiFi.SSID()) );
    WiFi.disconnect();
    delay(1000);
    ESP_LOGV(TAG, "Disconnected from ssid: %s\n" ,String(WiFi.SSID()) );
  }

  //set static ip
  if(strlen(ST_ip)>1){
    IPAddress _ip,_gw,_sn,_ns1,_ns2;
    _ip.fromString(ST_ip);
    _gw.fromString(ST_gw);
    _sn.fromString(ST_sn);
    _ns1.fromString(ST_ns1);
    _ns2.fromString(ST_ns2);
    //set static ip
    WiFi.config(_ip, _gw, _sn);    
  } 
  //
  if (strlen(ST_SSID)>0) {
     ESP_LOGI(TAG, "Got stored router credentials. Connecting to %s", ST_SSID); 
     WiFi.begin(ST_SSID, ST_Pass);
  } else {
     ESP_LOGI(TAG, "No stored Credentials. Starting Access point.");
     //Start AP config portal
     return setWifiAP();
  }
 
  int ret=0;
  uint8_t timeout = 40; // 40 * 200 ms = 8 sec time out  
  ESP_LOGI(TAG, "ST waiting for connection..");
  while ( ((ret = WiFi.status()) != WL_CONNECTED) && timeout ){    
    Serial.print(".");
    delay(200);
    Serial.flush();
    --timeout;
  }
  
  if(timeout==0){
    ESP_LOGE(TAG, "wifi ST timeout on connect. Failed.");
    return false;  
  }
  ESP_LOGI(TAG, "Connected! Navigate to 'http://%s to setup",WiFi.localIP().toString());  
  return true;
}
