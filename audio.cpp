//
// Handle microphone input, and speaker output via amp.
// The microphone input, and the output to amplifier, each make use of a
// separate I2S peripheral in the ESP32 or ESP32S3.
// I2S and PDM microphones are supported.
// I2S amplifiers are supported.
//
// If using I2S mic and I2S amp, then the following pins should be set to same values:
// - micSckPin = mampBckIo
// - micSWsPin = mampSwsIo
//
// A remote microphone on a PC or phone can be used:
// - for VoiceChanger app, this is used instead of local mic
//   - need to press PC Mic button before selecting an action
// - for MJPEG2SD app, this is passed thru to speaker, independent of local mic
//   - need to enable use amp and pins in Config / Peripherals for Start Mic button to be available on web page
//   - remote mic should only be activated when need to speak
// Windows needs to allow microphone use in Microphone Privacy Settings
// In Microphone Properties / Advanced, check bit depth and sample rate (normally 16 bit 48kHz)
// Chrome needs to allow access to mic from insecure (http) site:
// Go to : chrome://flags/#unsafely-treat-insecure-origin-as-secure
// Enter app URL in box: http://<app_ip_address>
//
// s60sc 2024

#include "appGlobals.h"

#if INCLUDE_AUDIO 

#include <ESP_I2S.h>
I2SClass I2Spdm;
I2SClass I2Sstd;

// On ESP32, only I2S1 available with camera
i2s_port_t MIC_CHAN = I2S_NUM_1;
i2s_port_t AMP_CHAN = I2S_NUM_0;

bool micUse = false; // use local mic
bool micRem = false; // use remote mic (depends on app)
bool mampUse = false;
bool volatile stopAudio = false;

// I2S devices
bool I2Smic; // true if I2S, false if PDM

// I2S SCK and I2S BCLK can share same pin
// I2S external Microphone pins
// INMP441 I2S microphone pinout, connect L/R to GND for left channel
// MP34DT01 PDM microphone pinout, connect SEL to GND for left channel
int micSckPin = -1; // I2S SCK
int micSWsPin = -1; // I2S WS, PDM CLK
int micSdPin = -1;  // I2S SD, PDM DAT

// I2S Amplifier pins
// MAX98357A 
// SD leave as mono (unconnected)
// Gain: 100k to GND works, not direct to GND. Unconnected is 9 dB 
int mampBckIo = -1; // I2S BCLK or SCK
int mampSwsIo = -1;  // I2S LRCLK or WS
int mampSdIo = -1;   // I2S DIN

int ampTimeout = 1000; // ms for amp write abandoned if no output
uint32_t SAMPLE_RATE = 16000;  // audio rate in Hz
uint8_t PREAMP_GAIN = 3; // microphone preamplification factor
int8_t ampVol = 3; // amplifier volume factor

TaskHandle_t audioHandle = NULL;
static TaskHandle_t micRemHandle = NULL;

uint8_t* audioBuffer = NULL;
static size_t audioBytesUsed = 0;
static int totalSamples = 0;
static const uint8_t sampleWidth = sizeof(int16_t);
const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
int16_t* sampleBuffer = NULL;
static const char* micLabels[2] = {"PDM", "I2S"};

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
    }
  } // else turn off volume
}

static bool setupMic() {
  bool res;
  if (I2Smic) {
    // I2S mic and I2S amp can share same I2S channel
    I2Sstd.setPins(micSckPin, micSWsPin, mampSdIo, micSdPin, -1); // BCLK/SCK, LRCLK/WS, SDOUT, SDIN, MCLK
    res = I2Sstd.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  } else {
    // PDM mic need separate channel to I2S
    I2Spdm.setPinsPdmRx(micSWsPin, micSdPin);
    res = I2Spdm.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  }
  return res;
}

static bool setupAmp() {
  bool res = false;
  if (!micUse || !I2Smic) {
    // if not already started by setupMic()
    I2Sstd.setPins(mampBckIo, mampSwsIo, mampSdIo, -1, -1); // BCLK/SCK, LRCLK/WS, SDOUT, SDIN, MCLK
    res = I2Sstd.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  } // already started by setupMic()
  return res;
}

static size_t micInput() {
  uint8_t gainFactor = pow(2, micGain - MIC_GAIN_CENTER);
  size_t bytesRead = I2Smic ? I2Sstd.readBytes((char*)sampleBuffer, sampleBytes) : I2Spdm.readBytes((char*)sampleBuffer, sampleBytes);
  int samplesRead = bytesRead / sampleWidth;
  // apply preamp gain
  for (int i = 0; i < samplesRead; i++) {
    sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
  }
  if (doStreamCapture && !audioBytesUsed) memcpy(audioBuffer, sampleBuffer, bytesRead);
  return bytesRead;
}

void restartI2S() {
  I2Sstd.end();
  I2Spdm.end();
}

static void ampOutput(size_t bytesRead = sampleBytes, bool speaker = true) {
  // output to amplifier
  // apply required filtering and volume
  applyFilters();
  if (speaker) {
  I2Sstd.write((uint8_t*)sampleBuffer, bytesRead);
  }
  displayAudioLed(sampleBuffer[0]);
}

size_t updateWavHeader() {
  // update wav header
  uint32_t dataBytes = totalSamples * sampleWidth;
  uint32_t wavFileSize = dataBytes ? dataBytes + WAV_HDR_LEN - 8 : 0; // wav file size excluding chunk header
  memcpy(wavHeader+4, &wavFileSize, 4); ////
  memcpy(wavHeader+24, &SAMPLE_RATE, 4); // sample rate
  uint32_t byteRate = SAMPLE_RATE * sampleWidth; // byte rate (SampleRate * NumChannels * BitsPerSample/8)
  memcpy(wavHeader+28, &byteRate, 4); 
  memcpy(wavHeader+WAV_HDR_LEN-4, &dataBytes, 4); // wav data size ////
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
  bytesWritten = I2Sstd.write(wsBuffer, wsBufferLen);
  bytesCtr += bytesWritten;
  if (bytesCtr > sampleWidth * SAMPLE_RATE / 5) {
    // update display 5 times per sec
    int16_t* wsPtr = (int16_t*) wsBuffer;
    displayAudioLed(wsPtr[0]);
    bytesCtr = 0;
  }
  wsBufferLen = 0;
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
    if (!stopAudio) wsJsonSend("stopRec", "1");
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
    if (!stopAudio) wsJsonSend("stopPlay", "1");
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
      if (micUse && !micRem) makeRecording(); // make recording from local mic
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

void setI2Schan(int whichChan) {
  // set I2S port for microphone, amp is opposite
  if (whichChan) {
    MIC_CHAN = I2S_NUM_1;
    AMP_CHAN = I2S_NUM_0;
  } else {
    MIC_CHAN = I2S_NUM_0;
    AMP_CHAN = I2S_NUM_1;
  }
}

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

  I2Smic = micSckPin == -1 ? false : true;
  
#ifdef CONFIG_IDF_TARGET_ESP32S3
  MIC_CHAN = I2S_NUM_0;
#endif
  
#ifdef ISCAM
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
    xTaskCreate(micRemTask, "micRemTask", MICREM_STACK_SIZE, NULL, MICREM_PRI, &micRemHandle);
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

  if (micUse && MIC_CHAN == I2S_NUM_1 && !I2Smic) {
    LOG_WRN("Only I2S devices supported on I2S_NUM_1");
    res = false;
  } else {
    if (micUse && micSdPin <= 0) {
      LOG_WRN("Microphone pins not defined");
      micUse = false;
    }
    if (mampUse && mampSdIo <= 0) {
      LOG_WRN("Amplifier pins not defined");
      mampUse = false;
    }
    if (micUse) {
      micUse = setupMic(); 
      if (!micUse) LOG_WRN("Unable to start mic");
    }
    if (mampUse) {
      mampUse = setupAmp();
      if (!mampUse) LOG_WRN("Unable to start amp");
    }
    
    if (micUse || mampUse) {
      if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(sampleBytes);
      if (audioBuffer == NULL && psramFound()) audioBuffer = (uint8_t*)ps_malloc(psramMax + (sizeof(int16_t) * DMA_BUFF_LEN));
      xTaskCreate(audioTask, "audioTask", AUDIO_STACK_SIZE, NULL, AUDIO_PRI, &audioHandle);
      if (micUse) LOG_INF("Sound capture is available using %s mic on I2S%i with gain %d", micLabels[I2Smic], MIC_CHAN, micGain);
      if (mampUse) LOG_INF("Speaker output is available using I2S amp on I2S%i with vol %d", AMP_CHAN, ampVol);
    } else res = false;
  }
  debugMemory("prepAudio");
  return res;
}

#endif
