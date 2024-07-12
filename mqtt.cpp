
// contributed by gemi254 and genehand

#include "appGlobals.h"

#if INCLUDE_MQTT
#define CONFIG_MQTT_PROTOCOL_311
#include "mqtt_client.h" 

#if (!INCLUDE_CERTS)
const char* mqtt_rootCACertificate = "";
#endif
 
char mqtt_broker[MAX_HOST_LEN] = "";         //Mqtt server ip to connect.  
char mqtt_port[5] = "";                      //Mqtt server port to connect.  
char mqtt_user[MAX_HOST_LEN] = "";           //Mqtt server username.  
char mqtt_user_Pass[MAX_PWD_LEN] = "";       //Mqtt server password.  
char mqtt_topic_prefix[FILE_NAME_LEN / 2] = "";  //Mqtt server topic to publish payloads.  

#define MQTT_LWT_QOS 2
#define MQTT_LWT_RETAIN 1
#define MQTT_RETAIN 0
#define MQTT_QOS 1

bool mqtt_active = false;         //Is enabled
bool mqttRunning = false;         //Is mqtt task running
bool mqttConnected = false;       //Is connected to broker?
esp_mqtt_client_handle_t mqtt_client = nullptr;
TaskHandle_t mqttTaskHandle = NULL;
static char remoteQuery[FILE_NAME_LEN * 2] = "";
static char lwt_topic[FILE_NAME_LEN];
static char cmd_topic[FILE_NAME_LEN];
static int mqttTaskDelay = 0;
static char mqttPublishTopic[FILE_NAME_LEN] = "";

void mqtt_client_publish(const char* topic, const char* payload){
  if (!mqtt_client || !mqttConnected) return;
  int id = esp_mqtt_client_publish(mqtt_client, topic, payload, strlen(payload), MQTT_QOS, MQTT_RETAIN);
  LOG_VRB("Mqtt pub, topic:%s, ID:%d, length:%i", topic, id, strlen(payload));
  LOG_VRB("Mqtt pub, payload:%s", payload);
}

void mqttPublish(const char* payload) {
  if (!strlen(mqtt_topic_prefix)) return; //Called before load config?    
  if (!strlen(mqttPublishTopic)) snprintf(mqttPublishTopic, FILE_NAME_LEN, "%s%s/status", mqtt_topic_prefix, hostName);
  mqtt_client_publish(mqttPublishTopic, payload);
}

void mqttPublishPath(const char* suffix, const char* payload) {
  char topic[FILE_NAME_LEN];

  if (!strlen(mqtt_topic_prefix)) return;
  snprintf(topic, FILE_NAME_LEN, "%s%s/%s", mqtt_topic_prefix, hostName, suffix);
  mqtt_client_publish(topic, payload);
}

static void mqtt_connected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_INF("Mqtt connected");
  esp_mqtt_client_publish(mqtt_client, lwt_topic, "online", 0, MQTT_LWT_QOS, MQTT_LWT_RETAIN);
  mqttConnected = true;

  int id = esp_mqtt_client_subscribe(mqtt_client, cmd_topic, 1);
  if (id == -1){
    LOG_WRN("Mqtt failed to subscribe: %s", cmd_topic );
    stopMqttClient();
    return;
  }
  else LOG_VRB("Mqtt subscribed: %s", cmd_topic );
}

static void mqtt_disconnected_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_INF("Mqtt disconnect");
  mqttConnected = false;
  //xTaskNotifyGive(mqttTaskHandle); //Unblock task    
}

static void mqtt_data_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  LOG_VRB("Mqtt topic=%.*s ", event->topic_len, event->topic);
  LOG_VRB("Mqtt data=%.*s ", event->data_len, event->data);
  if (strlen(remoteQuery) == 0) sprintf(remoteQuery, "%.*s", event->data_len, (char*)event->data);            
  mqttConnected = true;
  LOG_VRB("Resuming mqtt thread..");
  xTaskNotifyGive(mqttTaskHandle);
}

static void mqtt_error_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  LOG_VRB("Event base=%s, event_id=%d", base, event_id);
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
  LOG_VRB("Mqtt event error %i", event->msg_id);    
  if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
    // log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
    // log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
    // log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
    LOG_WRN("Last err string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
    mqttConnected = false;
  }
}
void checkForRemoteQuery() {
  //Execute remote query i.e. dbgVerbose=1;framesize=7;fps=1
  if (strlen(remoteQuery) > 0) {
    char* query = strtok(remoteQuery, ";");
    while (query != NULL) {
      char* value = strchr(query, '=');
      if (value != NULL) {
        *value = 0; // split remoteQuery into 2 strings, first is key name
        value++; // second is value
        LOG_VRB("Mqtt exec q: %s v: %s", query, value);
        //Extra handling
        if (!strcmp(query, "restart")) { //Reboot
            doRestart("Mqtt remote restart");             
        } else if (!strcmp(query, "clockUTC")) { //Set time from browser clock
            
        } else {  
#ifdef ISCAM
          //Block other tasks from accessing the camera
          if (!strcmp(query, "fps")) setFPS(atoi(value));
          else if (!strcmp(query, "framesize"))  setFPSlookup(fsizePtr);
#endif
          updateStatus(query, value);
        }          
      } else { //No params command
        LOG_VRB("Execute cmd: %s", query);
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
  LOG_VRB("Mqtt task start"); 
  while (mqtt_active) {
    //LOG_VRB("Waiting for signal..");
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    //LOG_VRB("Wake..");
    if (mqttConnected) {
      //Check if server sends a remote command
      checkForRemoteQuery();
      if (mqttTaskDelay > 0 ) vTaskDelay(mqttTaskDelay / portTICK_RATE_MS);
    } else { //Disconnected      
      LOG_WRN("Disconnected wait..");
      vTaskDelay(2000 / portTICK_RATE_MS);
    }        
    //xTaskNotifyGive(mqttTaskHandle);    
  }
  mqttRunning = false;
  LOG_VRB("Mqtt Task exiting..");  
  vTaskDelete(NULL);
}

void stopMqttClient() {
  if (mqtt_client == nullptr) return;
  if (mqttConnected){
    esp_mqtt_client_publish(mqtt_client, lwt_topic, "offline", 0, MQTT_LWT_QOS, MQTT_LWT_RETAIN);
    vTaskDelay(1000 / portTICK_RATE_MS);
  }
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_stop(mqtt_client));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_destroy(mqtt_client));    
  LOG_VRB("Checking task..%u", mqttTaskHandle);
  if ( mqttTaskHandle != NULL ) {
    LOG_VRB("Unlock task..");
    xTaskNotifyGive(mqttTaskHandle); //Unblock task
    vTaskDelay(1500 / portTICK_RATE_MS);
    LOG_VRB("Deleted task..?");
  }
  LOG_VRB("Exiting..");
  mqttConnected = false;
  mqtt_client = nullptr;
}

void startMqttClient(void){  
  if (!mqtt_active) {
    LOG_VRB("MQTT not active..");
    return;
  }
  
  if (mqttConnected) {
    LOG_VRB("MQTT already running.. Exiting");
    return;
  }
    
  if (WiFi.status() != WL_CONNECTED) {
    mqttConnected = false;
    LOG_VRB("Wifi disconnected.. Retry mqtt on connect");
    return;
  }
  
  char mqtt_uri[FILE_NAME_LEN];
  sprintf(mqtt_uri, "mqtt://%s:%s", mqtt_broker, mqtt_port);
  snprintf(lwt_topic, FILE_NAME_LEN, "%s%s/lwt", mqtt_topic_prefix, hostName);
  snprintf(cmd_topic, FILE_NAME_LEN, "%s%s/cmd", mqtt_topic_prefix, hostName);
 
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
      .address = { .uri = mqtt_uri },
    },
    .credentials = {
      .username = mqtt_user,
      .client_id = hostName,
      .authentication = { .password = mqtt_user_Pass },
    },
    .session = {
      .last_will = {
        .topic = lwt_topic,
        .msg = "offline",
        .qos = MQTT_LWT_QOS,
        .retain = MQTT_LWT_RETAIN,
      },
    },
  };
#else
  esp_mqtt_client_config_t mqtt_cfg{.event_handle = NULL, .host = "", .uri = mqtt_uri, .disable_auto_reconnect = false};
  mqtt_cfg.username = mqtt_user;
  mqtt_cfg.password = mqtt_user_Pass;
  mqtt_cfg.client_id = hostName;
  mqtt_cfg.lwt_qos = MQTT_LWT_QOS;
  mqtt_cfg.lwt_msg = "offline";
  mqtt_cfg.lwt_retain = MQTT_LWT_RETAIN;  
  mqtt_cfg.lwt_topic = lwt_topic;
#endif

  mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  LOG_INF("Mqtt connect to %s...", mqtt_uri);
  //LOG_INF("Mqtt connect pass: %s...", mqtt_user_Pass);
  if (mqtt_client != NULL) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_CONNECTED, mqtt_connected_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DISCONNECTED, mqtt_disconnected_handler, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_DATA, mqtt_data_handler, mqtt_client));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_register_event(mqtt_client, esp_mqtt_event_id_t::MQTT_EVENT_ERROR, mqtt_error_handler, mqtt_client));
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(esp_mqtt_client_start(mqtt_client)) != ESP_OK) {
      LOG_WRN("Mqtt start failed");
    } else {
      LOG_VRB("Mqtt started");        
      // Create a mqtt task
      BaseType_t xReturned = xTaskCreate(&mqttTask, "mqttTask", MQTT_STACK_SIZE, NULL, MQTT_PRI, &mqttTaskHandle);
      LOG_INF("Created mqtt task: %u", xReturned );
      mqttRunning = true;
    }
  }
}

#endif
