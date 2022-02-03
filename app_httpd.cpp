// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "myConfig.h"
#include "camera_index.h"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %10u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// end additions for mjpeg2sd.cpp

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        LOG_ERR("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len); 
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    LOG_INF("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t jpg_len = 0;
  uint8_t * jpg_buf = NULL;
  char * part_buf[64];

  static int64_t last_frame = 0;
  if (!last_frame) last_frame = esp_timer_get_time();

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  bool startPlayback = true;
  while (true) {
    // additions for mjpeg2sd.cpp
    if (doPlayback) {
      // playback mjpeg from SD
      if (startPlayback) openSDfile(); // open playback file when start streaming
      startPlayback = false;
      size_t* imgPtrs = getNextFrame(); 
      size_t clusterLen = imgPtrs[0];
      if (clusterLen) res = httpd_resp_send_chunk(req, (const char*)SDbuffer+imgPtrs[1], clusterLen);
      else doPlayback = false;
      if (res != ESP_OK) break;
    } else {   

      if (dbgMotion) {
        // wait for new move mapping image
        delay(100);
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpg_buf, &jpg_len);
        if (!jpg_len) res = ESP_FAIL;
      } else {
        xSemaphoreTake(frameMutex, portMAX_DELAY); 
        fb = esp_camera_fb_get();
        if (!fb) {
          LOG_ERR("Camera capture failed");
          res = ESP_FAIL;
        } else {
          jpg_len = fb->len;
          jpg_buf = fb->buf;
        }
      }
      // end of additions for mjpeg2sd.cpp
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));    
        size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, jpg_len);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen); 
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)jpg_buf, jpg_len);
      }
      if (fb){
        esp_camera_fb_return(fb);
        fb = NULL;
      } 
      (dbgMotion) ? xSemaphoreGive(motionMutex) : xSemaphoreGive(frameMutex);

      if (res != ESP_OK) break;
      int64_t fr_end = esp_timer_get_time();
      int64_t frame_time = fr_end - last_frame;
      last_frame = fr_end;
      frame_time /= 1000;

      if (dbgVerbose) LOG_INF("MJPG: %uB %ums (%.1ffps)", (uint32_t)(jpg_len),
         (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }
  }
  last_frame = 0;
  return res;
}

static void urlDecode(char* saveVal, const char* urlVal) {
  // replace url encoded characters
  std::string decodeVal(urlVal); 
  std::string replaceVal = decodeVal;
  std::smatch match; 
  while (regex_search(decodeVal, match, std::regex("(%)([0-9A-Fa-f]{2})"))) {
    std::string s(1, static_cast<char>(std::strtoul(match.str(2).c_str(),nullptr,16))); // hex to ascii 
    replaceVal = std::regex_replace(replaceVal, std::regex(match.str(0)), s);
    decodeVal = match.suffix().str();
  }
  strcpy(saveVal, replaceVal.c_str());
}
/* Not working with esp32 
bool formatMMC(){
    Serial.print("Formating card..");
    bool formatted = SPIFFS.format();
    if(formatted){
      Serial.println("\nSuccess formatting card");
    }else{
      Serial.println("\nError formatting card");
    }
    return formatted;    
}                
*/
static esp_err_t cmd_handler(httpd_req_t *req){
    esp_err_t res = ESP_OK;
                   
    char variable[32] = {0,};
    char value[100] = {0,};

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    char*  buf = (char*)malloc(buf_len);
    if (buf_len > 1) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char* decoded = (char*)malloc(buf_len);
        urlDecode(decoded, buf);
        if (httpd_query_key_value(decoded, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(decoded, "val", value, sizeof(value)) == ESP_OK) {
        } else res = ESP_FAIL;
                                                                  
      } else res = ESP_FAIL;
                                            
    } else res = ESP_FAIL;
         
    free(buf);
    if (res != ESP_OK) {    
      LOG_ERR("Failed to parse command query");    
      httpd_resp_send_404(req);
      return ESP_FAIL;
    }

    int val = atoi(value);
    sensor_t * s = esp_camera_sensor_get();
    res = ESP_OK;
    if(!strcmp(variable, "framesize")) {
      if(s->pixformat == PIXFORMAT_JPEG) {
        fsizePtr = val;
        setFPSlookup(fsizePtr);
        res = s->set_framesize(s, (framesize_t)fsizePtr);
      }
    }
    // additions for mjpeg2sd.cpp
    else if(!strcmp(variable, "sfile")) {
      listDir(value, htmlBuff); // get folders / files on SD
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, htmlBuff, strlen(htmlBuff));
    } 
    else if(!strcmp(variable, "fps")) setFPS(val);
    else if(!strcmp(variable, "minf")) minSeconds = val;
    else if(!strcmp(variable, "dbgVerbose")) {
      dbgVerbose = (val) ? true : false;
      Serial.setDebugOutput(dbgVerbose);
    } 
    else if(!strcmp(variable, "logMode")) {      
      logMode = val;
      if (logMode != 2) remote_log_init(); 
    } 
    else if(!strcmp(variable, "resetLog")) {            
      if (logMode == 1) reset_log(); 
    } 
    else if(!strcmp(variable, "updateFPS")) {
      fsizePtr = val;
      sprintf(htmlBuff, "{\"fps\":\"%u\"}", setFPSlookup(fsizePtr));
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, htmlBuff, strlen(htmlBuff));
    }
    else if(!strcmp(variable, "stopStream")) stopPlaying();
    else if(!strcmp(variable, "lamp")) {
      lampVal = (val) ? true : false; 
      controlLamp(lampVal);
    }
    else if(!strcmp(variable, "motion")) motionVal = val;
    else if(!strcmp(variable, "enableMotion")){
      //Turn on/off motion detection to save battery
      useMotion = (val) ? true : false; 
      LOG_INF("%s motion detection", useMotion ? "Enabling" : "Disabling");
    }
    else if(!strcmp(variable, "lswitch")) nightSwitch = val;
    else if(!strcmp(variable, "aviOn")) aviOn = val;
    else if(!strcmp(variable, "micGain")) micGain = val;
    else if(!strcmp(variable, "autoUpload")) autoUpload = val;
    else if(!strcmp(variable, "upload")) createUploadTask(value);  
    else if(!strcmp(variable, "uploadMove")) createUploadTask(value,true);  
    else if(!strcmp(variable, "delete")) deleteFolderOrFile(value);
    else if(!strcmp(variable, "record")) doRecording = (val) ? true : false;   
    else if(!strcmp(variable, "forceRecord")) forceRecord = (val) ? true : false;                                       
    else if(!strcmp(variable, "dbgMotion")) {
      dbgMotion = (val) ? true : false;   
      doRecording = !dbgMotion;
    }
    // enter <ip>/control?var=reset&val=1 on browser to force reset
    else if(!strcmp(variable, "reset")) { 
      logMode = 0;
      LOG_INF("Reset");
      remote_log_init(); // close any open logging
      ESP.restart();  
    }
    else if(!strcmp(variable, "save")) saveConfig();
    else if(!strcmp(variable, "defaults")) resetConfig();
    // end of additions for mjpeg2sd.cpp
    //Other settings
    else if(!strcmp(variable, "clockUTC")) syncToBrowser(value);      
    else if(!strcmp(variable, "timezone")) strcpy(timezone,value);
    else if(!strcmp(variable, "hostName")) urlDecode(hostName, value);
    else if(!strcmp(variable, "ST_SSID")) urlDecode(ST_SSID, value);
    else if(!strcmp(variable, "ST_Pass")) urlDecode(ST_Pass, value);
    else if(!strcmp(variable, "ftp_server")) urlDecode(ftp_server, value);
    else if(!strcmp(variable, "ftp_port")) strcpy(ftp_port,value);
    else if(!strcmp(variable, "ftp_user")) urlDecode(ftp_user, value);
    else if(!strcmp(variable, "ftp_pass")) urlDecode(ftp_pass, value);
    else if(!strcmp(variable, "ftp_wd")) strcpy(ftp_wd, value);
    
    else if(!strcmp(variable, "quality")) res = s->set_quality(s, val);
    else if(!strcmp(variable, "contrast")) res = s->set_contrast(s, val);
    else if(!strcmp(variable, "brightness")) res = s->set_brightness(s, val);
    else if(!strcmp(variable, "saturation")) res = s->set_saturation(s, val);
    else if(!strcmp(variable, "gainceiling")) res = s->set_gainceiling(s, (gainceiling_t)val);
    else if(!strcmp(variable, "colorbar")) res = s->set_colorbar(s, val);
    else if(!strcmp(variable, "awb")) res = s->set_whitebal(s, val);
    else if(!strcmp(variable, "agc")) res = s->set_gain_ctrl(s, val);
    else if(!strcmp(variable, "aec")) res = s->set_exposure_ctrl(s, val);
    else if(!strcmp(variable, "hmirror")) res = s->set_hmirror(s, val);
    else if(!strcmp(variable, "vflip")) res = s->set_vflip(s, val);
    else if(!strcmp(variable, "awb_gain")) res = s->set_awb_gain(s, val);
    else if(!strcmp(variable, "agc_gain")) res = s->set_agc_gain(s, val);
    else if(!strcmp(variable, "aec_value")) res = s->set_aec_value(s, val);
    else if(!strcmp(variable, "aec2")) res = s->set_aec2(s, val);
    else if(!strcmp(variable, "dcw")) res = s->set_dcw(s, val);
    else if(!strcmp(variable, "bpc")) res = s->set_bpc(s, val);
    else if(!strcmp(variable, "wpc")) res = s->set_wpc(s, val);
    else if(!strcmp(variable, "raw_gma")) res = s->set_raw_gma(s, val);
    else if(!strcmp(variable, "lenc")) res = s->set_lenc(s, val);
    else if(!strcmp(variable, "special_effect")) res = s->set_special_effect(s, val);
    else if(!strcmp(variable, "wb_mode")) res = s->set_wb_mode(s, val);
    else if(!strcmp(variable, "ae_level")) res = s->set_ae_level(s, val);
    else res = ESP_FAIL;
          
    if (res != ESP_OK) return httpd_resp_send_500(req);

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req){
    static char json_response[1024];

    sensor_t * s = esp_camera_sensor_get();
    char * p = json_response;
    *p++ = '{';
    // additions for mjpeg2sd.cpp
    p+=sprintf(p, "\"fps\":%u,", setFPS(0)); // get FPS value
    p+=sprintf(p, "\"minf\":%u,", minSeconds);
    p+=sprintf(p, "\"logMode\":%u,", logMode );
    p+=sprintf(p, "\"dbgVerbose\":%u,", dbgVerbose ? 1 : 0);
    p+=sprintf(p, "\"dbgMotion\":%u,", dbgMotion ? 1 : 0);
    p+=sprintf(p, "\"sfile\":%s,", "\"None\"");
    p+=sprintf(p, "\"lamp\":%u,", lampVal ? 1 : 0);
    p+=sprintf(p, "\"enableMotion\":%u,", useMotion ? 1 : 0);
    p+=sprintf(p, "\"motion\":%u,", (uint8_t)motionVal);
    p+=sprintf(p, "\"lswitch\":%u,", nightSwitch);
    p+=sprintf(p, "\"aviOn\":%u,", aviOn);
    p+=sprintf(p, "\"micGain\":%u,", micGain);
    p+=sprintf(p, "\"autoUpload\":%u,", autoUpload);
    p+=sprintf(p, "\"llevel\":%u,", lightLevel);
    p+=sprintf(p, "\"night\":%s,", nightTime ? "\"Yes\"" : "\"No\"");
    float aTemp = readDS18B20temp(true);
    if (aTemp > -127.0) p+=sprintf(p, "\"atemp\":\"%0.1f\",", aTemp);
    else p+=sprintf(p, "\"atemp\":\"n/a\",");
    float battV = battVoltage();
    if (battV < 0) p+=sprintf(p, "\"battv\":\"n/a\",");
    else p+=sprintf(p, "\"battv\":\"%0.1fV\",", battV);  
    p+=sprintf(p, "\"record\":%u,", doRecording ? 1 : 0);   
    p+=sprintf(p, "\"isrecord\":%s,", isCapturing ? "\"Yes\"" : "\"No\"");                                                              
    p+=sprintf(p, "\"forceRecord\":%u,", forceRecord ? 1 : 0);  
    // end of additions for mjpeg2sd.cpp
    p+=sprintf(p, "\"framesize\":%u,",fsizePtr);
    p+=sprintf(p, "\"quality\":%d,", s->status.quality);
    p+=sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p+=sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p+=sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p+=sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p+=sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p+=sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p+=sprintf(p, "\"awb\":%u,", s->status.awb);
    p+=sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p+=sprintf(p, "\"aec\":%u,", s->status.aec);
    p+=sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p+=sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p+=sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p+=sprintf(p, "\"agc\":%u,", s->status.agc);
    p+=sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p+=sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p+=sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p+=sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p+=sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p+=sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p+=sprintf(p, "\"vflip\":%u,", s->status.vflip);
    p+=sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p+=sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p+=sprintf(p, "\"colorbar\":%u,", s->status.colorbar);
    //Other settings 
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t currEpoch = tv.tv_sec;
    char buff[20];
    strftime(buff, 20, "%Y-%m-%d %H:%M:%S", localtime(&currEpoch));
    p+=sprintf(p, "\"clock\":\"%s\",", buff);
    strftime(buff, 20, "%Y-%m-%d %H:%M:%S", gmtime(&currEpoch));
    p+=sprintf(p, "\"clockUTC\":\"%s\",", buff);    
    p+=sprintf(p, "\"timezone\":\"%s\",", timezone);
    p+=sprintf(p, "\"hostName\":\"%s\",", hostName);
    p+=sprintf(p, "\"ST_SSID\":\"%s\",", ST_SSID);
    p+=sprintf(p, "\"ST_Pass\":\"%s\",", ST_Pass);
    p+=sprintf(p, "\"ftp_server\":\"%s\",", ftp_server);
    p+=sprintf(p, "\"ftp_port\":\"%s\",", ftp_port);
    p+=sprintf(p, "\"ftp_user\":\"%s\",", ftp_user);
    p+=sprintf(p, "\"ftp_pass\":\"%s\",", ftp_pass);
    p+=sprintf(p, "\"ftp_wd\":\"%s\",", ftp_wd);
    
    //Extend info
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) p+=sprintf(p, "\"card\":\"%s\",", "NO card");
    else{    
      if (cardType == CARD_MMC) p+=sprintf(p, "\"card\":\"%s\",", "MMC"); 
      else if (cardType == CARD_SD) p+=sprintf(p, "\"card\":\"%s\",", "SDSC");
      else if (cardType == CARD_SDHC) p+=sprintf(p, "\"card\":\"%s\",", "SDHC"); 
      uint64_t cardSize, totBytes, useBytes = 0;
      cardSize = SD_MMC.cardSize() / 1048576;
      totBytes = SD_MMC.totalBytes() / 1048576;
      useBytes = SD_MMC.usedBytes() / 1048576;
      p+=sprintf(p, "\"card_size\":\"%llu MB\",", cardSize);
      p+=sprintf(p, "\"used_bytes\":\"%llu MB\",", useBytes);
      p+=sprintf(p, "\"free_bytes\":\"%llu MB\",", totBytes - useBytes);
      p+=sprintf(p, "\"total_bytes\":\"%llu MB\",", totBytes);
    }
    p+=sprintf(p, "\"up_time\":\"%s\",", upTime().c_str());   
    p+=sprintf(p, "\"free_heap\":\"%u KB\",", (ESP.getFreeHeap() / 1024));    
    p+=sprintf(p, "\"wifi_rssi\":\"%i dBm\",", WiFi.RSSI() );  
    //p+=sprintf(p, "\"vcc\":\"%i V\",", ESP.getVcc() / 1023.0F; ); 
    p+=sprintf(p, "\"fw_version\":\"%s\"", APP_VER);  
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// additions for mjpeg2sd.cpp
static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");                              
    return httpd_resp_send(req, index_ov2640_html, strlen(index_ov2640_html));
}
static esp_err_t jquery_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/javascript");                              
    return httpd_resp_send(req, jquery_min_js_html, strlen(jquery_min_js_html));
}

static bool sendChunks(File f, httpd_req_t *req, bool doLog = false) {   
    /* Read file in chunks (relaxes any constraint due to large file sizes)
     * and send HTTP response in chunked encoding */
     
    #define BUFF_EXT 100
    #define BUFF_SIZE (32*1024)+BUFF_EXT // allow room for avi header
    byte *chunk = (byte*)ps_malloc(BUFF_SIZE); 
    if (chunk==NULL) {
      LOG_ERR("Chunk allocation failed");
      return false;
    }

    if (doLog){ //Header
      httpd_resp_send_chunk(req, "<html>\n", 7); // to enable html
      httpd_resp_send_chunk(req, "<body>\n", 7); 
      httpd_resp_send_chunk(req, "<pre>\n", 6); // to pretty format log lines
      httpd_resp_send_chunk(req, "<a name='top'></a>\n", 19); // top anchor
      sprintf((char*)chunk, " <a href='#bottom'>Go to Bottom</a>  <a onClick=\"if(!window.confirm('This will delete all log entries. Are you sure ?')) return false; fetch(`/control?var=resetLog&val=1`).then(response => { window.location.href='/log'; }); return false; \" href=''>Reset log</a>\n\n");
      httpd_resp_send_chunk(req, (char*)chunk, strlen((char*)chunk) );
    }
    // copy content of file from SD to browser
    size_t chunksize;
    do {
      chunksize = doLog ? f.read(chunk, BUFF_SIZE) : // raw log data
                          readClientBuf(f, chunk, BUFF_SIZE - BUFF_EXT); // formatted image data 
      if (chunksize > 0 && httpd_resp_send_chunk(req, (char*)chunk, chunksize) != ESP_OK) { //Don't send zero chunks as this will terminate httpd response
        f.close();        
        return false;
      }
    } while (chunksize != 0);
    free(chunk);
    f.close();
    if (doLog){ //Footer
      httpd_resp_send_chunk(req, "<a name='bottom'></a>\n", 22);
      httpd_resp_send_chunk(req, " <a href='#top'>Go to top</a>   <a onClick='window.location.reload' href=''>Refresh</a>\n", 88);
      httpd_resp_send_chunk(req, "<script>window.addEventListener('load',function(){setTimeout(function(){window.location.hash='#bottom';},200);});</script>\n", 123);
      httpd_resp_send_chunk(req, "</pre>\n", 7); //to pretty format log lines
      httpd_resp_send_chunk(req, "</body>\n", 8);
      httpd_resp_send_chunk(req, "</html>\n", 8);          
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return true;
}
          
// HTTP GET handler for downloading files 
esp_err_t file_get_handler(httpd_req_t *req)
{
    size_t filename_len = httpd_req_get_url_query_len(req)+1;
  
    char filename[filename_len+1] = {0};
    if (filename_len == 1) {
      const char * resp_str = "Please specify a filename. eg. file?somefile.txt";
      httpd_resp_send(req, resp_str, strlen(resp_str));
      return ESP_OK;
    }
    // Get null terminated filename
    httpd_req_get_url_query_str(req, filename, filename_len);
       
    File f = SD_MMC.open(filename);
    if (!f) {
      LOG_ERR("Failed to open: %s", filename);
      f.close();
      const char * resp_str = "File does not exist or cannot be opened";
      httpd_resp_send(req, resp_str, strlen(resp_str));
      return ESP_FAIL;
    }  

    if (strcmp(LOG_FILE_NAME, filename) == 0) flush_log(false);
    else {
      // determine if file is suitable for conversion to AVI 
      std::string sfile(filename);   
      if (isAVI(f)) sfile = std::regex_replace(sfile, std::regex("mjpeg"), "avi");
    }
    LOG_INF("Download file: %s, size: %0.1fMB", filename, (float)(f.size()/(1024*1024)));

    // change downloaded file name
    char newname[200] = {0};
    snprintf(newname, 199, "attachment; filename=%s", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", newname);
    
    return sendChunks(f, req) ? ESP_OK : ESP_FAIL;
}

esp_err_t log_get_handler(httpd_req_t *req) {
    
    flush_log(false); //Flush log
    
    File f = SD_MMC.open(LOG_FILE_NAME);
    if (!f) {
      LOG_ERR("Failed to open: %s", LOG_FILE_NAME);
      f.close();
      const char * resp_str = "Log file does not exist or cannot be opened";
      httpd_resp_send(req, resp_str, strlen(resp_str));
      return ESP_FAIL;
    }
    return sendChunks(f, req, true) ? ESP_OK : ESP_FAIL;
}

// end of additions for mjpeg2sd.cpp

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
        .user_ctx  = NULL
    };
    
    httpd_uri_t jquery_uri = {
        .uri       = "/jquery.min.js",
        .method    = HTTP_GET,
        .handler   = jquery_handler,
        .user_ctx  = NULL
    };
    
    httpd_uri_t file_serve = {
        .uri       = "/file",
        .method    = HTTP_GET,
        .handler   = file_get_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t log_serve = {
        .uri       = "/log",
        .method    = HTTP_GET,
        .handler   = log_get_handler,
        .user_ctx  = NULL
    };
    
    httpd_uri_t status_uri = {
        .uri       = "/status",
        .method    = HTTP_GET,
        .handler   = status_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t cmd_uri = {
        .uri       = "/control",
        .method    = HTTP_GET,
        .handler   = cmd_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };

    httpd_uri_t stream_uri = {
        .uri       = "/stream",
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };

    LOG_DBG("Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &jquery_uri);
        httpd_register_uri_handler(camera_httpd, &file_serve);
        httpd_register_uri_handler(camera_httpd, &log_serve);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    LOG_DBG("Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
