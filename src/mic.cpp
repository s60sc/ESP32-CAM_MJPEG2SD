
// Creates 16 bit single channel PCM WAV file from microphone input.
// Default sample rate is 16kHz
// Audio is not replayed on streaming, only via AVI file

// The following device has been tested with this application:
// - I2S microphone: INMP441
//
// Still needs work due to quality of recording and impact on frame rate
//
// I2S_NUM_1 does not support PDM or ADC microphones

// s60sc 2021

#include "globals.h"

int micGain = 0;  // microphone gain 0 is off
static int gainFactor; // 2 ^ microphone gain
const uint32_t SAMPLE_RATE = 16000; // sample rate used
const uint8_t BUFFER_WIDTH = sizeof(int16_t);
static File wavFile;
static int totalSamples = 0;
static TaskHandle_t micHandle = NULL;
static bool doMicCapture = false;
static bool captureRunning = false;
static int16_t* sampleBuffer = NULL;
static QueueHandle_t i2s_queue = NULL;
static i2s_event_t event;

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
  .dma_buf_len = 256,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// i2s microphone pins
static i2s_pin_config_t i2s_mic_pins = {
  .bck_io_num = micSckPin,
  .ws_io_num = micWsPin,
  .data_out_num = -1,
  .data_in_num = micSdPin
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
  // installing the driver kills esp_camera_fb_get() performance - why?
  i2s_driver_install(I2S_NUM_1, &i2s_mic_config, queueSize, &i2s_queue);
  i2s_set_pin(I2S_NUM_1, &i2s_mic_pins);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

static void stopMic() {
  // stop the relevant I2S peripheral
  i2s_stop(I2S_NUM_1);
  i2s_driver_uninstall(I2S_NUM_1);
  LOG_DBG("Stopped I2S_NUM_1 port");
}

static void getRecording() {
  // copy I2S data to SD
  size_t bytesRead = totalSamples = 0;
  captureRunning = true;
  while (doMicCapture) {
    // wait for i2s buffer to be ready
    if (xQueueReceive(i2s_queue, &event, (2 * SAMPLE_RATE) /  portTICK_PERIOD_MS) == pdPASS) {
      if (event.type == I2S_EVENT_RX_DONE) {
        i2s_read(I2S_NUM_1, sampleBuffer, 1024, &bytesRead, portMAX_DELAY);
        int samplesRead = bytesRead / BUFFER_WIDTH;
        // process each sample, 
        for (int i = 0; i < samplesRead; i++) {
          sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
        }
        wavFile.write((uint8_t*)sampleBuffer, bytesRead);
        totalSamples += samplesRead;
      }
    }  
  }
  captureRunning = false;
}

static void micTask(void* parameter) {
  startMic();
  if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(RAMSIZE);
  while (true) {
    // wait for recording request
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    doMicCapture = true;   
    gainFactor = pow(2, micGain-1);
    getRecording();
  }
  stopMic();
  vTaskDelete(NULL);
}

void prepMic() {
  LOG_INF("Sound recording is %s", micUse ? "On" : "Off");
  if (micUse) xTaskCreate(micTask, "micTask", 1024 * 3, NULL, 1, &micHandle);
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
      uint32_t dataBytes = totalSamples * BUFFER_WIDTH;
      uint32_t wavFileSize = dataBytes + WAV_HEADER_LEN - 8; // wav file size excluding chunk header
      memcpy(wavHeader+4, &wavFileSize, 4);
      memcpy(wavHeader+24, &SAMPLE_RATE, 4); // sample rate
      uint32_t byteRate = SAMPLE_RATE * BUFFER_WIDTH; // byte rate (SampleRate * NumChannels * BitsPerSample/8)
      memcpy(wavHeader+28, &byteRate, 4); 
      memcpy(wavHeader+WAV_HEADER_LEN-4, &dataBytes, 4); // wav data size
      wavFile.seek(0, SeekSet); // start of file
      wavFile.write(wavHeader, WAV_HEADER_LEN); // overwrite default header
      wavFile.close();   
         
      LOG_INF("Captured %d audio samples with gain factor %i", totalSamples, gainFactor);
      LOG_INF("Saved %ukB to SD for %s", (dataBytes + WAV_HEADER_LEN) / 1024, WAVTEMP);
    }
  }
}
