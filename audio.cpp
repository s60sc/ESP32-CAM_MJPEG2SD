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
// A browser microphone and on a PC or phone can be used:
// - for VoiceChanger app, this is used instead of local mic
//   - need to press PC Mic button before selecting an action
// - for MJPEG2SD app, this is passed thru to speaker, independent of local mic
//   - need to enable use amp and pins in Config / Peripherals for Start Mic button to be available on web page
//   - browser mic should only be activated when need to speak
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

static bool micUse = false; // esp mic available
bool micRem = false; // use browser mic (depends on app)
static bool ampUse = false; // whether esp amp / speaker available
bool spkrRem = false; // use browser speaker
bool volatile stopAudio = false;
static bool micRecording = false;

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
int micGain = 0;  // microphone gain 0 is off 
int8_t ampVol = 0; // amplifier volume factor 0 is off

TaskHandle_t audioHandle = NULL;

static int totalSamples = 0;
static const uint8_t sampleWidth = sizeof(int16_t);
const size_t sampleBytes = DMA_BUFF_LEN * sampleWidth;
int16_t* sampleBuffer = NULL;
static uint8_t* wsBuffer = NULL;
static size_t wsBufferLen = 0;
uint8_t* audioBuffer = NULL; // VC recording buffer or cam mic input streamed to NVR
size_t audioBytes = 0; 

static const char* micLabels[2] = {"PDM", "I2S"};

#ifdef CONFIG_IDF_TARGET_ESP32S3
#define psramMax (ONEMEG * 6)
#else
#define psramMax (ONEMEG * 2)
#endif
#ifdef ISCAM
bool AudActive = false; // whether to show audio features
static File wavFile;
#endif

static uint8_t wavHeader[WAV_HDR_LEN] = { // WAV header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x57, 0x41, 0x56, 0x45, 0x66, 0x6D, 0x74, 0x20,
  0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x64, 0x61, 0x74, 0x61, 0x00, 0x00, 0x00, 0x00,
};

void applyVolume() {
  // determine required volume setting
  int8_t adjVol = ampVol * 2; // use web page setting
#ifdef ISVC
  adjVol = checkPotVol(adjVol);  // use potentiometer setting if available
#endif
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
  bool res = true;
  if (!micUse || !I2Smic) {
    // if not already started by setupMic()
    I2Sstd.setPins(mampBckIo, mampSwsIo, mampSdIo, -1, -1); // BCLK/SCK, LRCLK/WS, SDOUT, SDIN, MCLK
    res = I2Sstd.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT);
  } // already started by setupMic()
  return res;
}

void closeI2S() {
  I2Sstd.end();
  I2Spdm.end();
}

static void applyMicGain(size_t bytesRead) {
  // change esp mic gain by required factor
  uint8_t gainFactor = pow(2, micGain - MIC_GAIN_CENTER);
  for (int i = 0; i < bytesRead / sampleWidth; i++) {
    sampleBuffer[i] = constrain(sampleBuffer[i] * gainFactor, SHRT_MIN, SHRT_MAX);
  }
}

static size_t espMicInput() {
  // read esp mic
  size_t bytesRead = 0;
  if (micUse) {
    bytesRead = I2Smic ? I2Sstd.readBytes((char*)sampleBuffer, sampleBytes) : I2Spdm.readBytes((char*)sampleBuffer, sampleBytes);
    applyMicGain(bytesRead);
  }
  return bytesRead;
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
  memcpy(audioBuffer, wavHeader, WAV_HDR_LEN);
  return dataBytes;
}

/*********************************************************************/

#ifdef ISVC

static size_t micInput() {
  // get input from browser mic or else esp mic
  size_t bytesRead = (micRem) ? wsBufferLen : espMicInput();
  if (bytesRead && micRem) {
    // double buffer browser mic input
    memcpy(sampleBuffer, wsBuffer, bytesRead);
    wsBufferLen = 0;
    applyMicGain(bytesRead);
  } else if (micRem) delay(20);
  return bytesRead;
}

void browserMicInput(uint8_t* wsMsg, size_t wsMsgLen) {
  // input from browser mic via websocket
  if (micRem && !wsBufferLen) {
    // copy browser mic input into sampleBuffer for amp
    wsBufferLen = wsMsgLen;
    memcpy(wsBuffer, wsMsg, wsMsgLen);
  }
}

static void ampOutput(size_t bytesRead = sampleBytes) {
  // output to amplifier, apply required filtering and volume
  applyFilters();
  if (spkrRem) wsAsyncSendBinary((uint8_t*)sampleBuffer, bytesRead); // browser speaker
  else if (ampUse) I2Sstd.write((uint8_t*)sampleBuffer, bytesRead); // esp amp speaker
  displayAudioLed(sampleBuffer[0]);
}

static void passThru() {
  // play buffer from mic direct to amp
  size_t bytesRead = micInput();
  if (bytesRead) ampOutput(bytesRead);
}

static void makeRecording() {
  if (psramFound()) {
    LOG_INF("Recording ...");
    audioBytes = WAV_HDR_LEN; // leave space for wave header
    wsBufferLen = 0;
    while (audioBytes < psramMax) {
      size_t bytesRead = micInput();
      if (bytesRead) {
        memcpy(audioBuffer + audioBytes, sampleBuffer, bytesRead);
        audioBytes += bytesRead;
      }
      if (stopAudio) break;
    } // psram full
    if (!stopAudio) wsJsonSend("stopRec", "1");
    totalSamples = (audioBytes  - WAV_HDR_LEN) / sampleWidth;
    LOG_INF("%s recording of %d samples", stopAudio ? "Stopped" : "Finished",  totalSamples);  
    stopAudio = true;
  } else LOG_WRN("PSRAM needed to record and play");
}

static void playRecording() {
  if (psramFound()) {
    LOG_INF("Playing %d samples, initial volume: %d", totalSamples, ampVol); 
    for (int i = WAV_HDR_LEN; i < totalSamples * sampleWidth; i += sampleBytes) { 
      memcpy(sampleBuffer, audioBuffer+i, sampleBytes);
      ampOutput();
      if (stopAudio) break;
    }
    if (!stopAudio) wsJsonSend("stopPlay", "1");
    LOG_INF("%s playing of %d samples", stopAudio ? "Stopped" : "Finished", totalSamples);
    stopAudio = true;
  } else LOG_WRN("PSRAM needed to record and play");
}

static void VCactions() {
  // action user request
  stopAudio = false;
  closeI2S();
  prepAudio();
  setupFilters();
          
  switch (THIS_ACTION) {
    case RECORD_ACTION: 
      if (micRem) wsAsyncSendText("#M1");
      if (micUse || micRem) makeRecording();
    break;
    case PLAY_ACTION:
      // continues till stopped
      if (ampUse || spkrRem) playRecording(); // play previous recording
    break;
    case PASS_ACTION:
      if (ampUse || spkrRem) {
        if (micRem) wsAsyncSendText("#M1");
        LOG_INF("Passthru started");
        wsBufferLen = 0;
        while (!stopAudio) passThru();
        LOG_INF("Passthru stopped"); 
      }
    break;
    default: 
    break;
  }
  displayAudioLed(0);
  xSemaphoreGive(audioSemaphore);
}

#endif

/*****************************************************************/

#ifdef ISCAM

void browserMicInput(uint8_t* wsMsg, size_t wsMsgLen) {
  // input from browser mic via websocket, send to esp amp
  if (micRem && !wsBufferLen) {
    wsBufferLen = wsMsgLen;
    memcpy(wsBuffer, wsMsg, wsMsgLen);
    int8_t adjVol = ampVol * 2; // use web page setting
    if (adjVol) {
      // increase or reduce volume, 6 is unity eg midpoint of web slider
      adjVol = adjVol > 5 ? adjVol - 5 : adjVol - 7; 
      // apply volume control to samples
      int16_t* wsPtr = (int16_t*) wsBuffer;
      for (int i = 0; i < wsBufferLen / sizeof(int16_t); i++) {   
        // apply volume control 
        wsPtr[i] = adjVol < 0 ? wsPtr[i] / abs(adjVol) : constrain((int32_t)wsPtr[i] * adjVol, SHRT_MIN, SHRT_MAX);
      }
    }
    I2Sstd.write(wsBuffer, wsBufferLen);
    wsBufferLen = 0;
  }
}    

void startAudioRecord() {
  // called from openAvi() in mjpeg2sd.cpp
  // start audio recording and write recorded audio to SD card as WAV file 
  // combined into AVI file as PCM channel on FTP upload or browser download
  // so can be read by media players
  if (micUse && micGain) {
      wavFile = STORAGE.open(WAVTEMP, FILE_WRITE);
      wavFile.write(wavHeader, WAV_HDR_LEN); 
      micRecording = true;
      totalSamples = 0;
  } else {
    micRecording = false;
    LOG_WRN("No ESP mic defined or mic is off");
  }
}

void finishAudioRecord(bool isValid) {
  // called from closeAvi() in mjpeg2sd.cpp
  if (micRecording) {
    // finish a recording and save if valid
    micRecording = false; 
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

static void camActions() {
  // apply esp mic input to required outputs
  while (true) {
    size_t bytesRead = 0;
    if (micRecording || !audioBytes || spkrRem) bytesRead = espMicInput(); // load sampleBuffer
    if (bytesRead) {
      if (micRecording) {
        // record mic input to SD
        wavFile.write((uint8_t*)sampleBuffer, bytesRead);
        totalSamples += bytesRead / sampleWidth; 
      }
      if (!audioBytes) {
        // fill audioBuffer to send to NVR
        memcpy(audioBuffer, sampleBuffer, bytesRead);
        audioBytes = bytesRead;
      }
      // intercom esp mic to browser speaker
      if (spkrRem) wsAsyncSendBinary((uint8_t*)sampleBuffer, bytesRead);
    } else delay(20);
  }
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
  updateStatus("micSdPin", micPin);
  sprintf(micPin, "%d", I2S_WS);
  updateStatus("micSWsPin", micPin);
  sprintf(micPin, "%d", I2S_SCK);
  updateStatus("micSckPin", micPin);
#endif

  I2Smic = micSckPin == -1 ? false : true;
  
#ifdef CONFIG_IDF_TARGET_ESP32S3
  MIC_CHAN = I2S_NUM_0;
#endif
}

static void audioTask(void* parameter) {
  // loops to service each requirement for audio processing
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef ISCAM
    camActions(); // runs constantly
#endif
#ifdef ISVC
    VCactions(); // runs once
#endif
  }
  vTaskDelete(NULL);
}

void prepAudio() {
  // VC uses audio task for all activities
  // Cam uses audio task for microphone and intercom task for amplifier
#ifdef ISCAM
  predefPins();
#endif
  if (MIC_CHAN == I2S_NUM_1 && !I2Smic) LOG_WRN("Only I2S devices supported on I2S_NUM_1");
  else {
    if (micSdPin <= 0) LOG_WRN("Microphone pins not defined");
    else {
      micUse = setupMic(); 
      if (micUse) LOG_INF("Sound capture is available using %s mic on I2S%i with gain %d", micLabels[I2Smic], MIC_CHAN, micGain);
      else LOG_WRN("Unable to start ESP mic");
    }
    if (mampSdIo <= 0) LOG_WRN("Amplifier pins not defined");
    else {
      ampUse = setupAmp();
      if (ampUse) LOG_INF("Speaker output is available using I2S amp on I2S%i with vol %d", AMP_CHAN, ampVol);
      else LOG_WRN("Unable to start ESP amp");
    }
  }

  if (sampleBuffer == NULL) sampleBuffer = (int16_t*)malloc(sampleBytes);
  if (wsBuffer == NULL) wsBuffer = (uint8_t*)malloc(MAX_PAYLOAD_LEN);
#ifdef ISVC
  if (audioBuffer == NULL && psramFound()) audioBuffer = (uint8_t*)ps_malloc(psramMax + (sizeof(int16_t) * DMA_BUFF_LEN));
#endif
#ifdef ISCAM
  if (audioBuffer == NULL && psramFound()) audioBuffer = (uint8_t*)ps_malloc(sampleBytes);
#endif  
#ifdef ISVC
  // VC can still use audio task without esp mic or amp
  if (!micUse && !ampUse) LOG_WRN("Only browser mic and speaker can be used");
#endif
#ifdef ISCAM
  wsBufferLen = 0;
  // Audio task only needed for esp microphone
  if (!micUse) return;
#endif
  if (audioHandle == NULL) xTaskCreate(audioTask, "audioTask", AUDIO_STACK_SIZE, NULL, AUDIO_PRI, &audioHandle);
#ifdef ISCAM
  xTaskNotifyGive(audioHandle);
#endif
  debugMemory("prepAudio");
}

#endif
