// definition of camera pins for different boards

#if defined(CAMERA_MODEL_WROVER_KIT)
#define CAM_BOARD "CAMERA_MODEL_WROVER_KIT"
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    21
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27

#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      19
#define Y4_GPIO_NUM      18
#define Y3_GPIO_NUM       5
#define Y2_GPIO_NUM       4
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22

#elif defined(CAMERA_MODEL_ESP_EYE)
#define CAM_BOARD "CAMERA_MODEL_ESP_EYE"
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    4
#define SIOD_GPIO_NUM    18
#define SIOC_GPIO_NUM    23

#define Y9_GPIO_NUM      36
#define Y8_GPIO_NUM      37
#define Y7_GPIO_NUM      38
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      35
#define Y4_GPIO_NUM      14
#define Y3_GPIO_NUM      13
#define Y2_GPIO_NUM      34
#define VSYNC_GPIO_NUM   5
#define HREF_GPIO_NUM    27
#define PCLK_GPIO_NUM    25

#define LED_GPIO_NUM     22

#elif defined(CAMERA_MODEL_M5STACK_PSRAM)
#define CAM_BOARD "CAMERA_MODEL_M5STACK_PSRAM"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#elif defined(CAMERA_MODEL_M5STACK_V2_PSRAM)
#define CAM_BOARD "CAMERA_MODEL_M5STACK_V2_PSRAM"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     22
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#elif defined(CAMERA_MODEL_M5STACK_WIDE)
#define CAM_BOARD "CAMERA_MODEL_M5STACK_WIDE"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     22
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#define LED_GPIO_NUM       2

#elif defined(CAMERA_MODEL_M5STACK_ESP32CAM)
#define CAM_BOARD "CAMERA_MODEL_M5STACK_ESP32CAM"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       17
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#elif defined(CAMERA_MODEL_M5STACK_UNITCAM)
#define CAM_BOARD "CAMERA_MODEL_M5STACK_UNITCAM"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#elif defined(CAMERA_MODEL_M5STACK_CAMS3_UNIT)
// supplied with PY260 camera (MEGA_CCM_PID)
#define USE_PY260 // comment out if not using PY260
#define CAM_BOARD "CAMERA_MODEL_M5STACK_CAMS3_UNIT"
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM 21
#define XCLK_GPIO_NUM  11
#define SIOD_GPIO_NUM  17
#define SIOC_GPIO_NUM  41

#define Y9_GPIO_NUM    13
#define Y8_GPIO_NUM    4
#define Y7_GPIO_NUM    10
#define Y6_GPIO_NUM    5
#define Y5_GPIO_NUM    7
#define Y4_GPIO_NUM    16
#define Y3_GPIO_NUM    15
#define Y2_GPIO_NUM    6
#define VSYNC_GPIO_NUM 42
#define HREF_GPIO_NUM  18
#define PCLK_GPIO_NUM  12

#define LED_GPIO_NUM 14

// SD Pins
#define SD_MMC_CLK 39
#define SD_MMC_CMD 38
#define SD_MMC_D0 40
// Chip select pin is GPIO9, not required for SD_MMC
// Mic Pins
#define I2S_SD 48 // PDM Microphone
#define I2S_WS 47
#define I2S_SCK -1 


#elif defined(CAMERA_MODEL_AI_THINKER) || defined(SIDE_ALARM)
#define CAM_BOARD "CAMERA_MODEL_AI_THINKER"
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// 4 for flash led or 33 for signal led    
#define LED_GPIO_NUM      4

#elif defined(CAMERA_MODEL_TTGO_T_JOURNAL)
#define CAM_BOARD "CAMERA_MODEL_TTGO_T_JOURNAL"
#define PWDN_GPIO_NUM      0
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23

#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       17
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

#elif defined(CAMERA_MODEL_XIAO_ESP32S3)
#define CAM_BOARD "CAMERA_MODEL_XIAO_ESP32S3"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

#define LED_GPIO_NUM 21
// Define SD Pins
#define SD_MMC_CLK 7 
#define SD_MMC_CMD 9
#define SD_MMC_D0 8
// Define Mic Pins
#define I2S_SD 41 // PDM Microphone
#define I2S_WS 42
#define I2S_SCK -1 

#elif defined(CAMERA_MODEL_ESP32_CAM_BOARD)
#define CAM_BOARD "CAMERA_MODEL_ESP32_CAM_BOARD"
// The 18 pin header on the board has Y5 and Y3 swapped
#define USE_BOARD_HEADER 0 
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   33
#define XCLK_GPIO_NUM     4
#define SIOD_GPIO_NUM    18
#define SIOC_GPIO_NUM    23

#define Y9_GPIO_NUM      36
#define Y8_GPIO_NUM      19
#define Y7_GPIO_NUM      21
#define Y6_GPIO_NUM      39
#if USE_BOARD_HEADER
#define Y5_GPIO_NUM      13
#else
#define Y5_GPIO_NUM      35
#endif
#define Y4_GPIO_NUM      14
#if USE_BOARD_HEADER
#define Y3_GPIO_NUM      35
#else
#define Y3_GPIO_NUM      13
#endif
#define Y2_GPIO_NUM      34
#define VSYNC_GPIO_NUM    5
#define HREF_GPIO_NUM    27
#define PCLK_GPIO_NUM    25

#elif defined(CAMERA_MODEL_ESP32S3_CAM_LCD)
#define CAM_BOARD "CAMERA_MODEL_ESP32S3_CAM_LCD"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     40
#define SIOD_GPIO_NUM     17
#define SIOC_GPIO_NUM     18

#define Y9_GPIO_NUM       39
#define Y8_GPIO_NUM       41
#define Y7_GPIO_NUM       42
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       3
#define Y4_GPIO_NUM       14
#define Y3_GPIO_NUM       47
#define Y2_GPIO_NUM       13
#define VSYNC_GPIO_NUM    21
#define HREF_GPIO_NUM     38
#define PCLK_GPIO_NUM     11

#elif defined(CAMERA_MODEL_ESP32S2_CAM_BOARD)
// ESP32S2 Not supported
#define CAM_BOARD "CAMERA_MODEL_ESP32S2_CAM_BOARD unsupported"
// The 18 pin header on the board has Y5 and Y3 swapped
#define USE_BOARD_HEADER 0
#define PWDN_GPIO_NUM     1
#define RESET_GPIO_NUM    2
#define XCLK_GPIO_NUM     42
#define SIOD_GPIO_NUM     41
#define SIOC_GPIO_NUM     18

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       39
#define Y7_GPIO_NUM       40
#define Y6_GPIO_NUM       15
#if USE_BOARD_HEADER
#define Y5_GPIO_NUM       12
#else
#define Y5_GPIO_NUM       13
#endif
#define Y4_GPIO_NUM       5
#if USE_BOARD_HEADER
#define Y3_GPIO_NUM       13
#else
#define Y3_GPIO_NUM       12
#endif
#define Y2_GPIO_NUM       14
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     4
#define PCLK_GPIO_NUM     3

#elif defined(CAMERA_MODEL_ESP32S3_EYE) || defined(CAMERA_MODEL_FREENOVE_ESP32S3_CAM) || defined(CAMERA_MODEL_ESP32_S3_CAM)
#if defined(CAMERA_MODEL_ESP32S3_EYE)
#define CAM_BOARD "CAMERA_MODEL_ESP32S3_EYE"
#elif defined(CAMERA_MODEL_FREENOVE_ESP32S3_CAM)
#define CAM_BOARD "CAMERA_MODEL_FREENOVE_ESP32S3_CAM"
#elif defined(CAMERA_MODEL_ESP32_S3_CAM)
#define CAM_BOARD "CAMERA_MODEL_ESP32_S3_CAM" // AI_THINKER style board
#endif

#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 15
#define SIOD_GPIO_NUM 4
#define SIOC_GPIO_NUM 5

#define Y2_GPIO_NUM 11
#define Y3_GPIO_NUM 9
#define Y4_GPIO_NUM 8
#define Y5_GPIO_NUM 10
#define Y6_GPIO_NUM 12
#define Y7_GPIO_NUM 18
#define Y8_GPIO_NUM 17
#define Y9_GPIO_NUM 16

#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM 7
#define PCLK_GPIO_NUM 13

#if defined(CAMERA_MODEL_FREENOVE_ESP32S3_CAM) || defined(CAMERA_MODEL_ESP32_S3_CAM)
#define USE_WS2812 // Use WS2812 rgb led
#endif
#ifdef USE_WS2812
#define LED_GPIO_NUM 48 // WS2812 rgb led
#else
#define LED_GPIO_NUM 2 // blue signal led    
#endif

// Define SD Pins
#define SD_MMC_CLK 39 
#define SD_MMC_CMD 38
#define SD_MMC_D0 40
#if defined(CAMERA_MODEL_ESP32_S3_CAM)
// uncomment following pins for SD MMC 4 bit mode
//#define SD_MMC_D1 41
//#define SD_MMC_D2 14
//#define SD_MMC_D3 47
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
// Define Mic Pins
#define I2S_SD 2  // I2S Microphone
#define I2S_WS 42
#define I2S_SCK 41
#endif


#elif defined(CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3) || defined(CAMERA_MODEL_DFRobot_Romeo_ESP32S3)
#define CAM_BOARD "CAMERA_MODEL_DFRobot_ESP32S3"
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     45
#define SIOD_GPIO_NUM     1
#define SIOC_GPIO_NUM     2

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       46
#define Y7_GPIO_NUM       8
#define Y6_GPIO_NUM       7
#define Y5_GPIO_NUM       4
#define Y4_GPIO_NUM       41
#define Y3_GPIO_NUM       40
#define Y2_GPIO_NUM       39
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     42
#define PCLK_GPIO_NUM     5

#define LED_GPIO_NUM     21
#if defined(CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3)
#define SD_MMC_CLK -1
#define SD_MMC_CMD -1
#define SD_MMC_D0 -1
#if SD_MMC_CLK == -1
#define NO_SD  // no SD card present
#endif
#endif

#elif defined(CAMERA_MODEL_TTGO_T_CAMERA_PLUS)
#define CAM_BOARD "CAMERA_MODEL_TTGO_T_CAMERA_PLUS"
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    4
#define SIOD_GPIO_NUM    18
#define SIOC_GPIO_NUM    23

#define Y9_GPIO_NUM      36
#define Y8_GPIO_NUM      37
#define Y7_GPIO_NUM      38
#define Y6_GPIO_NUM      39
#define Y5_GPIO_NUM      35
#define Y4_GPIO_NUM      26
#define Y3_GPIO_NUM      13
#define Y2_GPIO_NUM      34
#define VSYNC_GPIO_NUM   5
#define HREF_GPIO_NUM    27
#define PCLK_GPIO_NUM    25

#define LED_GPIO_NUM     -1
// Define SD Pins
#define SD_MMC_CLK 21 // SCLK
#define SD_MMC_CMD 19 // MOSI
#define SD_MMC_D0 22  // MISO

#elif defined(CAMERA_MODEL_NEW_ESPS3_RE1_0)
// aliexpress board with label RE:1.0, uses slow 8MB QSPI PSRAM, only 4MB addressable
#define CAM_BOARD "CAMERA_MODEL_NEW_ESPS3_RE1_0"
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 10
#define SIOD_GPIO_NUM 21
#define SIOC_GPIO_NUM 14

#define Y9_GPIO_NUM 11
#define Y8_GPIO_NUM 9
#define Y7_GPIO_NUM 8
#define Y6_GPIO_NUM 6
#define Y5_GPIO_NUM 4
#define Y4_GPIO_NUM 2
#define Y3_GPIO_NUM 3
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 13
#define HREF_GPIO_NUM 12
#define PCLK_GPIO_NUM 7

#define USE_WS2812 // Use SK6812 rgb led   
#ifdef USE_WS2812
#define LED_GPIO_NUM 33 // SK6812 rgb led
#else
#define LED_GPIO_NUM 34 // green signal led 
#endif
// Define SD Pins
#define SD_MMC_CLK 42
#define SD_MMC_CMD 39
#define SD_MMC_D0 41
// Define Mic Pins
#define I2S_SD 35 // I2S Microphone
#define I2S_WS 37
#define I2S_SCK 36 

#elif defined(CAMERA_MODEL_XENOIONEX)
#define CAM_BOARD "CAMERA_MODEL_XENOIONEX"
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    1 // Can use 
#define SIOD_GPIO_NUM    8 // Can use other i2c SDA pin, set this to -1 | If not using i2c set to 8 or 47
#define SIOC_GPIO_NUM    9 // Can use other i2c SCL pin, set this to -1 | If not using i2c set to 9 or 21

#define Y9_GPIO_NUM      3  //D7
#define Y8_GPIO_NUM      18 //D6
#define Y7_GPIO_NUM      42 //D5
#define Y6_GPIO_NUM      16 //D4
#define Y5_GPIO_NUM      41 //D3
#define Y4_GPIO_NUM      17 //D2
#define Y3_GPIO_NUM      40 //D1
#define Y2_GPIO_NUM      39 //D0
#define VSYNC_GPIO_NUM   45
#define HREF_GPIO_NUM    38
#define PCLK_GPIO_NUM    2

#define SD_MMC_CLK       13
#define SD_MMC_CMD       12
#define SD_MMC_D0        14

// I2S pins
#define I2S_SCK          4  // Serial Clock (SCK) or Bit Clock (BCLK)
#define I2S_WS           5  // Word Select (WS)or Left Right Clcok (LRCLK)
#define I2S_SDI          6  // Serial Data In (Mic)
#define I2S_SDO          7  // Serial Data Out (Amp)
//#define I2S_BCK          3  // Bit Clock (BCLK) !!! Not needed as of Core V3
//#define I2S_LRC          11  // Left Right Clcok (LRCLK) !!! Not needed as of Core V3

#define TRIGGER         15 // TRIGER FROM PIR OR RADAR

#define USE_WS2812
#define LED_GPIO_NUM     48

#elif defined(CAMERA_MODEL_UICPAL_ESP32)
#define CAM_BOARD "CAMERA_MODEL_UICPAL_ESP32"

// Camera
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM    5
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM    21
#define SIOC_GPIO_NUM    22

#define Y9_GPIO_NUM       2
#define Y8_GPIO_NUM      13
#define Y7_GPIO_NUM      12
#define Y6_GPIO_NUM      32
#define Y5_GPIO_NUM      25
#define Y4_GPIO_NUM      27
#define Y3_GPIO_NUM      26
#define Y2_GPIO_NUM      33
#define VSYNC_GPIO_NUM   17
#define HREF_GPIO_NUM    16
#define PCLK_GPIO_NUM    14

// SD Card
#define SD_MMC_CLK       18
#define SD_MMC_CMD       19
#define SD_MMC_D0        23


#elif defined(CAMERA_MODEL_Waveshare_ESP32_S3_ETH)
// Waveshare ESP32-S3-ETH per schematic found here https://files.waveshare.com/wiki/ESP32-S3-ETH/ESP32-S3-ETH-Schematic.pdf
#define CAM_BOARD "CAMERA_MODEL_Waveshare_ESP32_S3_ETH"
#define PWDN_GPIO_NUM    8  // Drives MOSFET's for camera power supplies. 
#define RESET_GPIO_NUM   -1 // 
#define XCLK_GPIO_NUM    3  // Clock
#define SIOD_GPIO_NUM    48 // SIO_DAT
#define SIOC_GPIO_NUM    47 // SIO_CLK

#define Y9_GPIO_NUM      18 // D7
#define Y8_GPIO_NUM      15 // D6
#define Y7_GPIO_NUM      38 // D5
#define Y6_GPIO_NUM      40 // D4
#define Y5_GPIO_NUM      42 // D3
#define Y4_GPIO_NUM      46 // D2
#define Y3_GPIO_NUM      45 // D1
#define Y2_GPIO_NUM      41 // D0
#define VSYNC_GPIO_NUM   1  // Potential for GP16, but that's normally NC
#define HREF_GPIO_NUM    2  //
#define PCLK_GPIO_NUM    39 //

#define USE_WS2812          // This board has a WS2812 RGB LED, so lets define it. 
#define LED_GPIO_NUM     21 // WS2812B rgb led

// Define SD Pins
#define SD_MMC_CLK 7        //
#define SD_MMC_CMD 6        // CMD/DI/MOSI
#define SD_MMC_D0 5         // DAT0/D0/MISO
// Chip select pin is GPIO4, this has 10k pull up, but not required for SD_MMC

// Define Mic Pins (DOES NOT have NATIVE Mic)
#define I2S_SD 34           // I2S Microphone
#define I2S_WS 33
#define I2S_SCK 35          // clock

// Ethernet W5500 (SPI) pins for this board
#define ETH_MOSI 11
#define ETH_MISO 12
#define ETH_SCLK 13
#define ETH_CS   14
#define ETH_RST  9
#define ETH_INT  10


#elif defined(CAMERA_MODEL_DFRobot_ESP32_S3_AI_CAM)
// https://wiki.dfrobot.com/SKU_DFR1154_ESP32_S3_AI_CAM
#define CAM_BOARD "CAMERA_MODEL_DFRobot_ESP32_S3_AI_CAM"
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    5
#define SIOD_GPIO_NUM    8
#define SIOC_GPIO_NUM    9

#define Y9_GPIO_NUM      4
#define Y8_GPIO_NUM      6
#define Y7_GPIO_NUM      7
#define Y6_GPIO_NUM      14
#define Y5_GPIO_NUM      17
#define Y4_GPIO_NUM      21
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM      16
#define VSYNC_GPIO_NUM   1
#define HREF_GPIO_NUM    2
#define PCLK_GPIO_NUM    15

#define LED_GPIO_NUM 3 
// IR pin 47

// Define SD Pins
#define SD_MMC_CLK 12      //
#define SD_MMC_CMD 13      // CMD/DI/MOSI
#define SD_MMC_D0  11      // DAT0/D0/MISO
// Chip select pin is GPIO10, not required for SD_MMC

// Define Mic Pins 
#define I2S_SD 39 // PDM Microphone
#define I2S_WS 38
#define I2S_SCK -1

// Define Amp Pins 
#define I2S_BCLK  45 // I2S amp
#define I2S_LRCLK 46
#define I2S_DIN   42
// Gain pin 41, mode pin 40


#elif defined(AUXILIARY)
#define CAM_BOARD "AUXILIARY"
#define PWDN_GPIO_NUM -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM -1
#define SIOD_GPIO_NUM -1
#define SIOC_GPIO_NUM -1

#define Y9_GPIO_NUM -1
#define Y8_GPIO_NUM -1
#define Y7_GPIO_NUM -1
#define Y6_GPIO_NUM -1
#define Y5_GPIO_NUM -1
#define Y4_GPIO_NUM -1
#define Y3_GPIO_NUM -1
#define Y2_GPIO_NUM -1
#define VSYNC_GPIO_NUM -1
#define HREF_GPIO_NUM -1
#define PCLK_GPIO_NUM -1

#define NO_SD

#else
#error "Camera model not selected"
#endif

 
