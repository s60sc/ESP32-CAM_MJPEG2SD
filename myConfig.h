#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>  
#include <Preferences.h>
#include "remote_log.h"
#include "esp_adc_cal.h"

#define APP_NAME "ESP32-CAM_MJPEG"

//Wifi Station parameters
//If no default SSID value defined here
//will start an access point if no saved value found

char hostName[32] = ""; //Host name for ddns
char ST_SSID[32]  = ""; //Router ssid
char ST_Pass[64]  = ""; //Router passd

// leave following blank for dhcp
char ST_ip[16]  = ""; //Static IP
char ST_sn[16]  = ""; // subnet normally 255.255.255.0
char ST_gw[16]  = ""; // gateway to internet, normally router IP
char ST_ns1[16] = ""; // DNS Server, can be router IP (needed for SNTP)
char ST_ns2[16] = ""; // alternative DNS Server, can be blank

//Access point Config Portal SSID and Pass
#define ESP_getChipId() ((uint32_t)ESP.getEfuseMac())
String AP_SSID = "CAM_" + String(ESP_getChipId(), HEX);
char   AP_Pass[20] = "123456789";
char   AP_ip[16]  = ""; //Leave blank for 192.168.4.1
char   AP_sn[16]  = "";
char   AP_gw[16]  = "";

//Ftp server default params
char ftp_server[32] = "test.ftp.com";
char ftp_port[6]    = "21";
char ftp_user[32]   = "test";
char ftp_pass[32]   = "test";
char ftp_wd[64]     = "/home/user/";

//mjpeg2sd parameters
char timezone[64] = "GMT0BST,M3.5.0/01,M10.5.0/02";
uint8_t fsizePtr; // index to frameData[]
uint8_t minSeconds = 5; // default min video length (includes POST_MOTION_TIME)
bool doRecording = true; // whether to capture to SD or not
extern uint8_t FPS;
extern bool aviOn;
extern bool autoUpload;
extern byte dbgMode;                    
extern bool useMotion;                      

bool lampVal = false;
void controlLamp(bool lampVal);
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting
uint8_t setFPSlookup(uint8_t val);
uint8_t setFPS(uint8_t val);
/*  Handle config nvs load & save and wifi start   */
Preferences pref;

bool resetConfig() {
  ESP_LOGI(TAG, "Resetting config..");
  if (!pref.begin(APP_NAME, false)) {
    ESP_LOGE(TAG, "Failed to open config.");
    return false;
  }
  // Remove all preferences under the opened namespace
  pref.clear();
  // Close the Preferences
  pref.end();
  ESP_LOGI(TAG, "Reseting config OK.\nRebooting..");
  ESP.restart();
  return true;
}

bool saveConfig() {
    
  ESP_LOGI(TAG, "Saving config..");
  if (!pref.begin(APP_NAME, false)) {
    ESP_LOGE(TAG, "Failed to open config.");
    return false;
  }

  pref.putString("hostName", hostName);
  pref.putString("ST_SSID", ST_SSID);
  pref.putString("ST_Pass", ST_Pass);

  /* Not working if field ST_pass type="password"
    ESP_LOGI(TAG, "Save pass %s",ST_Pass);
  */
  pref.putString("ST_ip", ST_ip);
  pref.putString("ST_gw", ST_gw);
  pref.putString("ST_sn", ST_sn);
  pref.putString("ST_ns1", ST_ns1);
  pref.putString("ST_ns2", ST_ns2);

  pref.putString("timezone", timezone);
  pref.putUShort("framesize", fsizePtr);
  pref.putUChar("fps", FPS);
  pref.putUChar("minf", minSeconds);
  pref.putBool("useMotion", useMotion); 
  pref.putBool("doRecording", doRecording);
  pref.putFloat("motion", motionVal);
  pref.putBool("lamp", lampVal);
  pref.putBool("aviOn", aviOn);
  pref.putUChar("dbgMode", dbgMode);                                    
  pref.putBool("autoUpload", autoUpload);  
  pref.putUChar("lswitch", nightSwitch);

  pref.putString("ftp_server", ftp_server);
  pref.putString("ftp_port", ftp_port);
  pref.putString("ftp_user", ftp_user);
  pref.putString("ftp_pass", ftp_pass);
  pref.putString("ftp_wd", ftp_wd);

  //Sensor settings
  sensor_t * s = esp_camera_sensor_get();
  pref.putBytes("camera_sensor", &s->status, sizeof(camera_status_t) );
  // Close the Preferences
  pref.end();
  return true;
}

bool loadConfig() {
  bool saveDefPrefs = false;
  //resetConfig(); 
  ESP_LOGI(TAG, "Loading config..");
  AP_SSID.toUpperCase();
  if (!pref.begin(APP_NAME, false)) {
    ESP_LOGE(TAG, "Failed to open config.");
    saveConfig();
    return false;
  }
  strcpy(hostName, pref.getString("hostName", String(hostName)).c_str());
  //Add default hostname
  if (strlen(hostName) < 1) {
    ESP_LOGE(TAG, "Setting default hostname %s", hostName);
    strcpy(hostName, AP_SSID.c_str());
    //No nvs prefs yet. Save them at end
    saveDefPrefs = true;
  }
  if (strlen(pref.getString("ST_SSID").c_str()) > 0) {
    // only used stored values if non blank SSID has been set
    strcpy(ST_SSID, pref.getString("ST_SSID", String(ST_SSID)).c_str());
    strcpy(ST_Pass, pref.getString("ST_Pass", String(ST_Pass)).c_str());
  }

  ESP_LOGI(TAG, "Loaded ssid: %s pass: %s", String(ST_SSID).c_str(), String(ST_Pass).c_str());

  if (strlen(pref.getString("ST_ip").c_str()) > 0) {
    // only used stored values if non blank static IP has been stored
    strcpy(ST_ip, pref.getString("ST_ip",ST_ip).c_str());
    strcpy(ST_gw, pref.getString("ST_gw",ST_gw).c_str());
    strcpy(ST_sn, pref.getString("ST_sn",ST_sn).c_str());
    strcpy(ST_ns1, pref.getString("ST_ns1",ST_ns1).c_str());
    strcpy(ST_ns2, pref.getString("ST_ns2",ST_ns2).c_str());
  }
  fsizePtr = pref.getUShort("framesize", fsizePtr);
  FPS = pref.getUChar("fps", FPS);
  
  minSeconds = pref.getUChar("minf", minSeconds );
  doRecording = pref.getBool("doRecording", doRecording);
  aviOn = pref.getBool("aviOn", aviOn);
  autoUpload = pref.getBool("autoUpload", autoUpload);
  dbgMode = pref.getUChar("dbgMode", dbgMode);                                                  
  useMotion = pref.getUChar("useMotion", useMotion);
  motionVal = pref.getFloat("motion", motionVal);
  lampVal = pref.getBool("lamp", lampVal);
  controlLamp(lampVal);
  nightSwitch = pref.getUChar("lswitch", nightSwitch);

  strcpy(timezone, pref.getString("timezone", String(timezone)).c_str());
  strcpy(ftp_server, pref.getString("ftp_server", String(ftp_server)).c_str());
  strcpy(ftp_port, pref.getString("ftp_port", String(ftp_port)).c_str());
  strcpy(ftp_user, pref.getString("ftp_user", String(ftp_user)).c_str());
  strcpy(ftp_pass, pref.getString("ftp_pass", String(ftp_pass)).c_str());
  strcpy(ftp_wd, pref.getString("ftp_wd", String(ftp_wd)).c_str());

  size_t schLen = pref.getBytesLength("camera_sensor");
  char buffer[schLen]; // prepare a buffer for the data
  schLen = pref.getBytes("camera_sensor", buffer, schLen);
  if (schLen < 1 || schLen % sizeof(camera_status_t)) { // simple check that data fits
    ESP_LOGE(TAG, "Camera sensor data is not correct size! get %u, size: %u",schLen,sizeof(camera_status_t));
    //return false;
  }else{
    ESP_LOGI(TAG, "Setup camera_sensor, size get %u, def size: %u",schLen, sizeof(camera_status_t));
    camera_status_t * st = (camera_status_t *)buffer; // cast the bytes into a struct ptr
    sensor_t *s = esp_camera_sensor_get();
    s->set_ae_level(s,st->ae_level);
    s->set_aec2(s,st->aec2);
    s->set_aec_value(s,st->aec_value);
    s->set_agc_gain(s,st->agc_gain);
    s->set_awb_gain(s,st->awb_gain);
    s->set_bpc(s,st->bpc);
    s->set_brightness(s,st->brightness);
    s->set_colorbar(s,st->colorbar);
    s->set_contrast(s,st->contrast);
    s->set_dcw(s,st->dcw);
    s->set_denoise(s,st->denoise);
    s->set_exposure_ctrl(s,st->aec);
    s->set_framesize(s,st->framesize);
    s->set_gain_ctrl(s,st->agc);          
    s->set_gainceiling(s,(gainceiling_t)st->gainceiling);
    s->set_hmirror(s,st->hmirror);
    s->set_lenc(s,st->lenc);
    s->set_quality(s,st->quality);
    s->set_raw_gma(s,st->raw_gma);
    s->set_saturation(s,st->saturation);
    s->set_sharpness(s,st->sharpness);
    s->set_special_effect(s,st->special_effect);
    s->set_vflip(s,st->vflip);
    s->set_wb_mode(s,st->wb_mode);
    s->set_whitebal(s,st->awb);
    s->set_wpc(s,st->wpc);
    
    /* Not working Set fps and frame size on load time
    if(s->pixformat == PIXFORMAT_JPEG) {
        setFPSlookup(fsizePtr);
        setFPS(FPS);
        s->set_framesize(s, (framesize_t)fsizePtr);
    }*/
    
  }
  // Close the Preferences
  pref.end();

  if (saveDefPrefs) {
    ESP_LOGW(TAG, "Saving default config.");
    saveConfig();
  }
  return true;
}

// if pin 33 used as input for battery voltage, set VOLTAGE_DIVIDER value to be divisor 
// of input voltage from resistor divider, or 0 if battery voltage not being monitored
#define VOLTAGE_DIVIDER 0
#define BATT_PIN ADC1_CHANNEL_5 // ADC pin 33 for monitoring battery voltage
#define DEFAULT_VREF 1100 // if eFuse or two point not available on old ESPs
static esp_adc_cal_characteristics_t *adc_chars; // holds ADC characteristics
static const adc_atten_t ADCatten = ADC_ATTEN_DB_11; // attenuation level
static const adc_unit_t ADCunit = ADC_UNIT_1; // using ADC1
static const adc_bits_width_t ADCbits = ADC_WIDTH_BIT_11; // ADC bit resolution

void setupADC() {
  // Characterise ADC to generate voltage curve for battery monitoring 
  if (VOLTAGE_DIVIDER) {
    adc1_config_width(ADCbits);
    adc1_config_channel_atten(BATT_PIN, ADCatten);
    adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADCunit, ADCatten, ADCbits, DEFAULT_VREF, adc_chars);
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {ESP_LOGI(TAG, "ADC characterised using eFuse Two Point Value");}
    else if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {ESP_LOGI(TAG, "ADC characterised using eFuse Vref");}
    else {ESP_LOGI(TAG, "ADC characterised using Default Vref");}
  }
}

float battVoltage() {
  if (VOLTAGE_DIVIDER) {
    // get multiple readings of battery voltage from ADC pin and average
    // input battery voltage may need to be reduced by voltage divider resistors to keep it below 3V3.
    #define NO_OF_SAMPLES 16 // ADC multisampling
    uint32_t ADCsample = 0;
    for (int j = 0; j < NO_OF_SAMPLES; j++) ADCsample += adc1_get_raw(BATT_PIN); 
    ADCsample /= NO_OF_SAMPLES;
    // convert ADC averaged pin value to curve adjusted voltage in mV
    if (ADCsample > 0) ADCsample = esp_adc_cal_raw_to_voltage(ADCsample, adc_chars);
    return (float)ADCsample*VOLTAGE_DIVIDER/1000.0; // convert to battery volts
  } else return -1.0;
}
