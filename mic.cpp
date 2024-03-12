
// Creates 16 bit single channel PCM WAV file from microphone input.
// Default sample rate is 16kHz
// Audio is not replayed on streaming, only via AVI file

// The following devices have been tested with this application:
// - I2S microphone: INMP441
// - PDM microphone: MP34DT01
//
// I2S_NUM_1 does not support PDM microphone
//
// microphone cannot be used on IO Extender

// s60sc 2021, 2022, 2024

#include "appGlobals.h"

bool micUse; // true to use external I2S microphone 

// INMP441 I2S microphone pinout, connect L/R to GND for left channel
// MP34DT01 PDM microphone pinout, connect SEL to GND for left channel
// if micSckPin > 0, an I2S microphone is assumed, if micSckPin = -1 a PDM microphone is assumed
// define requires pin numbers via web page config tab
int micSckPin; // I2S SCK / PDM n/a
int micSWsPin; // I2S WS  / PDM CLK
int micSdPin;  // I2S SD  / PDM DAT
// For XIAO_ESP32S3 Sense Cam board, the internal PDM pins are:
// I2S SCK -1
// I2S WS / PDM CLK 42
// I2S SD / PDM DAT 41
// For ESP32S3-EYE Cam board, the internal I2S pins are:
// I2S SCK 41
// I2S WS 42
// I2S SD 2
// For CAMERA_MODEL_NEW_ESPS3_RE1_0, the internal I2S pins are:
// I2S SCK 37
// I2S WS 36
// I2S SD 35

#define I2S_MIC 0
#define PDM_MIC 1
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define I2S_CHAN I2S_NUM_0
#else
#define I2S_CHAN I2S_NUM_1 // On ESP32, only I2S1 available
#endif
#define DMA_BUFF_LEN 1024 
int micGain = 0;  // microphone gain 0 is off
static bool micType; 
const uint32_t SAMPLE_RATE = 16000; // sample rate used
static const uint8_t sampleWidth = sizeof(int16_t); 
static const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
static File wavFile;
static int totalSamples = 0;
static size_t bytesRead = 0;
static size_t audBytes = 0;
TaskHandle_t micHandle = NULL;
static bool doMicCapture = false;
static bool captureRunning = false;
static bool doStreamCapture = false;
static bool captureStream = false;
static uint8_t* sampleBuffer = NULL;
uint8_t* audioBuffer = NULL;
static QueueHandle_t i2s_queue = NULL;
static i2s_event_t event;
static i2s_pin_config_t i2s_mic_pins;

const uint32_t WAV_HEADER_LEN = 44; // WAV header length
static uint8_t wavHeader[WAV_HEADER_LEN] = { // WAV header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};

/**********************************/

// i2s config for microphone
static i2s_config_t i2s_mic_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = DMA_BUFF_LEN,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

static inline void IRAM_ATTR wakeTask(TaskHandle_t thisTask) {
  // utility function to resume task from interrupt
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(thisTask, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void startMic() {
  // install & start up the I2S peripheral as microphone when activated
  int queueSize = 4;
  i2s_queue = xQueueCreate(queueSize, sizeof(i2s_event_t));
  if (micType == PDM_MIC) i2s_mic_config.mode = (i2s_mode_t)((int)(i2s_mic_config.mode) | I2S_MODE_PDM);
  i2s_driver_install(I2S_CHAN, &i2s_mic_config, queueSize, &i2s_queue);
  // set i2s microphone pins
  i2s_mic_pins = {
    .bck_io_num = micSckPin,
    .ws_io_num = micSWsPin,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = micSdPin
  };
  i2s_set_pin(I2S_CHAN, &i2s_mic_pins);
  i2s_zero_dma_buffer(I2S_CHAN);
}

static void stopMic() {
  // stop the relevant I2S peripheral
  i2s_stop(I2S_CHAN);
  i2s_driver_uninstall(I2S_CHAN);
  LOG_DBG("Stopped I2S port %d", I2S_CHAN);
}

static void getMicData() {
  // get mic I2S data
  bytesRead = 0;
  // wait for i2s buffer to be ready
  if (xQueueReceive(i2s_queue, &event, pdMS_TO_TICKS(2 * SAMPLE_RATE)) == pdPASS) {
    if (event.type == I2S_EVENT_RX_DONE) {
      i2s_read(I2S_CHAN, sampleBuffer, sampleBytes, &bytesRead, portMAX_DELAY);
      int samplesRead = bytesRead / sampleWidth;
      // process each sample as amplified 16 bit 
      int16_t* ampBuffer = (int16_t*)sampleBuffer;
      for (int i = 0; i < samplesRead; i++) 
        ampBuffer[i] = constrain(ampBuffer[i] * micGain, SHRT_MIN, SHRT_MAX);
      if (doStreamCapture) {
        if (!audBytes) {
          memcpy(audioBuffer, sampleBuffer, bytesRead);
          audBytes = bytesRead;
        }
      }
    } 
  }  
}

static void micTask(void* parameter) {
  startMic();
  while (true) {
    // wait for recording request
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    totalSamples = 0;
    captureRunning = true;
    while (captureStream) delay(10); // wait for stream to read mic buffer
    doMicCapture = true;   
    while (doMicCapture) {
      getMicData();
      wavFile.write(sampleBuffer, bytesRead);
      totalSamples += bytesRead / sampleWidth;
    }
    captureRunning = false;
  }
  stopMic();
  vTaskDelete(NULL);
}

void startAudio() {
  // start audio recording and write recorded audio to SD card as WAV file 
  // combined into AVI file as PCM channel on FTP upload or browser download
  // so can be read by media players
  if (micUse && micGain) {
    wavFile = STORAGE.open(WAVTEMP, FILE_WRITE);
    wavFile.write(wavHeader, WAV_HEADER_LEN); 
    wakeTask(micHandle);
  } 
}

static uint32_t updateWavHeader() {
  // update wav header
  uint32_t dataBytes = totalSamples * sampleWidth;
  uint32_t wavFileSize = dataBytes ? dataBytes + WAV_HEADER_LEN - 8 : 0; // wav file size excluding chunk header
  memcpy(wavHeader+4, &wavFileSize, 4);
  memcpy(wavHeader+24, &SAMPLE_RATE, 4); // sample rate
  uint32_t byteRate = SAMPLE_RATE * sampleWidth; // byte rate (SampleRate * NumChannels * BitsPerSample/8)
  memcpy(wavHeader+28, &byteRate, 4); 
  memcpy(wavHeader+WAV_HEADER_LEN-4, &dataBytes, 4); // wav data size
  return dataBytes;
}

void finishAudio(bool isValid) {
  if (doMicCapture) {
    // finish a recording and save if valid
    doMicCapture = false; 
    while (captureRunning) delay(100); // wait for getRecording() to complete
    if (isValid) {
      uint32_t dataBytes = updateWavHeader();
      wavFile.seek(0, SeekSet); // start of file
      wavFile.write(wavHeader, WAV_HEADER_LEN); // overwrite default header
      wavFile.close();  
      LOG_INF("Captured %d audio samples with gain factor %i", totalSamples, micGain);
      LOG_INF("Saved %s to SD for %s", fmtSize(dataBytes + WAV_HEADER_LEN), WAVTEMP);
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
    audBytes = 0;
    if (startStream) {
      updateWavHeader();
      memcpy(audioBuffer, wavHeader, WAV_HEADER_LEN);
      haveBytes = WAV_HEADER_LEN;
      doStreamCapture = true;
      startStream = false;
    } else {
      if (!captureRunning) {
        captureStream = true;
        getMicData(); 
      } // otherwise already loaded by micTask()
      haveBytes = audBytes;
    }
  }
  return haveBytes;
}

void prepMic() {
  if (micUse) { 
#if defined(CAMERA_MODEL_XIAO_ESP32S3)
    // built in PDM mic
    updateStatus("micSWsPin", "42");
    updateStatus("micSdPin", "41");
    updateStatus("micSckPin", "-1");
#endif
#if defined(CAMERA_MODEL_ESP32S3_EYE)
    // built in I2S mic
    updateStatus("micSWsPin", "42");
    updateStatus("micSdPin", "2");
    updateStatus("micSckPin", "41");
#endif
    if (micSckPin && micSWsPin && micSdPin) {
      if (sampleBuffer == NULL) sampleBuffer = (uint8_t*)malloc(sampleBytes);
      if (audioBuffer == NULL) audioBuffer = (uint8_t*)ps_malloc(sampleBytes);
      micType = micSckPin == -1 ? PDM_MIC : I2S_MIC;
      LOG_INF("Sound recording is available using %s mic on I2S%i", micType ? "PDM" : "I2S", I2S_CHAN);
      xTaskCreate(micTask, "micTask", MIC_STACK_SIZE, NULL, MIC_PRI, &micHandle);
      debugMemory("prepMic");
    } else {
      micUse = false;
      LOG_WRN("At least one mic pin is not defined");
    }
  }
}
