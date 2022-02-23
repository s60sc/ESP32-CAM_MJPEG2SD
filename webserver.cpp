// Provides web server for user control of app
// httpServer handles browser requests 
// streamServer handles streaming and stills
// otaServer does file uploads
//
// s60sc 2022

#include "myConfig.h"

#define OTAport 82
static WebServer otaServer(OTAport); 
static httpd_handle_t httpServer = NULL; // web server listens on port 80
static httpd_handle_t streamServer = NULL; // streamer listens on port 81
static camera_fb_t* fb = NULL;
static File df; 
static char inFileName[FILE_NAME_LEN];
byte* chunk;

static void OTAtask(void* parameter);

static bool sendChunks(File df, httpd_req_t *req) {   
  // use chunked encoding to send large content to browser
  size_t chunksize;
  do {
    chunksize = readClientBuf(df, chunk, RAMSIZE); // leave space for mjpeg header
    if (httpd_resp_send_chunk(req, (char*)chunk, chunksize) != ESP_OK) {
      df.close();
      return false;
    }
  } while (chunksize != 0);
  df.close();
  httpd_resp_send_chunk(req, NULL, 0);
  return true;
}

static esp_err_t fileHandler(httpd_req_t* req, bool download = false) {
  // send file contents to browser
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  File df = SD_MMC.open(inFileName);

  if (!df) {
    df.close();
    const char* resp_str = "File does not exist or cannot be opened";
    LOG_ERR("%s: %s", resp_str, inFileName);
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }
  char newFileName[FILE_NAME_LEN]; 
  strcpy(newFileName, inFileName);
  if (download) {  
    // download file as attachment
    if (isAVI(df)) changeExtension(newFileName, inFileName, "avi");
    else strcpy(newFileName, inFileName);
    LOG_INF("Download file: %s, size: %0.1fMB", newFileName, (float)(df.size()/ONEMEG));
    char contentDisp[FILE_NAME_LEN + 50];
    sprintf(contentDisp, "attachment; filename=%s", newFileName);
    httpd_resp_set_hdr(req, "Content-Disposition", contentDisp);
  } 
  
  if (sendChunks(df, req)) LOG_INF("Sent %s to browser", newFileName);
  else {
    LOG_ERR("Failed to send %s to browser", newFileName);
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, "Failed to send file to browser", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;  
  }
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

const char* defaultPage_html = R"~(
<!doctype html>                             
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32-CAM_MJPEG setup</title> 
</head>
<script>
function Config(){
  if(!window.confirm('This will reboot the device to activate new settings.'))  return false; 
  fetch('/control?ST_SSID=' + encodeURI(document.getElementById('ST_SSID').value))
  .then(r => { console.log(r); return fetch('/control?ST_Pass=' + encodeURI(document.getElementById('ST_Pass').value)) })
  .then(r => { console.log(r); return fetch('/control?save=1') })     
  .then(r => { console.log(r); return fetch('/control?reset=1') })
  .then(r => { console.log(r); }); 
  return false;
}
</script>
<body style="font-size:18px">
<br>
<center>
  <table border="0">
    <tr><th colspan="3">ESP32-CAM_MJPEG2SD Wifi setup..</th></tr>
    <tr><td colspan="3"></td></tr>
    <tr>
    <td>SSID</td>
    <td>&nbsp;</td>
    <td><input id="ST_SSID" name="ST_SSID" length=32 placeholder="Router SSID" class="input"></td>
  </tr>
    <tr>
    <td>Password</td>
    <td>&nbsp;</td>
    <td><input id="ST_Pass" name="ST_Pass" length=64 placeholder="Router password" class="input"></td>
  </tr>
  <tr><td colspan="3"></td></tr>
    <tr><td colspan="3" align="center">
        <button type="button" onClick="return Config()">Connect</button>&nbsp;<button type="button" onclick="window.location.reload;">Cancel</button>
    </td></tr>
  </table>
</center>      
</body>
</html>
)~";

static esp_err_t indexHandler(httpd_req_t* req) {
  //Show wifi wizard if not setup and access point mode..  
  if( !SD_MMC.exists(WEB_PAGE_PATH) && WiFi.status() != WL_CONNECTED ){
      //Redir to a simple wifi setup page
      httpd_resp_set_type(req, "text/html");                              
      return httpd_resp_send(req, defaultPage_html, strlen(defaultPage_html));
  }
  strcpy(inFileName, WEB_PAGE_PATH);
  return fileHandler(req);
}

static esp_err_t jqueryHandler(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "max-age=604800");
  strcpy(inFileName, JQUERY_PATH);
  return fileHandler(req);
}

static esp_err_t controlHandler(httpd_req_t *req) {
  // process control query from browser 
  char query[FILE_NAME_LEN]; 
  size_t queryLen = httpd_req_get_url_query_len(req) + 1;
  httpd_req_get_url_query_str(req, query, queryLen);
  urlDecode(query);
  // extract key value pair
  char* value = strchr(query, '=');
  if (value != NULL) {
    *value = 0; // split query into 2 strings, first is key name
    value++; // second is value
  } else {
    LOG_ERR("Invalid query string");
    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_send(req, "Invalid query string", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
  }
  updateStatus(query, value);
  if (!strcmp(query, "sfile")) {
    // get folders / files on SD, save received filename if has required extension
    strcpy(inFileName, value);
    doPlayback = listDir(inFileName, jsonBuff, JSON_BUFF_LEN, FILE_EXT); // browser control
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  else if (!strcmp(query, "updateFPS")) {
    sprintf(jsonBuff, "{\"fps\":\"%u\"}", setFPSlookup(fsizePtr));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, jsonBuff, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  } 
  else if(!strcmp(query, "fps")) setFPS(atoi(value));
  else if(!strcmp(query, "framesize")) setFPSlookup(fsizePtr);
  else if (!strcmp(query, "download")) {
    // handler for downloading selected file, required file name set by 
    // previous 'sfile' value in inFileName
    httpd_resp_set_type(req, "application/octet");
    return fileHandler(req, true);
  }
  else if (!strcmp(query, "ota")) {
    xTaskCreate(&OTAtask, "OTAtask", 4096, NULL, 1, NULL); 
    strcpy(inFileName, OTA_PAGE_PATH);
    return fileHandler(req);
  }
  else if (!strcmp(query, "config")) {
    httpd_resp_set_type(req, "text/plain");
    strcpy(inFileName, CONFIG_FILE_PATH);
    return fileHandler(req);
  }
  else if (!strcmp(query, "log")) {
    strcpy(inFileName, LOG_PAGE_PATH);
    return fileHandler(req);
  }
  else if (!strcmp(query, "logText")) {
    httpd_resp_set_type(req, "text/plain");
    strcpy(inFileName, LOG_FILE_PATH);
    return fileHandler(req);
  }
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

static esp_err_t streamHandler(httpd_req_t* req) {
  // send mjpeg stream or single frame
  esp_err_t res = ESP_OK;
  // if query string present, then frame required
  bool doFrame = (bool)httpd_req_get_url_query_len(req);                                       
  camera_fb_t* fb = NULL;
  size_t jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  char hdrBuf[HDR_BUF_LEN];
  uint32_t startTime = millis();
  uint32_t frameCnt = 0;
  uint32_t mjpegKB = 0;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  if (!doFrame) {
    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (doPlayback) openSDfile(inFileName);
  }
  
  do {
    if (doPlayback) {
      // playback mjpeg from SD
      size_t* imgPtrs = getNextFrame(); 
      jpgLen = imgPtrs[0];
      if (jpgLen) res = httpd_resp_send_chunk(req, (const char*)SDbuffer+imgPtrs[1], jpgLen);
      else doPlayback = false;
      if (res != ESP_OK) break;
    } else {  
      if (dbgMotion) {
        // motion tracking stream, wait for new move mapping image
        delay(100);
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpgBuf, &jpgLen);
        if (!jpgLen) res = ESP_FAIL;
      } else {
        // stream from camera
        xSemaphoreTake(frameMutex, portMAX_DELAY); 
        fb = esp_camera_fb_get();
        if (!fb) {
          LOG_ERR("Camera capture failed");
          res = ESP_FAIL;
        } else {
          jpgLen = fb->len;
          jpgBuf = fb->buf;
        }
      }
      if (res == ESP_OK) {
        // send jpeg to browser
        if (doFrame) {
           httpd_resp_set_type(req, "image/jpeg");
           httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
           res = httpd_resp_send(req, (const char*)jpgBuf, jpgLen); 
        } else {
          res = httpd_resp_send_chunk(req, JPEG_BOUNDARY, strlen(JPEG_BOUNDARY));    
          size_t hdrLen = snprintf(hdrBuf, HDR_BUF_LEN-1, JPEG_TYPE, jpgLen);
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)hdrBuf, hdrLen); 
          if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpgBuf, jpgLen);
        }
      }
      if (fb) {
        esp_camera_fb_return(fb);
        fb = NULL;
      } 
      (dbgMotion) ? xSemaphoreGive(motionMutex) : xSemaphoreGive(frameMutex);
    }
    frameCnt++;
    mjpegKB += jpgLen / 1024;
    if (res != ESP_OK) break;
  } while (!doFrame);
  uint32_t mjpegTime = millis() - startTime;
  float mjpegTimeF = float(mjpegTime) / 1000; // secs
  if (doFrame) LOG_INF("JPEG: %uB in %ums", jpgLen, mjpegTime);
  else LOG_INF("MJPEG: %u frames, total %ukB in %0.1fs @ %0.1ffps", frameCnt, mjpegKB, mjpegTimeF, (float)(frameCnt) / mjpegTimeF);
  doPlayback = false;
  return res;
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
  chunk = (byte*)malloc(BUFF_SIZE); 
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler, .user_ctx = NULL};
  httpd_uri_t jqueryUri = {.uri = "/jquery.min.js", .method = HTTP_GET, .handler = jqueryHandler, .user_ctx = NULL};
  httpd_uri_t controlUri = {.uri = "/control", .method = HTTP_GET, .handler = controlHandler, .user_ctx = NULL};
  httpd_uri_t statusUri = {.uri = "/status", .method = HTTP_GET, .handler = statusHandler, .user_ctx = NULL};

  config.max_open_sockets = MAX_CLIENTS; 
  if (httpd_start(&httpServer, &config) == ESP_OK) {
    httpd_register_uri_handler(httpServer, &indexUri);
    httpd_register_uri_handler(httpServer, &jqueryUri);
    httpd_register_uri_handler(httpServer, &controlUri);
    httpd_register_uri_handler(httpServer, &statusUri);
    LOG_INF("Starting web server on port: %u", config.server_port);
  } else LOG_ERR("Failed to start web server");

  httpd_uri_t streamUri = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = NULL};
  config.server_port += 1;
  config.ctrl_port += 1;
  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &streamUri);
    LOG_INF("Starting streaming server on port: %u", config.server_port);
  } else LOG_ERR("Failed to start streaming server");
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
  // callback function
  // apply received .bin file to SPIFFS or OTA partition
  // or update html file or config file on sd card
  HTTPUpload& upload = otaServer.upload();  
  char replaceFile[20] = DATA_DIR;
  static int cmd = 999;    
  String filename = upload.filename;
  if (upload.status == UPLOAD_FILE_START) {
    if ((strstr(filename.c_str(), HTML_EXT) != NULL)
        || (strstr(filename.c_str(), TEXT_EXT) != NULL)
        || (strstr(filename.c_str(), JS_EXT) != NULL)) {
      // replace relevant file
      strcat(replaceFile, "/");
      strcat(replaceFile, filename.c_str());
      LOG_INF("Data file update using %s", filename.c_str());
      // Create file
      df = SD_MMC.open(replaceFile, FILE_WRITE);
      if (!df) {
        LOG_ERR("Failed to open %s on SD", filename.c_str());
        otaServer.sendHeader("Connection", "close");
        doRestart();
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
        LOG_ERR("Failed to save %s on SD", filename.c_str());
        otaServer.sendHeader("Connection", "close");
        doRestart();
      }
    } else {
      // OTA update, if crashes, check that correct partition scheme has been selected
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (cmd == 999) {
      // web page update
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
