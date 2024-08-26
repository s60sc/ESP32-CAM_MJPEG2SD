
// Simple generic Telegram bot supporting:
// - message interaction
// - photo upload
// - file upload (avoid using simultaneously with file upload via ftp, smtp, or browser)
// Add custom processing to appSetupTelegram() in appSpecific.cpp
//
// Using ideas from:
// - https://github.com/jameszah/ESP32-CAM-Video-Telegram
// - https://github.com/cotestatnt/AsyncTelegram2
// 
//
// s60sc 2023

#include "appGlobals.h"

#if INCLUDE_TGRAM
#define TELEGRAM_HOST "api.telegram.org"
#define LONG_POLL 60 // how long in secs to keep connection open without reply                            
#define MAX_HTTP_MSG 2048 // max size of buffer for HTTP request or response body
#define FORM_OFFSET 256 // offset in tgramBuff to prepare form data
#define MAX_TGRAM_SIZE (50 * ONEMEG) // max size for Telegram file upload

#define HTTP_VER "HTTP/1.1"
#define HTTP_CODE HTTP_VER " %d %*s\r"
#define POST_HDR "POST /bot%s/%s " HTTP_VER "\r\nHost: " TELEGRAM_HOST "\r\nContent-Length: %u\r\nContent-Type: "
#define FORM_DATA "--" BOUNDARY_VAL "\r\nContent-disposition: form-data; name=\""
#define CONTENT_TYPE "\"; filename=\"%s\"\r\nContent-Type: \"%s\"\r\n\r\n"
#define MULTI_TYPE "multipart/form-data; boundary=" BOUNDARY_VAL
#define JSON_TYPE "application/json"
#define GETUP_JSON "{\"limit\":1,\"timeout\":%d,\"offset\":%ld}"
#define POST_JSON "{\"chat_id\":%s,\"text\":\"%s\n\n%s%s\n\"}"
#define PARSE_MODE ",\"parse_mode\":\"%s\"}"
#define END_BOUNDARY "\r\n--" BOUNDARY_VAL "--\r\n"

#if (!INCLUDE_CERTS)
const char* telegram_rootCACertificate = "";
#endif

// set via web interface
bool tgramUse = false;
char tgramToken[MAX_PWD_LEN] = "";
char tgramChatId[MAX_IP_LEN] = "";

char tgramHdr[FILE_NAME_LEN];
static char keyValue[60] = ""; // holds value for searched key in JSON response
static char* tgramBuff = NULL; // holds sent then received data
static int32_t lastUpdate = 0;

TaskHandle_t telegramHandle = NULL;
NetworkClientSecure tclient;

static inline bool connectTelegram() {
  // Connect to Telegram server if not already connected
  return remoteServerConnect(tclient, TELEGRAM_HOST, HTTPS_PORT, telegram_rootCACertificate, TGRAMCONN);
}

static bool searchJsonResponse(const char* keyName) {
  // search json to extract value for given key, must end with a colon
  char* keyPtr = strstr(tgramBuff, keyName);
  if (keyPtr == NULL) return false;
  char* startItem = keyPtr + strlen(keyName);
  char* endItem = strchr(startItem, ',');
  int valSize = endItem - startItem;
  if (valSize > sizeof(keyValue) - 1) {
    LOG_WRN("Telegram JSON value too long %d", valSize); 
    valSize = sizeof(keyValue - 1);
  }
  strncpy(keyValue, startItem, valSize);
  keyValue[valSize] = 0;
  return true;
}

size_t getResponseHeader(NetworkClientSecure& sclient, const char* host, int waitSecs) {
  // get response header from remote server if available
  if (!waitSecs) waitSecs = responseTimeoutSecs;
  bool endOfHeader = false;
  size_t contentLen = 0;
  int httpCode = 0;
  uint32_t startTime = millis();
  if (sclient.available()) {
    while (!endOfHeader && millis() - startTime < waitSecs * 1000) {
      if (sclient.available()) { 
        String tline = sclient.readStringUntil('\n');
        //printf("Res: %s\n", tline.c_str());
        endOfHeader = tline.length() > 1 ? false : true; // blank line ends header
        if (!httpCode) sscanf(tline.c_str(), HTTP_CODE, &httpCode);  
        // get contentLength from header
        if (!contentLen) sscanf(tline.c_str(), "Content-Length: %d\r", &contentLen); 
      } else delay(100); 
    }
    if (!endOfHeader) {
      LOG_WRN("Timed out waiting for response from %s", host);
      return 0;
    } 
  } 
  return contentLen;
}

static bool getTgramResponse() {
  // receive response from Telegram if available and check if ok
  bool haveResponse = false;
  size_t readLen = 0;
  size_t contentLen = getResponseHeader(tclient, TELEGRAM_HOST, LONG_POLL);
  if (contentLen) {
    if (contentLen >= MAX_HTTP_MSG - 1) {
      LOG_WRN("contentLen %d exceeds buffer size", contentLen);
      contentLen = MAX_HTTP_MSG - 1;
    }
    while (contentLen - readLen > 0) {
      // retrieve response content
      size_t availLen = tclient.available();
      if (availLen) readLen += tclient.readBytes((uint8_t*)tgramBuff + readLen, availLen);
      delay(50);
    }
    // format tgramBuff for searchJsonResponse() 
    if (readLen != contentLen) LOG_WRN("Telegram data %d not equal to contentLength %d", readLen, contentLen);
    tgramBuff[contentLen] = 0;
    removeChar(tgramBuff, '"');
    replaceChar(tgramBuff, '}', ',');
    // check if response from Telegram has ok'd request
    if (searchJsonResponse("ok:")) {
      if (strcmp(keyValue, "true")) {
        // get error description
        if (searchJsonResponse("description:")) LOG_WRN("Telegram error: %s", keyValue);
        else LOG_WRN("Telegram error, but description not retrieved");
      } else if (searchJsonResponse("result:")) {
        // have response if result contains data, else just an ack
        if (strcmp(keyValue, "[]")) haveResponse = true;
      }
    } 
    //printf("Cnt: %s\n", tgramBuff);
    remoteServerClose(tclient); // end of transaction 
  } // else nothing received, so leave connection open
  return haveResponse;
}

static bool sendTgramHeader(const char* tmethod, const char* contentType, const char* dataType, 
  size_t fileSize, const char* fileName, const char* caption) {
  if (connectTelegram()) {
    // create http post header
    char* p = tgramBuff + FORM_OFFSET; // leave space for http request data
    bool isFile = dataType != NULL ? true : false; 
    if (isFile) {
      p += sprintf(p, FORM_DATA "chat_id\"\r\n\r\n%s", tgramChatId);
      if (caption != NULL) p += sprintf(p, "\r\n" FORM_DATA "caption\"\r\n\r\n%s", caption);
      p += sprintf(p, "\r\n" FORM_DATA "%s", dataType);
      p += sprintf(p, CONTENT_TYPE, fileName, contentType);
    } // else JSON data already loaded by sendTgramMessage
    size_t formLen = strlen(tgramBuff + FORM_OFFSET);
    // create http request header
    p = tgramBuff;
    if (isFile) fileSize += formLen + strlen(END_BOUNDARY);
    p += sprintf(p, POST_HDR, tgramToken, tmethod, fileSize);
    isFile ? strcat(p, MULTI_TYPE) : strcat(p, JSON_TYPE);
    strcat(p, "\r\n\r\n");
    size_t reqLen = strlen(tgramBuff);
    // concatenate request and form data
    if (formLen) {
      memmove(tgramBuff + reqLen, tgramBuff + FORM_OFFSET, formLen);
      tgramBuff[reqLen + formLen] = 0;
    }
    tclient.print(tgramBuff); // http header
    //printf("header:\n%s\n", tgramBuff);
    return true;
  }
  return false;
}

static bool sendTgramBuff(uint8_t* buffData, size_t buffSize) {
  // generic for any post message sending buffer content, eg photo
  if (connectTelegram()) {
    // send as chunks
    for (size_t i = 0; i < buffSize; i += CHUNKSIZE) tclient.write(buffData + i, min((int)(buffSize - i), CHUNKSIZE));
    tclient.println(END_BOUNDARY);
    return true;
  } 
  return false; 
}
    
bool prepTelegram() {
  // setup and check access to Telegram if required
  if (tgramUse) {
    if (strlen(tgramToken)) {
      if (tgramBuff == NULL) tgramBuff = psramFound() ? (char*)ps_malloc(MAX_HTTP_MSG) : (char*)malloc(MAX_HTTP_MSG); 
      // check connection with getme request
      bool res = false;
      sendTgramHeader("getMe", NULL, NULL, 0, NULL, NULL);
      uint32_t startTime = millis();
      while (!res && (millis() - startTime < responseTimeoutSecs * 1000)) {
        if (getTgramResponse()) res = true;
        delay(200);
      }
      if (res) {
        // response loaded into tgramBuff
        if (searchJsonResponse("username:")) {      
          LOG_INF("Connected to Telegram Bot Handle: %s", keyValue);
          xTaskCreate(appSpecificTelegramTask, "telegramTask", TGRAM_STACK_SIZE, NULL, TGRAM_PRI, &telegramHandle); 
          debugMemory("setupTelegramTask");
          return true;
        } else LOG_WRN("getMe response not parsed %s", tgramBuff);
      } else LOG_WRN("Failed to communicate with Telegram server");
    } else LOG_WRN("No Telegram Bot token supplied");
  } else LOG_INF("Telegram not being used");
  return false;
} 

bool getTgramUpdate(char* responseText) {
  // get and process message from Telegram 
  if (tclient.connected()) {
    // check for incoming message
    if (getTgramResponse()) {
      // process response and extract command if present
      if (searchJsonResponse("update_id:")) {
        int32_t update_id = atoi(keyValue);
        if (lastUpdate < update_id) {
          // new message, ok to process
          lastUpdate = update_id;
          if (searchJsonResponse("chat:{id:")) {
            if (!strcmp(tgramChatId, keyValue)) {
              if (searchJsonResponse("text:")) {
                strncpy(responseText, keyValue, FILE_NAME_LEN - 1);
                return true; // user request for app to process
              } // No text, ignore
            } else LOG_WRN("Message from unknown chat id: %s", keyValue);
          } else LOG_WRN("No chat id found");
        } else LOG_WRN("Old update_id: %d", update_id);
      } // no update_id, ignore
    } 
  } else {
    // send getUpdates request as not connected
    char* t = tgramBuff + FORM_OFFSET;
    t += sprintf(t, GETUP_JSON, LONG_POLL, lastUpdate + 1);
    sendTgramHeader("getUpdates", NULL, NULL, strlen(tgramBuff + FORM_OFFSET), NULL, NULL);
  }
  return false; // no response for app to process
}

bool sendTgramMessage(const char* info, const char* item, const char* parseMode) {
  // format message data as json, append to http header (buff overflow unlikely)
  char* t = tgramBuff + FORM_OFFSET;
  t += sprintf(t, POST_JSON, tgramChatId, tgramHdr, info, item);
  if (strlen(parseMode)) t += sprintf(t - 1, PARSE_MODE, parseMode); // overwrite previous '}'
  return sendTgramHeader("sendMessage", NULL, NULL, strlen(tgramBuff + FORM_OFFSET), NULL, NULL);
}

bool sendTgramPhoto(uint8_t* photoData, size_t photoSize, const char* caption) {
  // send photo stored in buffer to Telegram
  // max size of photo upload to Telegram is 10MB, bigger than ESP camera maximum
  if (sendTgramHeader("sendPhoto", "image/jpeg", "photo", photoSize, "frame.jpg", caption))
    return sendTgramBuff(photoData, photoSize);
  return false;
}

bool sendTgramFile(const char* fileName, const char* contentType, const char* caption) {
  // retrieve identified file from selected storage and send to Telegram
  if (connectTelegram()) {
    fs::FS fp = STORAGE;
    File df = fp.open(fileName);
    char errMsg[100] = "";
    if (df) {
      if (df.size() < MAX_TGRAM_SIZE) {
        sendTgramHeader("sendDocument", contentType, "document", df.size(), fileName, caption);
        // upload file content in chunks
        uint8_t percentLoaded = 0;
        size_t chunksize = 0, totalSent = 0;
        while ((chunksize = df.read((uint8_t*)tgramBuff, MAX_HTTP_MSG))) {
          tclient.write((uint8_t*)tgramBuff, chunksize);
          totalSent += chunksize;
          if (calcProgress(totalSent, df.size(), 5, percentLoaded)) LOG_INF("Downloaded %u%%", percentLoaded); 
        }
        df.close();
        tclient.println(END_BOUNDARY);
      } else snprintf(errMsg, sizeof(errMsg) - 1, "File size too large: %s", fmtSize(df.size()));        
    } else snprintf(errMsg, sizeof(errMsg) - 1, "File does not exist or cannot be opened: %s", fileName);
    if (strlen(errMsg)) {
      LOG_WRN("%s", errMsg);
      sendTgramMessage("ERROR: ", errMsg, "");
    }
  } else return false;
  return true;
}

#endif
