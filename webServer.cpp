// Provides web server for user control of app
// 
// s60sc 2022 - 2023

#include "appGlobals.h"

#define MAX_PAYLOAD_LEN 1000 // bigger than biggest websocket msg

char inFileName[IN_FILE_NAME_LEN];
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN]; 
static char retainAction[2];
int refreshVal = 5000; // msecs

static httpd_handle_t httpServer = NULL; // web server port 
static int fdWs = -1; // websocket sockfd
static httpd_ws_frame_t wsPkt;
bool useHttps = false;
bool useSecure = false;

static fs::FS fp = STORAGE;
static byte* chunk;

esp_err_t sendChunks(File df, httpd_req_t *req, bool endChunking) {   
  // use chunked encoding to send large content to browser
  size_t chunksize = 0;
  while ((chunksize = df.read(chunk, CHUNKSIZE))) {
    if (httpd_resp_send_chunk(req, (char*)chunk, chunksize) != ESP_OK) break;
    // httpd_sess_update_lru_counter(req->handle, httpd_req_to_sockfd(req));
  } 
  if (endChunking) {
    df.close();
    httpd_resp_sendstr_chunk(req, NULL);
  }
  if (chunksize) {
    LOG_ERR("Failed to send %s to browser", inFileName);
    httpd_resp_set_status(req, "500 Failed to send file");
    httpd_resp_sendstr(req, NULL);
  } 
  return chunksize ? ESP_FAIL : ESP_OK;
}

esp_err_t fileHandler(httpd_req_t* req, bool download) {
  // send file contents to browser
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!strcmp(inFileName, LOG_FILE_PATH)) flush_log(false);
  File df = fp.open(inFileName);
  if (!df) {
    df.close();
    LOG_ERR("File does not exist or cannot be opened: %s", inFileName);
    httpd_resp_set_status(req, "404 File Not Found");
    httpd_resp_sendstr(req, NULL);
    return ESP_FAIL;
  } 
  return (download) ? downloadFile(df, req) : sendChunks(df, req);
}

static void displayLog(httpd_req_t *req) {
  // output ram log to browser
  if (ramLog) {
    int startPtr, endPtr;
    startPtr = endPtr = mlogEnd;  
    httpd_resp_set_type(req, "text/plain"); 
    
    // output log in chunks
    do {
      int maxChunk = startPtr < endPtr ? endPtr - startPtr : RAM_LOG_LEN - startPtr;
      size_t chunkSize = std::min(CHUNKSIZE, maxChunk);    
      if (chunkSize > 0) httpd_resp_send_chunk(req, messageLog + startPtr, chunkSize); 
      startPtr += chunkSize;
      if (startPtr >= RAM_LOG_LEN) startPtr = 0;
    } while (startPtr != endPtr);
    httpd_resp_sendstr_chunk(req, NULL);
  } else {
    LOG_WRN("RAM Log not enabled");
    httpd_resp_sendstr(req, "400 RAM Log not enabled");
  }
}

static esp_err_t indexHandler(httpd_req_t* req) {
  strcpy(inFileName, INDEX_PAGE_PATH);
  // first check if a startup failure needs to be reported
  if (strlen(startupFailure)) {
    httpd_resp_set_type(req, "text/html");                        
    return httpd_resp_sendstr(req, startupFailure);
  }
  // Show wifi wizard if not setup, using access point mode  
  if (!fp.exists(INDEX_PAGE_PATH) && WiFi.status() != WL_CONNECTED) {
    // Open a basic wifi setup page
    httpd_resp_set_type(req, "text/html");                             
    return httpd_resp_sendstr(req, setupPage_html);
  } else {
    // first check if authentication is required
    if (strlen(Auth_Name)) {
      // authentication required
      size_t credLen = strlen(Auth_Name) + strlen(Auth_Pass) + 2; // +2 for colon & terminator
      char credentials[credLen];
      snprintf(credentials, credLen, "%s:%s", Auth_Name, Auth_Pass);
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
        return httpd_resp_sendstr(req, NULL);
      }
    }
  }
  return fileHandler(req);
}

esp_err_t extractHeaderVal(httpd_req_t *req, const char* variable, char* value) {
  // check if header field present, if so extract value
  esp_err_t res = ESP_FAIL;
  size_t hdrFieldLen = httpd_req_get_hdr_value_len(req, variable);
  if (!hdrFieldLen) LOG_WRN("Field %s not present", variable);
  else if (hdrFieldLen >= IN_FILE_NAME_LEN - 1) LOG_WRN("Field %s value too long (%d)", variable, hdrFieldLen);
  else {
    res = httpd_req_get_hdr_value_str(req, variable, value, hdrFieldLen + 1);
    if (res != ESP_OK) LOG_ERR("Value for %s could not be retrieved: %s", variable, espErrMsg(res));
  }
  return res;
}

esp_err_t extractQueryKeyVal(httpd_req_t *req, char* variable, char* value) {
  // get variable and value pair from URL query
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, variable, queryLen);
  urlDecode(variable);
  // extract key 
  char* endPtr = strchr(variable, '=');
  if (endPtr != NULL) {
    *endPtr = 0; // split variable into 2 strings, first is key name
    strcpy(value, variable + strlen(variable) + 1); // value is now second part of string
  } else {
    LOG_ERR("Invalid query string %s", variable);
    httpd_resp_set_status(req, "400 Invalid query string");
    httpd_resp_sendstr(req, NULL);
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
    // request for built in OTA page, if index html defective
    httpd_resp_set_type(req, "text/html"); 
    return httpd_resp_sendstr(req, otaPage_html);
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
  int dlen = snprintf(inFileName, IN_FILE_NAME_LEN - 1, "%s/%s", DATA_DIR, variable);               
  if (dlen >= IN_FILE_NAME_LEN) LOG_WRN("file name truncated");
  return fileHandler(req);
}

static esp_err_t controlHandler(httpd_req_t *req) {
  // process control query from browser 
  // obtain details from query string
  if (extractQueryKeyVal(req, variable, value) != ESP_OK) return ESP_FAIL;
  if (!strcmp(variable, "displayLog")) displayLog(req);
  else {
    strcpy(value, variable + strlen(variable) + 1); // value points to second part of string
    if (!strcmp(variable, "reset")) {
      httpd_resp_sendstr(req, NULL); // stop browser resending reset
      doRestart("user requested restart"); 
      return ESP_OK;
    }
    if (!strcmp(variable, "startOTA")) snprintf(inFileName, IN_FILE_NAME_LEN - 1, "%s/%s", DATA_DIR, value); 
    else {
      updateStatus(variable, value);
      appSpecificWebHandler(req, variable, value); 
    }
  }
  httpd_resp_sendstr(req, NULL); 
  return ESP_OK;
}

static esp_err_t statusHandler(httpd_req_t *req) {
  uint8_t filter = (uint8_t)httpd_req_get_url_query_len(req); // filter number is length of query string
  buildJsonString(filter);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, jsonBuff);
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
  // bulk update of config, extract key pairs from received json string
  size_t rxSize = min(req->content_len, (size_t)JSON_BUFF_LEN);
  int ret = 0;
  // obtain json payload
  do {
    ret = httpd_req_recv(req, jsonBuff, rxSize);
    if (ret < 0) {  
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
      else {
        LOG_ERR("Update request failed with status %i", ret);
      }
    }
  } while (ret > 0);
  httpd_resp_sendstr(req, NULL); 
  if (ret >= 0 && parseJson(rxSize)) appSpecificWebHandler(req, "action", retainAction); 
  return ret < 0 ? ESP_FAIL : ESP_OK;
}

void progress(size_t prg, size_t sz) {
  static uint8_t pcProgress = 0;
  if (calcProgress(prg, sz, 5, pcProgress)) LOG_INF("OTA uploaded %d%%", pcProgress); 
}

static esp_err_t uploadHandler(httpd_req_t *req) {
  // upload file for storage or firmware update
  esp_err_t res = appSpecificHeaderHandler(req);
  if (res == ESP_OK) {
    size_t fileSize = req->content_len;
    size_t rxSize = min(fileSize, (size_t)JSON_BUFF_LEN);
    int bytesRead = -1;
    LOG_INF("Upload file %s", inFileName);
    
    if (strstr(inFileName, ".bin") != NULL) {
      // partition update - sketch or SPIFFS
      LOG_INF("Firmware update using file %s", inFileName);
      OTAprereq();
      if (fdWs >= 0) httpd_sess_trigger_close(httpServer, fdWs);
      // a spiffs binary must have 'spiffs' in the filename
      int cmd = (strstr(inFileName, "spiffs") != NULL) ? U_SPIFFS : U_FLASH;
      if (cmd == U_SPIFFS) STORAGE.end(); // close relevant file system
      if (Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
        do {
          bytesRead = httpd_req_recv(req, jsonBuff, rxSize);
          if (bytesRead < 0) {  
            if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
              delay(10);
              continue;
            } else {
              LOG_ERR("Upload request failed with status %i", bytesRead);
              break;
            }
          }
          Update.write((uint8_t*)jsonBuff, (size_t)bytesRead);
          Update.onProgress(progress);
          fileSize -= bytesRead;
        } while (bytesRead > 0);
        if (!fileSize) Update.end(true); // true to set the size to the current progress
      }
      if (Update.hasError()) LOG_ERR("OTA failed with error: %s", Update.errorString());
      else LOG_INF("OTA update complete for %s", cmd == U_FLASH ? "Sketch" : "SPIFFS");
      httpd_resp_set_hdr(req, "Connection", "close");
      httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
      httpd_resp_sendstr(req, Update.hasError() ? "OTA update failed, restarting ..." : "OTA update complete, restarting ...");   
      doRestart("Restart after OTA");
  
    } else {
      // create / replace data file on storage
      File uf = fp.open(inFileName, FILE_WRITE);
      if (!uf) LOG_ERR("Failed to open %s on storage", inFileName);
      else {
        // obtain file content
        do {
          bytesRead = httpd_req_recv(req, jsonBuff, rxSize);
          if (bytesRead < 0) {  
            if (bytesRead == HTTPD_SOCK_ERR_TIMEOUT) {
              delay(10);
              continue;
            } else {
              LOG_ERR("Upload request failed with status %i", bytesRead);
              break;
            }
          }
          uf.write((const uint8_t*)jsonBuff, bytesRead);
        } while (bytesRead > 0);
        uf.close();
        res = bytesRead < 0 ? ESP_FAIL : ESP_OK;
        httpd_resp_sendstr(req, res == ESP_OK ? "Completed upload file" : "Failed to upload file, retry");
        if (res == ESP_OK) LOG_INF("Uploaded file %s", inFileName);
        else LOG_ERR("Failed to upload file %s", inFileName);     
      }
    }
  }
  return res;
}

static esp_err_t sendCrossOriginHeader(httpd_req_t *req) {
  // prevent CORS from blocking request
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Max-Age", "600");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST,GET,HEAD,OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "*");
  httpd_resp_set_status(req, "204");
  httpd_resp_sendstr(req, NULL); 
  return ESP_OK;
}

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
    if (wsPkt.type == HTTPD_WS_TYPE_TEXT) appSpecificWsHandler((char*)wsMsg);
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
  esp_err_t res = ESP_FAIL;
  chunk = psramFound() ? (byte*)ps_malloc(CHUNKSIZE) : (byte*)malloc(CHUNKSIZE); 
  size_t prvtkey_len = strlen(prvtkey_pem);
  size_t cacert_len = strlen(cacert_pem);
  if (useHttps && (!cacert_len || !prvtkey_len)) {
    useHttps = false;
    LOG_ALT("HTTPS not available as server keys not defined, using HTTP");
  }
  if (useHttps) {
    // HTTPS server
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
#if CONFIG_IDF_TARGET_ESP32S3
    config.httpd.stack_size = SERVER_STACK_SIZE;
#endif  
    config.cacert_pem = (const uint8_t*)cacert_pem;
    config.cacert_len = cacert_len + 1;
    config.prvtkey_pem = (const uint8_t*)prvtkey_pem;
    config.prvtkey_len = prvtkey_len + 1;
    config.httpd.server_port = HTTPS_PORT;
    config.httpd.ctrl_port = HTTPS_PORT;
    config.httpd.lru_purge_enable = true; // close least used socket 
    config.httpd.max_uri_handlers = 10;
    config.httpd.max_open_sockets = HTTP_CLIENTS + MAX_STREAMS;
    res = httpd_ssl_start(&httpServer, &config);
  } else {
    // HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
#if CONFIG_IDF_TARGET_ESP32S3
    config.stack_size = SERVER_STACK_SIZE;
#endif  
    config.server_port = HTTP_PORT;
    config.ctrl_port = HTTP_PORT;
    config.lru_purge_enable = true;   
    config.max_uri_handlers = 10;
    config.max_open_sockets = HTTP_CLIENTS + MAX_STREAMS;
    res = httpd_start(&httpServer, &config);
  }
  
  httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = NULL};
  httpd_uri_t webUri = {.uri = "/web", .method = HTTP_GET, .handler = webHandler, .user_ctx = NULL};
  httpd_uri_t controlUri = {.uri = "/control", .method = HTTP_GET, .handler = controlHandler, .user_ctx = NULL};
  httpd_uri_t updateUri = {.uri = "/update", .method = HTTP_POST, .handler = updateHandler, .user_ctx = NULL};
  httpd_uri_t statusUri = {.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = NULL};
  httpd_uri_t wsUri = {.uri = "/ws", .method = HTTP_GET, .handler = wsHandler, .user_ctx = NULL, .is_websocket = true};
  httpd_uri_t uploadUri = {.uri = "/upload", .method = HTTP_POST, .handler = uploadHandler, .user_ctx = NULL};
  httpd_uri_t optionsUri = {.uri = "/upload", .method = HTTP_OPTIONS, .handler = sendCrossOriginHeader, .user_ctx = NULL};
  httpd_uri_t sustainUri = {.uri = "/sustain", .method = HTTP_GET, .handler = appSpecificSustainHandler, .user_ctx = NULL};
  httpd_uri_t checkUri = {.uri = "/sustain", .method = HTTP_HEAD, .handler = appSpecificSustainHandler, .user_ctx = NULL};
 
  if (res == ESP_OK) {
    httpd_register_uri_handler(httpServer, &indexUri);
    httpd_register_uri_handler(httpServer, &webUri);
    httpd_register_uri_handler(httpServer, &controlUri);
    httpd_register_uri_handler(httpServer, &updateUri);
    httpd_register_uri_handler(httpServer, &statusUri);
    httpd_register_uri_handler(httpServer, &wsUri);
    httpd_register_uri_handler(httpServer, &uploadUri);
    httpd_register_uri_handler(httpServer, &optionsUri);
    httpd_register_uri_handler(httpServer, &sustainUri);
    httpd_register_uri_handler(httpServer, &checkUri);
    LOG_INF("Starting web server on port: %u", useHttps ? HTTPS_PORT : HTTP_PORT);
    LOG_INF("Remote server certificates %s checked", useSecure ? "are" : "not");
    if (DEBUG_MEM) {
      uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
      LOG_INF("Task httpServer stack space %u", freeStack);
    }
  } else LOG_ERR("Failed to start web server");
  
  debugMemory("startWebserver");
}
