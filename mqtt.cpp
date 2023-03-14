#include "appGlobals.h"
#ifdef INCLUDE_MQTT
#include "mqtt_client.h"

char mqtt_broker[16] = "";                   //Mqtt server ip to connect.  
char mqtt_port[5] = "";                      //Mqtt server port to connect.  
char mqtt_user[32] = "";                     //Mqtt server username.  
char mqtt_user_pass[MAX_PWD_LEN] = "";       //Mqtt server password.  
char mqtt_topic_prefix[FILE_NAME_LEN] = "";  //Mqtt server topic to pulish payloads.  


bool mqttEnabled = false;         //Is configured
bool mqttRunning = false;         //Is mqtt task running
bool mqttConnected = false;       //Is connected to broker?
esp_mqtt_client_handle_t mqtt_client = nullptr;
static TaskHandle_t mqttTaskHandle = NULL;
static char remoteQuery[128] = "";
static int mqttTaskDelay = 0;

void mqtt_client_publish(const char *topic, const char *payload){
    static constexpr int MQTT_QUALITY_OF_SERVICE = 1;
    static constexpr int MQTT_RETAIN_MESSAGE = 0;

     if (mqtt_client && mqttConnected){
        int id = esp_mqtt_client_publish(
            mqtt_client, topic, payload, strlen(payload), MQTT_QUALITY_OF_SERVICE, MQTT_RETAIN_MESSAGE);
        LOG_DBG("Mqtt pub, topic:%s, ID:%d, length:%i", topic, id, strlen(payload));
        LOG_INF("Mqtt pub, payload:%s", payload);
    }
}
void mqttPublish(const char *payload){
  static String mqttPublishTopic="";
  //Called before load config?    
  if(strlen(mqtt_topic_prefix)==0)  return; 
  
  if(mqttPublishTopic=="") 
    mqttPublishTopic = String(mqtt_topic_prefix) + hostName + "/status";

  mqtt_client_publish(mqttPublishTopic.c_str(), payload);
}

static void mqtt_connected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    LOG_INF("Mqtt connected");
    mqttConnected = true;
}

static void mqtt_disconnected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    LOG_INF("Mqtt disconnect");
    mqttConnected = false;
}
static void mqtt_data_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    LOG_DBG("Mqtt topic=%.*s ", event->topic_len, event->topic);
    LOG_INF("Mqtt data=%.*s ", event->data_len, event->data);
    if (strlen(remoteQuery) == 0) sprintf(remoteQuery, "%.*s", event->data_len, (char *)event->data);            
    mqttConnected = true;
    LOG_DBG("Resuming mqtt thread..");
    xTaskNotifyGive(mqttTaskHandle);
}
static void mqtt_error_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    LOG_DBG("Event base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    LOG_ERR("Mqtt event error");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
    {
        // log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
        // log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
        // log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
        LOG_ERR("Last err no string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
    }
}
void checkForRemoteQuerry() {
  //Execure remote querry i.e. dbgVerbose=1;framesize=7;fps=1
  if(strlen(remoteQuery) > 0) {
    char* query = strtok(remoteQuery, ";");
    while (query != NULL) {
        char* value = strchr(query, '=');
        if (value != NULL) {
          *value = 0; // split remoteQuery into 2 strings, first is key name
          value++; // second is value
          LOG_DBG("Mqtt exec q: %s v: %s", query, value);
          //Extra handling
          if (!strcmp(query, "restart")) { //Reboot
             doRestart("Mqtt remote restart");             
          }else{  
             //Block other tasks from accessing the camera
             if (!strcmp(query, "fps")) setFPS(atoi(value));
             else if (!strcmp(query, "framesize"))  setFPSlookup(fsizePtr);
             updateStatus(query, value);
          }          
        } else { //No params command
          LOG_DBG("Execute cmd: %s", query);
          if (!strcmp(query, "status")) {
            buildJsonString(false);
            mqttPublish(jsonBuff);
          } else if (!strcmp(query, "status?q")) {
            buildJsonString(true);
            mqttPublish(jsonBuff);
          }
        }
        query = strtok(NULL, ";");
    }
    remoteQuery[0] = '\0';
  }  
}

static void mqttTask(void* parameter) { 
  LOG_INF("Mqtt task start"); 
  while (mqttEnabled) {
    //LOG_DBG("Waiting for signal..");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    //LOG_DBG("Wake..");
    if (mqttConnected) {
        //Check if server sends a remote command
        checkForRemoteQuerry();
        if(mqttTaskDelay > 0 ) vTaskDelay(mqttTaskDelay / portTICK_RATE_MS);
    }else{ //Disconnected      
      LOG_ERR("Disconnected wait..");
      vTaskDelay(2000 / portTICK_RATE_MS);
    }        
    //xTaskNotifyGive(mqttTaskHandle);    
  }
  mqttRunning = false;
  LOG_INF("Mqtt Task exiting..");  
  vTaskDelete(NULL);
}
void stopMqtt(){
    if (mqtt_client == nullptr) return;    
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_disconnect(mqtt_client));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(mqtt_client));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_destroy(mqtt_client));
    mqttEnabled = false;
    LOG_INF("Stopping..%u", mqttTaskHandle);
    if ( mqttTaskHandle != NULL ) {
      LOG_DBG("Unlock task..");
      xTaskNotifyGive(mqttTaskHandle); //Unblock task
      vTaskDelay(1500 / portTICK_RATE_MS);
      LOG_DBG("Deleted task..?");
    }
    LOG_DBG("Closing..");

    mqttConnected = false;
    mqtt_client = nullptr;
}

void startMqttClient(void){
  mqttEnabled = (strlen(mqtt_broker) > 0);
  if(!mqttEnabled){
    LOG_ERR("MQTT not configured..");
    return;
  }
  
  if (mqttConnected) {
    LOG_DBG("MQTT is running.. Exiting");
    return;
  }
    
  if (WiFi.status() != WL_CONNECTED) {
    mqttConnected = false;
    LOG_WRN("Wifi disconnected.. Retry mqtt on connect");
    return;
  }
  
  String mqtt_uri = "mqtt://" + String(mqtt_broker) + ":" + String(mqtt_port) + "";
 
  esp_mqtt_client_config_t mqtt_cfg{.event_handle = NULL, .host = "", .uri = mqtt_uri.c_str(), .disable_auto_reconnect = false};
  mqtt_cfg.username = mqtt_user;
  mqtt_cfg.password = mqtt_user_pass;
  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  LOG_INF("Mqtt connect to %s...", mqtt_uri.c_str());
  if (mqtt_client != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_CONNECTED, mqtt_connected_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DISCONNECTED, mqtt_disconnected_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DATA, mqtt_data_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_ERROR, mqtt_error_handler, NULL));
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(mqtt_client)) != ESP_OK) {
        LOG_ERR("Mqtt start failed");    
    }else{
        LOG_DBG("Mqtt started");
        String topicCmd = String(mqtt_topic_prefix) + hostName + "/cmd";
        esp_mqtt_client_subscribe(mqtt_client, topicCmd.c_str(), 1);
        LOG_INF("Mqtt subscribed: %s", topicCmd.c_str() );
        ///Create a mqtt task
        mqttEnabled = true;
        BaseType_t xReturned = xTaskCreate(&mqttTask, "mqttTask", 4096, NULL, 1, &mqttTaskHandle);
        LOG_INF("Created mqtt task: %u", xReturned );
    }
  }else{
    stopMqtt();
  }
}
#endif