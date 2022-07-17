// Provides web server for user control of app
// httpServer handles browser requests 
// otaServer does file uploads
//
// s60sc 2022

#include "globals.h"

#define MAX_CLIENTS 2 // allowing too many concurrent web clients can cause errors

static esp_err_t fileHandler(httpd_req_t* req, bool download = false);
static void OTAtask(void* parameter);

char inFileName[FILE_NAME_LEN];
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN]; 
int refreshVal = 5000; // msecs

/********************* generic Web Server functions **********************/

#define OTAport 82
static WebServer otaServer(OTAport); 
static httpd_handle_t httpServer = NULL; // web server listens on port 80
static int fdWs = -1; // for websockets
static fs::FS fp = STORAGE;
byte chunk[CHUNKSIZE];
uint8_t wsMsg[1024];

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
  // Show wifi wizard if not setup and access point mode  
  if (!fp.exists(INDEX_PAGE_PATH) && WiFi.status() != WL_CONNECTED) {
    // Open a basic wifi setup page
    httpd_resp_set_type(req, "text/html");                              
    return httpd_resp_send(req, defaultPage_html, HTTPD_RESP_USE_STRLEN);
  } else {
    // first check if authentication is required
    if (strlen(Auth_User)) {
      // authentication required
      size_t credLen = strlen(Auth_User) + strlen(Auth_Pass) + 2; // +2 for colon & terminator
      char credentials[credLen];
      sprintf(credentials, "%s:%s", Auth_User, Auth_Pass);
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

static esp_err_t extractQueryKey(httpd_req_t *req, char* variable) {
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
  if (!strcmp(variable, "OTA.htm")) {
    // special case for OTA
    xTaskCreate(&OTAtask, "OTAtask", 4096, NULL, 1, NULL);  
  } else if (!strcmp(variable, "LOG.htm")) {
    flush_log(false);
  } else if (!strcmp(HTML_EXT, variable+(strlen(variable)-strlen(HTML_EXT)))) {
    // any html file
    httpd_resp_set_type(req, "text/html");
  } else if (!strcmp(JS_EXT, variable+(strlen(variable)-strlen(JS_EXT)))) {
    // any js file
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");
  } else if (!strcmp(TEXT_EXT, variable+(strlen(variable)-strlen(TEXT_EXT)))) {
    // any text file
    httpd_resp_set_type(req, "text/plain");
  } else if (!strcmp(ICO_EXT, variable+(strlen(variable)-strlen(ICO_EXT)))) {
    // any icon file
    httpd_resp_set_type(req, "image/x-icon");
  } else LOG_WRN("Unknown file type %s", variable);  
  sprintf(inFileName, "%s/%s", DATA_DIR, variable);               
  return fileHandler(req);
}

static esp_err_t controlHandler(httpd_req_t *req) {
  // process control query from browser 
  // obtain key from query string
  extractQueryKey(req, variable);
  strcpy(value, variable + strlen(variable) + 1); // value is now second part of string
  if (!updateStatus(variable, value)) {
    httpd_resp_send(req, NULL, 0);  
    doRestart("user requested restart"); 
  }
  // handler for downloading selected file, required file name in inFileName
  webAppSpecificHandler(req, variable, value); 
  if (!strcmp(variable, "download") && atoi(value) == 1) return fileHandler(req, true);
  httpd_resp_send(req, NULL, 0); 
  return ESP_OK;
}

static esp_err_t statusHandler(httpd_req_t *req) {
  uint8_t filter = (uint8_t)httpd_req_get_url_query_len(req); 
  buildJsonString(filter);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t updateHandler(httpd_req_t *req) {
  // extract key pairs from received json string
  size_t rxSize = min(req->content_len, (size_t)JSON_BUFF_LEN);
  char retainAction[2];
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
  jsonBuff[rxSize - 1] = ','; // replace final '}' 

  // process json to extract key value pairs  
  char* ptr = jsonBuff + 1; 
  size_t itemLen = 0; 
  do {
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
    if (!strcmp(variable, "action")) strcpy(retainAction, value);
    else {
      if (!updateStatus(variable, value)) {
        httpd_resp_send(req, NULL, 0);  
        doRestart("user requested restart after data deletion");  
      } 
    }
  } while (ptr + itemLen - jsonBuff < rxSize);
  webAppSpecificHandler(req, "action", retainAction);
  httpd_resp_send(req, NULL, 0); 
  return ESP_OK;
}

void wsAsyncSend(char* wsData) {
  // websockets async send function, usedb for logging
  if (fdWs >=0) {
    httpd_ws_frame_t wsPkt;
    wsPkt.payload = (uint8_t*)wsData;
    wsPkt.len = strlen(wsData);
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    httpd_ws_send_frame_async(httpServer, fdWs, &wsPkt);
  }
}

static esp_err_t wsHandler(httpd_req_t *req) {
  // receive websocket data and determine response
  fdWs = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) LOG_INF("Websocket connection: %d", fdWs);
  else {
    // data content received
    httpd_ws_frame_t wsPkt;
    memset(&wsPkt, 0, sizeof(httpd_ws_frame_t));
    wsPkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
    if (ret != ESP_OK) {
      LOG_ERR("websocket failed to get frame len with %d", ret);
      return ret;
    }
    if (wsPkt.len) {
      wsPkt.payload = wsMsg;
      ret = httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
      if (ret != ESP_OK) {
        LOG_ERR("websocket receive failed with %d", ret);
        return ret;
      }
      wsMsg[wsPkt.len] = 0; // terminator
      processAppWSmsg(wsMsg);
    }
  }
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

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
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
}

/*
 To apply web based OTA update.
 In Arduino IDE, create sketch binary:
 - select Tools / Partition Scheme / Minimal SPIFFS
 - select Sketch / Export compiled Binary
 On browser, press OTA Upload button
 On returned page, select Choose file and navigate to sketch or spiffs .bin file
   in sketch folder, then press Update
 Similarly files ending '.htm' or '.txt' can be uploaded to the SD card /data folder
 */
 
static void uploadHandler() {
  // re-entrant callback function
  // apply received .bin file to SPIFFS or OTA partition
  // or update html file or config file on sd card
  HTTPUpload& upload = otaServer.upload();  
  static File df;
  static int cmd = 999;    
  String filename = upload.filename;
  if (upload.status == UPLOAD_FILE_START) {
    if ((strstr(filename.c_str(), HTML_EXT) != NULL)
        || (strstr(filename.c_str(), TEXT_EXT) != NULL)
        || (strstr(filename.c_str(), JS_EXT) != NULL)) {
      // replace relevant file
      char replaceFile[20] = DATA_DIR;
      strcat(replaceFile, "/");
      strcat(replaceFile, filename.c_str());
      LOG_INF("Data file update using %s", replaceFile);
      // Create file
      df = fp.open(replaceFile, FILE_WRITE);
      if (!df) {
        LOG_ERR("Failed to open %s on SD", replaceFile);
        return;
      }
    } else if (strstr(filename.c_str(), ".bin") != NULL) {
      // OTA update
      LOG_INF("OTA update using file %s", filename.c_str());
      OTAprereq();
      // if file name contains 'spiffs', update the spiffs partition
      cmd = (strstr(filename.c_str(), "spiffs") != NULL)  ? U_SPIFFS : U_FLASH;
      if (cmd == U_SPIFFS) SPIFFS.end(); // close SPIFFS if open
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) Update.printError(Serial);
    } else LOG_WRN("File %s not suitable for upload", filename.c_str());
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (cmd == 999) {
      // web page update
      if (df.write(upload.buf, upload.currentSize) != upload.currentSize) {
        LOG_ERR("Failed to save %s on SD", df.path());
        return;
      }
    } else {
      // OTA update, if crashes, check that correct partition scheme has been selected
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (cmd == 999) {
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
  static bool otaRunning = false;
  if (!otaRunning) {
    otaRunning = true;
    LOG_INF("Starting OTA server on port: %u", OTAport);
    otaServer.on("/upload", HTTP_OPTIONS, sendCrossOriginHeader); 
    otaServer.on("/upload", HTTP_POST, otaFinish, uploadHandler);
    otaServer.begin();
    while (true) {
      otaServer.handleClient();
      delay(100);
    }
  }
}
