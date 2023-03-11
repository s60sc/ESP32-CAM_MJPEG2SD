
// Creates 16 bit single channel PCM WAV file from microphone input.
// Default sample rate is 16kHz
// Audio is not replayed on streaming, only via AVI file

// The following device has been tested with this application:
// - I2S microphone: INMP441
//
// I2S_NUM_1 does not support PDM or ADC microphones

// s60sc 2021, 2022

#include "appGlobals.h"

// INMP441 I2S microphone pinout, connect L/R to GND for left channel
int micSckPin; // I2S SCK
int micWsPin;  // I2S WS
int micSdPin;  // I2S SD

#define I2S_MIC I2S_NUM_1
#define DMA_BUFF_LEN 1024 
int micGain = 0;  // microphone gain 0 is off
const uint32_t SAMPLE_RATE = 16000; // sample rate used
static const uint8_t sampleWidth = sizeof(int32_t); 
static const uint8_t audWidth = sizeof(int16_t);
static const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
static File wavFile;
static int totalSamples = 0;
static TaskHandle_t micHandle = NULL;
static bool doMicCapture = false;
static bool captureRunning = false;
static int32_t* sampleBuffer = NULL;
static int16_t* audBuffer = NULL;
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
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // I2S_BITS_PER_SAMPLE_16BIT doesnt work
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
  i2s_driver_install(I2S_MIC, &i2s_mic_config, queueSize, &i2s_queue);
  // set i2s microphone pins
  i2s_mic_pins = {
    .bck_io_num = micSckPin,
    .ws_io_num = micWsPin,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = micSdPin
  };
  i2s_set_pin(I2S_MIC, &i2s_mic_pins);
  i2s_zero_dma_buffer(I2S_MIC);
}

static void stopMic() {
  // stop the relevant I2S peripheral
  i2s_stop(I2S_MIC);
  i2s_driver_uninstall(I2S_MIC);
  LOG_DBG("Stopped I2S_MIC port");
}

static void getRecording() {
  // copy I2S data to SD
  size_t bytesRead = totalSamples = 0;
  const uint8_t gainFactor = 16 + 1 - micGain; 
  captureRunning = true;
  while (doMicCapture) {
     // wait for i2s buffer to be ready
     if (xQueueReceive(i2s_queue, &event, (2 * SAMPLE_RATE) /  portTICK_PERIOD_MS) == pdPASS) {
       if (event.type == I2S_EVENT_RX_DONE) {
         i2s_read(I2S_MIC, sampleBuffer, sampleBytes, &bytesRead, portMAX_DELAY);
         int samplesRead = bytesRead / sampleWidth;
         // process each sample, convert to amplified 16 bit 
         for (int i = 0; i < samplesRead; i++) {
          audBuffer[i] = constrain(sampleBuffer[i] >> gainFactor, SHRT_MIN, SHRT_MAX);
         }
         wavFile.write((uint8_t*)audBuffer, samplesRead * audWidth);
         totalSamples += samplesRead;
       }
     }  
  }
  captureRunning = false;
}

static void micTask(void* parameter) {
  startMic();
  if (sampleBuffer == NULL) sampleBuffer = (int32_t*)malloc(DMA_BUFF_LEN * sampleWidth);
  if (audBuffer == NULL) audBuffer = (int16_t*)malloc(DMA_BUFF_LEN * audWidth);
  while (true) {
    // wait for recording request
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    doMicCapture = true;   
    getRecording();
  }
  stopMic();
  vTaskDelete(NULL);
}

void startAudio() {
  // start audio recording and write recorded audio to SD card as WAV file 
  // combined into AVI file as PCM channel on FTP upload or browser download
  // so can be read by media players
  if (micUse && micGain) {
    wavFile = SD_MMC.open(WAVTEMP, FILE_WRITE);
    wavFile.write(wavHeader, WAV_HEADER_LEN); 
    wakeTask(micHandle);
  } 
}

void finishAudio(bool isValid) {
  if (doMicCapture) {
    // finish a recording and save if valid
    doMicCapture = false; 
    while (captureRunning) delay(100); // wait for getRecording() to complete
    if (isValid) {
      // update wav header
      uint32_t dataBytes = totalSamples * audWidth;
      uint32_t wavFileSize = dataBytes + WAV_HEADER_LEN - 8; // wav file size excluding chunk header
      memcpy(wavHeader+4, &wavFileSize, 4);
      memcpy(wavHeader+24, &SAMPLE_RATE, 4); // sample rate
      uint32_t byteRate = SAMPLE_RATE * audWidth; // byte rate (SampleRate * NumChannels * BitsPerSample/8)
      memcpy(wavHeader+28, &byteRate, 4); 
      memcpy(wavHeader+WAV_HEADER_LEN-4, &dataBytes, 4); // wav data size
      wavFile.seek(0, SeekSet); // start of file
      wavFile.write(wavHeader, WAV_HEADER_LEN); // overwrite default header
      wavFile.close();  
      LOG_INF("Captured %d audio samples with gain factor %i", totalSamples, micGain);
      LOG_INF("Saved %ukB to SD for %s", (dataBytes + WAV_HEADER_LEN) / 1024, WAVTEMP);
    }
  }
}

void prepMic() {
  if (micUse) { 
    if (micSckPin && micWsPin && micSdPin) {
      LOG_INF("Sound recording is available");
      xTaskCreate(micTask, "micTask", 1024 * 4, NULL, 1, &micHandle);
      debugMemory("prepMic");
    } else {
      micUse = false;
      LOG_WRN("At least one mic pin is not defined");
    }
  }
}

