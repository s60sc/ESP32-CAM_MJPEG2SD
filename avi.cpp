
/* 
Generate AVI format for recorded videos

s60sc 2020, 2022
*/

/* AVI file format:
header:
 310 bytes
per jpeg:
 4 byte 00dc marker
 4 byte jpeg size
 jpeg frame content
0-3 bytes filler to align on DWORD boundary
per PCM (audio file)
 4 byte 01wb marker
 4 byte pcm size
 pcm content
 0-3 bytes filler to align on DWORD boundary
footer:
 4 byte idx1 marker
 4 byte index size
 per jpeg:
  4 byte 00dc marker
  4 byte 0000
  4 byte jpeg location
  4 byte jpeg size
 per pcm:
  4 byte 01wb marker
  4 byte 0000
  4 byte pcm location
  4 byte pcm size
*/

#include "appGlobals.h"

// avi header data
const uint8_t dcBuf[4] = {0x30, 0x30, 0x64, 0x63};   // 00dc
const uint8_t wbBuf[4] = {0x30, 0x31, 0x77, 0x62};   // 01wb
static const uint8_t idx1Buf[4] = {0x69, 0x64, 0x78, 0x31}; // idx1
static const uint8_t zeroBuf[4] = {0x00, 0x00, 0x00, 0x00}; // 0000
static uint8_t* idxBuf[2] = {NULL, NULL};

uint8_t aviHeader[AVI_HEADER_LEN] = { // AVI header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54,
  0x16, 0x01, 0x00, 0x00, 0x68, 0x64, 0x72, 0x6C, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x6C, 0x00, 0x00, 0x00,
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x73,
  0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x18, 0x00, 0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x4C, 0x49, 0x53, 0x54, 0x56, 0x00, 0x00, 0x00, 
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x61, 0x75, 0x64, 0x73,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x11, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x12, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x11, 0x2B, 0x00, 0x00, 0x11, 0x2B, 0x00, 0x00,
  0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 
  0x4C, 0x49, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x6D, 0x6F, 0x76, 0x69,
};

struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};
// indexed by frame type - needs to be consistent with sensor.h framesize_t enum
static const frameSizeStruct frameSizeData[] = {
  {{0x60, 0x00}, {0x60, 0x00}}, // 96X96
  {{0xA0, 0x00}, {0x78, 0x00}}, // qqvga 
  {{0xB0, 0x00}, {0x90, 0x00}}, // qcif 
  {{0xF0, 0x00}, {0xB0, 0x00}}, // hqvga 
  {{0xF0, 0x00}, {0xF0, 0x00}}, // 240X240
  {{0x40, 0x01}, {0xF0, 0x00}}, // qvga 
  {{0x90, 0x01}, {0x28, 0x01}}, // cif 
  {{0xE0, 0x01}, {0x40, 0x01}}, // hvga 
  {{0x80, 0x02}, {0xE0, 0x01}}, // vga 
  {{0x20, 0x03}, {0x58, 0x02}}, // svga 
  {{0x00, 0x04}, {0x00, 0x03}}, // xga 
  {{0x00, 0x05}, {0xD0, 0x02}}, // hd
  {{0x00, 0x05}, {0x00, 0x04}}, // sxga
  {{0x40, 0x06}, {0xB0, 0x04}}  // uxga 
};

#define IDX_ENTRY 16 // bytes per index entry

// separate index for motion capture and timelapse
static size_t idxPtr[2];
static size_t idxOffset[2];
static size_t moviSize[2];
static size_t audSize;
static size_t indexLen[2];
static File wavFile;
bool haveSoundFile = false;


void prepAviIndex(bool isTL) {
  // prep buffer to store index data, gets appended to end of file
  if (idxBuf[isTL] == NULL) idxBuf[isTL] = (uint8_t*)ps_malloc((maxFrames+1)*IDX_ENTRY); // include some space for audio index
  memcpy(idxBuf[isTL], idx1Buf, 4); // index header
  idxPtr[isTL] = CHUNK_HDR;  // leave 4 bytes for index size
  moviSize[isTL] = indexLen[isTL] = 0;
  idxOffset[isTL] = 4; // 4 byte offset
}

void buildAviHdr(uint8_t FPS, uint8_t frameType, uint16_t frameCnt, bool isTL) {
  // update AVI header template with file specific details
  size_t aviSize = moviSize[isTL] + AVI_HEADER_LEN + ((CHUNK_HDR+IDX_ENTRY) * (frameCnt+(haveSoundFile?1:0))); // AVI content size 
  // update aviHeader with relevant stats
  memcpy(aviHeader+4, &aviSize, 4);
  uint32_t usecs = (uint32_t)round(1000000.0f / FPS); // usecs_per_frame 
  memcpy(aviHeader+0x20, &usecs, 4); 
  memcpy(aviHeader+0x30, &frameCnt, 2);
  memcpy(aviHeader+0x8C, &frameCnt, 2);
  memcpy(aviHeader+0x84, &FPS, 1);
  uint32_t dataSize = moviSize[isTL] + ((frameCnt+(haveSoundFile?1:0)) * CHUNK_HDR) + 4; 
  memcpy(aviHeader+0x12E, &dataSize, 4); // data size 

  // apply video framesize to avi header
  memcpy(aviHeader+0x40, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0xA8, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0x44, frameSizeData[frameType].frameHeight, 2);
  memcpy(aviHeader+0xAC, frameSizeData[frameType].frameHeight, 2);

#if INCLUDE_AUDIO
  uint8_t withAudio = 2; // increase number of streams for audio
  if (isTL) memcpy(aviHeader+0x100, zeroBuf, 4); // no audio for timelapse
  else {
    if (haveSoundFile) memcpy(aviHeader+0x38, &withAudio, 1); 
    memcpy(aviHeader+0x100, &audSize, 4); // audio data size
  }
  // apply audio details to avi header
  memcpy(aviHeader+0xF8, &SAMPLE_RATE, 4);
  uint32_t bytesPerSec = SAMPLE_RATE * 2;
  memcpy(aviHeader+0x104, &bytesPerSec, 4); // suggested buffer size
  memcpy(aviHeader+0x11C, &SAMPLE_RATE, 4);
  memcpy(aviHeader+0x120, &bytesPerSec, 4); // bytes per sec
#else
  memcpy(aviHeader+0x100, zeroBuf, 4);
#endif

  // reset state for next recording
  moviSize[isTL] = idxPtr[isTL] = 0;
  idxOffset[isTL] = 4; // 4 byte offset
}

void buildAviIdx(size_t dataSize, bool isVid, bool isTL) {
  // build AVI video index into buffer - 16 bytes per frame
  // called from saveFrame() for each frame
  moviSize[isTL] += dataSize;
  if (isVid) memcpy(idxBuf[isTL]+idxPtr[isTL], dcBuf, 4);
  else memcpy(idxBuf[isTL]+idxPtr[isTL], wbBuf, 4);
  memcpy(idxBuf[isTL]+idxPtr[isTL]+4, zeroBuf, 4);
  memcpy(idxBuf[isTL]+idxPtr[isTL]+8, &idxOffset[isTL], 4); 
  memcpy(idxBuf[isTL]+idxPtr[isTL]+12, &dataSize, 4); 
  idxOffset[isTL] += dataSize + CHUNK_HDR;
  idxPtr[isTL] += IDX_ENTRY; 
}

size_t writeAviIndex(byte* clientBuf, size_t buffSize, bool isTL) {
  // write completed index to avi file
  // called repeatedly from closeAvi() until return 0
  if (idxPtr[isTL] < indexLen[isTL]) {
    if (indexLen[isTL]-idxPtr[isTL] > buffSize) {
      memcpy(clientBuf, idxBuf[isTL]+idxPtr[isTL], buffSize);
      idxPtr[isTL] += buffSize;
      return buffSize;
    } else {
      // final part of index
      size_t final = indexLen[isTL]-idxPtr[isTL];
      memcpy(clientBuf, idxBuf[isTL]+idxPtr[isTL], final);
      idxPtr[isTL] = indexLen[isTL];
      return final;    
    }
  }
  return idxPtr[isTL] = 0;
}
  
void finalizeAviIndex(uint16_t frameCnt, bool isTL) {
  // update index with size
  uint32_t sizeOfIndex = (frameCnt+(haveSoundFile?1:0))*IDX_ENTRY;
  memcpy(idxBuf[isTL]+4, &sizeOfIndex, 4); // size of index 
  indexLen[isTL] = sizeOfIndex + CHUNK_HDR;
  idxPtr[isTL] = 0; // pointer to index buffer
}

bool haveWavFile(bool isTL) {
  haveSoundFile = false;
  audSize = 0;
#if INCLUDE_AUDIO
  if (isTL) return false;
  // check if wave file exists
  if (!STORAGE.exists(WAVTEMP)) return 0; 
  // open it and get its size
  wavFile = STORAGE.open(WAVTEMP, FILE_READ);
  if (wavFile) {
    // add sound file index
    audSize = wavFile.size() - WAV_HDR_LEN;
    buildAviIdx(audSize, false); 
    // add sound file header    
    wavFile.seek(WAV_HDR_LEN, SeekSet); // skip over header
    haveSoundFile = true;
  } 
#endif
  return haveSoundFile;
}

size_t writeWavFile(byte* clientBuf, size_t buffSize) {
  // read in wav file and write to avi file
  // called repeatedly from closeAvi() until return 0
  static size_t offsetWav = CHUNK_HDR;
  if (offsetWav) {
    // add sound file header         
    memcpy(clientBuf, wbBuf, 4);     
    memcpy(clientBuf+4, &audSize, 4); 
  } 
  size_t readLen = wavFile.read(clientBuf+offsetWav, buffSize-offsetWav) + offsetWav; 
  offsetWav = 0;
  if (readLen) return readLen; 
  // get here if finished
  wavFile.close();
  STORAGE.remove(WAVTEMP);
  offsetWav = CHUNK_HDR;
  return 0;
}
