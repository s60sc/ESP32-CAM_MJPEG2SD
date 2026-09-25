
/* 
 Detect movement in sequential images using background subtraction.
 
 Very small (96x96) bitmaps are used both to provide image smoothing to reduce spurious motion changes 
 and to enable rapid processing
 Bitmaps can either be color or grayscale. Color requires triple memory
 of grayscale and more processing.

 The amount of change between images will depend on the frame rate.
 A faster frame rate will need a higher sensitivity

 When frame size is changed the OV2640 outputs a few glitched frames whilst it 
 makes the transition. These could be interpreted as spurious motion.

 Machine Learning can be incorporated to further discriminate when motion detection 
 has occurred by classifying whether the object in the frame is of a particular
 type of interest, eg a human, animal, vehicle etc. 
 
 s60sc 2020, 2023, 2025
*/

#include "appGlobals.h"

#if INCLUDE_TINYML
#include TINY_ML_LIB
#endif

#define INACTIVE_COLOR 96 // color for inactive motion pixel
#define JPEG_QUAL 80 // % quality for generated motion detect jpeg
  
// motion recording parameters
bool dbgMotion = false;
int detectMotionFrames = 5; // min sequence of changed frames to confirm motion 
int detectNightFrames = 10; // frames of sequential darkness to avoid spurious day / night switching
// define region of interest, ie exclude top and bottom of image from movement detection if required
// divide image into detectNumBands horizontal bands, define start and end bands of interest, 1 = top
int detectNumBands = 10;
int detectStartBand = 3;
int detectEndBand = 8; // inclusive
int detectChangeThreshold = 15; // min difference in pixel comparison to indicate a change
uint8_t colorDepth; // set by depthColor config
bool mlUse = false; // whether to use ML for motion detection, requires INCLUDE_TINYML to be true
float mlProbability = 0.8; // minimum probability (0.0 - 1.0) for positive classification

uint8_t lightLevel; // Current ambient light level 
uint8_t nightSwitch = 20; // initial white level % for night/day switching
float motionVal = 8.0; // initial motion sensitivity setting
uint8_t* motionJpeg = NULL;
size_t motionJpegLen = 0;
static uint8_t* currBuff = NULL;

#ifndef AUXILIARY

#if INCLUDE_NEW_JPG
// use esp_new_jpeg library instead of built in
// uses more memory as not hard coded into ROM
// download latest repo from https://components.espressif.com/components/espressif/esp_new_jpeg
// convert to arduino library as per https://github.com/s60sc/ESP32-CAM_MJPEG2SD/discussions/661#discussioncomment-14507339
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

struct esp_jpeg_stream {
    jpeg_dec_handle_t       jpeg_dec;
    jpeg_dec_io_t*          jpeg_io;
    jpeg_dec_header_info_t* out_info;
    jpeg_pixel_format_t     output_type;
};
typedef struct esp_jpeg_stream* esp_jpeg_stream_handle_t;

static void jpgReduce(int inWidth, int inHeight, uint8_t downsize, int* outWidth, int* outHeight);
static bool jpg2rgbOpen(esp_jpeg_stream_handle_t jpegHandle, uint16_t width, uint16_t height);
static bool jpg2rgb(esp_jpeg_stream_handle_t jpegHandle, uint8_t* inputBuf, int inputLen, uint8_t* outputBuf);
static bool jpg2rgbClose(esp_jpeg_stream_handle_t jpegHandle);
static size_t rgb2jpg(uint8_t* rgb888, int width, int height, int qual, uint8_t* outputBuf);
#else
// built in
static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale);
#endif

/**********************************************************************************/


bool isNight(uint8_t nightSwitch) {
  // check if night time for suspending recording
  // or for switching relay if enabled
  static bool nightTime = false;
  static uint16_t nightCnt = 0;
  if (nightTime) {
    if (lightLevel > nightSwitch) {
      // light image
      if (nightCnt > 0) nightCnt--;
      // signal day time after given sequence of light frames
      if (nightCnt == 0) {
        nightTime = false;
        LOG_INF("Day time");
      }
    }
  } else {
    if (lightLevel < nightSwitch) {
      // dark image
      nightCnt++;
      // signal night time after given sequence of dark frames
      if (nightCnt > detectNightFrames) {
        nightTime = true;     
        nightCnt = detectNightFrames;           
        LOG_INF("Night time"); 
      }
    } else {
      // back to light while not yet in nightTime: reset counter
      if (nightCnt > 0) nightCnt--;
    }
  } 
  return nightTime;
}

// Bilinear interpolation of four neighboring pixels using 16‑bit fixed‑point fractions
static inline uint8_t interpolatePixel(
    uint8_t a, uint8_t b, uint8_t c, uint8_t d,
    uint32_t xFrac8, uint32_t yFrac8)
{
    constexpr uint32_t FRACTION_TO_8BIT_SHIFT = 8;
    constexpr uint32_t ROUNDING_OFFSET = 0x8000; // midpoint rounding for 16‑bit fixed‑point
    constexpr int32_t MIN_COLOR_VALUE = 0;
    constexpr int32_t MAX_COLOR_VALUE = 255;
    constexpr uint32_t FIXED_POINT_SHIFT = 16;

    // Convert 8‑bit samples to signed 32‑bit for arithmetic
    const int32_t val_a = a;
    const int32_t val_b = b;
    const int32_t val_c = c;
    const int32_t val_d = d;

    // Convert fractional inputs to signed 32‑bit
    const int32_t xFrac = static_cast<int32_t>(xFrac8);
    const int32_t yFrac = static_cast<int32_t>(yFrac8);

    // Horizontal deltas for top and bottom rows
    const int32_t diff_ab = val_b - val_a;
    const int32_t diff_cd = val_d - val_c;

    // Horizontal interpolation of top and bottom rows in 8‑bit fixed‑point
    const int32_t top = (val_a << FRACTION_TO_8BIT_SHIFT) + diff_ab * xFrac;
    const int32_t bot = (val_c << FRACTION_TO_8BIT_SHIFT) + diff_cd * xFrac;

    // Vertical interpolation between top and bottom
    const int32_t diffY = bot - top;
    const int32_t res = (top << FRACTION_TO_8BIT_SHIFT) + (diffY * yFrac);

    // Convert back to 8‑bit with rounding
    const int32_t final_val = (res + ROUNDING_OFFSET) >> FIXED_POINT_SHIFT;

    // Clamp to valid color range
    return static_cast<uint8_t>(
        final_val > MAX_COLOR_VALUE ? MAX_COLOR_VALUE :
        (final_val < MIN_COLOR_VALUE ? MIN_COLOR_VALUE : final_val));
}

// Rescales an image using bilinear interpolation with fixed‑point stepping
static void rescaleImage(const uint8_t* __restrict input, int inputWidth, int inputHeight,
    uint8_t* __restrict output, int outputWidth, int outputHeight,
    uint8_t colorDepth)
{
    // Minimum valid parameters and fixed‑point constants
    constexpr int MIN_VALID_DIMENSION = 1;
    constexpr int MIN_VALID_COLOR_DEPTH = 1;
    constexpr uint32_t FIXED_POINT_SHIFT = 16;
    constexpr uint32_t FIXED_POINT_MASK = 0xFFFF;
    constexpr uint32_t FRACTION_TO_8BIT_SHIFT = 8;
    constexpr uint32_t FRACTION_8BIT_MASK = 0xFF;
    constexpr uint8_t RGB_COLOR_DEPTH = 3;
    constexpr uint32_t ADJACENT_PIXEL_OFFSET = 1;

    // Reject invalid buffers or dimensions
    if (!input || !output ||
        inputWidth < MIN_VALID_DIMENSION || inputHeight < MIN_VALID_DIMENSION ||
        outputWidth < MIN_VALID_DIMENSION || outputHeight < MIN_VALID_DIMENSION ||
        colorDepth < MIN_VALID_COLOR_DEPTH) {
        return;
    }

    // Fast path: identical dimensions → direct copy
    if (inputWidth == outputWidth && inputHeight == outputHeight) {
        if (input != output) {
            const size_t totalBytes =
                static_cast<size_t>(inputWidth) *
                static_cast<size_t>(inputHeight) *
                colorDepth;
            memcpy(output, input, totalBytes); // raw block copy
        }
        return;
    }

    // Compute fixed‑point scaling increments for X/Y
    const uint32_t xStep =
        (static_cast<uint32_t>(inputWidth) << FIXED_POINT_SHIFT) /
        static_cast<uint32_t>(outputWidth);
    const uint32_t yStep =
        (static_cast<uint32_t>(inputHeight) << FIXED_POINT_SHIFT) /
        static_cast<uint32_t>(outputHeight);

    // Last valid source pixel index for bilinear lookup
    const uint32_t max_y = static_cast<uint32_t>(inputHeight) - ADJACENT_PIXEL_OFFSET;
    const uint32_t max_x = static_cast<uint32_t>(inputWidth) - ADJACENT_PIXEL_OFFSET;

    // Byte stride per row for input/output
    const size_t rowStride = static_cast<size_t>(inputWidth) * colorDepth;
    const size_t outRowStride = static_cast<size_t>(outputWidth) * colorDepth;

    const size_t colorDepthSz = colorDepth; // avoid repeated casts

    // Single‑channel grayscale path
    if (colorDepth == MIN_VALID_COLOR_DEPTH) {
        uint32_t yPos = 0;
        for (int i = 0; i < outputHeight; ++i) {
            const uint32_t yL = yPos >> FIXED_POINT_SHIFT; // source row low
            const uint32_t yFrac8 =
                (yPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT; // vertical fraction
            uint32_t yH = yL + ADJACENT_PIXEL_OFFSET;
            yH = (yH > max_y) ? max_y : yH; // clamp high row

            const uint8_t* rowL = input + static_cast<size_t>(yL) * rowStride;
            const uint8_t* rowH = input + static_cast<size_t>(yH) * rowStride;
            uint8_t* outRow = output + static_cast<size_t>(i) * outRowStride;

            uint32_t xPos = 0;
            for (int j = 0; j < outputWidth; ++j) {
                const uint32_t xL = xPos >> FIXED_POINT_SHIFT; // source col low
                const uint32_t xFrac8 =
                    (xPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT; // horizontal fraction
                uint32_t xH = xL + ADJACENT_PIXEL_OFFSET;
                xH = (xH > max_x) ? max_x : xH; // clamp high col

                // Four neighboring grayscale samples
                const uint8_t* pLL = rowL + xL;
                const uint8_t* pLH = rowL + xH;
                const uint8_t* pHL = rowH + xL;
                const uint8_t* pHH = rowH + xH;

                outRow[j] = interpolatePixel(
                    *pLL, *pLH, *pHL, *pHH, xFrac8, yFrac8); // bilinear sample

                xPos += xStep; // advance horizontal position
            }
            yPos += yStep; // advance vertical position
        }
    }
    // 3‑channel RGB path with optimized pointer increments
    else if (colorDepth == RGB_COLOR_DEPTH) {
        uint32_t yPos = 0;

        for (int i = 0; i < outputHeight; ++i) {
            const uint32_t yL = yPos >> FIXED_POINT_SHIFT;
            const uint32_t yFrac8 =
                (yPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT;

            uint32_t yH = yL + ADJACENT_PIXEL_OFFSET;
            yH = (yH > max_y) ? max_y : yH;

            const uint8_t* rowL = input + static_cast<size_t>(yL) * rowStride;
            const uint8_t* rowH = input + static_cast<size_t>(yH) * rowStride;
            uint8_t* outRow = output + static_cast<size_t>(i) * outRowStride;

            uint32_t xPos = 0;

            for (int j = 0; j < outputWidth; ++j) {
                const uint32_t xL = xPos >> FIXED_POINT_SHIFT;
                const uint32_t xFrac8 =
                    (xPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT;

                uint32_t xH = xL + ADJACENT_PIXEL_OFFSET;
                xH = (xH > max_x) ? max_x : xH;

                // Correct LL/HL pointers (based on xL)
                const uint8_t* pLL = rowL + static_cast<size_t>(xL) * RGB_COLOR_DEPTH;
                const uint8_t* pHL = rowH + static_cast<size_t>(xL) * RGB_COLOR_DEPTH;

                // Correct LH/HH pointers (based on xH)
                const uint8_t* pLH = rowL + static_cast<size_t>(xH) * RGB_COLOR_DEPTH;
                const uint8_t* pHH = rowH + static_cast<size_t>(xH) * RGB_COLOR_DEPTH;

                uint8_t* pOut = outRow + static_cast<size_t>(j) * RGB_COLOR_DEPTH;

                // Per‑channel bilinear interpolation
                pOut[0] = interpolatePixel(pLL[0], pLH[0], pHL[0], pHH[0], xFrac8, yFrac8); // R
                pOut[1] = interpolatePixel(pLL[1], pLH[1], pHL[1], pHH[1], xFrac8, yFrac8); // G
                pOut[2] = interpolatePixel(pLL[2], pLH[2], pHL[2], pHH[2], xFrac8, yFrac8); // B

                xPos += xStep;
            }

            yPos += yStep;
        }
    }
    // Generic multi‑channel path for arbitrary colorDepth
    else {
        uint32_t yPos = 0;
        for (int i = 0; i < outputHeight; ++i) {
            const uint32_t yL = yPos >> FIXED_POINT_SHIFT;
            const uint32_t yFrac8 =
                (yPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT;
            uint32_t yH = yL + ADJACENT_PIXEL_OFFSET;
            yH = (yH > max_y) ? max_y : yH;

            const uint8_t* rowL = input + static_cast<size_t>(yL) * rowStride;
            const uint8_t* rowH = input + static_cast<size_t>(yH) * rowStride;
            uint8_t* outRow = output + static_cast<size_t>(i) * outRowStride;

            uint32_t xPos = 0;
            for (int j = 0; j < outputWidth; ++j) {
                const uint32_t xL = xPos >> FIXED_POINT_SHIFT;
                const uint32_t xFrac8 =
                    (xPos & FIXED_POINT_MASK) >> FRACTION_TO_8BIT_SHIFT;
                uint32_t xH = xL + ADJACENT_PIXEL_OFFSET;
                xH = (xH > max_x) ? max_x : xH;

                // Byte offsets for multi‑channel pixels
                const size_t xL_off = static_cast<size_t>(xL) * colorDepthSz;
                const size_t xH_off = static_cast<size_t>(xH) * colorDepthSz;
                const size_t j_off = static_cast<size_t>(j) * colorDepthSz;

                // Four neighboring multi‑channel pixels
                const uint8_t* pLL = rowL + xL_off;
                const uint8_t* pLH = rowL + xH_off;
                const uint8_t* pHL = rowH + xL_off;
                const uint8_t* pHH = rowH + xH_off;

                // Interpolate each channel independently
                for (size_t ch = 0; ch < colorDepthSz; ++ch) {
                    outRow[j_off + ch] = interpolatePixel(
                        pLL[ch], pLH[ch], pHL[ch], pHH[ch], xFrac8, yFrac8);
                }
                xPos += xStep;
            }
            yPos += yStep;
        }
    }
}

static void rgbToGray(uint8_t* buffer, int width, int height) {
  // convert rgb buffer to grayscale in place
  for (int i = 0; i < width * height; ++i) {
    int index = i * 3;
    // Calculate grayscale value using luminance formula
    buffer[i] = (uint8_t)(((77 * buffer[index]) + (150 * buffer[index + 1]) + (29 * buffer[index + 2])) >> 8);
  }
}

#if INCLUDE_TINYML

static int getImageData(size_t offset, size_t length, float *out_ptr) {
  // copy to features as grayscale or RGB
  size_t pixelPtr = offset * colorDepth;
  size_t out_ptr_idx = 0;
  while (out_ptr_idx < length) {
    out_ptr[out_ptr_idx++] = (colorDepth == RGB888_BYTES)  
      ? (float)((currBuff[pixelPtr] << 16) + (currBuff[pixelPtr + 1] << 8) + currBuff[pixelPtr + 2])
      : (float)((currBuff[pixelPtr] << 16) + (currBuff[pixelPtr] << 8) + currBuff[pixelPtr]);  
    pixelPtr += colorDepth;
  } 
  return 0;
}

static bool tinyMLclassify(size_t (RESIZE_DIM) {
  // convert input data to appropriate format
  bool out = false;
  uint32_t dTime = millis(); 
  // reduce size of bitmap to that required by classifier and copy to features as grayscale or RGB
  if (RESIZE_DIM != EI_CLASSIFIER_INPUT_WIDTH) {
    size_t tempSize = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT * colorDepth;
    uint8_t* tempBuff = (uint8_t*)ps_malloc(tempSize);
    rescaleImage(currBuff, RESIZE_DIM, RESIZE_DIM, tempBuff, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT, colorDepth);
    memcpy(currBuff, tempBuff, tempSize);
    free(tempBuff);
  }
  signal_t features_signal;
  features_signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  features_signal.get_data = &getImageData;

  // Run the classifier
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, false);
  if (res == EI_IMPULSE_OK) {
    if (result.classification[0].value > mlProbability) {
      out = true; // sufficient classification match, so keep motion detection
      if (dbgVerbose) {
        LOG_VRB("Prob: %0.2f, Timing: DSP %d ms, inference %d ms, anomaly %d ms", 
        result.classification[0].value, result.timing.dsp, result.timing.classification, result.timing.anomaly);
        char outcome[200] = {0};
        char* outPtr = outcome; // ⚡ Bolt optimization: track pointer to prevent O(N^2) strlen overhead
        size_t rem = sizeof(outcome);
        for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
          int n = snprintf(outPtr, rem, "%s: %.2f, ", ei_classifier_inferencing_categories[i], result.classification[i].value);
          if (n > 0 && (size_t)n < rem) { 
            outPtr += n; 
            rem -= n; 
          } else break;
        }
        LOG_VRB("Predictions - %s in %ums", outcome, millis() - dTime);
      } 
    } 
  } else LOG_WRN("Failed to run classifier (%d)", res);
  return out;
}
#endif

// Configurable block size for motion detection (1x1, 2x2, 4x4, etc.)
uint8_t detectBlockSize = 1;

// Stores motion analysis results: changed pixel count, movement threshold, and ambient light level.
struct MotionAnalysisResult {
    int changeCount;
    int moveThreshold;
    uint32_t lightLevel;
};

// Analyzes a frame for motion and light by comparing current and previous pixel buffers.
static bool checkedImageBytes(size_t width, size_t height, size_t bytesPerPixel, size_t& result) {
    if (bytesPerPixel == 0 || (width != 0 && height > SIZE_MAX / width)) {
        return false;
    }

    const size_t pixels = width * height;
    if (pixels > SIZE_MAX / bytesPerPixel) {
        return false;
    }

    result = pixels * bytesPerPixel;
    return true;
}

static MotionAnalysisResult analyzeMotionFrame(
    const uint8_t* __restrict currBuff, 
    const uint8_t* __restrict prevBuff, 
    size_t currBuffSize, 
    uint8_t colorDepth, 
    size_t RESIZE_DIM, 
    size_t RESIZE_DIM_SQ,
    uint8_t* __restrict changeMap
#if INCLUDE_NEW_JPG
    , uint8_t* __restrict jpgBuf
#endif
) {
    MotionAnalysisResult res = {0, 0, 0}; // Initialize result structure to zero
    size_t requiredInputBytes = 0;
    if (currBuff == nullptr || prevBuff == nullptr ||
        !checkedImageBytes(RESIZE_DIM, RESIZE_DIM, colorDepth, requiredInputBytes) ||
        currBuffSize < requiredInputBytes) {
        LOG_ERR("motionDetect: invalid motion buffers");
        return res;
    }

    uint32_t lux = 0; // Accumulator for raw light level calculation
    int changeCount = 0; // Counter for blocks exceeding change threshold
    
    size_t blockSize = detectBlockSize;
    if (blockSize < 1) {
        blockSize = 1; // Enforce minimum block size
    }

    // Calculate total number of blocks to scale the threshold correctly
    size_t blocksX = (RESIZE_DIM + blockSize - 1) / blockSize;
    size_t blocksY = (RESIZE_DIM + blockSize - 1) / blockSize;
    size_t totalBlocks = blocksX * blocksY;

    size_t safeNumBands = (detectNumBands > 0) ? detectNumBands : 1; // Fallback to 1 band if undefined
    
    // Clamp band indices to prevent underflow and out-of-bounds access
    size_t startBand = (detectStartBand > 0) ? detectStartBand : 1; // Default to first band
    size_t endBand = (detectEndBand > 0) ? detectEndBand : safeNumBands; // Default to last band
    if (startBand > safeNumBands) startBand = safeNumBands; // Clamp start to max bands
    if (endBand > safeNumBands) endBand = safeNumBands; // Clamp end to max bands
    if (startBand > endBand) startBand = endBand; // Ensure valid range

    // Set horizontal region of interest in image (2D Y coordinates)
    size_t startY = (RESIZE_DIM * (startBand - 1) / safeNumBands);
    size_t endY = (RESIZE_DIM * endBand / safeNumBands);
    
    // Clamp ROI bounds to image dimensions
    if (startY > RESIZE_DIM) startY = RESIZE_DIM;
    if (endY > RESIZE_DIM) endY = RESIZE_DIM;
    if (startY > endY) startY = endY;

    // Threshold is a percentage of total BLOCKS, ensuring consistent physical sensitivity
    res.moveThreshold = std::max(1, (int)((totalBlocks * (11.0f - motionVal)) / 100.0f));

    // Pre-calculate fixed-point multiplier for light level normalization (avoids floating-point overhead)
    constexpr uint8_t FP_SHIFT = 24; // Fixed-point fractional bits
    uint32_t lightMult = 0; // Multiplier for converting raw lux to percentage
    if (RESIZE_DIM_SQ > 0 && RESIZE_DIM_SQ <= SIZE_MAX / 255U) {
        const uint32_t divisor =
            static_cast<uint32_t>(RESIZE_DIM_SQ * 255U);
        lightMult = (100U << FP_SHIFT) / divisor;
    }

    bool isDbg = dbgMotion && (changeMap != nullptr); // Flag indicating if debug visualization is active

    // Set up display image for motion tracking debug
    if (colorDepth == GRAYSCALE_BYTES) { 
        // --- GRAYSCALE PATH --- (8-bit per pixel processing)
        for (size_t by = 0; by < RESIZE_DIM; by += blockSize) {
            size_t blockEndY = (by + blockSize < RESIZE_DIM) ? (by + blockSize) : RESIZE_DIM;
            bool inROI = (by >= startY && by < endY);
            
            for (size_t bx = 0; bx < RESIZE_DIM; bx += blockSize) {
                size_t blockEndX = (bx + blockSize < RESIZE_DIM) ? (bx + blockSize) : RESIZE_DIM;
                
                uint32_t sad = 0; // Sum of Absolute Differences for the block
                uint32_t currSum = 0;
                size_t pixelsInBlock = 0;
                
                for (size_t y = by; y < blockEndY; ++y) {
                    const uint8_t* currRow = currBuff + y * RESIZE_DIM;
                    const uint8_t* prevRow = prevBuff + y * RESIZE_DIM;
                    for (size_t x = bx; x < blockEndX; ++x) {
                        const uint8_t currPix = currRow[x];
                        const uint8_t prevPix = prevRow[x];
                        currSum += currPix;

                        const int diff = static_cast<int>(currPix) - static_cast<int>(prevPix);
                        sad += static_cast<uint32_t>(diff < 0 ? -diff : diff);
                        ++pixelsInBlock;
                    }
                }
                
                if (pixelsInBlock == 0) continue;
                
                lux += currSum; // Add total block sum to lux
                uint16_t currAvg = (uint16_t)(currSum / pixelsInBlock);
                
                // Compare SAD against scaled threshold to prevent cancellation
                bool isChanged = (sad > (uint32_t)detectChangeThreshold * pixelsInBlock);
                
                if (inROI) {
                    if (isChanged) {
                        changeCount++; // Explicit conditional increment per block
                        
                        if (isDbg) {
                            uint8_t base = 0;
                            uint8_t overlay = 0xFF; // Show active changed pixel as bright red color in changeMap image
                            for (size_t y = by; y < blockEndY; ++y) {
                                size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                                for (size_t x = bx; x < blockEndX; ++x) {
                                    size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                                    changeMap[mapIdx]   = base;
                                    changeMap[mapIdx+1] = base;
                                    changeMap[mapIdx+2] = base | overlay;
                                }
                            }
                        }
                    } else if (isDbg) {
                        uint8_t base = (uint8_t)currAvg;
                        uint8_t overlay = 0x00;
                        for (size_t y = by; y < blockEndY; ++y) {
                            size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                            for (size_t x = bx; x < blockEndX; ++x) {
                                size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                                changeMap[mapIdx]   = base;
                                changeMap[mapIdx+1] = base;
                                changeMap[mapIdx+2] = base | overlay;
                            }
                        }
                    }
                } else if (isDbg) {
                    uint8_t mask = isChanged ? 0xFF : 0x00;
                    uint8_t not_mask = ~mask;
                    uint8_t base = (uint8_t)(currAvg & not_mask);
                    uint8_t overlay = 80 & mask; // Show inactive changed pixel as dark red color in changeMap image
                    
                    for (size_t y = by; y < blockEndY; ++y) {
                        size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                        for (size_t x = bx; x < blockEndX; ++x) {
                            size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                            changeMap[mapIdx]   = base;
                            changeMap[mapIdx+1] = base;
                            changeMap[mapIdx+2] = base | overlay;
                        }
                    }
                }
            }
        }
    } else {
        // --- RGB888 PATH --- (24-bit RGB image processing)
        for (size_t by = 0; by < RESIZE_DIM; by += blockSize) {
            size_t blockEndY = (by + blockSize < RESIZE_DIM) ? (by + blockSize) : RESIZE_DIM;
            bool inROI = (by >= startY && by < endY);
            
            for (size_t bx = 0; bx < RESIZE_DIM; bx += blockSize) {
                size_t blockEndX = (bx + blockSize < RESIZE_DIM) ? (bx + blockSize) : RESIZE_DIM;
                
                uint32_t sad = 0; // Sum of Absolute Differences for the block
                uint32_t blockCurrRgbSum = 0;
                size_t pixelsInBlock = 0;
                
                for (size_t y = by; y < blockEndY; ++y) {
                    const uint8_t* currRow = currBuff + y * RESIZE_DIM * RGB888_BYTES;
                    const uint8_t* prevRow = prevBuff + y * RESIZE_DIM * RGB888_BYTES;
                    for (size_t x = bx; x < blockEndX; ++x) {
                        const size_t offset = x * RGB888_BYTES;
                        const uint32_t currSum = static_cast<uint32_t>(currRow[offset]) +
                                                 currRow[offset + 1] + currRow[offset + 2];
                        const uint32_t prevSum = static_cast<uint32_t>(prevRow[offset]) +
                                                 prevRow[offset + 1] + prevRow[offset + 2];
                        const uint16_t currPix = static_cast<uint16_t>((currSum * 21846U) >> 16);
                        const uint16_t prevPix = static_cast<uint16_t>((prevSum * 21846U) >> 16);

                        blockCurrRgbSum += currSum;

                        const int diff = static_cast<int>(currPix) - static_cast<int>(prevPix);
                        sad += static_cast<uint32_t>(diff < 0 ? -diff : diff);
                        ++pixelsInBlock;
                    }
                }
                
                if (pixelsInBlock == 0) continue;
                
                lux += (blockCurrRgbSum * 21846U) >> 16; // Add scaled total block sum to lux
                uint16_t currAvg = (uint16_t)(((blockCurrRgbSum / pixelsInBlock) * 21846U) >> 16);
                
                // Compare SAD against scaled threshold to prevent cancellation
                bool isChanged = (sad > (uint32_t)detectChangeThreshold * pixelsInBlock);
                
                if (inROI) {
                    if (isChanged) {
                        changeCount++; // Explicit conditional increment per block
                        
                        if (isDbg) {
                            uint8_t base = 0;
                            uint8_t overlay = 0xFF; // Show active changed pixel as bright red color in changeMap image
                            for (size_t y = by; y < blockEndY; ++y) {
                                size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                                for (size_t x = bx; x < blockEndX; ++x) {
                                    size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                                    changeMap[mapIdx]   = base;
                                    changeMap[mapIdx+1] = base;
                                    changeMap[mapIdx+2] = base | overlay;
                                }
                            }
                        }
                    } else if (isDbg) {
                        uint8_t base = (uint8_t)currAvg;
                        uint8_t overlay = 0x00;
                        for (size_t y = by; y < blockEndY; ++y) {
                            size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                            for (size_t x = bx; x < blockEndX; ++x) {
                                size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                                changeMap[mapIdx]   = base;
                                changeMap[mapIdx+1] = base;
                                changeMap[mapIdx+2] = base | overlay;
                            }
                        }
                    }
                } else if (isDbg) {
                    uint8_t mask = isChanged ? 0xFF : 0x00;
                    uint8_t not_mask = ~mask;
                    uint8_t base = (uint8_t)(currAvg & not_mask);
                    uint8_t overlay = 80 & mask; // Show inactive changed pixel as dark red color in changeMap image
                    
                    for (size_t y = by; y < blockEndY; ++y) {
                        size_t rowMapIdx = y * RESIZE_DIM * RGB888_BYTES;
                        for (size_t x = bx; x < blockEndX; ++x) {
                            size_t mapIdx = rowMapIdx + (x * RGB888_BYTES);
                            changeMap[mapIdx]   = base;
                            changeMap[mapIdx+1] = base;
                            changeMap[mapIdx+2] = base | overlay;
                        }
                    }
                }
            }
        }
    }

    res.changeCount = changeCount; // Store final change count in result
    
    // Fixed-point light level calculation
    if (RESIZE_DIM_SQ > 0) {
        // Apply multiplier and round to nearest integer via half-ULP addition before shifting
        res.lightLevel =
            (lux * lightMult + (1U << (FP_SHIFT - 1))) >> FP_SHIFT;
    } else {
        res.lightLevel = 0; // Fallback for zero-dimension images
    }
    
    if (dbgMotion && changeMap && motionJpeg != nullptr && !motionJpegLen) {
        uint32_t dTime = millis(); // Start timing for performance logging
#if INCLUDE_NEW_JPG
        if (jpgBuf != nullptr) {
            motionJpegLen = rgb2jpg(changeMap, RESIZE_DIM, RESIZE_DIM, JPEG_QUAL, jpgBuf); // Encode using new JPEG API
            if (motionJpegLen == 0) {
                LOG_WRN("motionDetect: encode() failed");
            } else if (motionJpegLen <= 32 * 1024) {
                memcpy(motionJpeg, jpgBuf, motionJpegLen); // Copy to global buffer if within size limit
            } else {
                LOG_WRN("motionDetect: JPEG too large (%u bytes)", (unsigned)motionJpegLen);
                motionJpegLen = 0; // Reject oversized payloads
            }
        } else {
            LOG_WRN("motionDetect: JPEG workspace unavailable");
        }
#else
            uint8_t* jpg_buf = nullptr;
            size_t changeMapSize = RESIZE_DIM_SQ * RGB888_BYTES;
            // Encode using legacy fmt2jpg API
            if (!fmt2jpg(changeMap, changeMapSize, RESIZE_DIM, RESIZE_DIM, PIXFORMAT_RGB888, JPEG_QUAL, &jpg_buf, &motionJpegLen)) {
                LOG_WRN("motionDetect: fmt2jpg() failed"); 
                motionJpegLen = 0;
            } else if (motionJpeg != nullptr) {
                if (motionJpegLen <= 32 * 1024) {
                    memcpy(motionJpeg, jpg_buf, motionJpegLen); 
                } else {
                    LOG_WRN("motionDetect: JPEG too large (%u bytes)", (unsigned)motionJpegLen);
                    motionJpegLen = 0;
                }
            } else {
                motionJpegLen = 0;
            }
            free(jpg_buf); // Release temporary buffer allocated by fmt2jpg
            jpg_buf = nullptr;
#endif
        if (motionJpegLen > 0) {
            xSemaphoreGive(motionSemaphore); // Signal consumer thread that new debug frame is ready
        }
        LOG_VRB("Created changeMap JPEG %d bytes in %lums", 
                (int)motionJpegLen, 
                (unsigned long)(millis() - dTime)); // Log encoding duration
    }
    
    return res; // Return populated analysis result
}

// Evaluates motion status and light level by comparing current camera frame against previous state.
bool checkMotion(camera_fb_t* fb, bool motionStatus, bool lightLevelOnly) {
    // check difference between current and previous image (subtract background)
    // convert image from JPEG to downscaled RGB888 or 8 bit grayscale bitmap
    static size_t RESIZE_DIM = 96;  // dimensions of resized motion bitmap
    static bool isInitialized = false; // Guards one-time initialization
    if (!isInitialized) {
        if (ESP.getPsramSize() < 3 * ONEMEG) {
            RESIZE_DIM = 64; // otherwise insufficient PSRAM (issue #706)
        }
        isInitialized = true;
    }
    
    size_t RESIZE_DIM_SQ = RESIZE_DIM * RESIZE_DIM; // pixels in bitmap
    
    if (fsizePtr > FRAMESIZE_SXGA) {
        return false; // Abort if frame size index is invalid
    }
    
    uint32_t dTime = millis(); // Start timing for performance logging
    static uint32_t motionCnt = 0; // Consecutive frames with detected motion
    static uint8_t fsizePtrPrev = 255; // Tracks previous frame size to detect resolution changes
    static uint8_t scaling = 0; // JPEG scaling factor
    static uint8_t downsize = 0; // Combined downscaling divisor
    static uint16_t reducer = 0; // JPEG subsampling rate
    static int sampleWidth = 0; // Width of decoded sample
    static int sampleHeight = 0; // Height of decoded sample
    
    static uint8_t* rgbBuf = nullptr; // Persistent buffer for decoded RGB data
    if (rgbBuf == nullptr) {
        rgbBuf = (uint8_t*)heap_caps_aligned_calloc(
            16, 1, 
            frameData[FRAMESIZE_SXGA].frameWidth * frameData[FRAMESIZE_SXGA].frameHeight * RGB888_BYTES / 8, 
            MALLOC_CAP_SPIRAM 
        );
        if (rgbBuf == nullptr) {
            LOG_ERR("Failed to allocate rgbBuf");
            return motionStatus; 
        }
    }
    
#if INCLUDE_NEW_JPG
    static struct esp_jpeg_stream jpegHandle = {0}; 
    static uint8_t* jpgBuf = nullptr; 
#endif  

    // calculate parameters for sample size when resolution changes
    if (fsizePtr != fsizePtrPrev) {
        fsizePtrPrev = fsizePtr;
        scaling = frameData[fsizePtr].scaleFactor; 
        reducer = frameData[fsizePtr].sampleRate; 
        
        downsize = (1 << scaling) * reducer; 
        
        sampleWidth = frameData[fsizePtr].frameWidth / downsize; 
        sampleHeight = frameData[fsizePtr].frameHeight / downsize; 
        
#if INCLUDE_NEW_JPG
        jpg2rgbClose(&jpegHandle); 
        jpgReduce(fb->width, fb->height, downsize, &sampleWidth, &sampleHeight); 
        if (!jpg2rgbOpen(&jpegHandle, sampleWidth, sampleHeight)) {
            return motionStatus; 
        }
#endif
    }

#if INCLUDE_NEW_JPG
    if (!jpg2rgb(&jpegHandle, fb->buf, fb->len, rgbBuf)) {
        return motionStatus; 
    }
#else
    if (!jpg2rgb((uint8_t*)fb->buf, fb->len, rgbBuf, scaling)) {
        return motionStatus; 
    }
#endif

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
    if (colorDepth == GRAYSCALE_BYTES) {
        rgbToGray(rgbBuf, sampleWidth, sampleHeight); 
    }
#endif

    LOG_VRB("JPEG to rescaled %s bitmap conversion %u bytes in %lums", 
            colorDepth == RGB888_BYTES ? "color" : "grayscale", 
            (unsigned)(sampleWidth * sampleHeight * colorDepth), 
            (unsigned long)(millis() - dTime)); 
  
    // allocate buffer space on heap
    static uint8_t* currBuff = nullptr; 
    static uint8_t* prevBuff = nullptr; 
    static uint8_t* changeMap = nullptr; 
    static uint8_t lastColorDepth = 0; 
    static bool lastDbgMotion = false; 
    static bool previousFrameValid = false;
    
    size_t currBuffSize = 0;
    size_t changeMapSize = 0;
    if (!checkedImageBytes(RESIZE_DIM, RESIZE_DIM, colorDepth, currBuffSize) ||
        !checkedImageBytes(RESIZE_DIM, RESIZE_DIM, RGB888_BYTES, changeMapSize)) {
        LOG_ERR("motionDetect: image dimensions are too large");
        return motionStatus;
    }
    
    if (dbgMotion && !lastDbgMotion) {
        if (changeMap == nullptr) {
            changeMap = (uint8_t*)ps_malloc(changeMapSize);
            if (!changeMap) {
                LOG_ERR("Failed to allocate changeMap");
            }
        }
#if INCLUDE_NEW_JPG
        if (changeMap != nullptr && jpgBuf == nullptr) {
            size_t jpgBufSize = (changeMapSize > (32 * 1024)) ? changeMapSize : (32 * 1024);
            jpgBuf = (uint8_t*)ps_malloc(jpgBufSize); 
            if (!jpgBuf) {
                LOG_ERR("Failed to allocate jpgBuf");
                if (changeMap) { free(changeMap); changeMap = nullptr; } 
                lastDbgMotion = dbgMotion; 
                return motionStatus;
            }
        }
#endif
        if (changeMap != nullptr && motionJpeg == nullptr) {
            motionJpeg = (uint8_t*)ps_malloc(32 * 1024); 
            if (motionJpeg == nullptr) {
                LOG_ERR("Failed to allocate motionJpeg");
            }
        }
    } else if (!dbgMotion && lastDbgMotion) {
        if (changeMap) {
            free(changeMap);
            changeMap = nullptr;
        }
#if INCLUDE_NEW_JPG
        if (jpgBuf) {
            free(jpgBuf);
            jpgBuf = nullptr;
        }
#endif
        if (motionJpeg) {
            free(motionJpeg);
            motionJpeg = nullptr;
        }
        motionJpegLen = 0; 
    }
    lastDbgMotion = dbgMotion; 

    bool depthChanged = (colorDepth != lastColorDepth); 
    bool reallocNeeded = (currBuff == nullptr || depthChanged); 
    
    if (reallocNeeded) {
        if (currBuff) free(currBuff); 
        if (prevBuff) free(prevBuff); 
        currBuff = nullptr;
        prevBuff = nullptr;
        
        currBuff = (uint8_t*)ps_malloc(currBuffSize); 
        if (!currBuff) {
            LOG_ERR("Failed to allocate currBuff");
            return motionStatus; 
        }
        
        prevBuff = (uint8_t*)ps_malloc(currBuffSize); 
        if (!prevBuff) {
            LOG_ERR("Failed to allocate prevBuff");
            free(currBuff); 
            currBuff = nullptr;
            return motionStatus;
        }
        
        lastColorDepth = colorDepth; 
    }

    dTime = millis(); 
    rescaleImage(rgbBuf, sampleWidth, sampleHeight, currBuff, RESIZE_DIM, RESIZE_DIM, colorDepth); 
    LOG_VRB("Bitmap rescale to %u bytes in %lums", 
            (unsigned)currBuffSize, 
            (unsigned long)(millis() - dTime)); 
  
    if (!previousFrameValid || depthChanged) {
        memcpy(prevBuff, currBuff, currBuffSize);
        previousFrameValid = true;
    }

    dTime = millis(); 
    
    MotionAnalysisResult analysis = analyzeMotionFrame(
        currBuff, prevBuff, currBuffSize, colorDepth, 
        RESIZE_DIM, RESIZE_DIM_SQ, changeMap
#if INCLUDE_NEW_JPG
        , jpgBuf
#endif
    ); 

    lightLevel = analysis.lightLevel; // light value as a %
    nightTime = isNight(nightSwitch); 
    
    std::swap(currBuff, prevBuff); // save image for next comparison
    
    LOG_VRB("Detected %u changes, threshold %u, light level %u, in %lums", 
            (unsigned)analysis.changeCount, 
            (unsigned)analysis.moveThreshold, 
            (unsigned)lightLevel, 
            (unsigned long)(millis() - dTime)); 
            
    if (lightLevelOnly) {
        return false; // no motion checking, only calc of light level
    }
    
    if (!dbgMotion) {
        // normal motion detection
        dTime = millis(); 
        if (!nightTime && analysis.changeCount > analysis.moveThreshold) {
            LOG_VRB("### Change detected");
            motionCnt++; // number of consecutive changes
            // need minimum sequence of changes to signal valid movement
            if (!motionStatus && motionCnt >= detectMotionFrames) {
                LOG_VRB("***** Motion - START");
                motionStatus = true; // motion started
#if INCLUDE_TINYML
                // pass image to TinyML for classification
                if (mlUse) {
                    if (!tinyMLclassify(RESIZE_DIM)) {
                        motionCnt = 0; // not classified, so cancel motion
                        motionStatus = false; 
                    }
                }
#endif
                dTime = millis(); 
#if INCLUDE_MQTT
                if (mqtt_active && motionCnt) {
                    snprintf(jsonBuff, sizeof(jsonBuff), "{\"MOTION\":\"ON\",\"TIME\":\"%s\"}", esp_log_system_timestamp()); 
                    mqttPublish(jsonBuff); 
                    mqttPublishPath("motion", "on"); 
#if INCLUDE_HASIO
                    mqttPublishPath("cmd", "still"); 
#endif
                }
#endif
            } 
        } else {
            motionCnt = 0; 
        }
  
        if (motionStatus && !motionCnt) {
            // insufficient change or motion not classified
            LOG_VRB("***** Motion - STOP");
            motionStatus = false; // motion stopped
#if INCLUDE_MQTT
            if (mqtt_active) {
                snprintf(jsonBuff, sizeof(jsonBuff), "{\"MOTION\":\"OFF\",\"TIME\":\"%s\"}", esp_log_system_timestamp());
                mqttPublish(jsonBuff);
                mqttPublishPath("motion", "off");
            }
#endif
        } 
        
        if (motionStatus) {
            LOG_VRB("*** Motion - ongoing %lu frames", (unsigned long)motionCnt); 
        }
    }
  
    if (dbgVerbose) {
        checkMemory(); 
    }
    
    LOG_VRB("============================"); 
    
    // motionStatus indicates whether motion previously ongoing or not
    return nightTime ? false : motionStatus; 
}

/*****************************************************************************************************/

#if INCLUDE_NEW_JPG

// Need to have installed espressif__esp_new_jpeg library

static void jpgReduce(int inWidth, int inHeight, uint8_t downsize, int* outWidth, int* outHeight) {
  // downsize then round width and height up to the nearest multiple of 8 while preserving the aspect ratio
  uint8_t roundTo8 = 8; // new width and height must be multiples of 8
  // Calculate the original aspect ratio 
  inWidth /= downsize;
  inHeight /= downsize;
  float aspectRatio = (float)(inWidth) / inHeight;

  auto roundUpToMultiple = [](int n, int m) {
    // round n up to the nearest multiple of m
    return ((n + m - 1) / m) * m;
  };

  // determine larger dimension
  int newLarger = inWidth;
  int newSmaller = inHeight;   
  if (inWidth < inHeight) {
    newLarger = inHeight;
    newSmaller = inWidth;
  }

  // Round the larger dimension up to the nearest multiple of 8.
  newLarger = roundUpToMultiple(inWidth, roundTo8);
  
  // Calculate the new smaller based on the new larger and original aspect ratio, then round up.
  newSmaller = (int)(ceil((float)newLarger / aspectRatio));
  newSmaller = roundUpToMultiple(newSmaller, roundTo8);

  // update the values to return
  *outWidth = newLarger;
  *outHeight = newSmaller;
  if (inWidth < inHeight) {
    *outWidth = newSmaller;
    *outHeight = newLarger;
  }
}

static bool jpg2rgbOpen(esp_jpeg_stream_handle_t jpegHandle, uint16_t width, uint16_t height) {
  // configure jpeg handler
  jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
  config.output_type = JPEG_PIXEL_FORMAT_RGB888;
  config.rotate = JPEG_ROTATE_0D;
  config.scale.width = width;
  config.scale.height = height;
  jpegHandle->output_type = JPEG_PIXEL_FORMAT_RGB888;

  // Create jpeg_dec handle
  jpeg_error_t ret = jpeg_dec_open(&config, &jpegHandle->jpeg_dec);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Unable to create jpeg decoder handle: %d", ret);
    return false;
  }

  // Create io_callback handle
  jpegHandle->jpeg_io = (jpeg_dec_io_t*)calloc(1, sizeof(jpeg_dec_io_t));
  if (jpegHandle->jpeg_io == NULL) {
    LOG_ERR("Insufficient memory to create input handle");
    jpg2rgbClose(jpegHandle);
    return false;
  }

  // Create out_info handle
  jpegHandle->out_info = (jpeg_dec_header_info_t*)calloc(1, sizeof(jpeg_dec_header_info_t));
  if (jpegHandle->out_info == NULL) {
    LOG_ERR("Insufficient memory to create output handle");
    jpg2rgbClose(jpegHandle);
    return false;
  }
  return true;
}

static bool jpg2rgb(esp_jpeg_stream_handle_t jpegHandle, uint8_t* inputBuf, int inputLen, uint8_t* outputBuf) {
  // decode jpeg to rgb888
  // Set input buffer and buffer len to io_callback
  jpegHandle->jpeg_io->inbuf = inputBuf;
  jpegHandle->jpeg_io->inbuf_len = inputLen;

  // Parse jpeg header and get image for decoder
  jpeg_error_t ret = jpeg_dec_parse_header(jpegHandle->jpeg_dec, jpegHandle->jpeg_io, jpegHandle->out_info);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Failed to parse jpeg header: %d", ret);
    return false;
  }

  // decode jpeg into outputBuf
  jpegHandle->jpeg_io->outbuf = outputBuf;
  ret = jpeg_dec_process(jpegHandle->jpeg_dec, jpegHandle->jpeg_io);
  if (ret != JPEG_ERR_OK) {
    LOG_ERR("Failed to decode jpeg: %d", ret);
    return false;
  }
  return true;
}

static bool jpg2rgbClose(esp_jpeg_stream_handle_t jpegHandle) {
   // remove old stream handles when resolution changes
  jpeg_error_t ret = jpeg_dec_close(jpegHandle->jpeg_dec);
  if (jpegHandle->jpeg_io) free(jpegHandle->jpeg_io);
  if (jpegHandle->out_info) free(jpegHandle->out_info);
  return ret == JPEG_ERR_OK;
}

static size_t rgb2jpg(uint8_t* rgb888, int width, int height, int qual, uint8_t* outputBuf) {
  // encode rgb888 to jpeg
  static bool firstCall = true;
  static jpeg_enc_handle_t jpeg_enc = NULL;
  static int bufLen = width * height * RGB888_BYTES;
  jpeg_error_t ret = JPEG_ERR_OK;

  if (firstCall) {
    firstCall = false;
    // configure encoder
    jpeg_enc_config_t jpeg_enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
    jpeg_enc_cfg.width = width;
    jpeg_enc_cfg.height = height;
    jpeg_enc_cfg.src_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
    jpeg_enc_cfg.quality = qual;
    jpeg_enc_cfg.rotate = JPEG_ROTATE_0D;
    jpeg_enc_cfg.task_enable = false;
    jpeg_enc_cfg.hfm_task_priority = 13;
    jpeg_enc_cfg.hfm_task_core = 1;

    // open encoder
    ret = jpeg_enc_open(&jpeg_enc_cfg, &jpeg_enc);
    if (ret != JPEG_ERR_OK) {
      LOG_ERR("Failed to open decoder: %d", ret);
      return 0;
    }
  }

  // encoding
  int jpgLen = 0;
  ret = jpeg_enc_process(jpeg_enc, rgb888, bufLen, outputBuf, bufLen, &jpgLen);
  if (ret != JPEG_ERR_OK) LOG_ERR("Failed to encode: %d", ret);

  //jpeg_enc_close(jpeg_enc); // keep open
  return (size_t)jpgLen;
}

#else

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)

// based on jpg2rgb888() from esp32-camera/to_bmp.c for access to rescaling

static uint8_t work[3100]; // Default size is 3.1kB for JPEG decoder

static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale) {
  esp_jpeg_image_cfg_t jpeg_cfg = {
      .indata = (uint8_t *)src,
      .indata_size = src_len,
      .outbuf = out,
      .outbuf_size = UINT32_MAX, // sic @todo: this is very bold assumption, keeping this like this for now, not to break existing code
      .out_format = JPEG_IMAGE_FORMAT_RGB888,
      .out_scale = (esp_jpeg_image_scale_t)scale,
      .flags = {.swap_color_bytes = 0},
      .advanced = {
        .working_buffer = work,
        .working_buffer_size = sizeof(work)
      }
  };
  esp_jpeg_image_output_t output_img = {};
  esp_err_t res = esp_jpeg_decode(&jpeg_cfg, &output_img);
  if (res != ESP_OK) LOG_WRN("jpg2rgb failure: %s", espErrMsg(res)); 
  return (res == ESP_OK);
}

#else

// for arduino-esp32 versions 3.2.1 or earlier

/************* copied and modified from esp32-camera/to_bmp.c to access jpg_scale_t *****************/

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t data_offset;
  const uint8_t *input;
  uint8_t *output;
} rgb_jpg_decoder;

static bool _rgb_write(void * arg, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *data) {
  // mpjpeg2sd: modified to generate 24 bit RGB or 8 bit grayscale
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (!data){
    if (x == 0 && y == 0) {
      // write start
      jpeg->width = w;
      jpeg->height = h;
    } 
    return true;
  }

  size_t jw = jpeg->width*RGB888_BYTES;
  size_t t = y * jw;
  size_t b = t + (h * jw);
  size_t l = x * RGB888_BYTES;
  uint8_t *out = jpeg->output+jpeg->data_offset;
  uint8_t *o = out;
  size_t iy, ix;
  w *= RGB888_BYTES;
  uint8_t stride = (colorDepth == RGB888_BYTES) ? GRAYSCALE_BYTES : RGB888_BYTES; // stride is inverse of colorDepth
  for (iy=t; iy<b; iy+=jw) {
    o = out+(iy+l)/stride;
    for (ix=0; ix<w; ix+=RGB888_BYTES) {
      if (colorDepth == RGB888_BYTES) {
        o[ix] = data[ix+2];
        o[ix+1] = data[ix+1];
        o[ix+2] = data[ix];
      } else {
        // simple average for grayscale (matching original behaviour)
        o[ix / RGB888_BYTES] = (uint8_t)((data[ix + 2] + data[ix + 1] + data[ix]) / RGB888_BYTES);
      }
    }
    data+=w;
  }
  return true;
}

static unsigned int _jpg_read(void * arg, size_t index, uint8_t *buf, size_t len) {
  rgb_jpg_decoder * jpeg = (rgb_jpg_decoder *)arg;
  if (buf) memcpy(buf, jpeg->input + index, len);
  return len;
}

static bool jpg2rgb(const uint8_t* src, size_t src_len, uint8_t* out, uint8_t scale) {
  rgb_jpg_decoder jpeg;
  jpeg.width = 0;
  jpeg.height = 0;
  jpeg.input = src;
  jpeg.output = out;
  jpeg.data_offset = 0;
  esp_err_t res = esp_jpg_decode(src_len, (jpg_scale_t)scale, _jpg_read, _rgb_write, (void*)&jpeg);
  if (res != ESP_OK) LOG_WRN("jpg2rgb failure: %s", espErrMsg(res)); 
  return (res == ESP_OK);
}

#endif // ESP_ARDUINO_VERSION

#endif // INCLUDE_NEW_JPG

#else 
// dummies
bool isNight(uint8_t nightSwitch) {return false;}

#endif // AUXILIARY

