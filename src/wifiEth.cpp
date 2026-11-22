
// Network functions for:
// - wifi / ethernet
// - REmote server connection
//
// s60sc 2021 - 2026
// some functions based on code contributed by gemi254

#include "appGlobals.h"
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <atomic>

bool timeSynchronized = false;
bool dataFilesChecked = false;
char portFwd[7] = "";
float latLon[2] = {0};
bool appSetupDone = false;

static bool setupEspNow();


/************************** Network (WiFi/Ethernet) **************************/
 
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

static bool startPing();
char timezone[FILE_NAME_LEN] = "GMT0";
char ntpServer[MAX_HOST_LEN] = "pool.ntp.org";

int netMode = 0; // 0=WiFi only, 1=Ethernet only, 2=Ethernet+AP

// LAN8720
#define ETH_PHY_ADDR  0 
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN // external clock from crystal oscillator

static const char* wifiModes[] = {"NULL", "STA", "AP", "STA+AP", "NAN"};

static wifi_mode_t getWifiMode() {
  wifi_mode_t wifiMode = WiFi.getMode();
  LOG_INF("WiFi Mode: %s", wifiModes[wifiMode]);
  return wifiMode;
}

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
   	case (WIFI_AUTH_WPA3_PSK): return "WPA3_PSK";
	  case (WIFI_AUTH_WPA2_WPA3_PSK): return "WPA2_WPA3_PSK";
    case (WIFI_AUTH_MAX): return "AUTH_MAX";
    default: return "Not listed";
  }
}

static void onNetEvent(arduino_event_id_t event, arduino_event_info_t info) {
  // callback to report on network events
  switch (event) {
    case ARDUINO_EVENT_WIFI_READY: break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE: break;
    case ARDUINO_EVENT_WIFI_STA_START: LOG_INF("Wifi started"); break; 
    case ARDUINO_EVENT_WIFI_STA_STOP: LOG_INF("Wifi Station stopped %s", ST_SSID); break;
    case ARDUINO_EVENT_WIFI_AP_START: {
      if (WiFi.AP.SSID().length() > 0 && WiFi.AP.SSID() == AP_SSID) {
        LOG_INF("Wifi AP SSID: %s started, use 'http%s://%s' to connect", WiFi.AP.SSID().c_str(), useHttps ? "s" : "", formatIPstr(true));
        APstarted = true;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STOP: {
      if (WiFi.AP.SSID() == AP_SSID) {
        LOG_INF("Wifi AP stopped: %s", AP_SSID);
        APstarted = false;
      }
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: 
      LOG_INF("Wifi Station IP, use '%s://%s' to connect", useHttps ? "https" : "http", formatIPstr()); 
      if (pingHandle == NULL) startPing(); // Start ping monitor when IP is guaranteed
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP: LOG_INF("Wifi Station lost IP"); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED: LOG_INF("WiFi Station connection to %s, using hostname: %s", ST_SSID, hostName); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: LOG_INF("WiFi Station disconnected"); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: LOG_INF("WiFi AP client connection"); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: LOG_INF("WiFi AP client disconnection"); break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED: break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6: LOG_INF("AP interface V6 IP addr is preferred"); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6: LOG_INF("Station interface V6 IP addr is preferred"); break;
    case ARDUINO_EVENT_WIFI_OFF: LOG_INF("WiFi Off"); break;

    case ARDUINO_EVENT_ETH_START: LOG_INF("Ethernet started, speed %uMHz", ETH.linkSpeed()); break;
    case ARDUINO_EVENT_ETH_CONNECTED: LOG_INF("Ethernet connected, MAC: %s", ETH.macAddress().c_str()); break;
    case ARDUINO_EVENT_ETH_STOP: LOG_INF("Ethernet Stopped"); break;
    case ARDUINO_EVENT_ETH_GOT_IP: {
      LOG_INF("Ethernet IP, use '%s://%s' to connect", useHttps ? "https" : "http", formatIPstr()); 
      if (netMode == 2) WiFi.AP.enableNAPT(true);
      if (pingHandle == NULL) startPing(); // Start ping monitor when IP is guaranteed
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
    WiFi.AP.clear();
    WiFi.AP.end(); // Terminate residual AP state from previous runs.
    delay(100);
    WiFi.mode(WIFI_AP_STA); // Enable concurrent Station and Access Point modes.
    // Configure Access Point (AP) bandwidth to 20 MHz (BW20) for connection stability
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(4, 0, 0)
    esp_err_t err_ap = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20); // Options: WIFI_BW_HT20 - 72.2 Mbps, maximum stability; WIFI_BW_HT40 - 150 Mbps, higher throughput(NA due to hardware limitations).
#else
    esp_err_t err_ap = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20); // Options: WIFI_BW20 - 72.2 Mbps, maximum stability; WIFI_BW40 - 150 Mbps, higher throughput(NA due to hardware limitations).
#endif
    if (err_ap == ESP_OK) {
      LOG_VRB("AP bandwidth successfully changed to BW20\n");
    } else {
      LOG_WRN("AP bandwidth configuration error: 0x%X\n", err_ap);
    }
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
    WiFi.AP.enableNAPT(true);
    WiFi.AP.create(AP_SSID, AP_Pass, WiFi.channel());
    // allow AP time to start
    uint32_t t0 = millis();
    while (!APstarted && millis() - t0 < 2000) delay(50);
#if defined(ESPNOW_SENDER) || defined(ESPNOW_RECEIVER)
    setupEspNow();
#endif
    debugMemory("setWifiAP");
  }
}

static void setWifiIP() {
  // set station with static ip if provided
  if (ST_ip[0]) {
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
  if (!ETH.linkUp()) {
    LOG_WRN("Ethernet link not up");
    return false; // Abort early to prevent resource leaks
  }
  startAttemptTime = millis();
  while (!ETH.localIP() && millis() - startAttemptTime < 8000) delay(100);
  if (!ETH.localIP()) {
    LOG_WRN("Ethernet no IP yet");
    return false; // Abort early to prevent resource leaks
  }
  setupMdnsHost();
  // startPing() is primarily handled by ARDUINO_EVENT_ETH_GOT_IP. 
  // Kept here as a safe fallback if the event was missed.
  if (pingHandle == NULL) startPing();
  return true;
}

// Event group handles and bits for Wi-Fi connection status
static EventGroupHandle_t wifiEventGroup = NULL;
static bool wifiEventHandlerRegistered = false;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void wifiScan() {
  for (int i=0; i < WiFi.scanNetworks(); i++) {
    if (strcmp(WiFi.SSID(i).c_str(), ST_SSID) == 0) {
      LOG_INF("Connect to SSID: %s, signal strength: %ld dBm, Encryption: %s, channel: %lu", ST_SSID, WiFi.RSSI(i), getEncType(i), WiFi.channel(i));
      break;
    }
    yield();
  }
  WiFi.scanDelete(); // Release heap allocated for scan results.
  vTaskDelay(1);
}

// Initializes and manages Wi-Fi (STA/AP), returning true if STA connects successfully.
static bool startWifi(bool firstcall = true) { // firstcall: true on initial invocation to apply one-time HW config.
  // Apply one-time Wi-Fi hardware configuration and suppress default behaviors.
  wl_status_t wlStat = WL_NO_SSID_AVAIL;
  if (firstcall) {
    APstarted = false;
    WiFi.mode(WIFI_STA);
    vTaskDelay(1); // Yield to allow Wi-Fi driver task to process initialization.
    if (ST_SSID[0]) {
      wifiScan(); // get SSID details
      // Configure Station (STA) bandwidth to 20 MHz (BW20) for connection stability
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(4, 0, 0)
      esp_err_t err_sta = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20); // Options: WIFI_BW_HT20 - 72.2 Mbps, maximum stability; WIFI_BW_HT40 - 150 Mbps, higher throughput(NA due to hardware limitations).
#else
      esp_err_t err_sta = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW20); // Options: WIFI_BW20 - 72.2 Mbps, maximum stability; WIFI_BW40 - 150 Mbps, higher throughput(NA due to hardware limitations).
#endif
      if (err_sta == ESP_OK) {
        LOG_VRB("STA bandwidth successfully changed to BW20\n");
      } else {
        LOG_WRN("STA bandwidth configuration error: 0x%X\n", err_sta);
      }
#ifdef NO_WIFI_SLEEP
      WiFi.setSleep(false); // Disable Power Saving depending on app
#else
      WiFi.setSleep(true); // Setup modem sleep to configure association latency(Options: true/WIFI_PS_MIN_MODEM - for energy efficiency, false/WIFI_PS_NONE - for stable ping.
#endif
      WiFi.setTxPower(WIFI_POWER_15dBm); // Set the Wi-Fi transmit power(range vs energy efficiency). Options: WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_8_5dBm, WIFI_POWER_2dBm, etc ...(see the documentation)
      WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN); // Options: WIFI_FAST_SCAN or WIFI_ALL_CHANNEL_SCAN; fast vs reliable - helps with hopping STA
      WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL); // Options: WIFI_CONNECT_AP_BY_SIGNAL or WIFI_CONNECT_AP_BY_SECURITY
      WiFi.persistent(false); // Disable NVS flash writes for Wi-Fi credentials.
      WiFi.STA.setAutoReconnect(false); // Disable auto-reconnect to manage connection lifecycle manually.
      WiFi.STA.setHostname(hostName); // Assign custom hostname for DHCP/DNS resolution.
      vTaskDelay(pdMS_TO_TICKS(1)); // Yield to allow Wi-Fi driver task to process initialization.
    }

    // Attempt Station mode connection if network mode permits (netMode == 0).
    if (netMode == 0) {
      // Create the event group if it does not exist, BEFORE registering the handler.
      if (wifiEventGroup == NULL) {
        wifiEventGroup = xEventGroupCreate();
      }
      // Register the Wi-Fi event handler once to update the event group.
      if (!wifiEventHandlerRegistered) {
        WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
          if (wifiEventGroup == NULL) return;
          if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            xEventGroupSetBits(wifiEventGroup, WIFI_CONNECTED_BIT);
          } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            xEventGroupSetBits(wifiEventGroup, WIFI_FAIL_BIT);
          }
        });
        wifiEventHandlerRegistered = true;
      }

      if (wifiEventGroup != NULL) {
        // Clear bits before starting the connection attempt to prevent race conditions.
        xEventGroupClearBits(wifiEventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
      }

      setWifiIP();
      WiFi.STA.enableIPv6(USE_IP6); 
      WiFi.STA.begin();
      WiFi.STA.connect(ST_SSID, ST_Pass);
      wlStat = WL_NO_SSID_AVAIL; // Reset status; reconnection is deferred to background ping task.
      
      // Poll connection status with a 5-second timeout to prevent indefinite blocking.
      if (ST_SSID[0]) {
        if (wifiEventGroup != NULL) {
          EventBits_t bits = 0;
          uint32_t startAttemptTime = millis();
          // Wait for connection or failure with a 100ms loop to preserve progress logging.
          while (!(bits & (WIFI_CONNECTED_BIT | WIFI_FAIL_BIT)) && (millis() - startAttemptTime < 5000)) {
            bits = xEventGroupWaitBits(
              wifiEventGroup,
              WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
              pdFALSE, // Do not clear bits on exit
              pdFALSE, // Wait for any bit
              pdMS_TO_TICKS(100)
            );
          }
          
          if (bits & WIFI_CONNECTED_BIT) {
            wlStat = WL_CONNECTED;
          } else {
            wlStat = WiFi.STA.status(); // Get exact status if failed or timeout
          }
        } else {
          // Fallback to polling if event group creation failed
          uint32_t startAttemptTime = millis();
          wlStat = WiFi.STA.status();
          while (wlStat != WL_CONNECTED && (millis() - startAttemptTime < 5000)) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Yield to prevent task watchdog timeout.
            wlStat = WiFi.STA.status();
          }
        }
      }
      if (wlStat != WL_CONNECTED) LOG_WRN("WiFi failed to connect to SSID: %s", ST_SSID);
    }
  } 

  // Fallback to AP mode if STA connection failed or AP is explicitly allowed.
  if (allowAP ||
    wlStat == WL_NO_SSID_AVAIL ||
    wlStat == WL_CONNECT_FAILED ||
    wlStat == WL_DISCONNECTED ||
    wlStat == WL_IDLE_STATUS) {
    setWifiAP(); // Configure and start Access Point for provisioning or fallback access.
    // Fix Silent Initialization Failure: Verify AP actually started and has an IP.
    // This prevents waiting indefinitely for an IP that will never arrive if WiFi.softAP() fails.
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
      if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        APstarted = true;
      } else {
        LOG_WRN("WiFi AP start failed: No IP assigned");
      }
    } else {
      LOG_WRN("WiFi AP start failed: Mode not set to AP");
    }
  }
  
  // Start mDNS only on the ESP32-S3 and only if the Station obtained a valid IP address.
#if CONFIG_IDF_TARGET_ESP32S3
  if (netMode == 0) {
    if ((uint32_t)WiFi.localIP() != 0) {
      setupMdnsHost();
    }
  }
#endif

  // Ensure background ping task is active ONLY if Station mode achieved a connected state with a valid IP.
  if (wlStat == WL_CONNECTED && (uint32_t)WiFi.localIP() != 0) {
    if (pingHandle == NULL) startPing();
  }
  
  // Return true only if Station mode achieved a connected state or AP available
  return (wlStat == WL_CONNECTED || APstarted);
}

static void timeSyncCallback(struct timeval *tv) {
  // wait for local time to sync with NTP server
  LOG_INF("Time synced with NTP: %s, using timezone: %s", ntpServer, timezone);
  timeSynchronized = true;
}

// Helper to wait for network interface to become ready (synchronous barrier)
static bool waitForNetworkConnection(uint32_t timeoutMs) {
  uint32_t startWait = millis();
  while (millis() - startWait < timeoutMs) {
    // Check Ethernet first if it was attempted
    if (netMode > 0 && ETH.linkUp() && ETH.localIP() != IPAddress(0, 0, 0, 0)) {
      return true;
    }
    // Check WiFi STA
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      return true;
    }
    // Check WiFi AP (Network is ready as soon as AP is started and has an IP)
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
      if (WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        return true;
      }
    }
    // Yield to FreeRTOS scheduler, prevent watchdog trigger
    vTaskDelay(pdMS_TO_TICKS(100)); 
  }
  return false;
}

bool startNetwork(bool firstcall) {
  // start WiFi, Ethernet, Eth+AP by config
  bool res = false;
  if (firstcall) Network.onEvent(onNetEvent);
  
  // Anti-pattern fix: Only configure Ethernet pins if Ethernet is actually requested
  if (netMode > 0) {
    predefEthPins();
  }

  bool networkReady = false;
  if (netMode > 0) {
    // Ethernet or Eth+AP
    if (startEth(firstcall)) {
      if (netMode == 1) {
        // Quiet mode: stop WiFi/BLE radios for RF silence
        WiFi.mode(WIFI_OFF);
#ifdef APP_BT_ENABLED
        if (btStarted()) btStop();
#endif
        networkReady = true;
      }
    } else {
      LOG_WRN("Ethernet start failed, falling back to WiFi");
      ETH.end();
      netMode = 0;
      // Anti-pattern fix: Only disable NAPT if WiFi AP mode is currently active
      if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        WiFi.AP.enableNAPT(false);
      }
    }
  }

  // Wifi only / Eth fail / Eth + AP
  if (netMode == 2) allowAP = true;
  
  // connect wifi STA, or AP if router details not available
  if (!networkReady) {
    // Capture return value: startWifi returns true if STA connects OR AP starts successfully
    networkReady = startWifi(firstcall);
    if (!networkReady) {
      LOG_WRN("WiFi initialization failed (STA disconnected and AP failed to start)");
    }
  }
  
  // Anti-pattern fix: Enforce synchronous network readiness before starting dependent services
  if (networkReady) {
    const uint32_t networkTimeoutMs = 15000; // 15 seconds to acquire IP
    networkReady = waitForNetworkConnection(networkTimeoutMs);
    if (!networkReady) {
      LOG_WRN("Network connection timeout");
    }
  }

  // Web server start is a critical step for overall success
  bool webServerReady = false;
  bool isAPMode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
  
  // Always enable the web server when in WiFi Access Point mode
  if (networkReady || isAPMode) {
    webServerReady = startWebServer();
  }
  
#ifdef DEV_ONLY
  devCheck();
#endif
  
  if (webServerReady) {
    runStatusCheck();
    res = true;
	sntp_set_time_sync_notification_cb(timeSyncCallback);
    configTzTime(timezone, ntpServer);
  } else {
    snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Failed to complete network setup");
    LOG_WRN("%s", startupFailure);
    res = false; // Preserve original behavior: failure here fails the whole function
  }
  
  if (res) {
    // Prevent infinite loop and watchdog reset by adding a bounded timeout
    const uint32_t maxWaitMs = 30000; // 30 seconds maximum wait time
    uint32_t startWait = millis();
    while (!dataFilesChecked) {
      if (millis() - startWait > maxWaitMs) {
        LOG_WRN("Timeout waiting for dataFilesChecked");
        res = false;
        break;
      }
      // Yield to FreeRTOS scheduler
      vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
  }
  
  if (res) {
    getExtIP();
    LOG_INF("MAC Addr: %s", netMacAddress());
  }
  return res;
}

static IPAddress netLocalIP() { return (netMode > 0) ? ETH.localIP() : WiFi.STA.localIP(); }
static IPAddress netGatewayIP() { return (netMode > 0) ? ETH.gatewayIP() : WiFi.STA.gatewayIP(); }
int netRSSI() { return (netMode == 1) ? 0 : WiFi.STA.RSSI(); }
bool netIsConnected() { return (netMode > 0) ? (ETH.linkUp() && ETH.localIP()) : (WiFi.STA.status() == WL_CONNECTED); }

const char* netMacAddress() {
  static char macStr[18];
  String mac = (netMode > 0) ? ETH.macAddress() : WiFi.STA.macAddress();
#ifdef ESPNOW_RECEIVER
  if (APstarted) mac = WiFi.AP.macAddress();
#endif
  strlcpy(macStr, mac.c_str(), sizeof(macStr));
  return macStr;
}

const char* formatIPstr(bool getAP) {
  // Rotating buffer pool supports up to 4 concurrent calls without data corruption
  static char localIP[4][16];
  static uint8_t idx = 0;
  char* buf = localIP[idx++ & 3];
  
  IPAddress ipLocal = getAP ? WiFi.AP.localIP() : netLocalIP();
  // snprintf prevents buffer overflow even if IP data is malformed
  snprintf(buf, sizeof(localIP[0]), "%u.%u.%u.%u", ipLocal[0], ipLocal[1], ipLocal[2], ipLocal[3]); 
  return buf;
}

void resetWatchDog(int wdIndex, uint32_t wdTimeout) {
  // customised watchdogs for particular tasks
  // ping task (0) used as watchdog in case of esp freeze
  static std::atomic<bool> watchDogStarted[4] = {false, false, false, false};
  
  // Prevent array out-of-bounds access
  if (wdIndex < 0 || wdIndex >= 4) {
    LOG_ERR("Invalid watchdog index: %d", wdIndex);
    return;
  }

  if (watchDogStarted[wdIndex].load(std::memory_order_relaxed)) {
    esp_task_wdt_reset();
  } else {
    // setup watchdog on first call for this index
    esp_task_wdt_config_t twdt_config = {
      .timeout_ms = wdTimeout,
      .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
      .trigger_panic = true, // panic abort on watchdog alert (contains wdt_isr)
    };
    
    // Reconfigure if the default Watchdog is already initialized by the system
    esp_err_t err = esp_task_wdt_reconfigure(&twdt_config);
    if (err == ESP_ERR_NOT_FOUND) {
      // TWDT is not initialized yet, initialize it
      err = esp_task_wdt_init(&twdt_config);
    }
    
    // ESP_ERR_INVALID_STATE from init means it was already initialized by another task or the system
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
      // Subscribe the current task to the TWDT
      err = esp_task_wdt_add(NULL);
      if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means the task is already subscribed, which is safe to accept
        watchDogStarted[wdIndex].store(true, std::memory_order_relaxed);
        esp_task_wdt_reset();
        LOG_INF("WatchDog started for task: %s", pcTaskGetName(NULL));
      } else {
        LOG_ERR("WatchDog failed to add task: %s, error: 0x%X", pcTaskGetName(NULL), err);
      }
    } else {
      LOG_ERR("WatchDog failed to init/reconfigure for task: %s, error: 0x%X", pcTaskGetName(NULL), err);
    }
  }
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
  runStatusCheck();
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
      if (netIsConnected()) runStatusCheck();
      else {
        LOG_WRN("Disconnected, restart ethernet ...");
        startNetwork(false);
      }
    }
  } else {
    if (ST_SSID[0]) {
      wl_status_t wStat = WiFi.STA.status();
      if (wStat != WL_NO_SSID_AVAIL && wStat != WL_NO_SHIELD) {
        if (wStat == WL_CONNECTED && dataFilesChecked) resetCrashLoop(); // for routers that dont answer pings
        if (usePing) {
          LOG_WRN("Failed to ping gateway, restart wifi ...");
          startWifi(false);
        } else {
          if (wStat == WL_CONNECTED) runStatusCheck();
          else {
            LOG_WRN("Disconnected, restart wifi ...");
            startWifi(false);
          }
        }
      }
    }
  }
}

static bool startPing() {
  IPAddress ipAddr = netGatewayIP();
  if (!ipAddr) return false; // don't start ping until gateway is known
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
  return true;
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

#include <esp_crt_bundle.h>
extern const uint8_t x509_certificate_bundle_start[] __asm__("_binary_x509_crt_bundle_start");
extern const uint8_t x509_certificate_bundle_end[]   __asm__("_binary_x509_crt_bundle_end");

static uint8_t failCounts[REMFAILCNT] = {0};

void remoteServerClose(Client& client) {
  uint32_t startAttempt = millis();
  uint8_t dumpBuf[64];
  while (client.available() > 0 && (millis() - startAttempt < 1000)) client.read(dumpBuf, sizeof(dumpBuf));
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
  // first check remote server exists / is available
  NetworkClient tcp;
  bool ok = tcp.connect(host, port);
  tcp.stop();
  if (!ok) {
    LOG_WRN("Server %s not reachable", host);
    return false;
  }

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

static bool remoteServerConnectSec(NetworkClientSecure& client, const char* host, uint16_t port, uint8_t idx) {
  if (checkFailureThreshold(host, idx)) {
    // Additional operations for secure client
    if (timeSynchronized || !useSecure) {
      if (ESP.getFreeHeap() <= TLS_HEAP) {
        LOG_WRN("Insufficient heap %s for %s TLS session", fmtSize(ESP.getFreeHeap()), host);
        failCounts[idx]++;
        return false;
      }
      if (remoteServerConnect(static_cast<Client&>(client), host, port, idx)) return true;
      else {
        // failed to connect in allocated time
        // 'Memory allocation failed' indicates lack of heap space
        // 'Generic error' can indicate DNS failure
        char buf[100];
        int err = client.lastError(buf, sizeof(buf));
        LOG_WRN("Failed to %s connect to %s: Err %d: %s", useSecure ? "securely" : "insecurely", host, err, buf);
        return false;
      }
    } else LOG_WRN("Remote server certificate checks require a valid device datetime");
  }
  return false;
}

bool remoteServerConnect(NetworkClientSecure& client, const char* host, uint16_t port, const char* cert, uint8_t idx) {
  // Configure security using own public certs
  if (useSecure) client.setCACert(cert);
  else client.setInsecure(); 
  return remoteServerConnectSec(client, host, port, idx);
}

bool remoteServerConnect(NetworkClientSecure& client, const char* host, uint16_t port, uint8_t idx) {
  // Configure security using certs from IDF ESP x509 Certificate Bundle
  if (useSecure) client.setCACertBundle(x509_certificate_bundle_start, (size_t)(x509_certificate_bundle_end - x509_certificate_bundle_start)); 
  else client.setInsecure(); 
  return remoteServerConnectSec(client, host, port, idx);
}

void remoteServerReset() {
  // reset fail counts
  for (uint8_t i = 0; i < REMFAILCNT; i++) failCounts[i] = 0;
}


/******************** ESP-NOW sender or receiver ************************/

#if defined(ESPNOW_SENDER) || defined(ESPNOW_RECEIVER) 

#include <esp_now.h>
#include <esp_wifi.h>

// configuration

#define CH_MIN 1
#define CH_MAX 13
#define CH_PROBE 0xF2
#define CH_REPLY 0xF3
#define PROBES_PER_CHANNEL 3
#define REPLY_WAIT_MS 200 // 60
#define HEARTBEAT_INTERVAL 250  // ms, sender: max silence before a heartbeat
#define HEARTBEAT_TIMEOUT  750  // ms, receiver: silence before link is lost
#define RX_QUEUE_LEN  32
#define PKT_LEN 6

static const uint8_t BROADCAST_ADDR[PKT_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t otherMAC[PKT_LEN] = {0};
static uint8_t ch = 0;
static uint32_t lastCommandSend = 0;
static bool firstOK = true;
static bool firstFail = true;
volatile bool gotReply = false;
TaskHandle_t espNowHandle = NULL;

static QueueHandle_t rxQueue;
static volatile uint32_t lastReceive = 0;
static bool linkUp = false;

void sendCommandEspNow(const AppDataPacket &p) {
  // Normal commands (not heartbeat) update lastCommandSend.
  esp_err_t result = esp_now_send(otherMAC, (uint8_t*)&p, PKT_LEN);
  if (result == ESP_OK) lastCommandSend = millis();
}

static void macToString(const uint8_t* mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static bool parseMAC(const char* macstr = "") {
  // parse MAC string into array
  unsigned int b[6];
  if (sscanf(macstr, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
  else for (int i = 0; i < 6; i++) otherMAC[i] = static_cast<uint8_t>(b[i]);
  return true;
}

static void processHeartbeat() {
  // Heartbeat deliberately not sent through sendCommand().
  if (otherMAC[0]) {
    uint32_t now = millis();
    if (now - lastCommandSend < HEARTBEAT_INTERVAL) return;
    AppDataPacket p;
    p.command = HB_PING;
    esp_now_send(otherMAC, (uint8_t*)&p, PKT_LEN);

    // Treat heartbeat as the start of the next heartbeat period.
    lastCommandSend = now;
  }
}

static void OnDataSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  char macstr[18];
  // suppress multiple messages when state changes
  if (status == ESP_NOW_SEND_SUCCESS ) {
    if (firstOK) {
      macToString(info->des_addr, macstr);
      LOG_INF("Sent to %s", macstr);
      firstOK = false;
      firstFail = true;
    }
  } else {
    if (firstFail) {
      macToString(info->des_addr, macstr);
      LOG_WRN("Failed to send to %s", macstr);
      firstOK = true;
      firstFail = false;
    }
  } 
}

static bool setChannel(uint8_t ch) {
  return esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

static bool scanForReceiver() {
  AppDataPacket p;
  p.command = CH_PROBE;
  for (ch = CH_MIN; ch <= CH_MAX; ch++) {
    if (!setChannel(ch)) continue;
    LOG_INF("Try channel %u", ch);
    for (uint8_t i = 0; i < PROBES_PER_CHANNEL; i++) {
      esp_now_send(BROADCAST_ADDR, (uint8_t*)&p, PKT_LEN);
      uint32_t start = millis();
      while (millis() - start < REPLY_WAIT_MS) {
        if (otherMAC[0]) {
          char macstr[18];
          macToString(otherMAC, macstr);
          LOG_INF("Reply on channel %u from %s", ch, macstr);
          return true;
        }
        delay(1);
      }
    }
  }
  return false;
}

static bool addPeer(const uint8_t* mac) {
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mac, 6);
  peerInfo.channel = 0; // 0 = current channel (set above)
  peerInfo.ifidx = WIFI_IF_AP;
  peerInfo.encrypt = false;
  esp_err_t err = esp_now_add_peer(&peerInfo);
  if (err != ESP_OK) {
    LOG_WRN("Failed to add ESP-NOW peer: %s", esp_err_to_name(err) );
    return false;
  } else {
    char macstr[18];
    macToString(mac, macstr);
    LOG_INF("ESP-NOW Sender AP MAC: %s using channel: %d", macstr, ch);
  }
  return true;
}

static void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // validate, timestamp and queue received data
  if (len != sizeof(AppDataPacket)) return;
  if (!otherMAC[0]) memcpy(otherMAC, info->src_addr, PKT_LEN);
  lastReceive = millis();
  xQueueSend(rxQueue, data, 0);   // drops the packet if the queue is full
}

static void processReceiver() {
  AppDataPacket p;
  while (xQueueReceive(rxQueue, &p, 0) == pdTRUE) {
    if (!linkUp) {
      linkUp = true;
      espNowLinkRestored();
    }
    switch(p.command) {
      case HB_PING:
        p.command = HB_PONG;
        esp_now_send(otherMAC, (uint8_t*)&p, PKT_LEN);
      break;
      case HB_PONG:
      break; 
      case CH_PROBE:
        // receiver obtains sender MAC and replies
        addPeer(otherMAC);
        p.command = CH_REPLY;
        esp_now_send(otherMAC, (uint8_t*)&p, PKT_LEN);
      break;
      case CH_REPLY:
        // sender obtains Receiver MAC
        esp_now_del_peer(BROADCAST_ADDR);
        addPeer(otherMAC);
      break;
      default: 
        appControllerCommand(p);
      break;
    }
  }

  // Signed difference, so a timestamp set by the callback just now cannot underflow
  int32_t age = (int32_t)(millis() - lastReceive);
  if (linkUp && age > HEARTBEAT_TIMEOUT) {
    linkUp = false;
    espNowLinkLost();
  }
}

static void espNowTask (void* parameter) {
  // runs at periodic interval 
  TickType_t lastWake = xTaskGetTickCount(); 
  while (true) {
      processReceiver();
#ifdef ESPNOW_SENDER
      processHeartbeat();
#endif
    const TickType_t period = pdMS_TO_TICKS(20); // 50Hz
    vTaskDelayUntil(&lastWake, period);
  }
  vTaskDelete(NULL);
}

static bool setupEspNow() {
  if (esp_now_init() != ESP_OK) {
    LOG_WRN("ESP-NOW init failed");
    return false;
  }
  esp_now_register_send_cb(OnDataSent);
  rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(AppDataPacket));
  if (esp_now_register_recv_cb(onReceive) != ESP_OK) {
    LOG_WRN("Failed to register receiver callback");
    return false;
  }
#ifdef ESPNOW_SENDER
  // find a compatible receiver
  addPeer(BROADCAST_ADDR);
  if (!scanForReceiver()) {
    LOG_WRN("Failed to find Receiver");
    return false;
  }
#else
  LOG_INF("ESP-NOW Receiver AP MAC: %s listening on channel: %ld", WiFi.AP.macAddress().c_str(), WiFi.channel()); 
#endif
  if (!espNowHandle) xTaskCreate(&espNowTask, "espNowTask", ESPNOW_STACK_SIZE, NULL, ESPNOW_PRI, &espNowHandle);
  if (!espNowHandle) {
    LOG_WRN("Failed to create espNowTask");
    return false;
  }
  return true;
}

#endif // ESPNOW_SENDER || ESPNOW_RECEIVER
