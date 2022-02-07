// Utilities to support wifi, NTP, preferences, remote logging, battery voltage

#include "myConfig.h"


/************************** NTP  **************************/

static bool timeSynchronized = false;
char timezone[64] = "GMT0BST,M3.5.0/01,M10.5.0/02"; 
static inline time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  // construct filename from date/time
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}

void getLocalNTP() {
  // get current time from NTP server and apply to ESP32
  const char* ntpServer = "pool.ntp.org";
  configTzTime(timezone, ntpServer);
  if (getEpoch() > 10000) {
    time_t currEpoch = getEpoch();
    char timeFormat[20];
    strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
    timeSynchronized = true;
    LOG_INF("Got current time from NTP: %s", timeFormat);
  }
  else LOG_WRN("Not yet synced with NTP");
}

void syncToBrowser(char *val) {
  if (timeSynchronized) return;
  
  //Synchronize clock to browser clock if no sync with NTP
  LOG_INF("Sync clock to: %s with tz:%s", val, timezone);
  struct tm now;
  getLocalTime(&now, 0);

  int Year, Month, Day, Hour, Minute, Second ;
  sscanf(val, "%d-%d-%dT%d:%d:%d", &Year, &Month, &Day, &Hour, &Minute, &Second);

  struct tm t;
  t.tm_year = Year - 1900;
  t.tm_mon  = Month - 1;    // Month, 0 - jan
  t.tm_mday = Day;          // Day of the month
  t.tm_hour = Hour;
  t.tm_min  = Minute;
  t.tm_sec  = Second;

  time_t t_of_day = mktime(&t);
  timeval epoch = {t_of_day, 0};
  struct timezone utc = {0, 0};
  settimeofday(&epoch, &utc);
  //setenv("TZ", timezone, 1);
//  Serial.print(&now, "Before sync: %B %d %Y %H:%M:%S (%A) ");
  getLocalTime(&now, 0);
//  Serial.println(&now, "After sync: %B %d %Y %H:%M:%S (%A)");
}

/************** Wifi **************/

// Wifi Station parameters
// If no default SSID value defined here
// will start an access point if no saved value found

#define ALLOW_AP false  // set to true to allow AP to startup if cannot reconnect to STA
char hostName[32] = ""; // Default Host name
char ST_SSID[32]  = ""; //Router ssid
char ST_Pass[64]  = ""; //Router passd

// leave following blank for dhcp
char ST_ip[16]  = ""; // Static IP
char ST_sn[16]  = ""; // subnet normally 255.255.255.0
char ST_gw[16]  = ""; // gateway to internet, normally router IP
char ST_ns1[16] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[16] = ""; // alternative DNS Server, can be blank

// Access point Config Portal SSID and Pass
#define AP_PASSWD "123456789" // change it, max 20 chars
String AP_SSID = "CAM_" + String((uint32_t)ESP.getEfuseMac(), HEX); 
char   AP_Pass[21] = AP_PASSWD;
char   AP_ip[16]  = ""; //Leave blank for 192.168.4.1
char   AP_sn[16]  = "";
char   AP_gw[16]  = "";

static void create_keepWiFiAliveTask();

static void setupHost(){  //Mdns services   
  if (MDNS.begin(hostName) ) {
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws", "udp", 81);
    //MDNS.addService("ftp", "tcp", 21);    
    LOG_INF("Mdns services http://%s Started.", hostName );
  } else {LOG_ERR("Mdns host name: %s Failed.", hostName);}
}

static bool setWifiAP() {
  // Set access point
  WiFi.mode(WIFI_AP);
  //set static ip
  if(strlen(AP_ip)>1){
    LOG_DBG("Setting ap static ip :%s, %s, %s", AP_ip,AP_gw,AP_sn);  
    IPAddress _ip,_gw,_sn,_ns1,_ns2;
    _ip.fromString(AP_ip);
    _gw.fromString(AP_gw);
    _sn.fromString(AP_sn);
    //set static ip
    WiFi.softAPConfig(_ip, _gw, _sn);
  } 
  LOG_DBG("Starting Access point with SSID %s", AP_SSID.c_str());
  WiFi.softAP(AP_SSID.c_str(), AP_Pass );
  LOG_INF("Connect to Access point with SSID: %s", AP_SSID.c_str()); 
  setupHost();
  return true;
}

bool startWifi() {
  WiFi.persistent(false); //prevent the flash storage WiFi credentials
  WiFi.setAutoReconnect(false); //Set whether module will attempt to reconnect to an access point in case it is disconnected
  WiFi.setAutoConnect(false);
  LOG_INF("Setting wifi hostname: %s", hostName);
  WiFi.setHostname(hostName);
  if (strlen(ST_SSID) > 0) { 
    LOG_INF("Got stored router credentials. Connecting to: %s", ST_SSID);
    if (strlen(ST_ip) > 1) {
      LOG_INF("Setting config static ip :%s, %s, %s, %s", ST_ip,ST_gw,ST_sn, ST_ns1);
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      _ip.fromString(ST_ip);
      _gw.fromString(ST_gw);
      _sn.fromString(ST_sn);
      _ns1.fromString(ST_ns1);
      _ns2.fromString(ST_ns2);
      //set static ip
      WiFi.config(_ip, _gw, _sn, _ns1); // need DNS for SNTP
    } else {LOG_INF("Getting ip from dhcp..");}
    WiFi.mode(WIFI_STA);
    create_keepWiFiAliveTask();
  } else {
    LOG_INF("No stored Credentials. Starting Access point.");
    // Start AP config portal
    return setWifiAP();
  }
  return true;
}

#define WIFI_TIMEOUT_MS (30 * 1000)
void keepWiFiAliveTask(void * parameters) {
  // Keep wifi station conection alive 
  WiFi.mode(WIFI_STA);
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      LOG_DBG("WIFI still connected to: %s, IP: %s, mode: %d, status: %d, ap clients: %d", ST_SSID, (WiFi.localIP()).toString().c_str(), WiFi.getMode(), WiFi.status(), WiFi.softAPgetStationNum() );
      delay(1000);
      if (!timeSynchronized) getLocalNTP();
      delay(WIFI_TIMEOUT_MS);
      continue;
    }
    LOG_DBG("Reconnecting to: %s, mode: %d,", ST_SSID, WiFi.getMode());
    WiFi.begin(ST_SSID, ST_Pass);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS)  {
      Serial.print(".");
      delay(500);
      Serial.flush();
    }
    Serial.println(".");
    if (WiFi.status() != WL_CONNECTED) {
      LOG_WRN("Failed to connect to %s, mode: %d", ST_SSID, WiFi.getMode());
      if (ALLOW_AP) {
        // Start AP config portal instead in case router details incorrect
        setWifiAP();
        vTaskDelete(NULL);
      } else delay(WIFI_TIMEOUT_MS);
    } else {
      LOG_INF("WIFI connected to %s, mode: %d, status: %d, with IP: %s", ST_SSID, WiFi.getMode(), WiFi.status(), (WiFi.localIP()).toString().c_str());
      setupHost();
    }
  }
}

static void create_keepWiFiAliveTask() {
  xTaskCreate(keepWiFiAliveTask, "keepWiFiAlive", 4096, NULL, 1, NULL);
}


/************************ preferences ************************/

// Store user configuration in flash for persistence
// gemi254 
Preferences pref;

bool resetConfig() {
  LOG_INF("Resetting config..");
  if (!pref.begin(APP_NAME, false)) {
    LOG_ERR("Failed to open config.");
    return false;
  }
  // Remove all preferences under the opened namespace
  pref.clear();
  // Close the Preferences
  pref.end();
  LOG_INF("Reseting config OK.\nRebooting..");
  ESP.restart();
  return true;
}

bool saveConfig() {
  LOG_INF("Saving config..");
  if (!pref.begin(APP_NAME, false)) {
    LOG_WRN("Failed to open config.");
    return false;
  }

  pref.putString("hostName", hostName);
  pref.putString("ST_SSID", ST_SSID);
  pref.putString("ST_Pass", ST_Pass);

  /* Not working if field ST_pass type="password"
    LOG_INF("Save pass %s",ST_Pass);
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
  pref.putBool("useMotion", useMotion); 
  pref.putBool("doRecording", doRecording);
  pref.putFloat("motion", motionVal);
  pref.putBool("lamp", lampVal);
  pref.putBool("aviOn", aviOn);
  pref.putUChar("micGain", micGain);  
  pref.putUChar("logMode", logMode);                                    
  pref.putBool("autoUpload", autoUpload);  
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
  LOG_INF("Loading config..");
  AP_SSID.toUpperCase();
  if (!pref.begin(APP_NAME, false)) {
    LOG_ERR("Failed to open config.");
    saveConfig();
    return false;
  }
  strcpy(hostName, pref.getString("hostName", String(hostName)).c_str());
  // Add default hostname
  if (strlen(hostName) < 1) {
    strcpy(hostName, AP_SSID.c_str());
    LOG_INF("Setting default hostname %s", hostName);
    //No nvs prefs yet. Save them at end
    saveDefPrefs = true;
  }
  if (strlen(pref.getString("ST_SSID").c_str()) > 0) {
    // only used stored values if non blank SSID has been set
    strcpy(ST_SSID, pref.getString("ST_SSID", String(ST_SSID)).c_str());
    strcpy(ST_Pass, pref.getString("ST_Pass", String(ST_Pass)).c_str());
  }

  LOG_INF("Loaded ssid: %s", String(ST_SSID).c_str());

  if (strlen(pref.getString("ST_ip").c_str()) > 0) {
    // only used stored values if non blank static IP has been stored
    strcpy(ST_ip, pref.getString("ST_ip",ST_ip).c_str());
    strcpy(ST_gw, pref.getString("ST_gw",ST_gw).c_str());
    strcpy(ST_sn, pref.getString("ST_sn",ST_sn).c_str());
    strcpy(ST_ns1, pref.getString("ST_ns1",ST_ns1).c_str());
    strcpy(ST_ns2, pref.getString("ST_ns2",ST_ns2).c_str());
  }
  fsizePtr = pref.getUShort("framesize", fsizePtr);
  FPS = pref.getUChar("fps", FPS);
  minSeconds = pref.getUChar("minf", minSeconds );
  doRecording = pref.getBool("doRecording", doRecording);
  aviOn = pref.getBool("aviOn", aviOn);
  micGain = pref.getUChar("micGain", micGain);  
  autoUpload = pref.getBool("autoUpload", autoUpload);
  logMode = pref.getUChar("logMode", logMode);
  //On telnet mode enable serial as wifi is not connected yet
  if(logMode == 2) logMode = 0;
  else remote_log_init();
  useMotion = pref.getUChar("useMotion", useMotion);
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
  if (schLen < 1 || schLen % sizeof(camera_status_t)) { // simple check that data fits
    LOG_DBG("Camera sensor data is not correct size! get %u, size: %u",schLen,sizeof(camera_status_t));
    //return false;
  }else{
    LOG_INF("Setup camera_sensor, size get %u, def size: %u",schLen, sizeof(camera_status_t));
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
    
    /* Not working Set fps and frame size on load time
    if(s->pixformat == PIXFORMAT_JPEG) {
        setFPSlookup(fsizePtr);
        setFPS(FPS);
        s->set_framesize(s, (framesize_t)fsizePtr);
    }*/
    
  }
  // Close the Preferences
  pref.end();

  if (saveDefPrefs) {
    LOG_INF("Saving default config.");
    saveConfig();
  }
  return true;
}


/************* Remote loggging **************/

/*
 * Log mode selection in user interface: 0-Serial, 1-log.txt, 2-telnet
 * 0 : log to serial monitor only
 * 1 : saves log on SD card. To download the log generated, either:
 *     - enter <ip address>/file?/Log/log.txt on the browser
 *     - select the file from web page 'Select folder / file' field
 * 2 : run telnet <ip address> 443 on a remote host
 * To close an SD or Telnet connection, select log mode 0
 */

#define LOG_FORMAT_BUF_LEN 512
#define LOG_PORT 443 // Define telnet port
#define WRITE_CACHE_CYCLE 128 //Auto resync log, On show log command it wll be auto sync
#define LOG_FILE_PATH "/sdcard" LOG_FILE_NAME
byte logMode = 0; // 0 - Disabled, log to serial port only, 1 - Internal log to sdcard file, 2 - Remote telnet on port 443
static int log_serv_sockfd = -1;
static int log_sockfd = -1;
static struct sockaddr_in log_serv_addr, log_cli_addr;
static char fmt_buf[LOG_FORMAT_BUF_LEN];
const char* log_file_name = "/sdcard/Log/log.txt";
FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;

static void remote_log_free_telnet() {
  if (log_sockfd != -1) {
    LOG_DBG("Sending telnet quit string Ctrl ]"); 
    send(log_sockfd, "^]\n\rquit\n\r", 10, 0);
    delay(100);
    close(log_sockfd);
    log_sockfd = -1;
  }
      
  if (log_serv_sockfd != -1) {
    if (close(log_serv_sockfd) != 0) LOG_ERR("Cannot close the socket");        
    log_serv_sockfd = -1;    
    LOG_INF("Closed telnet connection");
  }
}

static void remote_log_init_telnet() {
  LOG_DBG("Initialize telnet remote log");
  memset(&log_serv_addr, 0, sizeof(log_serv_addr));
  memset(&log_cli_addr, 0, sizeof(log_cli_addr));

  log_serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  log_serv_addr.sin_family = AF_INET;
  log_serv_addr.sin_port = htons(LOG_PORT);

  if ((log_serv_sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
    LOG_ERR("Failed to create socket, fd value: %d", log_serv_sockfd);
    return;
  }
  LOG_DBG("Socket FD is %d", log_serv_sockfd);

  int reuse_option = 1;
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse_option, sizeof(reuse_option)) < 0) {
    LOG_ERR("Failed to set reuse, returned %s", strerror(errno));
    remote_log_free_telnet();
    return;
  }

  if (bind(log_serv_sockfd, (struct sockaddr *)&log_serv_addr, sizeof(log_serv_addr)) < 0) {
    LOG_ERR("Failed to bind the port, reason: %s", strerror(errno));
    remote_log_free_telnet();
    return;
  }

  if (listen(log_serv_sockfd, 1) != 0) {
    LOG_ERR("Server failed to listen");
    return;
  }

  // Set timeout
  struct timeval timeout = {
    .tv_sec = 30,
    .tv_usec = 0
  };

  // Set receive timeout
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
      LOG_ERR("Setting receive timeout failed");
      remote_log_free_telnet();
      return;
  }

  // Set send timeout
  if (setsockopt(log_serv_sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
    LOG_ERR("Setting send timeout failed");
    remote_log_free_telnet();
    return;
  }
  LOG_INF("Server created, make telnet connection within 30 seconds");

  size_t cli_addr_len = sizeof(log_cli_addr);
  if ((log_sockfd = accept(log_serv_sockfd, (struct sockaddr *)&log_cli_addr, &cli_addr_len)) < 0) {
      LOG_WRN("Failed to accept, returned: %s", strerror(errno));
      remote_log_free_telnet();
      return;
  }
  LOG_INF("Established telnet connection");
}

void flush_log(bool andClose) {
  if (log_remote_fp != NULL) {
    counter_write=0;
    fflush(log_remote_fp);
    fsync(fileno(log_remote_fp));  
    if (andClose) {
      LOG_INF("Closing log SD file");
      //fflush(log_remote_fp);
      fclose(log_remote_fp);
      log_remote_fp = NULL;
    } else delay(500);
  }  
}
void reset_log(){
    flush_log(true); //Close log file
    if(SD_MMC.exists(LOG_FILE_NAME)) SD_MMC.remove(LOG_FILE_NAME);
    log_remote_fp = fopen(LOG_FILE_PATH, "at"); // a=append t=textmode "wb");
    if (log_remote_fp == NULL) LOG_ERR("Failed to reopen SD log file %s", LOG_FILE_PATH);
    else LOG_INF("Reseted log file..");    
}

static void remote_log_init_SD() {
  SD_MMC.mkdir(LOG_DIR);
  // Don't delete old log.. It will be overwritten..
  //if(false && SD_MMC.exists(LOG_FILE_NAME)) SD_MMC.remove(LOG_FILE_NAME); 
  // Open remote file
  log_remote_fp = NULL;
  log_remote_fp = fopen(LOG_FILE_PATH, "at"); // a=append t=textmode "wb");
  if (log_remote_fp == NULL) {LOG_ERR("Failed to open SD log file %s", LOG_FILE_PATH);}
  else {LOG_INF("Opened SD file for logging");}
}

void remote_log_init() {
  LOG_INF("Enabling logging mode %d", logMode);
  // close off any existing remote logging
  flush_log(true);
  remote_log_free_telnet();
  // setup required mode
  if (logMode == 1) remote_log_init_SD();
  if (logMode == 2) remote_log_init_telnet();
}

void logPrint(const char *fmtStr, ...) {
  va_list arglist;
  va_start(arglist, fmtStr);
  vprintf(fmtStr, arglist); // serial monitor
  if (log_remote_fp != NULL) { // log.txt
    vfprintf(log_remote_fp, fmtStr, arglist);
    /////printf("fsync'ing log file on SPIFFS (WRITE_CACHE_PERIOD=%u)\n", WRITE_CACHE_CYCLE);
    // periodic sync to SD !
    if (counter_write++ % WRITE_CACHE_CYCLE == 0) { fsync(fileno(log_remote_fp)); }
  }
  if (log_sockfd != -1) { // telnet
    int len = vsprintf((char*)fmt_buf, fmtStr, arglist);
    fmt_buf[len++] = '\r'; // in case terminal expects carriage return
    // Send the log message, or terminate connection if unsuccessful
    if (send(log_sockfd, fmt_buf, len, 0) < 0) remote_log_free_telnet();
  }
  va_end(arglist);
}

/******************* battery monitoring *********************/

// s60sc
// if pin 33 used as input for battery voltage, set VOLTAGE_DIVIDER value to be divisor 
// of input voltage from resistor divider, or 0 if battery voltage not being monitored
#define VOLTAGE_DIVIDER 0
#define BATT_PIN ADC1_CHANNEL_5 // ADC pin 33 for monitoring battery voltage
#define DEFAULT_VREF 1100 // if eFuse or two point not available on old ESPs
static esp_adc_cal_characteristics_t *adc_chars; // holds ADC characteristics
static const adc_atten_t ADCatten = ADC_ATTEN_DB_11; // attenuation level
static const adc_unit_t ADCunit = ADC_UNIT_1; // using ADC1
static const adc_bits_width_t ADCbits = ADC_WIDTH_BIT_11; // ADC bit resolution

void setupADC() {
  // Characterise ADC to generate voltage curve for battery monitoring 
  if (VOLTAGE_DIVIDER) {
    adc1_config_width(ADCbits);
    adc1_config_channel_atten(BATT_PIN, ADCatten);
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADCunit, ADCatten, ADCbits, DEFAULT_VREF, adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {LOG_INF("ADC characterised using eFuse Two Point Value");}
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {LOG_INF("ADC characterised using eFuse Vref");}
    else {LOG_INF("ADC characterised using Default Vref");}
  }
}

float battVoltage() {
  if (VOLTAGE_DIVIDER) {
    // get multiple readings of battery voltage from ADC pin and average
    // input battery voltage may need to be reduced by voltage divider resistors to keep it below 3V3.
    #define NO_OF_SAMPLES 16 // ADC multisampling
    uint32_t ADCsample = 0;
    for (int j = 0; j < NO_OF_SAMPLES; j++) ADCsample += adc1_get_raw(BATT_PIN); 
    ADCsample /= NO_OF_SAMPLES;
    // convert ADC averaged pin value to curve adjusted voltage in mV
    if (ADCsample > 0) ADCsample = esp_adc_cal_raw_to_voltage(ADCsample, adc_chars);
    return (float)ADCsample*VOLTAGE_DIVIDER/1000.0; // convert to battery volts
  } else return -1.0;
}
