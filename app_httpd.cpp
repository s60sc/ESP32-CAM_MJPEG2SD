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
#include "esp_http_server.h"
#include <SD_MMC.h>
#include "WiFi.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "camera_index.h"
#include "Arduino.h"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

// additions for mjpeg2sd.cpp
extern uint8_t fsizePtr;
extern uint8_t minSeconds;
extern bool debug;
extern bool debugMotion;
extern bool doRecording;
extern bool isCapturing;
extern uint8_t* SDbuffer;
extern char* htmlBuff; 
extern bool doPlayback;
extern bool stopPlayback;
extern SemaphoreHandle_t frameMutex;
extern SemaphoreHandle_t motionMutex;
bool lampVal = false;
void listDir(const char* fname, char* htmlBuff);
uint8_t setFPSlookup(uint8_t val);
uint8_t setFPS(uint8_t val);
size_t* getNextFrame();
bool fetchMoveMap(uint8_t **out, size_t *out_len);
void stopPlaying();
void controlLamp(bool lampVal);
void doUploadNAS();
void deleteFolderOrFile(const char* val);                   
// status & control fields 
extern float moduleTemp;
extern bool lampOn;
extern float motionVal;
extern bool nightTime;
extern uint8_t lightLevel;   
extern uint8_t nightSwitch;                                  
// end additions for mjpeg2sd.cpp

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t out_len, out_width, out_height;
    uint8_t * out_buf;
    bool s;
    size_t fb_len = 0;
    fb_len = fb->len;
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len); 
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t jpg_len = 0;
  uint8_t * jpg_buf = NULL;
  char * part_buf[64];
  int64_t fr_start = 0;

  static int64_t last_frame = 0;
  if (!last_frame) last_frame = esp_timer_get_time();

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    // additions for mjpeg2sd.cpp
    if (doPlayback) {
      // playback mjpeg from SD
      size_t* imgPtrs = getNextFrame(); 
      size_t clusterLen = imgPtrs[0];
      if (clusterLen) res = httpd_resp_send_chunk(req, (const char*)SDbuffer+imgPtrs[1], clusterLen);
      else doPlayback = false;
      if (res != ESP_OK) break;
    } else {   

      if (debugMotion) {
        // wait for new move mapping image
        delay(100);
        xSemaphoreTake(motionMutex, portMAX_DELAY);
        fetchMoveMap(&jpg_buf, &jpg_len);
        if (!jpg_len) res = ESP_FAIL;
      } else {
        xSemaphoreTake(frameMutex, portMAX_DELAY); 
        fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("Camera capture failed");
          res = ESP_FAIL;
        } else {
          jpg_len = fb->len;
          jpg_buf = fb->buf;
        }
      }
      // end of additions for mjpeg2sd.cpp
      fr_start = esp_timer_get_time();
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
      xSemaphoreGive(frameMutex);
      xSemaphoreGive(motionMutex);

      if (res != ESP_OK) break;
      int64_t fr_end = esp_timer_get_time();
      int64_t frame_time = fr_end - last_frame;
      last_frame = fr_end;
      frame_time /= 1000;

      if (debug) Serial.printf("MJPG: %uB %ums (%.1ffps)\n", (uint32_t)(jpg_len),
         (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time);
    }
  }
  last_frame = 0;
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
    esp_err_t res = ESP_OK;
                   
    char variable[32] = {0,};
    char value[100] = {0,};

    size_t buf_len = httpd_req_get_url_query_len(req) + 1;
    char*  buf = (char*)malloc(buf_len);
    if (buf_len > 1) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
          httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK) {
        } else res = ESP_FAIL;
                                                                  
      } else res = ESP_FAIL;
                                            
    } else res = ESP_FAIL;
         
    free(buf);
    if (res != ESP_OK) {    
      Serial.println("Failed to parse command query");    
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
    // additions for mpjpeg2sd.cpp
    else if(!strcmp(variable, "sfile")) {
      listDir(value, htmlBuff); // get folders / files on SD
      httpd_resp_set_type(req, "application/json");
      return httpd_resp_send(req, htmlBuff, strlen(htmlBuff));
    } 
    else if(!strcmp(variable, "fps")) setFPS(val);
    else if(!strcmp(variable, "minf")) minSeconds = val;
    else if(!strcmp(variable, "dbg")) {
      debug = (val) ? true : false;
      Serial.setDebugOutput(debug);
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
    else if(!strcmp(variable, "lswitch")) nightSwitch = val;
    else if(!strcmp(variable, "upload")) doUploadNAS();
    else if(!strcmp(variable, "delete")) deleteFolderOrFile(value); 
    else if(!strcmp(variable, "record")) doRecording = (val) ? true : false;   
    else if(!strcmp(variable, "dbgMotion")) {
      debugMotion = (val) ? true : false;   
      doRecording = !debugMotion;
    }
    // enter <ip>/control?var=reset&val=1 on browser to force reset
    else if(!strcmp(variable, "reset")) ESP.restart();   
    // end of additions for mpjpeg2sd.cpp
    
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
    // additions for mpjpeg2sd.cpp
    p+=sprintf(p, "\"fps\":%u,", setFPS(0)); // get FPS value
    p+=sprintf(p, "\"minf\":%u,", minSeconds);
    p+=sprintf(p, "\"dbg\":%u,", debug ? 1 : 0);
    p+=sprintf(p, "\"dbgMotion\":%u,", debugMotion ? 1 : 0);
    p+=sprintf(p, "\"sfile\":%s,", "\"None\"");
    p+=sprintf(p, "\"lamp\":%u,", lampVal ? 1 : 0);
    p+=sprintf(p, "\"motion\":%u,", (uint8_t)motionVal);
    p+=sprintf(p, "\"lswitch\":%u,", nightSwitch);
    p+=sprintf(p, "\"llevel\":%u,", lightLevel);
    p+=sprintf(p, "\"night\":%s,", nightTime ? "\"Yes\"" : "\"No\"");
    p+=sprintf(p, "\"atemp\":\"%0.1f\",", moduleTemp);
    p+=sprintf(p, "\"record\":%u,", doRecording ? 1 : 0);   
    p+=sprintf(p, "\"isrecord\":%s,", isCapturing ? "\"Yes\"" : "\"No\"");                                                              
    // end of additions for mpjpeg2sd.cpp
    p+=sprintf(p, "\"framesize\":%u,",fsizePtr);
    p+=sprintf(p, "\"quality\":%u,", s->status.quality);
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
    p+=sprintf(p, "\"colorbar\":%u", s->status.colorbar);
    //Extend info
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) p+=sprintf(p, "\"card\":%s", "NO card");
    else{    
      if (cardType == CARD_MMC) p+=sprintf(p, "\"card\":%s", "MMC"); 
      else if (cardType == CARD_SD) p+=sprintf(p, "\"card\":%s", "SDSC");
      else if (cardType == CARD_SDHC) p+=sprintf(p, "\"card\":%s", "SDHC"); 
      uint64_t cardSize, totBytes, useBytes = 0;
      cardSize = SD_MMC.cardSize() / 1048576;
      totBytes = SD_MMC.totalBytes() / 1048576;
      useBytes = SD_MMC.usedBytes() / 1048576;
      p+=sprintf(p, "\"card_size\":\"%llu MB\",", cardSize);
      p+=sprintf(p, "\"used_bytes\":\"%llu MB\",", useBytes);
      p+=sprintf(p, "\"total_bytes\":\"%llu MB\"", totBytes);
    }
    p+=sprintf(p, "\"free_heap\":\"%u KB\",", (ESP.getFreeHeap() / 1024));    
    p+=sprintf(p, "\"wifi_rssi\":\"%i dBm\"", WiFi.RSSI() );  
    
    *p++ = '}';
    *p++ = 0;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

// additions for mpjpeg2sd.cpp
static esp_err_t index_handler(httpd_req_t *req){
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_ov2640_html, strlen(index_ov2640_html));
}
// end of additions for mpjpeg2sd.cpp

void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    httpd_uri_t index_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_handler,
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

    if (debug) Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    if (debug) Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}
