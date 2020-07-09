
/* 
On the fly convert MJPEG file to AVI format when uploaded via FTP.
Allows recordings to replay at correct frame rate on media players.
The file names must include the frame count to be converted, 
so older style files will still be uploaded as MJPEGs.

s60sc 2020
*/

/* AVI file format:
header:
 240 bytes
per jpeg:
 4 byte 00dc marker
 4 byte jpeg size
 jpeg frame content
 0-3 bytes filler to align on DWORD boundary
footer:
 4 byte idx1 marker
 4 byte index size
 per jpeg:
  4 byte 00dc marker
  4 byte 0000
  4 byte jpeg location
  4 byte jpeg size
*/

#include "Arduino.h"
#include "FS.h" 

// avi header data
static const uint8_t dcBuf[4] = {0x30, 0x30, 0x64, 0x63};   // 00dc
static const uint8_t idx1Buf[4] = {0x69, 0x64, 0x78, 0x31}; // idx1
static const uint8_t zeroBuf[4] = {0x00, 0x00, 0x00, 0x00}; // 0000
static uint8_t* idxBuf;

#define AVI_HEADER_LEN 240 // AVI header length
static uint8_t aviHeader[AVI_HEADER_LEN] = { // AVI header template
  0x52, 0x49, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54,
  0xD0, 0x00, 0x00, 0x00, 0x68, 0x64, 0x72, 0x6C, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x84, 0x00, 0x00, 0x00,
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x73,
  0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x18, 0x00, 0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x4E, 0x46, 0x4F,
  0x10, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x4C, 0x49, 0x53, 0x54, 0x00, 0x00, 0x00, 0x00, 0x6D, 0x6F, 0x76, 0x69,
};

struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};
// indexed by frame type - needs to be consistent with sensor.h enum
static const frameSizeStruct frameSizeData[] = {
  {{0xA0, 0x00}, {0x78, 0x00}}, // qqvga
  {{0,0}, {0,0}}, 
  {{0,0}, {0,0}}, 
  {{0xF0, 0x00}, {0xB0, 0x00}}, // hqvga 
  {{0x40, 0x01}, {0xF0, 0x00}}, // qvga 
  {{0x90, 0x01}, {0x28, 0x01}}, // cif 
  {{0x80, 0x02}, {0xE0, 0x01}}, // vga 
  {{0x20, 0x03}, {0x58, 0x02}}, // svga 
  {{0x00, 0x04}, {0x00, 0x03}}, // xga 
  {{0x00, 0x05}, {0x00, 0x04}}, // sxga
  {{0x40, 0x06}, {0xB0, 0x04}}  // uxga 
};

extern const char* _STREAM_BOUNDARY; 
extern const char* _STREAM_PART;
static const size_t streamBoundaryLen = strlen(_STREAM_BOUNDARY);
static const size_t streamPartLen = strlen(_STREAM_PART)+6;
#define LENGTH_OFFSET 78 // from start of mjpeg boundary to Content-Length: value
#define REMAINDER_OFFSET 14 // from LENGTH_OFFSET to start of jpeg data
#define MJPEG_HDR (LENGTH_OFFSET + REMAINDER_OFFSET)
#define CHUNK_HDR 8 // bytes per jpeg hdr in AVI 
#define IDX_ENTRY 16 // bytes per index entry
#define MAX_FRAMES 20000

char mjpegHdrStr[MJPEG_HDR];
static bool doAVI = false;
static bool doAVIheader = false;
static uint16_t frameCnt = 0;
static uint16_t framePtr = 0;
static uint32_t idxOffset;
static uint8_t frameType;
static uint8_t FPS;
static size_t fileSize;

int* extractMeta(const char* fname);  
size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize);  

bool isAVI(File &fh) {
  // extract file metadata and determine if mjpeg or avi upload
  int* meta = extractMeta(fh.name()); 
  frameCnt = (uint16_t)meta[3];
  if (frameCnt > 0) { 
    // presence of frame count in file name indicates file suitable for conversion to AVI
    frameType = (uint8_t)meta[0];
    FPS = (uint8_t)meta[1];
    fileSize = fh.size();
    doAVI = true;
    doAVIheader = true;
    Serial.println("Uploading as AVI");
    return true;
  } else {
    doAVI = false;
    Serial.println("Uploading as MJPEG");
    return false;
  }
}

static inline void bigEndian(uint8_t* inBuff, uint32_t in) {
  // arrange bits in big endian order
  for (int i=0; i<4; i++) {
    inBuff[i] = in % 0x100;
    in = in >> 8;  
  }
}

static size_t buildAVIhdr(byte* &clientBuf) {
  // first call on file, build AVI header
  // apply framesize to avi header
  memcpy(aviHeader+0x40, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0xA8, frameSizeData[frameType].frameWidth, 2);
  memcpy(aviHeader+0x44, frameSizeData[frameType].frameHeight, 2);
  memcpy(aviHeader+0xAC, frameSizeData[frameType].frameHeight, 2);
  // update aviHeader with relevant stats
  size_t moviSize = fileSize - (streamBoundaryLen+streamPartLen)*frameCnt - streamBoundaryLen; // movie size
  size_t aviSize = moviSize + AVI_HEADER_LEN + ((CHUNK_HDR+IDX_ENTRY) * frameCnt); // AVI file size
  bigEndian(aviHeader+4, aviSize);
  bigEndian(aviHeader+0x20, (uint32_t)round(1000000.0f / FPS)); // us_per_frame 
  bigEndian(aviHeader+0x30, frameCnt);
  bigEndian(aviHeader+0x8c, frameCnt);
  bigEndian(aviHeader+0x84, FPS);
  bigEndian(aviHeader+0xe8, moviSize + (frameCnt * CHUNK_HDR) + 4); // data size

  memcpy(clientBuf, aviHeader, AVI_HEADER_LEN);
  doAVIheader = false;
  // prep buffer to store index data, appended to end of file
  free(idxBuf); // previous one
  idxBuf = (uint8_t*)ps_malloc(MAX_FRAMES*IDX_ENTRY); 
  memcpy(idxBuf, idx1Buf, 4); // index header
  bigEndian(idxBuf+4, frameCnt*IDX_ENTRY); // size of index
  idxOffset = 4;
  return AVI_HEADER_LEN;
}

static void buildIdx(size_t jpegSize) {
  // build AVI index into buffer
  // frame index is 8 byte header + 16 bytes per frame
  int idxPtr = CHUNK_HDR + (framePtr*IDX_ENTRY);
  memcpy(idxBuf+idxPtr, dcBuf, 4);
  memcpy(idxBuf+idxPtr+4, zeroBuf, 4);
  bigEndian(idxBuf+idxPtr+8, idxOffset); 
  bigEndian(idxBuf+idxPtr+12, jpegSize); 
  idxOffset += jpegSize + CHUNK_HDR;
}

size_t readClientBuf(File &fh, byte* &clientBuf, size_t buffSize) {
  static int32_t readLen = 0; 
  size_t indexLen = (frameCnt*IDX_ENTRY)+CHUNK_HDR;
  static int jStart = 0; // start of current jpeg
  static int jEnd = 0; // end of current jpeg
  static int iPtr = 0; // pointer in index buffer
  static int hdrOffset = 0; // indicates if mjpeg header straddles buffers
  static bool theEnd = false;
  if (theEnd) {
    // end of avi file processing, reset for next file
    theEnd = false;
    jStart = 0;
    jEnd = 0; 
    iPtr = 0;
    hdrOffset = 0;
    Serial.printf("\nProcessed %d of %d frames\n", framePtr, frameCnt);
    return 0; 
  }
  if (doAVI) {
    // AVI upload, make modifications
    if (doAVIheader) {
      framePtr = 0;
      return buildAVIhdr(clientBuf);
      
    } else {
      // subsequent AVI processing calls
      readLen = fh.read(clientBuf, buffSize); // load 32k cluster from SD
      if (readLen == 0) {
        // reached end of file, append index data, loop until done
        size_t sendLen = buffSize;
        if (indexLen-iPtr > buffSize) {
          // index bigger than buffer
          memcpy(clientBuf, idxBuf+iPtr, buffSize);
          iPtr += buffSize;
        } else {
          // final part of index
          memcpy(clientBuf, idxBuf+iPtr, indexLen-iPtr);
          sendLen = indexLen-iPtr;
          theEnd = true;
        }
        return sendLen;
        
      } else {
        // next buffer to modify to remove mjpeg headers and add avi headers 
        while (true) { // break out of loop when conditions occur
          if (jEnd < readLen) {
            // move to mjpeg header
            if (hdrOffset > 0) {
              // need to shift up buffer and copy in saved partial mjpeg header 
              memmove(clientBuf+hdrOffset, clientBuf, buffSize); 
              memcpy(clientBuf, mjpegHdrStr, hdrOffset); 
              readLen += hdrOffset;
              hdrOffset = jEnd = 0;
            }
            
            if (MJPEG_HDR > (readLen-jEnd)) {
              // remaining buffer content less than mjpeg header block, so postpone to next buffer
              hdrOffset = (readLen-jEnd);
              if (hdrOffset > 0) {
                memcpy(mjpegHdrStr, clientBuf+jEnd, hdrOffset); // string containing partial mjpeg header
                readLen -= hdrOffset;
                break;     
              } // else ignore     
            }

            jStart = jEnd + LENGTH_OFFSET; // offset from end of previous jpeg    
            if (jStart > readLen) {
                jEnd = readLen - jStart; // set -ve as offset to next buffer
                readLen = jEnd;
                break;
            }
            // extract jpeg size
            memcpy(mjpegHdrStr, clientBuf+jStart, 10); // string containing jpeg size
            mjpegHdrStr[10] = 0; // terminator
            size_t jpegSize = atoi(mjpegHdrStr);  
            jStart += REMAINDER_OFFSET;                                        
            // create AVI header for jpeg
            memcpy(clientBuf+jEnd, dcBuf, 4); 
            bigEndian(clientBuf+jEnd+4, jpegSize);
            buildIdx(jpegSize); // build index entry for this jpeg
            framePtr++;
            
            // shift jpeg data so starts after avi header
            readLen -= (MJPEG_HDR - CHUNK_HDR); // length of relevant data reduced  
            memmove(clientBuf+jEnd+CHUNK_HDR, clientBuf+jStart, readLen-jEnd);  
            // determine end of this jpeg
            jEnd += CHUNK_HDR + jpegSize;      
            if (jEnd > readLen) {
              jEnd -= readLen; // adjust for next buffer 
              break; 
              
            }
          } else {
            // for jpeg bigger than buffer
            jEnd -= readLen;
            break;
          }
        }   
      }
      // post loop processing, return modified data for ftp
      if (readLen < 0) return jStart-LENGTH_OFFSET;  // if reached end of file, send last part of final jpeg 
      else return readLen; 
    } 
  } else {
    // mjpeg upload, just return what received from SD card
    return fh.read(clientBuf, buffSize); 
  }
}

 
