// Provides web server for user control of app
// httpServer handles browser http requests and websocket interaction 
// otaServer does file uploads
//
// s60sc 2022

#include "appGlobals.h"

#define MAX_PAYLOAD_LEN 1000 // bigger than biggest websocket msg
#define DATA_UPDATE 999
#define OTAport 82
static WebServer otaServer(OTAport); 

static esp_err_t fileHandler(httpd_req_t* req, bool download = false);
static void startOTAserver();

char inFileName[FILE_NAME_LEN];
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN]; 
static char retainAction[2];
int refreshVal = 5000; // msecs

static httpd_handle_t httpServer = NULL; // web server listens on port 80
static int fdWs = -1; //websocket sockfd
static httpd_ws_frame_t wsPkt;

static fs::FS fp = STORAGE;
byte chunk[CHUNKSIZE];

static bool sendChunks(File df, httpd_req_t *req) {   
  // use chunked encoding to send large content to browser
  size_t chunksize;
  do {
    chunksize = df.read(chunk, CHUNKSIZE); 
    if (httpd_resp_send_chunk(req, (char*)chunk, chunksize) != ESP_OK) {
      df.close();
      return false;
    }
  } while (chunksize != 0);
  df.close();
  httpd_resp_send_chunk(req, NULL, 0);
  return true;
}

static esp_err_t fileHandler(httpd_req_t* req, bool download) {
  // send file contents to browser
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  File df = fp.open(inFileName);

  if (!df) {
    df.close();
    const char* resp_str = "File does not exist or cannot be opened";
    LOG_ERR("%s: %s", resp_str, inFileName);
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  } 
  if (download) {  
    // download file as attachment, required file name in inFileName
    LOG_INF("Download file: %s, size: %0.1fMB", inFileName, (float)(df.size()/ONEMEG));
    httpd_resp_set_type(req, "application/octet");
    char contentDisp[FILE_NAME_LEN + 50];
    char contentLength[10];
    sprintf(contentDisp, "attachment; filename=%s", inFileName);
    httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
    sprintf(contentLength, "%i", df.size());
    httpd_resp_set_hdr(req, "Content-Length", contentLength);
  }
  
  if (sendChunks(df, req)) LOG_INF("Sent %s to browser", inFileName);
  else {
    LOG_ERR("Failed to send %s to browser", inFileName);
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, "Failed to send file to browser", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;  
  }
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t indexHandler(httpd_req_t* req) {
  strcpy(inFileName, INDEX_PAGE_PATH);
  // first check if a startup failure needs to be reported
  if (strlen(startupFailure)) {
    httpd_resp_set_type(req, "text/html");                        
    return httpd_resp_send(req, startupFailure, HTTPD_RESP_USE_STRLEN);
  }
  // Show wifi wizard if not setup, using access point mode  
  if (!fp.exists(CONFIG_FILE_PATH) && WiFi.status() != WL_CONNECTED) {
    // Open a basic wifi setup page
    httpd_resp_set_type(req, "text/html");                              
    return httpd_resp_send(req, defaultPage_html, HTTPD_RESP_USE_STRLEN);
  } else {
    // first check if authentication is required
    if (strlen(Auth_Name)) {
      // authentication required
      size_t credLen = strlen(Auth_Name) + strlen(Auth_Pass) + 2; // +2 for colon & terminator
      char credentials[credLen];
      sprintf(credentials, "%s:%s", Auth_Name, Auth_Pass);
      size_t authLen = httpd_req_get_hdr_value_len(req, "Authorization") + 1;
      if (authLen) {
        // check credentials supplied are valid
        char auth[authLen];
        httpd_req_get_hdr_value_str(req, "Authorization", auth, authLen);
        if (!strstr(auth, encode64(credentials))) authLen = 0; // credentials not valid
      }
      if (!authLen) {
        // not authenticated
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic");
        httpd_resp_set_status(req, "401 Unauthorised");
        return httpd_resp_send(req, NULL, 0);
      }
    }
  }
  return fileHandler(req);
}

esp_err_t extractQueryKey(httpd_req_t *req, char* variable) {
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);
  // extract key 
  char* endPtr = strchr(variable, '=');
  if (endPtr != NULL) *endPtr = 0; // split variable into 2 strings, first is key name
  else {
    LOG_ERR("Invalid query string %s", variable);
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, "Invalid query string", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }
  return ESP_OK;
}

static esp_err_t webHandler(httpd_req_t* req) {
  // return required web page or component to browser using filename from query string
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);

  // check file extension to determine required processing before response sent to browser
  if (!strcmp(variable, "LOG.htm")) {
    flush_log(false);
  } else if (!strcmp(variable, "OTA.htm")) {
    // request for built in OTA page, if index html defective
    httpd_resp_set_type(req, "text/html"); 
    return httpd_resp_send(req, otaPage_html, HTTPD_RESP_USE_STRLEN);
  } else if (!strcmp(HTML_EXT, variable+(strlen(variable)-strlen(HTML_EXT)))) {
    // any other html file
    httpd_resp_set_type(req, "text/html");
  } else if (!strcmp(JS_EXT, variable+(strlen(variable)-strlen(JS_EXT)))) {
    // any js file
    httpd_resp_set_type(req, "text/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=604800");
  } else if (!strcmp(CSS_EXT, variable+(strlen(variable)-strlen(CSS_EXT)))) {
    // any css file
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");
  } else if (!strcmp(TEXT_EXT, variable+(strlen(variable)-strlen(TEXT_EXT)))) {
    // any text file
    httpd_resp_set_type(req, "text/plain");
  } else if (!strcmp(ICO_EXT, variable+(strlen(variable)-strlen(ICO_EXT)))) {
    // any icon file
    httpd_resp_set_type(req, "image/x-icon");
  } else if (!strcmp(SVG_EXT, variable+(strlen(variable)-strlen(SVG_EXT)))) {
    // any svg file
    httpd_resp_set_type(req, "image/svg+xml");
  } else LOG_WRN("Unknown file type %s", variable);  
  int dlen = snprintf(inFileName, FILE_NAME_LEN - 1, "%s/%s", DATA_DIR, variable);               
  if (dlen > FILE_NAME_LEN - 1) LOG_WRN("file name truncated");
  return fileHandler(req);
}

static esp_err_t controlHandler(httpd_req_t *req) {
  // process control query from browser 
  // obtain key from query string
  extractQueryKey(req, variable);
  if (!strcmp(variable, "startOTA")) startOTAserver();
  else {
    strcpy(value, variable + strlen(variable) + 1); // value points to second part of string
    if (!strcmp(variable, "reset")) {
      httpd_resp_send(req, NULL, 0); // stop browser resending reset
      doRestart("user requested restart"); 
      return ESP_OK;
    }
    updateStatus(variable, value);
    webAppSpecificHandler(req, variable, value); 
    // handler for downloading selected file, required file name in inFileName
    if (!strcmp(variable, "download") && atoi(value) == 1) return fileHandler(req, true);
  }
  httpd_resp_send(req, NULL, 0); 
  return ESP_OK;
}

static esp_err_t statusHandler(httpd_req_t *req) {
  uint8_t filter = (uint8_t)httpd_req_get_url_query_len(req); // filter number is length of query string
  buildJsonString(filter);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

bool parseJson(int rxSize) {
  // process json in jsonBuff to extract properly formatted flat key:value pairs  
  jsonBuff[rxSize - 1] = ','; // replace final '}' 
  jsonBuff[rxSize] = 0; // terminator
  char* ptr = jsonBuff + 1; // skip over initial '{'
  size_t itemLen = 0; 
  bool retAction = false;
  do {
    // get and process each key:value in turn
    char* endItem = strchr(ptr += itemLen, ':');
    itemLen = endItem - ptr;
    memcpy(variable, ptr, itemLen);
    variable[itemLen] = 0;
    removeChar(variable, '"');
    ptr++;
    endItem = strchr(ptr += itemLen, ',');
    itemLen = endItem - ptr;
    memcpy(value, ptr, itemLen);
    value[itemLen] = 0;
    removeChar(value, '"');
    ptr++;
    if (!strcmp(variable, "action")) {
      strcpy(retainAction, value);
      retAction = true;
    } else updateStatus(variable, value);
  } while (ptr + itemLen - jsonBuff < rxSize);
  return retAction;
}

static esp_err_t updateHandler(httpd_req_t *req) {
  // extract key pairs from received json string
  size_t rxSize = min(req->content_len, (size_t)JSON_BUFF_LEN);
  int ret = 0;
  // obtain json payload
  do {
    ret = httpd_req_recv(req, jsonBuff, rxSize);
    if (ret < 0) {  
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
      else {
        LOG_ERR("Post request failed with status %i", ret);
        return ESP_FAIL;
      }
    }
  } while (ret > 0);

  if (parseJson(rxSize)) webAppSpecificHandler (req, "action", retainAction); 
  httpd_resp_send(req, NULL, 0); 
  return ESP_OK;
}

static void sendCrossOriginHeader() {
  // prevent CORS from blocking request
  otaServer.sendHeader("Access-Control-Allow-Origin", "*");
  otaServer.sendHeader("Access-Control-Max-Age", "600");
  otaServer.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
  otaServer.sendHeader("Access-Control-Allow-Headers", "*");
  otaServer.send(204);
};


void wsAsyncSend(const char* wsData) {
  // websockets send function, used for async logging and status updates
  if (fdWs >= 0) {
    // send if connection active
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));                                           
    wsPkt.payload = (uint8_t*)(wsData);
    wsPkt.len = strlen(wsData);
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    wsPkt.final = true;
    esp_err_t ret = httpd_ws_send_frame_async(httpServer, fdWs, &wsPkt);
    if (ret != ESP_OK) LOG_ERR("websocket send failed with %s", esp_err_to_name(ret));
  } // else ignore
}

static esp_err_t wsHandler(httpd_req_t *req) {
  // receive websocket data and determine response
  // if a new connection is received, the old connection is closed, but the browser
  // page on the newer connection may need to be manually refreshed to take over the log
  if (req->method == HTTP_GET) {
    // websocket connection request from browser client
    if (fdWs != -1) {
      if (fdWs != httpd_req_to_sockfd(req)) {
        // websocket connection from browser when another browser connection is active
        LOG_WRN("closing connection, as newer Websocket on %u", httpd_req_to_sockfd(req));
        // kill older connection
        httpd_sess_trigger_close(httpServer, fdWs);
      }
    }
    fdWs = httpd_req_to_sockfd(req);
    if (fdWs < 0) {
      LOG_ERR("failed to get socket number");
      return ESP_FAIL;
    }
    LOG_INF("Websocket connection: %d", fdWs);
  } else {
    // data content received
    uint8_t wsMsg[MAX_PAYLOAD_LEN];
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    wsPkt.payload = wsMsg;
    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, MAX_PAYLOAD_LEN);
    if (ret != ESP_OK) {
      LOG_ERR("websocket receive failed with %s", esp_err_to_name(ret));
      return ret;
    }
    wsMsg[wsPkt.len] = 0; // terminator
    if (wsPkt.type == HTTPD_WS_TYPE_TEXT) wsAppSpecificHandler((char*)wsMsg);
  }
  return ESP_OK;
}

void killWebSocket() {
  // user requested
  if (fdWs >= 0) {
    httpd_sess_trigger_close(httpServer, fdWs);
    fdWs = -1;
  }
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32S3
  config.stack_size = 1024 * 8;
#endif  
  httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = NULL};
   httpd_uri_t webUri = {.uri = "/web", .method = HTTP_GET, .handler = webHandler, .user_ctx = NULL};
   httpd_uri_t controlUri = {.uri = "/control", .method = HTTP_GET, .handler = controlHandler, .user_ctx = NULL};
  httpd_uri_t updateUri = {.uri = "/update", .method = HTTP_POST, .handler = updateHandler, .user_ctx = NULL};
  httpd_uri_t statusUri = {.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = NULL};
  httpd_uri_t wsUri = {.uri = "/ws", .method = HTTP_GET, .handler = wsHandler, .user_ctx = NULL, .is_websocket = true};

  config.max_open_sockets = MAX_CLIENTS; 
  if (httpd_start(&httpServer, &config) == ESP_OK) {
    httpd_register_uri_handler(httpServer, &indexUri);
    httpd_register_uri_handler(httpServer, &webUri);
    httpd_register_uri_handler(httpServer, &controlUri);
    httpd_register_uri_handler(httpServer, &updateUri);
    httpd_register_uri_handler(httpServer, &statusUri);
    httpd_register_uri_handler(httpServer, &wsUri);
    LOG_INF("Starting web server on port: %u", config.server_port);
  } else LOG_ERR("Failed to start web server");
  debugMemory("startWebserver");
}

/*
 To apply web based OTA update.
 In Arduino IDE, create sketch binary:
 - select Tools / Partition Scheme / Minimal SPIFFS
 - select Sketch / Export compiled Binary
 On browser, press OTA Upload button
 On returned page, select Choose file and navigate to sketch .bin file,
 or data file to be uploaded to the storage /data folder
 */

static void uploadHandler() {
  // re-entrant callback function
  // apply received .bin file to OTA partition
  // or data file to SD card or SPIFFS partition
  HTTPUpload& upload = otaServer.upload();  
  static File df;
  static int cmd = DATA_UPDATE;    
  String filename = upload.filename;
  
  if (upload.status == UPLOAD_FILE_START) {
    if (strstr(filename.c_str(), ".bin") != NULL) {
      // partition update, sketch or SPIFFS
      LOG_INF("Partition update using file %s", filename.c_str());
      // a spiffs binary must have 'spiffs' in the filename
      cmd = (strstr(filename.c_str(), "spiffs") != NULL)  ? U_SPIFFS : U_FLASH;
      if (cmd == U_SPIFFS) STORAGE.end();// close relevant file system
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) Update.printError(Serial);
    } else {
      // replace relevant data file on storage
      char replaceFile[20] = DATA_DIR;
      strcat(replaceFile, "/");
      strcat(replaceFile, filename.c_str());
      LOG_INF("Data file update using %s", replaceFile);
      // Create file
      df = fp.open(replaceFile, FILE_WRITE);
      if (!df) {
        LOG_ERR("Failed to open %s on storage", replaceFile);
        return;
      }
    } 
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (cmd == DATA_UPDATE) {
      // web page update
      if (df.write(upload.buf, upload.currentSize) != upload.currentSize) {
        LOG_ERR("Failed to save %s on Storage", df.path());
        return;
      }
    } else {
      // OTA update, if crashes, check that correct partition scheme has been selected
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (cmd == DATA_UPDATE) {
      // data file update
      df.close();
      LOG_INF("Data file update complete");
    } else {
      if (Update.end(true)) { // true to set the size to the current progress
        LOG_INF("OTA update complete for %s", cmd == U_FLASH ? "Sketch" : "SPIFFS");
      } else Update.printError(Serial);
    }
  }
}

static void otaFinish() {
  flush_log(true);
  otaServer.sendHeader("Connection", "close");
  otaServer.sendHeader("Access-Control-Allow-Origin", "*");
  otaServer.send(200, "text/plain", (Update.hasError()) ? "OTA update failed, restarting ..." : "OTA update complete, restarting ...");
  doRestart("OTA completed");
}

static void OTAtask(void* parameter) {
  // receive OTA upload details
  LOG_INF("Starting OTA server on port: %u", OTAport);
  otaServer.on("/upload", HTTP_OPTIONS, sendCrossOriginHeader); 
  otaServer.on("/upload", HTTP_POST, otaFinish, uploadHandler);
  otaServer.begin();
  while (true) {
    otaServer.handleClient();
    delay(100);
  }
}

static void startOTAserver() {
  OTAprereq();
  if (fdWs >= 0) httpd_sess_trigger_close(httpServer, fdWs);
  // start OTA task
  static TaskHandle_t otaHandle = NULL;
  if (otaHandle == NULL) xTaskCreate(&OTAtask, "OTAtask", 1024 * 4, NULL, 1, &otaHandle);  
}
