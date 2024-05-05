//
// Handle microphone input, and speaker output via amp.
// The microphone input, and the output to amplifier, each make use of a
// separate I2S peripheral in the ESP32 or ESP32S3.
// I2S and PDM microphones are supported.
// I2S and (ESP32 only) analog amplifiers are supported.
//
// A remote microphone on another device can be used:
// - for VoiceChanger app, this is used instead of local mic
//   - need to select remote mic before selecting an action
// - for MJPEG2SD app, this is passed thru to speaker, independent of local mic
//   - need to enable use amp and pins in Config / Peripherals for Start Mic button to be available on web page
//   - remote mic should only be activated when need to speak
// Windows needs to allow microphone use in Microphone Privacy Settings
// In Microphone Properties / Advanced, check bit depth and sample rate (normally 16 bit 48kHz)
// Chrome needs to allow access to mic from insecure (http) site:
// Go to : chrome://flags/#unsafely-treat-insecure-origin-as-secure
// Enter following URL in box: http://<app_ip_address>
//
// s60sc 2024

#include "appGlobals.h"

#if INCLUDE_AUDIO 

bool micUse = false; // use local mic
bool micRem = false; // use remote mic (depends on app)
bool mampUse = false;
bool volatile stopAudio = false;

// I2S devices
bool I2Smic; // true if I2S, false if PDM
bool I2Samp; // true if I2S, false if DAC
i2s_port_t I2S_MIC_PORT;
i2s_port_t I2S_AMP_PORT;

// I2S Microphone pins
// INMP441 I2S microphone pinout, connect L/R to GND for left channel
// MP34DT01 PDM microphone pinout, connect SEL to GND for left channel
int micSckPin = -1; // I2S SCK
int micSWsPin = -1; // I2S WS, PDM CLK
int micSdPin = -1;  // I2S SD, PDM DAT, ADC

// I2S Amplifier pins
// MAX98357A 
// SD leave as mono (unconnected)
// Gain: 100k to GND works, not direct to GND. Unconnected is 9 dB 
int mampBckIo = -1; // I2S BCLK
int mampSwsIo = -1;  // I2S LRCLK
int mampSdIo = -1;   // I2S DIN

// ICSK025A analog amplifier (LM386)
// use right channel 
#ifdef CONFIG_IDF_TARGET_ESP32
#define I2S_DAC_CHANNEL I2S_DAC_CHANNEL_RIGHT_EN // GPIO25 (J5+ pin)
#endif

static i2s_pin_config_t i2s_mic_pins;
static i2s_pin_config_t i2s_amp_pins;

int ampTimeout = 1000; // ms for amp write abandoned if no output
uint32_t SAMPLE_RATE = 16000;  // audio rate in Hz
uint8_t PREAMP_GAIN = 3; // microphone preamplification factor
int8_t ampVol = 3; // amplifier volume factor

TaskHandle_t audioHandle = NULL;
static QueueHandle_t i2s_queue = NULL;
static i2s_event_t event;
static TaskHandle_t micRemHandle = NULL;

uint8_t* audioBuffer = NULL;
static size_t audioBytesUsed = 0;
static int totalSamples = 0;
static const uint8_t sampleWidth = sizeof(int16_t);
const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
int16_t* sampleBuffer = NULL;
static const char* micLabels[2] = {"PDM", "I2S"};
static const char* ampLabels[2] = {"DAC", "I2S"};

int micGain = 0;  // microphone gain 0 is off
static bool doStreamCapture = false; 
static size_t wsBufferLen = 0;
static uint8_t* wsBuffer = NULL;

#ifdef ISVC
#ifdef CONFIG_IDF_TARGET_ESP32S3
#define psramMax (ONEMEG * 6)
#else
#define psramMax (ONEMEG * 2)
#endif
#endif
#ifdef ISCAM
#define psramMax sampleBytes
static File wavFile;
static bool doMicCapture = false;
static bool captureRunning = false;
static bool captureStream = false;
#endif

static uint8_t wavHeader[WAV_HDR_LEN] = { // WAV header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};

// i2s config for microphone
static i2s_config_t i2s_mic_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = DMA_BUFF_CNT,
  .dma_buf_len = DMA_BUFF_LEN,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// I2S config for amplifier
static i2s_config_t i2s_amp_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // was right
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = DMA_BUFF_CNT,
  .dma_buf_len = DMA_BUFF_LEN,
  .use_apll = false,
  .tx_desc_auto_clear = true,
  .fixed_mclk = 0
};

static inline void IRAM_ATTR wakeTask(TaskHandle_t thisTask) {
  // utility function to resume task from interrupt
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(thisTask, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

void applyVolume() {
  // determine required volume setting
  int8_t adjVol = ampVol * 2; // use web page setting
  adjVol = checkPotVol(adjVol);  // use potentiometer setting if availabale
  if (adjVol) {
    // increase or reduce volume, 6 is unity eg midpoint of pot / web slider
    adjVol = adjVol > 5 ? adjVol - 5 : adjVol - 7; 
    // apply volume control to samples
    for (int i = 0; i < DMA_BUFF_LEN; i++) {   
      // apply volume control 
      sampleBuffer[i] = adjVol < 0 ? sampleBuffer[i] / abs(adjVol) : constrain((int32_t)sampleBuffer[i] * adjVol, SHRT_MIN, SHRT_MAX);
      if (!I2Samp) sampleBuffer[i] += 0x8000; // needs to be unsigned for DAC output
    }
  } // else turn off volume
}
  
static esp_err_t setupMic() {
  // setup microphone based on its type, default I2S
  int i2sMode = I2S_MODE_MASTER | I2S_MODE_RX;
  i2s_mic_pins.bck_io_num = micSckPin; 
  i2s_mic_pins.ws_io_num = micSWsPin;
  i2s_mic_pins.data_in_num = micSdPin;
  i2s_mic_pins.data_out_num = I2S_PIN_NO_CHANGE;
  
  if (micSWsPin == micSdPin) LOG_WRN("Microphone pins not defined");
  else {
    if (!I2Smic) {
      // Setup PDM microphone config & pins
      i2sMode |= I2S_MODE_PDM;
      i2s_mic_pins.bck_io_num = I2S_PIN_NO_CHANGE;
    }
    i2s_mic_config.mode = (i2s_mode_t)i2sMode;
    // set required sample rate
    i2s_mic_config.sample_rate = SAMPLE_RATE;
    return ESP_OK;
  }
  return ESP_FAIL;
}

static esp_err_t setupAmp() {
  // setup amplifier based on its type, default I2S
  int i2sMode = I2S_MODE_MASTER | I2S_MODE_TX;
  i2s_amp_pins.bck_io_num = mampBckIo;
  i2s_amp_pins.ws_io_num = mampSwsIo;
  i2s_amp_pins.data_out_num = mampSdIo;
  i2s_amp_pins.data_in_num = I2S_PIN_NO_CHANGE;
  // Setup ADC amp config
  if (!I2Samp) {
#ifdef CONFIG_IDF_TARGET_ESP32
    i2sMode |= I2S_MODE_DAC_BUILT_IN;
#else
    LOG_WRN("Analog Amp not supported");
    return ESP_FAIL;
#endif 
  }
  i2s_amp_config.sample_rate = SAMPLE_RATE;
  i2s_amp_config.mode = (i2s_mode_t)i2sMode;
  return ESP_OK; 
}

static esp_err_t startupMic() {
  // install & start up the I2S peripheral as microphone when activated
  const int queueSize = 4;
  if (i2s_queue == NULL) i2s_queue = xQueueCreate(queueSize, sizeof(i2s_event_t));
  esp_err_t res = i2s_driver_install(I2S_MIC_PORT, &i2s_mic_config, queueSize, &i2s_queue);
  if (res == ESP_OK) {
    i2s_set_pin(I2S_MIC_PORT, &i2s_mic_pins);
    // clear the DMA buffers
    i2s_zero_dma_buffer(I2S_MIC_PORT);  
  } else LOG_WRN("Unable to startup mic");
  return res;
}

static esp_err_t startupAmp() {
  // install & start up the I2S peripheral as amplifier
  esp_err_t res = i2s_driver_install(I2S_AMP_PORT, &i2s_amp_config, 0, NULL);
  if (res == ESP_OK) {
    if (!I2Samp) {
#if defined(CONFIG_IDF_TARGET_ESP32) && defined (ISVC)
      i2s_set_dac_mode(I2S_DAC_CHANNEL); // DAC device
#endif
    } else i2s_set_pin(I2S_AMP_PORT, &i2s_amp_pins); // I2S or PDM device
    // clear the DMA buffers
    i2s_zero_dma_buffer(I2S_AMP_PORT);
  } else LOG_WRN("Unable to startup amp");
  return res;
}

static void stopI2S(i2s_port_t I2Schan) {
  // stop the relevant I2S peripheral
  i2s_stop(I2Schan);
  i2s_driver_uninstall(I2Schan);
  LOG_DBG("Stopped I2S port %d", I2Schan);
}

static size_t micInput() {
  // read sample from microphone if DMA ready
  size_t bytesRead = 0; 
  uint8_t gainFactor = pow(2, micGain - MIC_GAIN_CENTER);
  // wait a period for i2s buffer to be ready
  if (xQueueReceive(i2s_queue, &event, (2 * SAMPLE_RATE) /  portTICK_PERIOD_MS) == pdPASS) {
    if (event.type == I2S_EVENT_RX_DONE) {
      i2s_read(I2S_MIC_PORT, sampleBuffer, sampleBytes, &bytesRead, portMAX_DELAY);
      int samplesRead = bytesRead / sampleWidth;
      // apply preamp gain
      for (int i = 0; i < samplesRead; i++) sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
      if (doStreamCapture && !audioBytesUsed) memcpy(audioBuffer, sampleBuffer, bytesRead);
    }
  }
  return bytesRead;
}

static void ampOutput(size_t bytesRead = sampleBytes, bool speaker = true) {
  // output to amplifier
  // apply required filtering and volume
  applyFilters();
  size_t bytesWritten = 0;
  if (speaker) i2s_write(I2S_AMP_PORT, sampleBuffer, bytesRead, &bytesWritten, ampTimeout);
  displayAudioLed(sampleBuffer[0]);
}

size_t updateWavHeader() {
  // update wav header
  uint32_t dataBytes = totalSamples * sampleWidth;
  uint32_t wavFileSize = dataBytes ? dataBytes + WAV_HDR_LEN - 8 : 0; // wav file size excluding chunk header
  memcpy(wavHeader+4, &wavFileSize, 4);
  memcpy(wavHeader+24, &SAMPLE_RATE, 4); // sample rate
  uint32_t byteRate = SAMPLE_RATE * sampleWidth; // byte rate (SampleRate * NumChannels * BitsPerSample/8)
  memcpy(wavHeader+28, &byteRate, 4); 
  memcpy(wavHeader+WAV_HDR_LEN-4, &dataBytes, 4); // wav data size
#ifdef ISVC
  memcpy(audioBuffer, wavHeader, WAV_HDR_LEN);
#endif
  return dataBytes;
}

void remoteMicHandler(uint8_t* wsMsg, size_t wsMsgLen) {
  // input from remote mic via websocket
  if (!stopAudio && !wsBufferLen) {
    memcpy(wsBuffer, wsMsg, wsMsgLen);
    wsBufferLen = wsMsgLen;
    if (micRemHandle != NULL) xTaskNotifyGive(micRemHandle);
    else LOG_WRN("Reboot ESP to use remote mic");
  }
}

void applyMicRemGain() {
  float gainFactor = (float)pow(2, micGain - MIC_GAIN_CENTER);
  int16_t* wsPtr = (int16_t*) wsBuffer;
  for (int i = 0; i < wsBufferLen / sizeof(int16_t); i++) wsPtr[i] = constrain(wsPtr[i] * gainFactor, SHRT_MIN, SHRT_MAX);
}
  
static void ampOutputRem() {
  // output to speaker from remote mic
  static int bytesCtr = 0;
  applyMicRemGain();
#ifdef ISVC
  applyFilters();
#endif
  size_t bytesWritten = 0;
  i2s_write(I2S_AMP_PORT, wsBuffer, wsBufferLen, &bytesWritten, ampTimeout);
  bytesCtr += bytesWritten;
  if (bytesCtr > sampleWidth * SAMPLE_RATE / 5) {
    // update display 5 times per sec
    int16_t* wsPtr = (int16_t*) wsBuffer;
    displayAudioLed(wsPtr[0]);
    bytesCtr = 0;
  }
  wsBufferLen = 0;
}

void restartI2S() {
  // restart peripherals after configuration changed from web
#ifdef CONFIG_IDF_TARGET_ESP32
  i2s_adc_disable(I2S_NUM_0);
  i2s_adc_disable(I2S_NUM_1);
#endif
  i2s_driver_uninstall(I2S_NUM_0);
  i2s_driver_uninstall(I2S_NUM_1);
}

/*********************************************************************/

#ifdef ISVC

void passThru() {
  // play buffer from mic direct to amp
  size_t bytesRead = micInput();
  if (bytesRead) ampOutput(bytesRead);
}

void makeRecordingRem(bool isRecording) { 
  static bool initCall = true;
  if (isRecording) {
    if (initCall) {
      audioBytesUsed = WAV_HDR_LEN; // leave space for wave header
      LOG_INF("Start recording from remote mic");
      initCall = false;
    }
    if (audioBytesUsed > psramMax) stopAudio = true; 
    else if (psramFound()) {
      applyMicRemGain();
      memcpy(audioBuffer + audioBytesUsed, wsBuffer, wsBufferLen);
      audioBytesUsed += wsBufferLen;
      wsBufferLen = 0;
    }
  } else {
    if (!initCall) {
      // finish remote mic recording
      totalSamples = (audioBytesUsed - WAV_HDR_LEN) / sampleWidth;
      if (totalSamples < 0) totalSamples = 0;
      audioBytesUsed = 0;
      LOG_INF("Finished remote mic recording of %d samples", totalSamples);  
      THIS_ACTION = NO_ACTION;
      initCall = true;
    }
  }
}

void makeRecording() {
  if (psramFound()) {
    LOG_INF("Recording ...");
    audioBytesUsed = WAV_HDR_LEN; // leave space for wave header
    while (audioBytesUsed < psramMax) {
      size_t bytesRead = micInput();
      if (bytesRead) {
        memcpy(audioBuffer + audioBytesUsed, sampleBuffer, bytesRead);
        audioBytesUsed += bytesRead;
      }
      if (stopAudio) break;
    }
    totalSamples = (audioBytesUsed  - WAV_HDR_LEN) / sampleWidth;
    LOG_INF("%s recording of %d samples", stopAudio ? "Stopped" : "Finished",  totalSamples);  
  } else LOG_WRN("PSRAM needed to record and play");
}

void playRecording() {
  if (psramFound()) {
    LOG_INF("Playing, initial volume: %d", ampVol); 
    for (int i = WAV_HDR_LEN; i < totalSamples * sampleWidth; i += sampleBytes) { 
      memcpy(sampleBuffer, audioBuffer+i, sampleBytes);
      ampOutput();
      if (stopAudio) break;
    }
    LOG_INF("%s playing of %d samples", stopAudio ? "Stopped" : "Finished", totalSamples);
  } else LOG_WRN("PSRAM needed to record and play");
}

static void VCaudioTask() {
  switch (THIS_ACTION) {
    case STOP_ACTION:
      // complete current action
      stopAudio = true;
      displayAudioLed(0);
      makeRecordingRem(false);
    break;
    case RECORD_ACTION: 
     // make recording
      if (micUse && !micRem) makeRecording();
    break;
    case PLAY_ACTION: 
      if (mampUse) playRecording(); // play previous recording
    break;
    case PASS_ACTION:
      if (micUse && !micRem) {
        LOG_INF("Passthru started");
        while (!stopAudio) passThru();
        LOG_INF("Passthru stopped"); 
      }
    break;
    default: // ignore
    break;
  }
}

#endif

/*****************************************************************/

#ifdef ISCAM

void startAudio() {
  // start audio recording and write recorded audio to SD card as WAV file 
  // combined into AVI file as PCM channel on FTP upload or browser download
  // so can be read by media players
  if (micUse && micGain) {
    wavFile = STORAGE.open(WAVTEMP, FILE_WRITE);
    wavFile.write(wavHeader, WAV_HDR_LEN); 
    if (audioHandle != NULL) xTaskNotifyGive(audioHandle);
    else LOG_WRN("Reboot ESP to use audio");
  } 
}

void finishAudio(bool isValid) {
  if (doMicCapture) {
    // finish a recording and save if valid
    doMicCapture = false; 
    while (captureRunning) delay(100); // wait for getRecording() to complete
    if (isValid) {
      size_t dataBytes = updateWavHeader();
      wavFile.seek(0, SeekSet); // start of file
      wavFile.write(wavHeader, WAV_HDR_LEN); // overwrite default header
      wavFile.close();  
      LOG_INF("Captured %d audio samples with gain factor %i", totalSamples, micGain - MIC_GAIN_CENTER);
      LOG_INF("Saved %s to SD for %s", fmtSize(dataBytes + WAV_HDR_LEN), WAVTEMP);
    }
  }
}

size_t getAudioBuffer(bool endStream) {
  // called from audioStream();
  size_t haveBytes = 0;
  static bool startStream = true;
  captureStream = false;
  if (micUse) {
    if (endStream) {
      doStreamCapture = false;
      startStream = true;
      return 0; // reset for end of stream
    }
    audioBytesUsed = 0;
    if (startStream) {
      updateWavHeader();
      memcpy(audioBuffer, wavHeader, WAV_HDR_LEN);
      haveBytes = WAV_HDR_LEN;
      doStreamCapture = true;
      startStream = false;
    } else {
      if (!captureRunning) {
        captureStream = true;
        audioBytesUsed = micInput(); 
      } // otherwise already loaded by audioTask()
      haveBytes = audioBytesUsed;
    }
  }
  return haveBytes;
}

static void camAudioTask() {
  // capture audio from esp microphone
  captureRunning = true;
  while (captureStream) delay(10); // wait for stream to read mic buffer
  doMicCapture = true;   
  totalSamples = 0;
  while (doMicCapture) {
    size_t bytesRead = micInput();
    if (bytesRead) {
      wavFile.write((uint8_t*)sampleBuffer, bytesRead);
      totalSamples += bytesRead / sampleWidth;
    }
  }
  captureRunning = false;
}

#endif

/************************************************************************/

static void predefPins() {
#if defined(I2S_SD)
    char micPin[3];
    sprintf(micPin, "%d", I2S_SD);
    updateStatus("micSWsPin", micPin);
    sprintf(micPin, "%d", I2S_WS);
    updateStatus("micSdPin", micPin);
    sprintf(micPin, "%d", I2S_SCK);
    updateStatus("micSckPin", micPin);
#endif

  I2Smic = micUse && micSckPin == -1 ? false : true;
  I2Samp = true;
  
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32S3 does not have DAC for analog amp
  I2S_MIC_PORT = I2S_NUM_0; // PDM or I2S mic
  I2S_AMP_PORT = I2S_NUM_1; // I2S amp only
#else
  // On ESP32, only I2S1 available and must use I2S mic
  I2S_MIC_PORT = I2S_NUM_1; 
  mampUse = false;
#endif
}

static void micRemTask(void* parameter) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (micRem) {
      if (THIS_ACTION == PASS_ACTION) ampOutputRem();
#ifdef ISVC
      else if (THIS_ACTION == RECORD_ACTION) makeRecordingRem(true);
#endif
    }
  }
}

void micTaskStatus() {
  // task to handle remote mic
  if (mampUse) {
    wsBufferLen = 0;
    if (wsBuffer == NULL) wsBuffer = (uint8_t*)malloc(MAX_PAYLOAD_LEN);
    xTaskCreate(micRemTask, "micRemTask", 2048, NULL, HTTP_PRI, &micRemHandle);
  } else if (micRemHandle != NULL) {
    vTaskDelete(micRemHandle);
    micRemHandle = NULL;
  }
}

static void audioTask(void* parameter) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef ISCAM
    camAudioTask();
#endif
#ifdef ISVC
    VCaudioTask();
#endif
  }
  stopI2S(I2S_MIC_PORT);
  vTaskDelete(NULL);
}

bool prepAudio() {
  bool res = true; 
#ifdef ISVC 
  micUse = mampUse = true;
  micTaskStatus();
#endif
#ifdef ISCAM
  predefPins();
#endif
  if ((micUse && I2S_MIC_PORT == I2S_NUM_1 && !I2Smic) || (mampUse && I2S_AMP_PORT == I2S_NUM_1 && !I2Samp)) {
    LOG_WRN("Only I2S devices supported on I2S_NUM_1");
    res = false;
  } else {
    esp_err_t restartOK;
    if (micUse) {
      restartOK = setupMic(); 
      if (restartOK == ESP_OK) restartOK = startupMic();
      if (restartOK != ESP_OK) micUse = false;
    }
    if (mampUse) {
      restartOK = setupAmp();
      if (restartOK == ESP_OK) restartOK = startupAmp();
      if (restartOK != ESP_OK) mampUse = false;
    }
    if (micUse || mampUse) {
      if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(sampleBytes);
      if (audioBuffer == NULL && psramFound()) audioBuffer = (uint8_t*)ps_malloc(psramMax + (sizeof(int16_t) * DMA_BUFF_LEN));
      xTaskCreate(audioTask, "audioTask", AUDIO_STACK_SIZE, NULL, AUDIO_PRI, &audioHandle);
      if (micUse) LOG_INF("Sound capture is available using %s mic on I2S%i", micLabels[I2Smic], I2S_MIC_PORT);
      if (mampUse) LOG_INF("Speaker output is available using %s amp on I2S%i", ampLabels[I2Samp], I2S_AMP_PORT);
      LOG_INF("Mic gain: %d, Amp volume: %d", micGain, ampVol); 
    } else res = false;
  }
  debugMemory("prepAudio");
  return res;
}

#endif
