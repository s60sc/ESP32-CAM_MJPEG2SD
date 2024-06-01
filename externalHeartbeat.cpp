#include "appGlobals.h"

// External Heartbeat
char external_heartbeat_domain[256] = "";         //External Heartbeat domain/IP  
char external_heartbeat_uri[256] = "";         //External Heartbeat uri (i.e. /myesp32-cam-hub/index.php)
char external_heartbeat_port[5] = "";                      //External Heartbeat server port to connect.  
char external_heartbeat_token[256] = "";           //External Heartbeat server username.  

bool external_heartbeat_active = false;

void sendExternalHeartbeat() {
  
  // external_heartbeat_active~0~2~C~External Heartbeat Server enabled
  // external_heartbeat_domain~~2~T~Heartbeat receiver domain or IP (i.e. www.mydomain.com)
  // external_heartbeat_uri~~2~T~Heartbeat receiver URI (i.e. /my-esp32cam-hub/index.php)
  // external_heartbeat_port~443~2~N~Heartbeat receiver port
  // external_heartbeat_token~~2~T~Heartbeat receiver auth token
  
  // POST to external heartbeat address
  
  char domain[256] = "";
  char uri[256] = "";
  char url[512] = ""; 
  
  strcpy( domain, external_heartbeat_domain);
  strcpy( uri, external_heartbeat_uri);
  
  strcpy( url, strcat(domain,uri));
  strcpy( url, strcat(url,"?token="));
  strcpy( url, strcat(url,external_heartbeat_token));
  
  strcpy( uri, strcat(uri,"?token="));
  strcpy( uri, strcat(uri,external_heartbeat_token));
  

  WiFiClientSecure hclient;
  
  
  buildJsonString(false);

  //hclient.setInsecure();
  if (remoteServerConnect(hclient, external_heartbeat_domain, atoi(external_heartbeat_port), "")) {
    HTTPClient https;
    int httpCode = HTTP_CODE_NOT_FOUND;
    if (https.begin(hclient, external_heartbeat_domain, atoi(external_heartbeat_port), uri, true)) {

      https.addHeader("Content-Type", "application/json");
      
      httpCode = https.POST(jsonBuff);
      //httpCode = https.GET();
      if (httpCode == HTTP_CODE_OK) {
        LOG_INF("External Heartbeat sent to: %s", url);
      } else LOG_WRN("External Heartbeat request failed, error: %s", https.errorToString(httpCode).c_str());    
      //if (httpCode != HTTP_CODE_OK) doGetExtIP = false;
      https.end();     
    }
    remoteServerClose(hclient);
  }
}
