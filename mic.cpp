
// Creates 16 bit single channel PCM WAV file from microphone input.
// Default sample rate is 16kHz
// Audio is not replayed on streaming, only via AVI file

// The following device has been tested with this application:
// - I2S microphone: INMP441
//
// I2S_NUM_1 does not support PDM or ADC microphones

// s60sc 2021

#include "Arduino.h"
#include <driver/i2s.h>
#include "FS.h" 
#include "SD_MMC.h"
#include <regex>
#include "remote_log.h"

int micGain = 0;  // microphone gain 0 is off
static int gainFactor; // 2 ^ microphone gain
extern const uint32_t SAMPLE_RATE = 16000; // sample rate used
extern const uint32_t RAMSIZE;
extern const uint8_t BUFFER_WIDTH = sizeof(int16_t);
static File wavFile;
static int totalSamples = 0;
static const char* TEMPFILE = "/current.wav";
static TaskHandle_t micHandle = NULL;
static bool doMicCapture = false;
static bool captureRunning = false;
static const char* TAG = "mic";
static int16_t* sampleBuffer;

extern const uint32_t WAV_HEADER_LEN = 44; // WAV header length
static uint8_t wavHeader[WAV_HEADER_LEN] = { // WAV header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};

/**********************************/

// I2S Microphone pins
// INMP441 I2S microphone pinout, connect L/R to GND for left channel
#define MIC_SCK_IO GPIO_NUM_4  // I2S SCK
#define MIC_WS_IO  GPIO_NUM_12 // I2S WS
#define MIC_SD_IO  GPIO_NUM_3 // I2S SD

// i2s config for microphone
static i2s_config_t i2s_mic_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 4,
  .dma_buf_len = 1024,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// i2s microphone pins
static i2s_pin_config_t i2s_mic_pins = {
  .bck_io_num = MIC_SCK_IO,
  .ws_io_num = MIC_WS_IO,
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = MIC_SD_IO
};

static inline void IRAM_ATTR wakeTask(TaskHandle_t thisTask) {
  // utility function to resume task from interrupt
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(thisTask, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void startMic() {
  // install & start up the I2S peripheral as microphone when activated
  i2s_driver_install(I2S_NUM_1, &i2s_mic_config, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &i2s_mic_pins);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

static void stopMic() {
  // stop the relevant I2S peripheral
  i2s_stop(I2S_NUM_1);
  i2s_driver_uninstall(I2S_NUM_1);
  ESP_LOGI(TAG, "Stopped I2S_NUM_1 port");
}

static void getRecording() {
  // copy I2S data to SD
  size_t bytesRead = totalSamples = 0;
  captureRunning = true;
  while (doMicCapture) {
    i2s_read(I2S_NUM_1, sampleBuffer, RAMSIZE, &bytesRead, portMAX_DELAY);
    int samplesRead = bytesRead / BUFFER_WIDTH;
    // process each sample, 
    for (int i = 0; i < samplesRead; i++) {
      sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
    }
    wavFile.write((uint8_t*)sampleBuffer, bytesRead);
    totalSamples += samplesRead;
  }
  captureRunning = false;
}

static inline void littleEndian(uint8_t* inBuff, uint32_t in) {
  // arrange bits in little endian order
  for (int i=0; i<4; i++) {
    inBuff[i] = in % 0x100;
    in = in >> 8;  
  }
}

void micTask(void* parameter) {
  startMic();
  sampleBuffer = (int16_t*)malloc(RAMSIZE);
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
  ESP_LOGI(TAG, "Sound recording is %s", micGain ? "On" : "Off");
  if (micGain) xTaskCreate(micTask, "micTask", 4096, NULL, 1, &micHandle);
}

void startAudio() {
  // start audio recording and write recorded audio to SD card as WAV file 
  // combined into AVI file as PCM channel on FTP upload or browser download
  // so can read by media players
  if (micGain) {
    wavFile = SD_MMC.open(TEMPFILE, FILE_WRITE);
    wavFile.write(wavHeader, WAV_HEADER_LEN); 
    wakeTask(micHandle);
  } 
}

void finishAudio(const char* mjpegName, bool isValid) {
  if (doMicCapture) {
    // finish a recording and save if valid
    doMicCapture = false; 
    while (captureRunning) delay(100); // wait for getRecording() to complete
    if (isValid) {
      // build wav file name
      std::string wfile(mjpegName);
      wfile = std::regex_replace(wfile, std::regex("mjpeg"), "wav");

      // update wav header
      int dataBytes = totalSamples * BUFFER_WIDTH;
      littleEndian(wavHeader+4,  dataBytes + WAV_HEADER_LEN - 8); // wav file size excluding chunk header
      littleEndian(wavHeader+24, SAMPLE_RATE); // sample rate
      littleEndian(wavHeader+28, SAMPLE_RATE * BUFFER_WIDTH); // byte rate (SampleRate * NumChannels * BitsPerSample/8)
      littleEndian(wavHeader+WAV_HEADER_LEN-4, dataBytes); // wav data size
      wavFile.seek(0, SeekSet); // start of file
      wavFile.write(wavHeader, WAV_HEADER_LEN); // overwrite default header
      wavFile.close();   
      
      // rename file
      SD_MMC.rename(TEMPFILE, wfile.data());    
      ESP_LOGI(TAG, "Captured %d audio samples with gain factor %i", totalSamples, gainFactor);
      ESP_LOGI(TAG, "Saved %ukB to SD for %s", (dataBytes + WAV_HEADER_LEN) / 1024, wfile.data());
    } 
  }
  SD_MMC.remove(TEMPFILE);
}
