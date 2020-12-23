//#ifndef MY_CONFIG
#define MY_CONFIG
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>                    
#include <Preferences.h>
#include "esp_log.h"


#define APP_NAME "ESP32-CAM_MJPEG"

//Wifi Station parameters
//If no default SSID value defined here
//will start an access point if no saved value found

char hostName[32] = ""; //Host name for ddns
char ST_SSID[32]  = ""; //Router ssid
char ST_Pass[64]  = ""; //Router passd

char ST_ip[16]  = ""; //Leave blank for dhcp
char ST_sn[16]  = "";
char ST_gw[16]  = "";
char ST_ns1[16] = "";
char ST_ns2[16] = "";

//Access point Config Portal SSID and Pass
#define ESP_getChipId() ((uint32_t)ESP.getEfuseMac())
String AP_SSID = "CAM_" + String(ESP_getChipId(), HEX);
char   AP_Pass[20] = "123456789";
char   AP_ip[16]  = ""; //Leave blank for 192.168.4.1
char   AP_sn[16]  = "";
char   AP_gw[16]  = "";

//Ftp server default params
char ftp_server[32] = "test.ftp.com";
char ftp_port[6]    = "21";
char ftp_user[32]   = "test";
char ftp_pass[32]   = "test";
char ftp_wd[64]     = "/home/user/";

//mjpeg2sd parameters
char timezone[64] = "GMT0BST,M3.5.0/01,M10.5.0/02";
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not
extern uint8_t FPS;
extern bool aviOn;                 
bool lampVal = false;
void controlLamp(bool lampVal);
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting

/*  Handle config nvs load & save and wifi start   */
DNSServer dnsAPServer;                      
Preferences pref;

bool resetConfig() {
  puts("Reseting config..");
  if (!pref.begin(APP_NAME, false)) {
    puts("Failed to open config.");
    return false;
  }
  // Remove all preferences under the opened namespace
  pref.clear();
  // Close the Preferences
  pref.end();
  return true;
}

bool saveConfig() {
  puts("Saving config.");
  if (!pref.begin(APP_NAME, false)) {
    puts("Failed to open config.");
    return false;
  }

  pref.putString("hostName", hostName);
  pref.putString("ST_SSID", ST_SSID);
  pref.putString("ST_Pass", ST_Pass);

  /* Not working if field ST_pass type="password"
    ESP_LOGI(TAG, "Save pass %s",ST_Pass);
  */
  pref.putString("ST_ip", ST_ip);
  pref.putString("ST_gw", ST_gw);
  pref.putString("ST_sn", ST_sn);
  pref.putString("ST_ns1", ST_ns1);
  pref.putString("ST_ns2", ST_ns2);

  pref.putString("timezone", timezone);
  pref.putUShort("framesize", fsizePtr);
  pref.putUChar("fps", FPS);
  pref.putUChar("minf", minSeconds);
  pref.putBool("doRecording", doRecording);
  pref.putFloat("motion", motionVal);
  pref.putBool("lamp", lampVal);
  pref.putBool("aviOn", aviOn);                              
  pref.putUChar("lswitch", nightSwitch);

  pref.putString("ftp_server", ftp_server);
  pref.putString("ftp_port", ftp_port);
  pref.putString("ftp_user", ftp_user);
  pref.putString("ftp_pass", ftp_pass);
  pref.putString("ftp_wd", ftp_wd);

  //Sensor settings
  sensor_t * s = esp_camera_sensor_get();
  pref.putBytes("camera_sensor", &s->status, sizeof(camera_status_t) );
  // Close the Preferences
  pref.end();
  return true;
}

bool loadConfig() {
  bool saveDefPrefs = false;
  //resetConfig();
  puts("Loading config..");
  AP_SSID.toUpperCase();
  if (!pref.begin(APP_NAME, false)) {
    puts("Failed to open config.");
    return false;
  }
  strcpy(hostName, pref.getString("hostName", String(hostName)).c_str());
  //Add default hostname
  if (strlen(hostName) < 1) {
    printf("Setting default hostname %s\n", hostName);
    strcpy(hostName, AP_SSID.c_str());
    //No nvs prefs yet. Save them at end
    saveDefPrefs = true;
  }
  
  // prevent empty saved SSID overwriting default
  if (strlen(pref.getString("ST_SSID").c_str()) > 0) {
    strcpy(ST_SSID, pref.getString("ST_SSID", String(ST_SSID)).c_str());
    strcpy(ST_Pass, pref.getString("ST_Pass", String(ST_Pass)).c_str());
  }

  ESP_LOGI(TAG, "Loaded ssid: %s pass: %s", String(ST_SSID).c_str(), String(ST_Pass).c_str());

  strcpy(ST_ip, pref.getString("ST_ip").c_str());
  strcpy(ST_gw, pref.getString("ST_gw").c_str());
  strcpy(ST_sn, pref.getString("ST_sn").c_str());
  strcpy(ST_ns1, pref.getString("ST_ns1").c_str());
  strcpy(ST_ns2, pref.getString("ST_ns2").c_str());

  fsizePtr = pref.getUShort("framesize", fsizePtr);
  FPS = pref.getUChar("fps", FPS);
  minSeconds = pref.getUChar("minf", minSeconds );
  doRecording = pref.getBool("doRecording", doRecording);
  aviOn = pref.getBool("aviOn", aviOn);                                       
  motionVal = pref.getFloat("motion", motionVal);
  lampVal = pref.getBool("lamp", lampVal);
  controlLamp(lampVal);
  nightSwitch = pref.getUChar("lswitch", nightSwitch);

  strcpy(timezone, pref.getString("timezone", String(timezone)).c_str());
  strcpy(ftp_server, pref.getString("ftp_server", String(ftp_server)).c_str());
  strcpy(ftp_port, pref.getString("ftp_port", String(ftp_port)).c_str());
  strcpy(ftp_user, pref.getString("ftp_user", String(ftp_user)).c_str());
  strcpy(ftp_pass, pref.getString("ftp_pass", String(ftp_pass)).c_str());
  strcpy(ftp_wd, pref.getString("ftp_wd", String(ftp_wd)).c_str());

  size_t schLen = pref.getBytesLength("camera_sensor");
  char buffer[schLen]; // prepare a buffer for the data
  schLen = pref.getBytes("camera_sensor", buffer, schLen);
  if (true || schLen < 1 || schLen % sizeof(camera_status_t)) { // simple check that data fits
    ESP_LOGE(TAG, "Camera sensor data is not correct size! get %u, size: %u",schLen,sizeof(camera_status_t));
    //return false;
  }else{
    printf("Setup camera_sensor, size get %u, def size: %u\n",schLen, sizeof(camera_status_t));
    camera_status_t * st = (camera_status_t *)buffer; // cast the bytes into a struct ptr
    sensor_t *s = esp_camera_sensor_get();
    s->set_ae_level(s,st->ae_level);
    s->set_aec2(s,st->aec2);
    s->set_aec_value(s,st->aec_value);
    s->set_agc_gain(s,st->agc_gain);
    s->set_awb_gain(s,st->awb_gain);
    s->set_bpc(s,st->bpc);
    s->set_brightness(s,st->brightness);
    s->set_colorbar(s,st->colorbar);
    s->set_contrast(s,st->contrast);
    s->set_dcw(s,st->dcw);
    s->set_denoise(s,st->denoise);
    s->set_exposure_ctrl(s,st->aec);
    s->set_framesize(s,st->framesize);
    s->set_gain_ctrl(s,st->agc);          
    s->set_gainceiling(s,(gainceiling_t)st->gainceiling);
    s->set_hmirror(s,st->hmirror);
    s->set_lenc(s,st->lenc);
    s->set_quality(s,st->quality);
    s->set_raw_gma(s,st->raw_gma);
    s->set_saturation(s,st->saturation);
    s->set_sharpness(s,st->sharpness);
    s->set_special_effect(s,st->special_effect);
    s->set_vflip(s,st->vflip);
    s->set_wb_mode(s,st->wb_mode);
    s->set_whitebal(s,st->awb);
    s->set_wpc(s,st->wpc);
  }
  // Close the Preferences
  pref.end();

  if (saveDefPrefs) {
    puts("Saving default config.");
    saveConfig();
  }
  return true;
}

String ipToString(IPAddress ip) {
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i  ? "." + String(ip[i]) : String(ip[i]);
  return s;
}
bool setWifiAP() {
  //Set access point if disabled
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP);
  else if (WiFi.getMode() == WIFI_STA) WiFi.mode(WIFI_AP_STA);

  //set static ip
 if(strlen(AP_ip)>1){
    IPAddress _ip,_gw,_sn,_ns1,_ns2;
    _ip.fromString(AP_ip);
    _gw.fromString(AP_gw);
    _sn.fromString(AP_sn);
    //set static ip
    WiFi.softAPConfig(_ip, _gw, _sn);
  } 
  ESP_LOGI(TAG, "Starting Access point with SSID %s", AP_SSID.c_str());
  WiFi.softAP(AP_SSID.c_str(), AP_Pass );
  ESP_LOGI(TAG, "Done. Connect to SSID: %s and navigate to http://%s", AP_SSID.c_str(), ipToString(WiFi.softAPIP()).c_str());
  /*//Start mdns for AP
    ESP_LOGI(TAG, "Starting ddns on port 53: %s", ipToString(WiFi.softAPIP()).c_str() );
    dnsAPServer.start(53, "*", WiFi.softAPIP());
  */
  return true;
}
void setupHost(){  //Mdns services   
  if (MDNS.begin(hostName) ) {
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "udp", 81);
    //MDNS.addService("ftp", "tcp", 21);    
    ESP_LOGI(TAG,"Mdns services http://%s Started.", hostName );
  } else {
    ESP_LOGE(TAG, "Mdns host name: %s Failed.", hostName);
  }
}
bool startWifi() {
  //No config found. Setup AP to create one
  if (!loadConfig()) return setWifiAP();
  WiFi.persistent(false); //prevent the flash storage WiFi credentials
  WiFi.setAutoReconnect(false); //Set whether module will attempt to reconnect to an access point in case it is disconnected
  WiFi.setAutoConnect(false);
  ESP_LOGV(TAG, "Starting wifi, exist mode: %i", WiFi.getMode() );
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_AP);
  else if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
  ESP_LOGV(TAG, "Setup wifi, mode: %i", WiFi.getMode() );
  //Setup mdns services
  setupHost();
  //Disconnect if already connected
  if (WiFi.status() == WL_CONNECTED) {
    ESP_LOGI(TAG, "Disconnecting from ssid: %s", String(WiFi.SSID()) );
    WiFi.disconnect();
    delay(1000);
    ESP_LOGV(TAG, "Disconnected from ssid: %s", String(WiFi.SSID()) );
  }
  //Set hostname
  ESP_LOGI(TAG, "Setting wifi hostname: %s", hostName);
  WiFi.setHostname(hostName);
  //set static ip
  if (strlen(ST_ip) > 1) {
    IPAddress _ip, _gw, _sn, _ns1, _ns2;
    _ip.fromString(ST_ip);
    _gw.fromString(ST_gw);
    _sn.fromString(ST_sn);
    _ns1.fromString(ST_ns1);
    _ns2.fromString(ST_ns2);
    //set static ip
    WiFi.config(_ip, _gw, _sn);
  }
  //
  if (strlen(ST_SSID) > 0) {
    //ESP_LOGI(TAG, "Got stored router credentials. Connecting to %s", ST_SSID);
    ESP_LOGI(TAG, "Got stored router credentials. Connecting to: %s with pass: %s", ST_SSID, ST_Pass);
  } else {
    ESP_LOGI(TAG, "No stored Credentials. Starting Access point.");
    //Start AP config portal
    return setWifiAP();
  }
  int tries = 3;
  uint8_t timeout;
  while (tries > 0) {
    int ret = 0;
    timeout = 40; // 40 * 200 ms = 8 sec time out
    WiFi.begin(ST_SSID, ST_Pass);
    ESP_LOGI(TAG, "ST waiting for connection. Try %i", tries);
    while ( ((ret = WiFi.status()) != WL_CONNECTED) && timeout ) {
      Serial.print(".");
      delay(200);
      Serial.flush();
      --timeout;
    }
    Serial.println(".");

    if (timeout > 0) {
      tries = 0;
    } else {
      tries--;
      WiFi.disconnect();
      delay(1000);
    }
  }

  if (timeout <= 0) {
    ESP_LOGE(TAG, "wifi ST timeout on connect. Failed.");
    return setWifiAP();
  }
  ESP_LOGI(TAG, "Connected! Navigate to 'http://%s to setup", WiFi.localIP().toString());
  return true;
}

//Check for Station disconnections and reboot if not connected for some seconds
//and not ap clients connected. 
unsigned long tmConn=millis();
unsigned long tmReboot=0;
void checkConnection(){
  //Reboot?
  if(tmReboot>0 && millis() - tmReboot > 15000 ){
    int apClients = WiFi.softAPgetStationNum();
    Serial.printf("Reboot.. Clients active: %i\n",apClients);
    if(apClients < 1 ) //Reboot if no clients
       ESP.restart();
    else
       tmReboot = 0; 
  }
  
  //Check for wifi station reconnection every 20 seconds
  if(WiFi.status() != WL_CONNECTED && millis() - tmConn > 20000){
    ESP_LOGI(TAG, "Wifi not connected, mode: %i, status: %i, ap clients: ", WiFi.getMode(), WiFi.status(), WiFi.softAPgetStationNum() );        
    tmConn = millis();   //Recheck
    tmReboot = millis(); //Reboot after 15 seconds      
  }
} 
