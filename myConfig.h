//#ifndef MY_CONFIG
#define MY_CONFIG
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "esp_log.h"

static const char* TAG = "myConfig";
#define APP_NAME "ESP32-CAM_MJPED"

//Wifi Station parameters
//If no default SSID value defined here will start an access point
char hostName[20] ="";  //Host name for ddns
char ST_SSID[20]  ="";  //Router ssid
char ST_Pass[12]  ="";  //Router passd

char ST_ip[16]  = ""; //Leave blank for dhcp
char ST_sn[16]  = ""; 
char ST_gw[16]  = ""; 
char ST_ns1[16] = "";
char ST_ns2[16] = ""; 

//Access point Config Portal SSID and Pass 
#define ESP_getChipId() ((uint32_t)ESP.getEfuseMac())
String AP_SSID = "CAM_" + String(ESP_getChipId(), HEX);
char   AP_Pass[20]="123456789";

//Ftp server default params
char ftp_server[32] = "test.ftp.com";
char ftp_port[6]    = "21";
char ftp_user[32]   = "test";
char ftp_pass[32]   = "test";
char ftp_wd[64]     = "/home/user/";

//mjpeg2sd parameters 
const char* TIMEZONE="GMT0BST,M3.5.0/01,M10.5.0/02"; 
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not
extern uint8_t FPS;
bool lampVal = false;
void controlLamp(bool lampVal);
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting 

/*  Hanlde config nvs load & save and wifi start   */
DNSServer dnsAPServer;
Preferences pref;

bool saveConfig(){  
  ESP_LOGI(TAG, "Saving config.");
  if(!pref.begin(APP_NAME, false)){
      ESP_LOGE(TAG, "Failed to open config.");
      return false;
  }
 
  pref.putString("hostName",hostName);
  pref.putString("ST_SSID",ST_SSID);
  pref.putString("ST_Pass",ST_Pass);
  
  /* Not working if field ST_pass type="password"
  ESP_LOGI(TAG, "Save pass %s",ST_Pass);
  */
  pref.putString("ST_ip",ST_ip);
  pref.putString("ST_gw",ST_gw);
  pref.putString("ST_sn",ST_sn);
  pref.putString("ST_ns1",ST_ns1);
  pref.putString("ST_ns2",ST_ns2);
  
  pref.putUShort("framesize",fsizePtr);
  pref.putUChar("fps",FPS);
  pref.putUChar("minf",minSeconds);
  pref.putBool("doRecording",doRecording);
  pref.putFloat("motion",motionVal);
  pref.putBool("lamp",lampVal);
  pref.putUChar("lswitch",nightSwitch);
 
  pref.putString("ftp_server",ftp_server);
  pref.putString("ftp_port",ftp_port);
  pref.putString("ftp_user",ftp_user);
  pref.putString("ftp_pass",ftp_pass);
  pref.putString("ftp_wd",ftp_wd);
  
  //Sensor settings
  sensor_t * s = esp_camera_sensor_get();
  pref.putBytes("camera_sensor", s, sizeof(s));
  // Close the Preferences
  pref.end();
  return true;
}

bool loadConfig(){
  AP_SSID.toUpperCase();
  ESP_LOGI(TAG, "Loading config..");
  if(!pref.begin(APP_NAME, false)){
    ESP_LOGE(TAG, "Failed to open config.");
    return false;
  }   
  strcpy(hostName, pref.getString("hostName", String(hostName)).c_str());
  //Add default hostname
  if(strlen(hostName)<1){
    ESP_LOGE(TAG, "Setting default hostname %s",hostName);
    strcpy(hostName, AP_SSID.c_str());
  }
  strcpy(ST_SSID, pref.getString("ST_SSID", String(ST_SSID)).c_str());
  strcpy(ST_Pass, pref.getString("ST_Pass", String(ST_Pass)).c_str());  
  strcpy(ST_ip, pref.getString("ST_ip").c_str());
  strcpy(ST_gw, pref.getString("ST_gw").c_str());
  strcpy(ST_sn, pref.getString("ST_sn").c_str());
  strcpy(ST_ns1, pref.getString("ST_ns1").c_str());
  strcpy(ST_ns2, pref.getString("ST_ns2").c_str());
  
  fsizePtr = pref.getUShort("framesize", fsizePtr);
  FPS = pref.getUShort("fps", FPS);  
  minSeconds = pref.getUChar("minf",minSeconds );
  doRecording = pref.getBool("doRecording",doRecording);
  motionVal = pref.getFloat("motion",motionVal);
  lampVal = pref.getBool("lamp",lampVal);
  controlLamp(lampVal);
  nightSwitch = pref.getUChar("lswitch",nightSwitch);
  
  strcpy(ftp_server, pref.getString("ftp_server", String(ftp_server)).c_str());
  strcpy(ftp_port, pref.getString("ftp_port", String(ftp_port)).c_str());
  strcpy(ftp_user, pref.getString("ftp_user", String(ftp_user)).c_str());
  strcpy(ftp_pass, pref.getString("ftp_pass", String(ftp_pass)).c_str());
  strcpy(ftp_wd, pref.getString("ftp_wd", String(ftp_wd)).c_str());
  
  /*
  size_t schLen = pref.getBytesLength("camera_sensor");
  char buffer[schLen]; // prepare a buffer for the data  
  pref.getBytes("camera_sensor", buffer, schLen);  
  if (schLen % sizeof(sensor_t)) { // simple check that data fits
    log_e("Data camera_sensor is not correct size!");
    //return false;
  }
  sensor_t * s = (sensor_t *)buffer; // cast the bytes into a struct ptr  
  //Todo setup camera with loaded settings
  */
  // Close the Preferences
  pref.end();
  return true;  
}

bool resetConfig(){
  ESP_LOGI(TAG, "Reseting config..");
  if(!pref.begin(APP_NAME, false)){
    ESP_LOGE(TAG, "Failed to open config.");
    return false;
  }
  // Remove all preferences under the opened namespace
  pref.clear();
  // Close the Preferences
  pref.end();
}

String ipToString(IPAddress ip){
  String s="";
  for (int i=0; i<4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
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
  ESP_LOGI(TAG, "Done. Connect to SSID: %s and navigate to http://%s", AP_SSID.c_str(), ipToString(WiFi.softAPIP()).c_str());   
  /*//Start mdns for AP
  ESP_LOGI(TAG, "Starting ddns on port 53: %s", ipToString(WiFi.softAPIP()).c_str() );
  dnsAPServer.start(53, "*", WiFi.softAPIP());
  */
  return true;
}

bool startWifi() {  
  //No config found. Setup AP to create one
  if(!loadConfig()) return setWifiAP();
  ESP_LOGV(TAG, "Starting wifi, mode:" + String(WiFi.getMode()) + "");   
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP);
  else if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  ESP_LOGV(TAG, "Setup wifi mode:" + String(WiFi.getMode()) + "");
  
  //Disconnect if already connected
  if(WiFi.status() == WL_CONNECTED){
    ESP_LOGI(TAG, "Disconnecting from ssid: %s", String(WiFi.SSID()) );
    WiFi.disconnect();
    delay(1000);
    ESP_LOGV(TAG, "Disconnected from ssid: %s", String(WiFi.SSID()) );
  }
  //Set hostname
  ESP_LOGI(TAG, "Setting wifi hostname: %s", hostName);
  WiFi.setHostname(hostName);
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
     //ESP_LOGI(TAG, "Got stored router credentials. Connecting to %s", ST_SSID); 
     ESP_LOGI(TAG, "Got stored router credentials. Connecting to: %s with pass: %s", ST_SSID,ST_Pass); 
  } else {
     ESP_LOGI(TAG, "No stored Credentials. Starting Access point.");
     //Start AP config portal
     return setWifiAP();
  }
  int tries=3;
  uint8_t timeout;
  while(tries>0){
    int ret=0;
    timeout = 40; // 40 * 200 ms = 8 sec time out  
    WiFi.begin(ST_SSID, ST_Pass);
    ESP_LOGI(TAG, "ST waiting for connection. Try %i", tries);
    while ( ((ret = WiFi.status()) != WL_CONNECTED) && timeout ){    
      Serial.print(".");
      delay(200);
      Serial.flush();
      --timeout;
    }
    Serial.println(".");
    
    if(timeout>0){
      tries=0;
    }else{ 
      tries--; 
      WiFi.disconnect();  
      delay(1000);
    }
  }
  
  if(timeout<=0){
    ESP_LOGE(TAG, "wifi ST timeout on connect. Failed.");
    return setWifiAP();  
  }
  ESP_LOGI(TAG, "Connected! Navigate to 'http://%s to setup",WiFi.localIP().toString());  
  return true;
}
