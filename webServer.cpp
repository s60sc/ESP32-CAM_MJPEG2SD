// Provides web server for user control of app
// httpServer handles browser requests 
// streamServer handles streaming and stills
// otaServer does file uploads
//
// s60sc 2022

#include "myConfig.h"

static esp_err_t fileHandler(httpd_req_t* req, bool download = false);
static void OTAtask(void* parameter);

static char inFileName[FILE_NAME_LEN];
static char variable[FILE_NAME_LEN]; 
static char value[FILE_NAME_LEN]; 

/********************* mjpeg2sd specific function **********************/

// stream separator
#define STREAM_CONTENT_TYPE "multipart/x-mixed-replace;boundary=" BOUNDARY_VAL
#define JPEG_BOUNDARY "\r\n--" BOUNDARY_VAL "\r\n"
#define JPEG_TYPE "Content-Type: image/jpeg\r\nContent-Length: %10u\r\n\r\n"
#define HDR_BUF_LEN 64
static const size_t boundaryLen = strlen(JPEG_BOUNDARY);
static char hdrBuf[HDR_BUF_LEN];

static httpd_handle_t streamServer = NULL; // streamer listens on port 81

static esp_err_t appSpecificHandler(httpd_req_t *req, const char* variable, const char* value) {
  // update handling specific to mjpeg2sd
  int intVal = atoi(value);
  if (!strcmp(variable, "sfile")) {
    // get folders / files on SD, save received filename if has required extension
    strcpy(inFileName, value);
    doPlayback = listDir(inFileName, jsonBuff, JSON_BUFF_LEN, FILE_EXT); // browser control
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  else if (!strcmp(variable, "updateFPS")) {
    sprintf(jsonBuff, "{\"fps\":\"%u\"}", setFPSlookup(fsizePtr));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  else if (!strcmp(variable, "fps")) setFPS(intVal);
  else if (!strcmp(variable, "framesize")) setFPSlookup(fsizePtr);
  return ESP_OK;
}

static esp_err_t streamHandler(httpd_req_t* req) {
  // send mjpeg stream or single frame
  esp_err_t res = ESP_OK;
  // if query string present, then single frame required
  bool singleFrame = (bool)httpd_req_get_url_query_len(req);                                       
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  char hdrBuf[HDR_BUF_LEN];
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegKB = 0;
  mjpegStruct mjpegData;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  // output header if streaming request
  if (!singleFrame) httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  if (doPlayback) {
    // playback mjpeg from SD
    openSDfile(inFileName);
    mjpegData = getNextFrame(true);
    while (doPlayback) {
      jpgLen = mjpegData.buffLen;
      size_t buffOffset = mjpegData.buffOffset;
      if (!jpgLen && !buffOffset) {
        // complete mjpeg streaming
        res = httpd_resp_send(req, JPEG_BOUNDARY, boundaryLen);
        doPlayback = false; 
      } else {
        if (jpgLen) {
          if (mjpegData.jpegSize) { // start of frame
            // send mjpeg header 
            res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, boundaryLen);
            size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, mjpegData.jpegSize);
            res = httpd_resp_send_chunk(req, hdrBuf, hdrLen);   
            frameCnt++;
          } 
          // send buffer 
          res = httpd_resp_send_chunk(req, (const char*)iSDbuffer+buffOffset, jpgLen);
        }
        mjpegData = getNextFrame(); 
      }
    }
  } else { 
    // live images
    do {
      camera_fb_t* fb;
      if (dbgMotion) {
        // motion tracking stream, wait for new move mapping image
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpgBuf, &jpgLen);
        if (!jpgLen) res = ESP_FAIL;
      } else {
        // stream from camera
        fb = esp_camera_fb_get();
        if (fb == NULL) return ESP_FAIL;
        jpgLen = fb->len;
        jpgBuf = fb->buf;
      }
      if (res == ESP_OK) {
        if (singleFrame) {
           httpd_resp_set_type(req, "image/jpeg");
           httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
           // send single jpeg to browser
           res = httpd_resp_send(req, (const char*)jpgBuf, jpgLen); 
        } else {
          // send next frame in stream
          res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, boundaryLen);    
          size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, hdrBuf, hdrLen);
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
        }
        frameCnt++;
      }
      xSemaphoreGive(motionMutex);
      if (fb != NULL) esp_camera_fb_return(fb);
      fb = NULL;  
      mjpegKB += jpgLen / 1024;
      if (res != ESP_OK) break;
    } while (!singleFrame);
    uint32_t mjpegTime = millis() - startTime;
    float mjpegTimeF = float(mjpegTime) / 1000; // secs
    if (singleFrame) LOG_INF("JPEG: %uB in %ums", jpgLen, mjpegTime);
    else LOG_INF("MJPEG: %u frames, total %ukB in %0.1fs @ %0.1ffps", frameCnt, mjpegKB, mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
  }
  return res;
}

void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t streamUri = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = NULL};
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &streamUri);
    LOG_INF("Starting streaming server on port: %u", config.server_port);
  } else LOG_ERR("Failed to start streaming server");
}

/********************* generic Web Server functions **********************/

#define OTAport 82
static WebServer otaServer(OTAport); 
static httpd_handle_t httpServer = NULL; // web server listens on port 80
static fs::FS fp = STORAGE;
byte chunk[RAMSIZE];

static bool sendChunks(File df, httpd_req_t *req) {   
  // use chunked encoding to send large content to browser
  size_t chunksize;
  do {
    chunksize = df.read(chunk, RAMSIZE); 
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
    sprintf(contentDisp, "attachment; filename=%s", inFileName);
    httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
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
  } else if (!strcmp(HTML_EXT, variable+(strlen(variable)-strlen(HTML_EXT)))) {
    // any html file
    httpd_resp_set_type(req, "text/html");
  } else if (!strcmp(JS_EXT, variable+(strlen(variable)-strlen(JS_EXT)))) {
    // any js file
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");
  } else if (!strcmp(TEXT_EXT, variable+(strlen(variable)-strlen(TEXT_EXT)))) {
    // any text file
    httpd_resp_set_type(req, "text/plain");
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
    doRestart(); 
  }
  // handler for downloading selected file, required file name in inFileName
  appSpecificHandler(req, variable, value); 
  if (!strcmp(variable, "download") && atoi(value) == 1) return fileHandler(req, true);
  httpd_resp_send(req, NULL, 0); 
  return ESP_OK;
}

static esp_err_t statusHandler(httpd_req_t *req) {
  bool quick = (bool)httpd_req_get_url_query_len(req); 
  buildJsonString(quick);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static void sendCrossOriginHeader() {
  // prevent CORS blocking request
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
  httpd_uri_t statusUri = {.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = NULL};

  config.max_open_sockets = MAX_CLIENTS; 
  if (httpd_start(&httpServer, &config) == ESP_OK) {
    httpd_register_uri_handler(httpServer, &indexUri);
    httpd_register_uri_handler(httpServer, &webUri);
    httpd_register_uri_handler(httpServer, &controlUri);
    httpd_register_uri_handler(httpServer, &statusUri);
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
  doRestart();
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
