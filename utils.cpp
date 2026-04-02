
// General utilities not specific to this app to support:
// - wifi / ethernet
// - NTP
// - base64 encoding
// - device startup & sleep
//
// s60sc 2021, 2023, 2025
// some functions based on code contributed by gemi254

#include "appGlobals.h"

bool timeSynchronized = false;
bool monitorOpen = true;
bool dataFilesChecked = false;
size_t alertBufferSize = 0;
size_t maxAlertBuffSize = 32 * 1024;
byte* alertBuffer = NULL; // buffer for telegram / smtp alert image
static void printPartitionTable();
int wakePin; // if wakeUse is true
int wakeLevel; // if wakeUse is true
bool wakeUse = false; // true to allow app to sleep and wake
char* jsonBuff = NULL;
char portFwd[6] = "";
UBaseType_t STACK_MEM; // allow some task stacks to use psram if available
float latLon[2];

/************************** Network (WiFi/Ethernet) **************************/

#include <esp_task_wdt.h>
 
/** Do not hard code anything below here unless you know what you are doing **/
/** Use the web interface to configure wifi settings **/

char hostName[MAX_HOST_LEN] = ""; // Default Host name
char ST_SSID[MAX_HOST_LEN]  = ""; //Default router ssid
char ST_Pass[MAX_PWD_LEN] = ""; //Default router passd

// leave following blank for dhcp
char ST_ip[MAX_IP_LEN]  = ""; // Static IP
char ST_sn[MAX_IP_LEN]  = ""; // subnet normally 255.255.255.0
char ST_gw[MAX_IP_LEN]  = ""; // gateway to internet, normally router IP
char ST_ns1[MAX_IP_LEN] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[MAX_IP_LEN] = ""; // alternative DNS Server, can be blank

// Access point Config Portal SSID and Pass
char AP_SSID[MAX_HOST_LEN] = "";
char AP_Pass[MAX_PWD_LEN] = "";
char AP_ip[MAX_IP_LEN]  = ""; // Leave blank to use 192.168.4.1
char AP_sn[MAX_IP_LEN]  = "";
char AP_gw[MAX_IP_LEN]  = "";

// SPI pins for Ethernet
int ethCS = -1; // W5500 chip select / LAN8720 MDC
int ethInt = -1; // W5500 interrupt / LAN8720 MDIO
int ethRst = -1; // W5500 reset / LAN8720 POWER
int ethSclk = -1; // W5500 SPI clock / LAN8720 CLOCK
int ethMiso = -1; // W5500 SPI data pin
int ethMosi = -1; // W5500 SPI data pin

// basic HTTP Authentication access to web page
char Auth_Name[MAX_HOST_LEN] = ""; 
char Auth_Pass[MAX_PWD_LEN] = "";

int responseTimeoutSecs = 10; // time to wait for FTP or SMTP response
bool allowAP = true;  // set to true to allow AP to startup if cannot connect to STA (router)
uint32_t wifiTimeoutSecs = 30; // how often to check wifi status
static bool APstarted = false;
esp_ping_handle_t pingHandle = NULL;
bool usePing = true;

static void startPing();
static bool getLocalNTP();

int netMode = 0; // 0=WiFi only, 1=Ethernet only, 2=Ethernet+AP

// LAN8720
#define ETH_PHY_ADDR  0 
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN // external clock from crystal oscillator

static void setupMdnsHost() {  
  // set up MDNS service 
  char mdnsName[MAX_IP_LEN]; // max mdns host name length
  snprintf(mdnsName, MAX_IP_LEN, "%.*s", MAX_IP_LEN - 1, hostName);
  MDNS.end();
  if (MDNS.begin(mdnsName)) {
    // Add service to MDNS
    useHttps ? MDNS.addService("https", "tcp", HTTPS_PORT) : MDNS.addService("http", "tcp", HTTP_PORT);
    MDNS.addService("ws", "udp", 83);
    MDNS.addService("ftp", "tcp", 21);    
    LOG_INF("mDNS service: http%s://%s.local", useHttps ? "s" : "", mdnsName);
  } else LOG_WRN("mDNS host: %s Failed", mdnsName);
  debugMemory("setupMdnsHost");
}

static const char* wifiStatusStr(wl_status_t wlStat) {
  switch (wlStat) {
    case WL_NO_SHIELD: return "wifi not initialised";
    case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
    case WL_NO_SSID_AVAIL: return "not available, use AP";
    case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
    case WL_CONNECTED: return "WL_CONNECTED";
    case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
    case WL_DISCONNECTED: return "unable to connect";  
    case WL_STOPPED: return "wifi stopped";
    default: return "Invalid WiFi.status";
  }
}

const char* getEncType(int ssidIndex) {
  switch (WiFi.encryptionType(ssidIndex)) {
    case (WIFI_AUTH_OPEN): return "Open";
    case (WIFI_AUTH_WEP): return "WEP";
    case (WIFI_AUTH_WPA_PSK): return "WPA_PSK";
    case (WIFI_AUTH_WPA2_PSK): return "WPA2_PSK";
    case (WIFI_AUTH_WPA_WPA2_PSK): return "WPA_WPA2_PSK";
    case (WIFI_AUTH_WPA2_ENTERPRISE): return "WPA2_ENTERPRISE";
    case (WIFI_AUTH_MAX): return "AUTH_MAX";
    default: return "Not listed";
  }
}

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  // callback to report on network events
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY: break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE: break;
    case ARDUINO_EVENT_WIFI_STA_START: LOG_INF("Wifi Station started, connecting to: %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_STA_STOP: LOG_INF("Wifi Station stopped %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_AP_START: {
      if (strlen(AP_SSID) && !strcmp(WiFi.AP.SSID().c_str(), AP_SSID)) {
        LOG_INF("Wifi AP SSID: %s started, use 'http%s://%s' to connect", WiFi.AP.SSID().c_str(), useHttps ? "s" : "", formatIPstr(true));
        APstarted = true;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STOP: {
      if (!strcmp(WiFi.AP.SSID().c_str(), AP_SSID)) {
        LOG_INF("Wifi AP stopped: %s", AP_SSID);
        APstarted = false;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: LOG_INF("Wifi Station IP, use '%s://%s' to connect", useHttps ? "https" : "http", formatIPstr()); break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: LOG_INF("Wifi Station lost IP"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: LOG_INF("WiFi Station connection to %s, using hostname: %s", ST_SSID, hostName); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: LOG_INF("WiFi Station disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: LOG_INF("WiFi AP client connection"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: LOG_INF("WiFi AP client disconnection"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6: LOG_INF("AP interface V6 IP addr is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6: LOG_INF("Station interface V6 IP addr is preferred"); break;

    case ARDUINO_EVENT_ETH_START: LOG_INF("Ethernet started, speed %uMHz", ETH.linkSpeed()); break;
    case ARDUINO_EVENT_ETH_CONNECTED: LOG_INF("Ethernet connected, MAC: %s", ETH.macAddress().c_str()); break;
    case ARDUINO_EVENT_ETH_STOP: LOG_INF("Ethernet Stopped"); break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      LOG_INF("Ethernet IP, use '%s://%s' to connect", useHttps ? "https" : "http", formatIPstr()); 
      if (netMode == 2) WiFi.AP.enableNAPT(true);
      break;
    }
    case ARDUINO_EVENT_ETH_DISCONNECTED: {
      LOG_INF("Ethernet disconnected");
      if (netMode == 2) WiFi.AP.enableNAPT(false);
      break;
    }
    case ARDUINO_EVENT_ETH_LOST_IP: {
      LOG_INF("Ethernet lost IP");
      if (netMode == 2) WiFi.AP.enableNAPT(false);
      break;
    }
    default: LOG_WRN("Unhandled network event %d", event); break;
  }
}

static void setWifiAP() {
  if (!APstarted) {
    WiFi.AP.begin();
    // Set access point with static ip if provided
    if (strlen(AP_ip) > 1) {
      LOG_INF("Set AP static IP :%s, %s, %s", AP_ip, AP_gw, AP_sn);  
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      _ip.fromString(AP_ip);
      _gw.fromString(AP_gw);
      _sn.fromString(AP_sn);
      // set static ip
      WiFi.AP.config(_ip, _gw, _sn);
    } 
    WiFi.AP.create(AP_SSID, AP_Pass);
    debugMemory("setWifiAP");
  }
}

static void setWifiSTA() {
  // set station with static ip if provided
  if (strlen(ST_ip) > 1) {
    IPAddress _ip, _gw, _sn, _ns1, _ns2;
    if (!_ip.fromString(ST_ip)) LOG_WRN("Failed to parse IP: %s", ST_ip);
    else {
      _ip.fromString(ST_ip);
      _gw.fromString(ST_gw);
      _sn.fromString(ST_sn);
      _ns1.fromString(ST_ns1);
      _ns2.fromString(ST_ns2);
      // set static ip
      WiFi.STA.config(_ip, _gw, _sn, _ns1); // need DNS for SNTP
      LOG_INF("Wifi Station set static IP");
    } 
  } else LOG_INF("Wifi Station IP from DHCP");
  WiFi.STA.enableIPv6(USE_IP6); 
  WiFi.STA.begin();
  WiFi.STA.connect(ST_SSID, ST_Pass);
  debugMemory("setWifiSTA");
}

static void predefEthPins() {
  // set board specific pins if defined
#if defined(ETH_CS)
  char ethPin[3];
  sprintf(ethPin, "%d", ETH_CS);
  updateStatus("ethCS", ethPin);
  sprintf(ethPin, "%d", ETH_INT);
  updateStatus("ethInt", ethPin);
  sprintf(ethPin, "%d", ETH_RST);
  updateStatus("ethRst", ethPin);
  sprintf(ethPin, "%d", ETH_SCLK);
  updateStatus("ethSclk", ethPin);
  sprintf(ethPin, "%d", ETH_MISO);
  updateStatus("ethMiso", ethPin);
  sprintf(ethPin, "%d", ETH_MOSI);
  updateStatus("ethMosi", ethPin);
#endif
}

static bool startEth(bool firstcall) {
  // Initialize Ethernet (W5500) via SPI, only viable on ESP32-S3 board
  // Internal on ESP32-S3-ETH board, or use separate external board
  if (ethCS != -1) {
    if (!firstcall) {
      // reset after ping failure
      ETH.end();
      // Give PHY time to fully de-assert
      unsigned long phySettle = millis() + 5000; // 5 secs
      while (millis() < phySettle) yield();
    }
#if CONFIG_IDF_TARGET_ESP32S3
    if (!ETH.begin(ETH_PHY_W5500,
                   ETH_PHY_ADDR_AUTO,
                   ethCS,
                   ethInt,
                   ethRst,
                   SPI2_HOST,
                   ethSclk,
                   ethMiso,
                   ethMosi,
                   ETH_PHY_SPI_FREQ_MHZ)) { 
      LOG_WRN("Ethernet W5500 init failed");
      return false;
    }
#endif
#if CONFIG_IDF_TARGET_ESP32
 #ifdef ISCAM
    LOG_WRN("Insufficient pins for Ethernet on ESP32");
    netMode = 0;
    return false;
 #else
    // RMII uses predefined pins 19, 21, 22, 25, 26, 27
    if (!ETH.begin(ETH_PHY_LAN8720,
                   ETH_PHY_ADDR,
                   ethCS,  // LAN8720 MDC 
                   ethInt, // LAN8720 MDIO
                   ethRst, // LAN8720 POWER
                   ETH_CLK_MODE)) { 
      LOG_WRN("Ethernet LAN8720 init failed");
      return false;
    }
 #endif
#endif

    // Apply static IP to Ethernet if configured in existing fields
    if (strlen(ST_ip) > 1) {
      IPAddress _ip, _gw, _sn, _ns1, _ns2;
      if (_ip.fromString(ST_ip)) {
        _gw.fromString(ST_gw);
        _sn.fromString(ST_sn);
        _ns1.fromString(ST_ns1);
        _ns2.fromString(ST_ns2);
        ETH.config(_ip, _gw, _sn, _ns1, _ns2);
        LOG_INF("Ethernet set static IP");
      } else LOG_WRN("Failed to parse Ethernet static IP: %s", ST_ip);
    }
  } else {
    LOG_WRN("Ethernet pins not defined");
    return false;
  }

  // wait for link and DHCP or static assignment
  uint32_t startAttemptTime = millis();
  while (!ETH.linkUp() && millis() - startAttemptTime < 8000) delay(100);
  if (!ETH.linkUp()) LOG_WRN("Ethernet link not up");
  startAttemptTime = millis();
  while (!ETH.localIP() && millis() - startAttemptTime < 8000) delay(100);
  if (!ETH.localIP()) LOG_WRN("Ethernet no IP yet");
  setupMdnsHost();
  if (pingHandle == NULL) startPing();
  return ETH.linkUp();
}

static bool startWifi(bool firstcall = true) {
  // start wifi station (and wifi AP if allowed or station not defined)
  if (firstcall) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false); // prevent the flash storage WiFi credentials
    WiFi.STA.setAutoReconnect(false); // Set whether module will attempt to reconnect to an access point in case it is disconnected
    WiFi.AP.clear();
    WiFi.AP.end(); // kill rogue AP on startup
    WiFi.STA.setHostname(hostName);
    delay(100);
  }
  
  wl_status_t wlStat = WL_NO_SSID_AVAIL;
  if (netMode == 0) {
    // connect to Wifi station
    setWifiSTA();
    uint32_t startAttemptTime = millis();
    // Stop trying on failure timeout, will try to reconnect later by ping
    wlStat = WL_NO_SSID_AVAIL;
    if (strlen(ST_SSID)) {
      while (wlStat = WiFi.STA.status(), wlStat != WL_CONNECTED && millis() - startAttemptTime < 5000)  {
        LOG_SEND(".");
        delay(500);
      }
    }
    // show stats of requested SSID
    int numNetworks = WiFi.scanNetworks();
    for (int i=0; i < numNetworks; i++) {
      if (!strcmp(WiFi.SSID(i).c_str(), ST_SSID))
        LOG_INF("Wifi stats for %s - signal strength: %ld dBm; Encryption: %s; channel: %ld",  ST_SSID, WiFi.RSSI(i), getEncType(i), WiFi.channel(i));
    }
    if (wlStat != WL_CONNECTED) LOG_WRN("SSID %s not connected %s", ST_SSID, wifiStatusStr(wlStat));
  }
  
  if (wlStat == WL_NO_SSID_AVAIL || allowAP) setWifiAP(); // AP allowed if no Station SSID eg on first time use 
#if CONFIG_IDF_TARGET_ESP32S3
  if (netMode == 0) setupMdnsHost(); // not on ESP32 as uses 6k of heap
#endif
  if (pingHandle == NULL) startPing();
  return wlStat == WL_CONNECTED ? true : false;
}

bool startNetwork(bool firstcall) {
  // start WiFi, Ethernet, Eth+AP by config
  bool res = false;
  if (firstcall) Network.onEvent(onNetEvent);
  predefEthPins();
  if (netMode > 0) {
    // Ethernet or Eth+AP
    if (startEth(firstcall)) {
      if (netMode == 1) {
        // Quiet mode: stop WiFi/BLE radios for RF silence
        WiFi.mode(WIFI_OFF);
#ifdef APP_BT_ENABLED
        if (btStarted()) btStop();
#endif
        res = true;
      }
    } else {
      LOG_WRN("Ethernet start failed, falling back to WiFi");
      ETH.end();
      WiFi.AP.enableNAPT(false);
      netMode = 0;
    }
  }
  // Wifi only / Eth fail / Eth + AP
  if (netMode == 2) {
    WiFi.AP.enableNAPT(true);
    allowAP = true;
  }
  // connect wifi STA, or AP if router details not available
  if (!res) startWifi(firstcall);
  getExtIP();
  res = startWebServer(); 
#ifdef DEV_ONLY
  devCheck();
#endif
  return res;
}

static IPAddress netLocalIP() { return (netMode > 0) ? ETH.localIP() : WiFi.STA.localIP(); }
static IPAddress netGatewayIP() { return (netMode > 0) ? ETH.gatewayIP() : WiFi.STA.gatewayIP(); }
String netMacAddress() { return (netMode > 0) ? ETH.macAddress().c_str() : WiFi.STA.macAddress().c_str(); }
int netRSSI() { return (netMode == 1) ? 0 : WiFi.STA.RSSI(); }
bool netIsConnected() { return (netMode > 0) ? (ETH.linkUp() && ETH.localIP()) : (WiFi.STA.status() == WL_CONNECTED); }

const char* formatIPstr(bool getAP) {
  static char localIP[16] = "";
  IPAddress ipLocal = getAP ? WiFi.AP.localIP() : netLocalIP();
  sprintf(localIP, "%u.%u.%u.%u", ipLocal[0], ipLocal[1], ipLocal[2], ipLocal[3]); 
  return localIP;
}

void resetWatchDog(int wdIndex, uint32_t wdTimeout) {
  // customised watchdogs for particular tasks
  // ping task (0) used as watchdog in case of esp freeze
  static bool watchDogStarted[4] = {false, false, false, false};
  if (watchDogStarted[wdIndex]) esp_task_wdt_reset();
  else {
    // setup watchdog on first call
    esp_task_wdt_deinit(); 
    esp_task_wdt_config_t twdt_config = {
      .timeout_ms = wdTimeout,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true, // panic abort on watchdog alert (contains wdt_isr)
    };
    esp_task_wdt_init(&twdt_config);
    esp_task_wdt_add(NULL);
    if (esp_task_wdt_status(NULL) == ESP_OK) {
      watchDogStarted[wdIndex] = true;
      esp_task_wdt_reset();
      LOG_INF("WatchDog started for task: %s", pcTaskGetName(NULL));
    } else LOG_ERR("WatchDog failed to start for task: %s ", pcTaskGetName(NULL));
  }
}

static void statusCheck() {
  // regular status checks
  doAppPing();
  if (!timeSynchronized) getLocalNTP();
  if (!dataFilesChecked) dataFilesChecked = checkDataFiles();
#if INCLUDE_MQTT
  if (mqtt_active) startMqttClient();
#endif
}

static void pingSuccess(esp_ping_handle_t hdl, void *args) {
  //uint32_t elapsed_time;
  //esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));
  if (DEBUG_MEM) {
    static uint32_t minStack = UINT32_MAX;
    uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    if (freeStack < minStack) {
      minStack = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Task ping stack space only: %lu", freeStack);
      else LOG_INF("Task ping stack space reduced to: %lu", freeStack);
    }
  }
  resetWatchDog(0, wifiTimeoutSecs * 1000 * 2);
  if (dataFilesChecked) resetCrashLoop();
  statusCheck();
}

static void pingTimeout(esp_ping_handle_t hdl, void *args) {
  // a ping check is used because esp may maintain a connection to gateway which may be unuseable, which is detected by ping failure
  // but some routers may not respond to ping - https://github.com/s60sc/ESP32-CAM_MJPEG2SD/issues/221
  // so setting usePing to false ignores ping failure if connection still present
  resetWatchDog(0, wifiTimeoutSecs * 1000 * 2);
  if (netMode > 0) {
    if (usePing) {
      LOG_WRN("Failed to ping gateway, restart ethernet ...");
      startNetwork(false);
    } else {
      if (netIsConnected()) statusCheck();
      else {
        LOG_WRN("Disconnected, restart ethernet ...");
        startNetwork(false);
      }
    }
  } else {
    if (strlen(ST_SSID)) {
      wl_status_t wStat = WiFi.STA.status();
      if (wStat != WL_NO_SSID_AVAIL && wStat != WL_NO_SHIELD) {
        if (usePing) {
          LOG_WRN("Failed to ping gateway, restart wifi ...");
          startWifi(false);
        } else {
          if (wStat == WL_CONNECTED) statusCheck();
          else {
            LOG_WRN("Disconnected, restart wifi ...");
            startWifi(false);
          }
        }
      }
    }
  }
}

static void startPing() {
  IPAddress ipAddr = netGatewayIP();
  if (!ipAddr) return; // don't start ping until gateway is known
  ip_addr_t pingDest; 
  IP_ADDR4(&pingDest, ipAddr[0], ipAddr[1], ipAddr[2], ipAddr[3]);
  esp_ping_config_t pingConfig = ESP_PING_DEFAULT_CONFIG();
  pingConfig.target_addr = pingDest;  
  pingConfig.count = ESP_PING_COUNT_INFINITE;
  pingConfig.interval_ms = wifiTimeoutSecs * 1000;
  pingConfig.timeout_ms = 5000;
  pingConfig.task_stack_size = PING_STACK_SIZE;
  pingConfig.task_prio = 1;
  // set ping task callback functions 
  esp_ping_callbacks_t cbs;
  cbs.on_ping_success = pingSuccess;
  cbs.on_ping_timeout = pingTimeout;
  cbs.on_ping_end = NULL; 
  cbs.cb_args = NULL;
  esp_ping_new_session(&pingConfig, &cbs, &pingHandle);
  esp_ping_start(pingHandle);
  LOG_INF("Started ping monitoring - %s", usePing ? "On" : "Off");
  debugMemory("startPing");
}

void stopPing() {
  if (pingHandle != NULL) {
    esp_ping_stop(pingHandle);
    esp_ping_delete_session(pingHandle);
    pingHandle = NULL;
  }
}

#define EXT_IP_HOST "ipwhois.app"
#define EXT_IP_PATH "/json"
char extIP[MAX_IP_LEN] = "Not assigned"; // router external IP
bool doGetExtIP = true;

void getExtIP() {
  // Get external IP address
  if (doGetExtIP) { 
    NetworkClient hclient;
    if (remoteServerConnect(hclient, EXT_IP_HOST, HTTP_PORT, GETEXTIP)) {
      HTTPClient http;
      int httpCode = HTTP_CODE_NOT_FOUND;
      if (http.begin(hclient, EXT_IP_HOST, HTTP_PORT, EXT_IP_PATH)) {
        httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
          String payload = http.getString();
          char jsonVal[FILE_NAME_LEN] = "";
          if (getJsonValue(payload.c_str(), "ip", jsonVal)) {
            if (strcmp(jsonVal, extIP)) {
              // external IP changed
              strncpy(extIP, jsonVal, sizeof(extIP) - 1);
              updateStatus("extIP", extIP);
              updateStatus("save", "0");
              externalAlert("External IP changed", extIP);
            } else LOG_INF("External IP: %s", extIP);
          } else LOG_WRN("'ip' field not present");
          
          if (getJsonValue(payload.c_str(), "latitude", jsonVal)) latLon[0] = atof(jsonVal);
          else LOG_WRN("'latitude' field not present");
          if (getJsonValue(payload.c_str(), "longitude", jsonVal)) latLon[1] = atof(jsonVal);
          else LOG_WRN("'longitude' field not present");
        } else LOG_WRN("External IP request failed, error: %s", http.errorToString(httpCode).c_str());    
        
        if (httpCode != HTTP_CODE_OK) doGetExtIP = false;
        http.end();     
      }
      remoteServerClose(hclient);
    }
  }
}

/************** generic NetworkClientSecure functions ******************/

static uint8_t failCounts[REMFAILCNT] = {0};

void remoteServerClose(Client& client) {
  uint32_t startAttempt = millis();
  while (client.available() > 0 && (millis() - startAttempt < 1000)) client.read();
  if (client.connected()) client.stop();
}

static bool checkFailureThreshold(const char* host, uint8_t idx) {
  // Check failure threshold
  if (failCounts[idx] >= MAX_FAIL) {
    if (failCounts[idx] == MAX_FAIL) {
      LOG_ERR("Abandon %s connection attempt until next rollover", host);
      failCounts[idx] = MAX_FAIL + 1;
    }
    return false;
  } 
  return true;
}

bool remoteServerConnect(Client& client, const char* host, uint16_t port, uint8_t idx) {
  if (client.connected()) return true;
  if (checkFailureThreshold(host, idx)) {
    // Connection loop
    uint32_t start = millis();
    while (!client.connected()) {
      if (client.connect(host, port)) break;
      if (millis() - start > (uint32_t)responseTimeoutSecs * 1000) break;
      delay(500); 
    }

    // Final status & error reporting
    if (client.connected()) {
      failCounts[idx] = 0;
      return true;
    }

    failCounts[idx]++;
    LOG_WRN("Failed to connect to %s", host);
  }
  return false;
}

bool remoteServerConnect(NetworkClientSecure& client, const char* host, uint16_t port, const char* cert, uint8_t idx) {
  if (checkFailureThreshold(host, idx)) {
    // Additional operations for secure client
    if (ESP.getFreeHeap() <= TLS_HEAP) {
      LOG_WRN("Insufficient heap %s for %s TLS session", fmtSize(ESP.getFreeHeap()), host);
      failCounts[idx]++;
      return false;
    }
    // Configure security
    if (useSecure && strlen(cert)) client.setCACert(cert);
    else client.setInsecure();
    if (remoteServerConnect(static_cast<Client&>(client), host, port, idx)) return true;
    else {
      // failed to connect in allocated time
      // 'Memory allocation failed' indicates lack of heap space
      // 'Generic error' can indicate DNS failure
      char buf[100];
      int err = client.lastError(buf, sizeof(buf));
      LOG_WRN("TSL connect Fail: %s, Err %d: %s", host, err, buf);
    }
  }
  return false;
}

void remoteServerReset() {
  // reset fail counts
  for (uint8_t i = 0; i < REMFAILCNT; i++) failCounts[i] = 0;
}

/************************** NTP  **************************/

// Needs to be a time zone string from: https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv
char timezone[FILE_NAME_LEN] = "GMT0";
char ntpServer[MAX_HOST_LEN] = "pool.ntp.org";
uint8_t alarmHour = 1;

time_t getEpoch() {
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

static void showLocalTime(const char* timeSrc) {
  time_t currEpoch = getEpoch();
  char timeFormat[20];
  strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
  LOG_INF("Got current time from %s: %s with tz: %s", timeSrc, timeFormat, timezone);
  timeSynchronized = true;
}

static bool getLocalNTP() {
  // get current time from NTP server and apply to ESP32
  LOG_INF("Using NTP server: %s", ntpServer);
  configTzTime(timezone, ntpServer);
  if (getEpoch() > 10000) {
    showLocalTime("NTP");
    getExtIP();
    return true;
  } else {
    LOG_WRN("Not yet synced with NTP");
    return false;
  }
}

void syncToBrowser(uint32_t browserUTC) {
  // Synchronize to browser clock if out of sync
  if (!timeSynchronized) {
    struct timeval tv;
    tv.tv_sec = browserUTC;
    settimeofday(&tv, NULL);
    setenv("TZ", timezone, 1);
    tzset();
    showLocalTime("browser");
  }
}

void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays) {
  // elapsed time that app has been running
  uint32_t secs = timeVal / 1000; //convert milliseconds to seconds
  uint32_t mins = secs / 60; //convert seconds to minutes
  uint32_t hours = mins / 60; //convert minutes to hours
  uint32_t days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the converted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the converted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the converted hours to days in order to display 23 hours max
  if (noDays) sprintf(timeStr, "%02lu:%02lu:%02lu", hours, mins, secs);
  else sprintf(timeStr, "%lu-%02lu:%02lu:%02lu", days, hours, mins, secs);
}

static time_t setAlarm(uint8_t alarmHour) {
  // calculate future alarm datetime based on current datetime
  // ensure relevant timezone identified (default GMT0)
  time_t currEpoch = getEpoch();
  struct tm* timeinfo = localtime(&currEpoch);
  // set alarm date & time for next given hour
  int nextDay = 0; // try same day then next day
  do {
    timeinfo->tm_mday += nextDay;
    timeinfo->tm_hour = alarmHour;
    timeinfo->tm_min = 0;
    timeinfo->tm_sec = 0;
    nextDay = 1;
  } while (mktime(timeinfo) < currEpoch);
  char inBuff[30];
  strftime(inBuff, sizeof(inBuff), "%d/%m/%Y %H:%M:%S", timeinfo);
  LOG_INF("Alarm scheduled at %s", inBuff);
  // return future alarm time as epoch seconds
  return mktime(timeinfo);
}

bool checkAlarm() {
  // call from appPing() to check if daily alarm time at given hour has occurred
  static time_t rolloverEpoch = 0;
  if (timeSynchronized && getEpoch() >= rolloverEpoch) {
    // alarm time reached, unless first call
    bool notInit = (rolloverEpoch == 0) ? false : true;
    rolloverEpoch = setAlarm(alarmHour); // set next alarm time
    return notInit;
  }
  return false;
}

/********************** misc functions ************************/

bool changeExtension(char* fileName, const char* newExt) {
  // replace original file extension with supplied extension (buffer must be large enough)
  size_t inNamePtr = strlen(fileName);
  // find '.' before extension text
  while (inNamePtr > 0 && fileName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(fileName + inNamePtr, newExt, extLen);
  fileName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

void showProgress(const char* marker) {
  // show progess as dots or supplied marker
  static uint8_t dotCnt = 0;
  LOG_SEND("%s", marker); // progress marker
  if (++dotCnt >= DOT_MAX) {
    dotCnt = 0;
    LOG_SEND("\n");
  }
}

bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress) {
  // calculate percentage progress, only report back on percentReport boundary
  uint8_t percentage = (progressVal * 100) / totalVal;
  if (percentage >= pcProgress + percentReport) {
    pcProgress = percentage;
    return true;
  } else return false;
}

bool urlEncode(const char* inVal, char* encoded, size_t maxSize) {
  int encodedLen = 0;
  char hexTable[] = "0123456789ABCDEF";
  while (*inVal) {
    if (isalnum(*inVal) || strchr("$-_.+!*'(),:@~#", *inVal)) {
      *encoded++ = *inVal;
      encodedLen++;
    } else {
      encodedLen += 3; 
      *encoded++ = '%';
      *encoded++ = hexTable[(*inVal) >> 4];
      *encoded++ = hexTable[*inVal & 0xf];
    }
    if (encodedLen >= maxSize) return false;  // Buffer overflow
    inVal++;
  }
  *encoded = 0;
  return true;
}

void urlDecode(char* inVal) {
  // replace url encoded characters in-place
  // decoded output is always equal or shorter than input
  char* src = inVal;
  char* dst = inVal;
  while (*src) {
    if (*src == '%' && isxdigit((unsigned char)*(src+1)) && isxdigit((unsigned char)*(src+2))) {
      // decode %XX hex sequence
      char hex[3] = {*(src+1), *(src+2), 0};
      *dst++ = (char)strtoul(hex, nullptr, 16);
      src += 3;
    } else if (*src == '+') {
      // + is encoded space in form data
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0; // NUL terminate
}

void listBuff (const uint8_t* b, size_t len) {
  // output buffer content as hex, 16 bytes per line
  if (!len || !b) LOG_WRN("Nothing to print");
  else {
    for (size_t i = 0; i < len; i += 16) {
      int linelen = (len - i) < 16 ? (len - i) : 16;
      for (size_t k = 0; k < linelen; k++) LOG_SEND(" %02x", b[i+k]);
      puts(" ");
    }
  }
}

size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) {
  // find a subarray (needle) in another array (haystack)
  size_t h = 0, n = 0; // Two pointers to traverse the arrays
  // Traverse both arrays simultaneously
  while (h < hSize && n < nSize) {
    // If element matches, increment both pointers
    if (haystack[h] == needle[n]) {
      h++;
      n++;
      // If needle is completely traversed
      if (n == nSize) return h; // position of end of needle
    } else {
      // if not, increment h and reset n
      h = h - n + 1;
      n = 0;
    }
  }
  return 0; // not found
}

void removeChar(char* s, char c) {
  // remove specified character from string
  int writer = 0, reader = 0;
  while (s[reader]) {
    if (s[reader] != c) s[writer++] = s[reader];
    reader++;       
  }
  s[writer] = 0;
}

void replaceChar(char* s, char c, char r) {
  // replace specified character in string
  int reader = 0;
  while (s[reader]) {
    if (s[reader] == c) s[reader] = r;
    reader++;
  }
}

char* fmtSize (uint64_t sizeVal) {
  // format size according to magnitude
  // only one call per format string
  static char returnStr[20];
  if (sizeVal < 50 * 1024) sprintf(returnStr, "%llu bytes", sizeVal);
  else if (sizeVal < ONEMEG) sprintf(returnStr, "%lluKB", sizeVal / 1024);
  else if (sizeVal < ONEMEG * 1024) sprintf(returnStr, "%0.1fMB", (double)(sizeVal) / ONEMEG);
  else sprintf(returnStr, "%0.1fGB", (double)(sizeVal) / (ONEMEG * 1024));
  return returnStr;
}

char* trim(char* str) {
  // trim whitespace around string
    char* start = str;
    char* end;
    // Trim leading whitespace
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == '\0') {
        *str = '\0';
        return str;
    }
    // Trim trailing whitespace
    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    // Shift string back to original buffer if needed
    if (start != str) memmove(str, start, end - start + 2);
    return str;
}

char* toCase(char *s, bool toLower) {
  // convert supplied string to lower or upper case
  for (char *p = s; *p; ++p) *p = toLower ? (char)tolower((unsigned char)*p) : (char)toupper((unsigned char)*p);
  return s;
}

/********************** analog functions ************************/

uint16_t smoothAnalog(int analogPin, int samples) {
  // get averaged analog pin value 
  uint32_t level = 0; 
  if (analogPin > 0) {
    for (int j = 0; j < samples; j++) level += analogRead(analogPin); 
    level /= samples;
  }
  return level;
}

void setupADC() {
  analogSetAttenuation(ADC_ATTEN);
  analogReadResolution(ADC_BITS);
}

float smoothSensor(float latestVal, float smoothedVal, float alpha) {
  // simple Exponential Moving Average filter 
  // where alpha between 0.0 (max smooth) and 1.0 (no smooth)
  return (latestVal * alpha) + smoothedVal * (1.0 - alpha);
}

// onboard chip temperature sensor
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
// Use internal on chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t temp_sensor = NULL;
#endif

static void prepInternalTemp() {
#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
  // setup internal sensor
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  temperature_sensor_install(&temp_sensor_config, &temp_sensor);
  temperature_sensor_enable(temp_sensor);
#endif
}

float readInternalTemp() {
  float intTemp = NULL_TEMP;
#if CONFIG_IDF_TARGET_ESP32
  // convert on chip raw temperature in F to Celsius degrees
  intTemp = (temprature_sens_read() - 32) / 1.8;  // value of 55 means not present
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
    temperature_sensor_get_celsius(temp_sensor, &intTemp); 
#endif
  return intTemp;
}


/****************** base 64 ******************/

#define BASE64 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

const uint8_t* encode64chunk(const uint8_t* inp, int rem) {
  // receive 3 byte input buffer and return 4 byte base64 buffer
  rem = 3 - rem; // last chunk may be less than 3 bytes 
  uint32_t buff = 0; // hold 3 bytes as shifted 24 bits
  static uint8_t b64[4];
  // shift input into buffer
  for (int i = 0; i < 3 - rem; i++) buff |= inp[i] << (8*(2-i)); 
  // shift 6 bit output from buffer and encode
  for (int i = 0; i < 4 - rem; i++) b64[i] = BASE64[buff >> (6*(3-i)) & 0x3F]; 
  // filler for last chunk if less than 3 bytes
  for (int i = 0; i < rem; i++) b64[3-i] = '='; 
  return b64;
}

const char* encode64(const char* inp) {
  // helper to base64 encode strings up to 90 chars long
  static char encoded[121]; // space for 4/3 expansion + terminator
  encoded[0] = 0;
  int len = strlen(inp);
  if (len > 90) {
    LOG_WRN("Input string too long: %u chars", len);
    len = 90;
  }
  for (int i = 0; i < len; i += 3) 
    strncat(encoded, (char*)encode64chunk((uint8_t*)inp + i, min(len - i, 3)), 4);
  return encoded;
}

void debugMemory(const char* caller) {
  if (DEBUG_MEM) {
    LOG_SEND("%s > Free: heap %lu, block: %lu, min: %lu, pSRAM %lu\n", caller, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
    delay(FLUSH_DELAY);
  }
}

/****************** startup, send device to sleep (light or deep), restart ******************/

#include <esp_wifi.h>

void doRestart(const char* restartStr) {
  LOG_ALT("Controlled restart: %s", restartStr);
#ifdef ISCAM
  appShutdown();
#endif
#if INCLUDE_MQTT
  if (mqtt_active) stopMqttClient();
#endif  
  resetCrashLoop();
  flush_log(true);
  delay(2000);
  ESP.restart();
}

void goToSleep(bool deepSleep) {
#if !CONFIG_IDF_TARGET_ESP32C3
  // if deep sleep, restarts with reset
  // if light sleep, restarts by continuing this function
  LOG_INF("Going into %s sleep", deepSleep ? "deep" : "light");
  delay(100);
  if (deepSleep) { 
    if (wakePin >= 0) {
      // wakeup on pin low (0) or high (1)
      // needs to be RTC pin and support input pullup/down
      pinMode(wakePin, wakeLevel == 0 ? INPUT_PULLUP : INPUT_PULLDOWN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)wakePin, wakeLevel);
    }
    esp_deep_sleep_start();
  } else {
    // light sleep
    esp_wifi_stop();
    // wakeup on selected pin
    if (wakePin >= 0) gpio_wakeup_enable((gpio_num_t)wakePin, wakeLevel == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL); 
    esp_light_sleep_start();
  }
  // light sleep restarts here
  LOG_INF("Light sleep wakeup");
  if (netMode != 1) esp_wifi_start();
#else
  LOG_WRN("This function not compatible with ESP32-C3");
#endif
}

bool utilsStartup() {
  bool res = false;
#if CONFIG_IDF_TARGET_ESP32S3
  STACK_MEM = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
#else
  // Original ESP32 must use internal memory for stacks
  STACK_MEM = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
  logSetup();
#ifdef NEED_PSRAM
  if (psramFound()) {
    if (ESP.getPsramSize() < MIN_PSRAM * ONEMEG) 
      snprintf(startupFailure, SF_LEN, STARTUP_FAIL "App needs at least %dMB PSRAM", MIN_PSRAM);
  } else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Need PSRAM to be enabled");
#endif
  
  prepInternalTemp();
  if (jsonBuff == NULL) jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN);
  LOG_INF("Compiled with arduino-esp32 v%s", ESP_ARDUINO_VERSION_STR);
  // prep storage
  res = startStorage();
  // Load saved user configuration
  if (res) res = loadConfig();
#ifdef DEV_ONLY
  devSetup();
#endif
  return res;
}
