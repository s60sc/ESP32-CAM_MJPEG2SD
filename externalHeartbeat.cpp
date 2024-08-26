
// Contributed by alojzjakob

#include "appGlobals.h"

#if INCLUDE_EXTHB

// External Heartbeat
char external_heartbeat_domain[32] = "";  //External Heartbeat domain/IP  
char external_heartbeat_uri[64] = "";     //External Heartbeat uri (i.e. /myesp32-cam-hub/index.php)
int external_heartbeat_port;              //External Heartbeat server port to connect.  
char external_heartbeat_token[32] = "";   //External Heartbeat server username.  

bool external_heartbeat_active = false;

void sendExternalHeartbeat() {
  
  // external_heartbeat_active~0~2~C~External Heartbeat Server enabled
  // external_heartbeat_domain~~2~T~Heartbeat receiver domain or IP (i.e. www.mydomain.com)
  // external_heartbeat_uri~~2~T~Heartbeat receiver URI (i.e. /my-esp32cam-hub/index.php)
  // external_heartbeat_port~443~2~N~Heartbeat receiver port
  // external_heartbeat_token~~2~T~Heartbeat receiver auth token
  
  // POST to external heartbeat address
  char uri[104] = "";
  strcpy(uri, external_heartbeat_uri);
  strcat(uri, "?token=");
  strcat(uri, external_heartbeat_token);
  
  NetworkClientSecure hclient;
  
  buildJsonString(false);

  //hclient.setInsecure();
  if (remoteServerConnect(hclient, external_heartbeat_domain, external_heartbeat_port, "", EXTERNALHB)) {
    HTTPClient https;
    int httpCode = HTTP_CODE_NOT_FOUND;
    if (https.begin(hclient, external_heartbeat_domain, external_heartbeat_port, uri, true)) {

      https.addHeader("Content-Type", "application/json");
      
      httpCode = https.POST(jsonBuff);
      //httpCode = https.GET();
      if (httpCode == HTTP_CODE_OK) {
        LOG_INF("External Heartbeat sent to: %s%s", external_heartbeat_domain, uri);
      } else LOG_WRN("External Heartbeat request failed, error: %s", https.errorToString(httpCode).c_str());    
      //if (httpCode != HTTP_CODE_OK) doGetExtIP = false;
      https.end();     
    }
    remoteServerClose(hclient);
  }
}

#endif
