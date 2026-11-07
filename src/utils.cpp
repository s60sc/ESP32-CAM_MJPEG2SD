
// General utilities to support:
// - NTP and time
// - base64 / URl encoding
// - device startup & sleep
// - string functions
// - LED handler
//
// s60sc 2021, 2023, 2025, 2026
// some functions based on code contributed by gemi254

#include "appGlobals.h"

size_t alertBufferSize = 0;
size_t maxAlertBuffSize = 32 * 1024;
byte* alertBuffer = NULL; // buffer for telegram / smtp alert image
int wakePin = -1; // if wakeUse is true
int wakeLevel; // if wakeUse is true
bool wakeUse = false; // true to allow app to sleep and wake
char* jsonBuff = NULL;
UBaseType_t STACK_MEM; // allow some task stacks to use psram if available
RTC_DATA_ATTR uint32_t remainingSeconds = 0;
RTC_NOINIT_ATTR char brownoutStatus;
uint32_t deepSleepTimer = 0;
TaskHandle_t statusCheckHandle = NULL;

#define xstr(s) str(s)
#define str(s) #s
const char* storageType = xstr(STORAGE);

static void initBrownout();

/************************** NTP  **************************/

// Needs to be a time zone string from: https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/zones.csv
uint8_t alarmHour = 1;

time_t getEpoch() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec;
}

void dateFormat(char* inBuff, size_t inBuffLen, bool isFolder) {
  // construct filename from date/time
  time_t currEpoch = getEpoch();
  if (isFolder) strftime(inBuff, inBuffLen, "/%Y%m%d", localtime(&currEpoch));
  else strftime(inBuff, inBuffLen, "/%Y%m%d/%Y%m%d_%H%M%S", localtime(&currEpoch));
}

static void showLocalTime(const char* timeSrc) {
  time_t currEpoch = getEpoch();
  char timeFormat[20];
  strftime(timeFormat, sizeof(timeFormat), "%d/%m/%Y %H:%M:%S", localtime(&currEpoch));
  LOG_INF("Got current time from %s: %s with tz: %s", timeSrc, timeFormat, timezone);
}

void syncToBrowser(uint32_t browserUTC) {
  // Synchronize to browser clock if out of sync
  if (!timeSynchronized) {
    struct timeval tv;
    tv.tv_sec = browserUTC;
    settimeofday(&tv, NULL);
    setenv("TZ", timezone, 1);
    tzset();
    showLocalTime("browser");
  }
}

void formatElapsedTime(char* timeStr, uint32_t timeVal, bool noDays) {
  // elapsed time that app has been running
  uint32_t secs = timeVal / 1000; //convert milliseconds to seconds
  uint32_t mins = secs / 60; //convert seconds to minutes
  uint32_t hours = mins / 60; //convert minutes to hours
  uint32_t days = hours / 24; //convert hours to days
  secs = secs - (mins * 60); //subtract the converted seconds to minutes in order to display 59 secs max
  mins = mins - (hours * 60); //subtract the converted minutes to hours in order to display 59 minutes max
  hours = hours - (days * 24); //subtract the converted hours to days in order to display 23 hours max
  if (noDays) sprintf(timeStr, "%02lu:%02lu:%02lu", hours, mins, secs);
  else sprintf(timeStr, "%lu-%02lu:%02lu:%02lu", days, hours, mins, secs);
}

static time_t setAlarm(uint8_t alarmHour) {
  // calculate future alarm datetime based on current datetime
  // ensure relevant timezone identified (default GMT0)
  time_t currEpoch = getEpoch();
  struct tm* timeinfo = localtime(&currEpoch);
  // set alarm date & time for next given hour
  int nextDay = 0; // try same day then next day
  do {
    timeinfo->tm_mday += nextDay;
    timeinfo->tm_hour = alarmHour;
    timeinfo->tm_min = 0;
    timeinfo->tm_sec = 0;
    nextDay = 1;
  } while (mktime(timeinfo) < currEpoch);
  char inBuff[30];
  strftime(inBuff, sizeof(inBuff), "%d/%m/%Y %H:%M:%S", timeinfo);
  LOG_INF("Alarm scheduled at %s", inBuff);
  // return future alarm time as epoch seconds
  return mktime(timeinfo);
}

bool checkAlarm() {
  // call from appPing() to check if daily alarm time at given hour has occurred
  static time_t rolloverEpoch = 0;
  if (timeSynchronized && getEpoch() >= rolloverEpoch) {
    // alarm time reached, unless first call
    bool notInit = (rolloverEpoch == 0) ? false : true;
    rolloverEpoch = setAlarm(alarmHour); // set next alarm time
    return notInit;
  }
  return false;
}

/****************** weekly scheduled restart ******************/
// restart c. 02:00 every Tuesday (local time per 'timezone' setting), to clear any
// long term memory fragmentation. If NTP time is never obtained, falls back to a
// restart 7 days after boot, based on millis() uptime.

#define RESTART_WDAY 2 // struct tm tm_wday: 0=Sun, 1=Mon, 2=Tue, ...
#define RESTART_HOUR 2 // 02:00
#define MIN_RESTART_GAP_SECS (24UL * 60 * 60) // ignore a scheduled restart if the last one was more recent than this
#define FALLBACK_UPTIME_MS (7UL * 24 * 60 * 60 * 1000UL) // 7 days, compared against millis() (wraps safely after c. 49.7 days)

// last scheduled restart time (epoch secs), guarding against a runaway restart loop caused
// eg by a miscalculated next-restart time. RTC_DATA_ATTR is reinitialised on ESP.restart()
// (it only survives deep sleep wake), so RTC_NOINIT_ATTR + a magic number validity flag is
// used instead, matching the messageLog/crashLoop pattern in utilsLog.cpp - this does mean it
// contains random data on a genuine power up, which lastScheduledRestartValid guards against
RTC_NOINIT_ATTR time_t lastScheduledRestartEpoch;
RTC_NOINIT_ATTR uint32_t lastScheduledRestartValid;

static time_t nextWeeklyRestartEpoch(time_t fromEpoch) {
  // next occurrence of RESTART_WDAY at RESTART_HOUR:00:00, strictly after fromEpoch
  struct tm timeinfo = *localtime(&fromEpoch);
  int daysAhead = (RESTART_WDAY - timeinfo.tm_wday + 7) % 7;
  timeinfo.tm_mday += daysAhead;
  timeinfo.tm_hour = RESTART_HOUR;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;
  timeinfo.tm_isdst = -1;
  time_t nextEpoch = mktime(&timeinfo);
  if (nextEpoch <= fromEpoch) nextEpoch += 7UL * 24 * 60 * 60; // today is restart day but time already passed
  return nextEpoch;
}

static void checkScheduledRestart() {
  // call regularly (from statusCheckTask()) to trigger the weekly restart, or the NTP-unavailable fallback
  static time_t nextRestartEpoch = 0;
  static bool fallbackArmed = true;

  if (timeSynchronized) {
    time_t nowEpoch = getEpoch();
    if (!nextRestartEpoch) {
      nextRestartEpoch = nextWeeklyRestartEpoch(nowEpoch);
      char inBuff[30];
      strftime(inBuff, sizeof(inBuff), "%d/%m/%Y %H:%M:%S", localtime(&nextRestartEpoch));
      LOG_INF("Next scheduled weekly restart due: %s", inBuff);
    }
    if (nowEpoch >= nextRestartEpoch) {
      bool noPriorRestart = (lastScheduledRestartValid != MAGIC_NUM);
      if (noPriorRestart || (nowEpoch - lastScheduledRestartEpoch) >= (time_t)MIN_RESTART_GAP_SECS) {
        lastScheduledRestartEpoch = nowEpoch;
        lastScheduledRestartValid = MAGIC_NUM;
        LOG_ALT("Weekly scheduled restart due (Tuesday %02u:00 %s)", RESTART_HOUR, timezone);
        doRestart("weekly scheduled restart");
      } else {
        LOG_WRN("Weekly scheduled restart due, but skipped as last restart was under 24 hrs ago");
        nextRestartEpoch = nextWeeklyRestartEpoch(nowEpoch); // reschedule so this isn't retried every cycle
      }
    }
  } else if (fallbackArmed && millis() >= FALLBACK_UPTIME_MS) {
    // NTP time never obtained - fallback to uptime based restart
    fallbackArmed = false; // guard re-entry until actual reboot occurs
    LOG_ALT("Weekly scheduled restart fallback: 7 days uptime reached without NTP sync");
    doRestart("uptime fallback restart (no NTP sync)");
  }
}

/********************** misc functions ************************/

bool changeExtension(char* fileName, const char* newExt) {
  // replace original file extension with supplied extension (buffer must be large enough)
  size_t inNamePtr = strlen(fileName);
  // find '.' before extension text
  while (inNamePtr > 0 && fileName[inNamePtr] != '.') inNamePtr--;
  inNamePtr++;
  size_t extLen = strlen(newExt);
  memcpy(fileName + inNamePtr, newExt, extLen);
  fileName[inNamePtr + extLen] = 0;
  return (inNamePtr > 1) ? true : false;
}

bool calcProgress(int progressVal, int totalVal, int percentReport, uint8_t &pcProgress) {
  // calculate percentage progress, only report back on percentReport boundary
  uint8_t percentage = (progressVal * 100) / totalVal;
  if (percentage >= pcProgress + percentReport) {
    pcProgress = percentage;
    return true;
  } else return false;
}

bool urlEncode(const char* inVal, char* encoded, size_t maxSize) {
  int encodedLen = 0;
  char hexTable[] = "0123456789ABCDEF";
  while (*inVal) {
    if (isalnum(*inVal) || strchr("$-_.+!*'(),:@~#", *inVal)) {
      *encoded++ = *inVal;
      encodedLen++;
    } else {
      encodedLen += 3; 
      *encoded++ = '%';
      *encoded++ = hexTable[(*inVal) >> 4];
      *encoded++ = hexTable[*inVal & 0xf];
    }
    if (encodedLen >= maxSize) return false;  // Buffer overflow
    inVal++;
  }
  *encoded = 0;
  return true;
}

void urlDecode(char* inVal) {
  // Optimized: Precomputed constant-time lookup table (LUT) to eliminate generic bitwise
  // and arithmetic operations in the hot loop, reducing CPU overhead during hex decoding.
  static const uint8_t hexDecodeLUT[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
      0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 10, 11, 12, 13, 14, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };

  // replace url encoded characters in-place
  char* readPtr = inVal;
  char* writePtr = inVal;
  while (*readPtr) {
    if (*readPtr == '%' && isxdigit((unsigned char)readPtr[1]) && isxdigit((unsigned char)readPtr[2])) {
      char h1 = readPtr[1];
      char h2 = readPtr[2];
      int v1 = hexDecodeLUT[(unsigned char)h1];
      int v2 = hexDecodeLUT[(unsigned char)h2];
      *writePtr++ = (char)((v1 << 4) | v2);
      readPtr += 3;
    } else if (*readPtr == '+') {
      // + is encoded space in form data
      *writePtr++ = ' ';
      readPtr++;
    } else {
      *writePtr++ = *readPtr++;
    }
  }
  *writePtr = 0;
}

void listBuff (const uint8_t* b, size_t len) {
  // output buffer content as hex, 16 bytes per line
  if (!len || !b) LOG_WRN("Nothing to print");
  else {
    for (size_t i = 0; i < len; i += 16) {
      int linelen = (len - i) < 16 ? (len - i) : 16;
      for (size_t k = 0; k < linelen; k++) LOG_RAW(" %02x", b[i+k]);
      puts(" ");
    }
  }
}

size_t isSubArray(uint8_t* haystack, uint8_t* needle, size_t hSize, size_t nSize) {
  // find a subarray (needle) in another array (haystack)
  size_t h = 0, n = 0; // Two pointers to traverse the arrays
  // Traverse both arrays simultaneously
  while (h < hSize && n < nSize) {
    // If element matches, increment both pointers
    if (haystack[h] == needle[n]) {
      h++;
      n++;
      // If needle is completely traversed
      if (n == nSize) return h; // position of end of needle
    } else {
      // if not, increment h and reset n
      h = h - n + 1;
      n = 0;
    }
  }
  return 0; // not found
}

void removeChar(char* s, char c) {
  // remove specified character from string
  int writer = 0, reader = 0;
  while (s[reader]) {
    if (s[reader] != c) s[writer++] = s[reader];
    reader++;       
  }
  s[writer] = 0;
}

void replaceChar(char* s, char c, char r) {
  // replace specified character in string
  int reader = 0;
  while (s[reader]) {
    if (s[reader] == c) s[reader] = r;
    reader++;
  }
}

char* fmtSize (uint64_t sizeVal) {
  // format size according to magnitude
   // rotating buffer pool to support up to 4 calls per format string
  static char returnStr[4][24];
  static uint8_t idx = 0;
  char* buf = returnStr[idx++ & 3];
  if (sizeVal < 50 * 1024) snprintf(buf, sizeof(returnStr[0]), "%llu bytes", (unsigned long long)sizeVal);
  else if (sizeVal < ONEMEG) snprintf(buf, sizeof(returnStr[0]), "%lluKB", (unsigned long long)(sizeVal / 1024));
  else if (sizeVal < ONEMEG * 1024) snprintf(buf, sizeof(returnStr[0]), "%0.1fMB", (double)(sizeVal) / ONEMEG);
  else snprintf(buf, sizeof(returnStr[0]), "%0.1fGB", (double)(sizeVal) / (ONEMEG * 1024));
  return buf;
}

char* trim(char* str) {
  // trim whitespace around string
    char* start = str;
    char* end;
    // Trim leading whitespace
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start == '\0') {
        *str = '\0';
        return str;
    }
    // Trim trailing whitespace
    end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    // Shift string back to original buffer if needed
    if (start != str) memmove(str, start, end - start + 2);
    return str;
}

char* toCase(char *s, bool toLower) {
  // convert supplied string to lower or upper case
  for (char *p = s; *p; ++p) *p = toLower ? (char)tolower((unsigned char)*p) : (char)toupper((unsigned char)*p);
  return s;
}

/********************** analog functions ************************/

uint16_t smoothAnalog(int analogPin, int samples) {
  // get averaged analog pin value 
  uint32_t level = 0; 
  if (analogPin > 0) {
    for (int j = 0; j < samples; j++) level += analogRead(analogPin); 
    level /= samples;
  }
  return level;
}

void setupADC() {
  analogSetAttenuation(ADC_ATTEN);
  analogReadResolution(ADC_BITS);
}

float smoothSensor(float latestVal, float smoothedVal, float alpha) {
  // simple Exponential Moving Average filter 
  // where alpha between 0.0 (max smooth) and 1.0 (no smooth)
  return (latestVal * alpha) + smoothedVal * (1.0f - alpha);
}

// onboard chip temperature sensor
#if CONFIG_IDF_TARGET_ESP32
extern "C" {
// Use internal on-chip temperature sensor (if present)
uint8_t temprature_sens_read(); // sic
}
#elif CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S2
#include "driver/temperature_sensor.h"
static temperature_sensor_handle_t temp_sensor = NULL;
#endif

static void prepInternalTemp() {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S2
  // setup internal sensor
  temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
  temperature_sensor_install(&temp_sensor_config, &temp_sensor);
  temperature_sensor_enable(temp_sensor);
#endif
}

float readInternalTemp() {
  float intTemp = NULL_TEMP;
#if CONFIG_IDF_TARGET_ESP32
  // convert on chip raw temperature in F to Celsius degrees
  intTemp = (temprature_sens_read() - 32) / 1.8f;  // value of 55 means not present
#elif CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S3
    temperature_sensor_get_celsius(temp_sensor, &intTemp); 
#endif
  return intTemp;
}


/****************** base 64 ******************/

#define BASE64 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

const uint8_t* encode64chunk(const uint8_t* inp, int rem) {
  // receive 3 byte input buffer and return 4 byte base64 buffer
  rem = 3 - rem; // last chunk may be less than 3 bytes 
  uint32_t buff = 0; // hold 3 bytes as shifted 24 bits
  static uint8_t b64[4];
  // shift input into buffer
  for (int i = 0; i < 3 - rem; i++) buff |= inp[i] << (8*(2-i)); 
  // shift 6 bit output from buffer and encode
  for (int i = 0; i < 4 - rem; i++) b64[i] = BASE64[buff >> (6*(3-i)) & 0x3F]; 
  // filler for last chunk if less than 3 bytes
  for (int i = 0; i < rem; i++) b64[3-i] = '='; 
  return b64;
}

const char* encode64(const char* inp) {
  // helper to base64 encode strings up to 90 chars long
  static char encoded[121]; // space for 4/3 expansion + terminator
  encoded[0] = 0;
  int len = strlen(inp);
  if (len > 90) {
    LOG_WRN("Input string too long: %u chars", len);
    len = 90;
  }
  int outLen = 0;
  for (int i = 0; i < len; i += 3) {
    memcpy(encoded + outLen, encode64chunk((uint8_t*)inp + i, min(len - i, 3)), 4);
    outLen += 4;
  }
  encoded[outLen] = 0;  
  return encoded;
}


/********************* LED Lamp Driver **********************/

// Single-colour (analogWrite), WS2812 / SK6812 (rgbLedWrite), 3-pin RGB (ledcWrite)
// DevStatus -> colour + blink, global perceptual brightness,
// Use the widest-separated colors (Red, Yellow, Green, Cyan, Blue, Magenta) for important states
// For single color LED the averages are Red/Green/Blue 85, Orange 98, Yellow 131, Lime 122, Cyan 170, Magenta 138, White 255.
//
// s60sc 2026

int lampPinG = -1; // RGB3Pin only (green)
int lampPinB = -1; // RGB3Pin only (blue)
int lampPin; // pin for single color LED, WS2812, 3 color RGB red
uint8_t lampLevel; // brightness of on board lamp led 
bool lampAuto = false; // if true in conjunction with pirUse and accelUse, switch on lamp when PIR or accelerometer activated at night
bool lampNight; // if true, lamp comes on at night (not used)
int lampMode; // how lamp is used
int lampType; // type of lamp LED 
int lampActiveHi; // whether lamp lit when pulled Hi (true) or Lo (false)

// Multiples of 2 from Rapid blink duration
enum class Blink : uint8_t { Solid, Slow, Medium, Fast, Rapid, Count };
enum class Color : uint8_t {
  Red, Orange, Yellow, Lime, Green, Teal, Cyan, 
  Azure, Blue, Violet, Magenta, White, Count
};

// Data types
struct Rgb { uint8_t r, g, b; };

struct LedStyle {
  Color color;
  Blink blink;
};

TaskHandle_t ledHandle = NULL;
static Brightness thisBright = (Brightness)lampLevel;
static LedType thisLedType = (LedType)lampType;
static DevStatus thisStatus = DevStatus::STARTING; // initial state 
static const uint32_t pwmFreqHz = 5000; // used by RGB3Pin and Mono
static constexpr uint32_t kTickMs = 125; // duration of rapid blink in ms
static bool lampInit = false;

// Predeclared tables 
constexpr Rgb kPalette[] = {
  // must be in same order as enum Color
    /* Red     */ {255,   0,   0},
    /* Orange  */ {255,  40,   0},
    /* Yellow  */ {255, 140,   0},
    /* Lime    */ {110, 255,   0},
    /* Green   */ {  0, 255,   0},
    /* Teal    */ {  0, 255,  80},
    /* Cyan    */ {  0, 255, 255},
    /* Azure   */ {  0, 100, 255},
    /* Blue    */ {  0,   0, 255},
    /* Violet  */ {120,   0, 255},
    /* Magenta */ {255,   0, 160},
    /* White   */ {255, 255, 255},
};
static_assert(std::size(kPalette) == static_cast<size_t>(Color::Count), "kPalette must have one entry per Color");

constexpr LedStyle kStyles[] = {
  // DevStatus values applied in this order
  // first entry should always be solid white if different states not used
  {Color::White,  Blink::Solid},
  {Color::Blue,   Blink::Medium},
  {Color::Red,    Blink::Fast},
  {Color::Green,  Blink::Solid},
  {Color::Yellow, Blink::Slow},
  {Color::Magenta, Blink::Rapid},
  {Color::Cyan,   Blink::Medium},
};
static_assert(std::size(kStyles) > static_cast<size_t>(DevStatus::Count), "LedStyle must have an entry in order of each devStatus");

// Perceived % -> scale, gamma 2.2: round(255 * p^2.2)
constexpr uint8_t kBrightnessScale[] = { 0, 12, 55, 135, 255 };   // Off/Low/Medium/High/Full
static_assert(std::size(kBrightnessScale) == static_cast<size_t>(Brightness::Count),
    "kBrightnessScale must have one entry per Brightness");

constexpr uint8_t kBlinkToggleTicks[] = {
  0,   // Solid
  8,   // Slow   (toggle every 1000 ms)
  4,   // Medium (toggle every 500 ms)
  2,   // Fast   (toggle every 250 ms)
  1    // Rapid  (toggle every 125 ms)
};
static_assert(std::size(kBlinkToggleTicks) == static_cast<size_t>(Blink::Count), "kBlinkToggleTicks must have one entry per Blink");

// Lookup helpers 
constexpr const LedStyle& styleFor(DevStatus s) { return kStyles[static_cast<size_t>(s)]; }
constexpr Rgb rgbOf(Color c)                 { return kPalette[static_cast<size_t>(c)]; }

constexpr uint8_t scaleChannel(uint8_t ch, uint8_t s) {
  return static_cast<uint8_t>((ch * s + 127) / 255);
}

constexpr Rgb render(Color c, Brightness b) {
  const Rgb     p = rgbOf(c);
  const uint8_t s = kBrightnessScale[static_cast<size_t>(b)];
  return { scaleChannel(p.r, s), scaleChannel(p.g, s), scaleChannel(p.b, s) };
}

// Single-colour LED: average of the (already brightness-scaled) channels
constexpr uint8_t monoLevel(const Rgb& c) {
  return static_cast<uint8_t>((c.r + c.g + c.b) / 3);
}

static_assert(monoLevel({255, 255, 255}) == 255, "White must map to full level");

// Shared between API callers and ledTask (guarded by m_mux)
portMUX_TYPE m_mux  = portMUX_INITIALIZER_UNLOCKED;
Rgb          m_onColor     = {0, 0, 0};
uint8_t      m_toggleTicks = 0;  // 0 = solid
uint8_t      m_tickCount   = 0;
bool         m_phaseOn     = true;
bool         m_change      = false;   // change, redraw on next tick

// Output stage: called from ledTask (or ledInit before the task starts)
void writeLed(const Rgb& c) {
  switch (thisLedType) {
    case LedType::WS2812:
      rgbLedWrite(lampPin, c.r, c.g, c.b);
      break;
    case LedType::RGB3Pin: {
      const uint8_t r = lampActiveHi ? 255 - c.r : c.r;
      const uint8_t g = lampActiveHi ? 255 - c.g : c.g;
      const uint8_t b = lampActiveHi ? 255 - c.b : c.b;
      ledcWrite(lampPin, r);
      ledcWrite(lampPinG, g);
      ledcWrite(lampPinB, b);
      break;
    }
    case LedType::Mono: 
    default: {
      const uint8_t v = monoLevel(c);
      analogWrite(lampPin, lampActiveHi ? 255 - v : v);
      break;
    }
  }
}

void ledTask(void*) {
  TickType_t lastWake = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(kTickMs);

  while (true) {
    vTaskDelayUntil(&lastWake, period);
    bool doWrite = false;
    Rgb  out;

    portENTER_CRITICAL(&m_mux);
    if (m_change) {  // new status or brightness
      m_change = false;
      doWrite = true;
    } else if (m_toggleTicks != 0 && ++m_tickCount >= m_toggleTicks) {
      m_tickCount = 0;
      m_phaseOn   = !m_phaseOn;
      doWrite     = true;
    }
    out = m_phaseOn ? m_onColor : Rgb{0, 0, 0};
    portEXIT_CRITICAL(&m_mux);

    if (doWrite) writeLed(out);
  }
}

// Recompute task data from status + brightness; restart blink phase "on"
void applyState() {
  const LedStyle& st = styleFor(thisStatus);
  const Rgb     on = render(st.color, thisBright);
  const uint8_t tk = kBlinkToggleTicks[static_cast<size_t>(st.blink)];

  portENTER_CRITICAL(&m_mux);
  m_onColor     = on;
  m_toggleTicks = tk;
  m_tickCount   = 0;
  m_phaseOn     = true;
  m_change       = true;
  portEXIT_CRITICAL(&m_mux);
}

static void setupLamp() {
  // setup lamp LED according to type
  if (!lampInit) {
    char val[10];
#ifdef LED_GPIO_NUM
    lampPin = LED_GPIO_NUM;
    itoa(lampPin, val, 10);
    updateConfigVect("lampPin", val);
#endif
#ifdef USE_WS2812
    lampType = (int)LedType::WS2812;
#else
    lampType = (int)LedType::Mono;
#endif
    itoa(lampType, val, 10);
    updateConfigVect("lampType", val);
    thisLedType = (LedType)lampType;
    char lampStr[10];
    if (lampPin) {
      // hardware setup
      switch (thisLedType) {
        case LedType::RGB3Pin:
          ledcAttach(lampPin, pwmFreqHz, 8);
          ledcAttach(lampPinG, pwmFreqHz, 8);
          ledcAttach(lampPinB, pwmFreqHz, 8);
          strcpy(lampStr, "RGB3Pin");
          break;
        case LedType::WS2812:
          // rgbLedWrite() sets up RMT on first call
          strcpy(lampStr, "WS2812");
          break;
        case LedType::Mono:
        default:
          analogWriteResolution(lampPin, 8);  // 8 bit
          analogWriteFrequency(lampPin, pwmFreqHz);
          strcpy(lampStr, "Mono");
          break;
      }
      applyState();
      m_change = false;
      writeLed(m_onColor);  // initial state
      if (ledHandle == NULL) xTaskCreateWithCaps(&ledTask, "ledTask", LED_STACK_SIZE, NULL, LED_PRI, &ledHandle, STACK_MEM);
      lampInit = true;
      lampLevel > 0 ? setLamp(lampLevel) : setLamp((uint8_t)Brightness::Medium);
 #ifdef LED_GPIO_NUM
      updateStatus("save", "0", true);
 #endif
      LOG_INF("%s LED available on pin %d", lampStr, lampPin);
    }
  } 
}

// Set device status: colour and blink rate come from the predeclared table.
static void updateStatusLed(DevStatus s) {
  thisStatus = s;
  applyState();
}

// Set overall brightness (perceptually scaled)
static void setLedBrightness(Brightness b) {
  thisBright = b;
  applyState();
}

void setLamp(uint8_t lampVal) {
  setLamp(DevStatus::READY, (Brightness)lampVal);
}

void setLamp(DevStatus s, uint8_t lampVal) {
  setLamp(s, (Brightness)lampVal);
}

void setLamp(DevStatus s, Brightness b) {
  // control lamp status
  if (lampPin) {
    if (!lampInit) setupLamp();
    if (lampInit) {
      if (b == Brightness::Off && !appSetupDone) b = Brightness::Medium; // show lamp status during startup if off      
      updateStatusLed(s);
      setLedBrightness(b);
    }
  }
}

void twinkleLed(uint8_t ledPin, uint16_t interval, uint8_t blinks) {
  // twinkle led, for given number of blinks, 
  //  with given interval in ms between blinks
  bool ledState = true;
  for (int i=0; i<blinks*2; i++) {
    digitalWrite(ledPin, ledState);
    delay(interval);
    ledState = !ledState;
  }
}

/****************** startup, send device to sleep (light or deep), restart ******************/

#include <esp_wifi.h>

void doRestart(const char* restartStr) {
  LOG_ALT("Controlled restart: %s", restartStr);
#ifdef ISCAM
  appShutdown();
#endif
#if INCLUDE_MQTT
  if (mqtt_active) stopMqttClient();
#endif  
  resetCrashLoop();
  flush_log(true); // SD
  logFlush(500); // monitor
  delay(1000);
  ESP.restart();
}

static void sleepTimer(bool startCycle) {
  // go into deep sleep for defined time
  // maximum sleep time is c. 2 hours, so multiple sleep cycles needed for long durations
  if (startCycle) remainingSeconds = deepSleepTimer;
  if (remainingSeconds > 0) {
    uint32_t sleepNow = min(remainingSeconds, (uint32_t)3600); // use 1 hour (3600 secs)  as sleep cycle duration
    remainingSeconds -= sleepNow;
    esp_sleep_enable_timer_wakeup((uint64_t)sleepNow * 1000000ULL); // in micro secs
    esp_deep_sleep_start();
  }
}

void goToSleep(bool deepSleep) {
#if !CONFIG_IDF_TARGET_ESP32C3
  // if deep sleep, restarts with reset
  // if light sleep, restarts by continuing this function
  LOG_INF("Going into %s sleep", deepSleep ? "deep" : "light");
  delay(100);
  if (deepSleep) { 
    if (wakePin >= 0) {
      // wakeup on pin low (0) or high (1)
      // needs to be RTC pin and support input pullup/down
      // Boot button can be used as manual wake pin
      // To use LDR or TDR on wake pin, connect it between pin and 3V3
      // uses internal pulldown resistor as voltage divider
      // but may need to add external pull down between pin
      // and GND to alter required level for wakeup
      pinMode(wakePin, wakeLevel == 0 ? INPUT_PULLUP : INPUT_PULLDOWN);
      esp_sleep_enable_ext0_wakeup((gpio_num_t)wakePin, wakeLevel);
      esp_deep_sleep_start();
    } else if (deepSleepTimer) sleepTimer(true); // use timer
    // no action if no wake pin or timer
  } else {
    // light sleep
    esp_wifi_stop();
    // wakeup on selected pin
    if (wakePin >= 0) gpio_wakeup_enable((gpio_num_t)wakePin, wakeLevel == 0 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL); 
    esp_light_sleep_start();
  }
  // light sleep restarts here
  LOG_INF("Light sleep wakeup");
  if (netMode != 1) esp_wifi_start();
#else
  LOG_WRN("This function not compatible with ESP32-C3");
#endif
}

static void statusCheckTask(void* parameter) {
  while (true) {
    // regular status checks
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!dataFilesChecked) checkDataFiles();
    if (appSetupDone) doAppPing(timeSynchronized);
    checkScheduledRestart();
#if INCLUDE_MQTT
    if (mqtt_active) startMqttClient();
#endif
  }
}

void runStatusCheck() {
  if (statusCheckHandle) xTaskNotifyGive(statusCheckHandle);
}

bool checkAppReady() {
  if (!DBG_ON) esp_log_level_set("*", ESP_LOG_NONE); // suppress ESP_LOG_ERROR messages
  appSetupDone = startupFailure[0] ? false : true;
  if (startupFailure[0]) LOG_WRN("%s", startupFailure);
  appSetupDone ? setLamp(DevStatus::READY, lampLevel) : setLamp(DevStatus::FAILURE, Brightness::Low);
  return appSetupDone;
}

bool utilsStartup() {
  bool res = false;
  initBrownout();
  sleepTimer(false);
#if CONFIG_IDF_TARGET_ESP32S3
  STACK_MEM = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
#else
  // Original ESP32 must use internal memory for stacks
  STACK_MEM = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
#endif
  logSetup();
#ifdef NEED_PSRAM
  if (psramFound()) {
    if (ESP.getPsramSize() < MIN_PSRAM * ONEMEG) 
      snprintf(startupFailure, SF_LEN, STARTUP_FAIL "App needs at least %dMB PSRAM", MIN_PSRAM);
  } else snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Need PSRAM to be enabled");
#endif

  prepInternalTemp();
  if (jsonBuff == NULL) jsonBuff = psramFound() ? (char*)ps_malloc(JSON_BUFF_LEN) : (char*)malloc(JSON_BUFF_LEN);
  LOG_INF("Compiled with arduino-esp32 v%s", ESP_ARDUINO_VERSION_STR);
  // prep storage
  res = startStorage();
  // Load saved user configuration
  if (res) res = loadConfig();
  setupLamp();
  setLamp(DevStatus::STARTING, lampLevel);
#ifdef DEV_ONLY
  devSetup();
#endif
  if (statusCheckHandle == NULL) {
    if (!strcmp(storageType, "SD_MMC")) xTaskCreateWithCaps(&statusCheckTask, "statusCheckTask", STATUS_STACK_SIZE, NULL, STATUS_PRI, &statusCheckHandle, STACK_MEM);
    else xTaskCreate(&statusCheckTask, "statusCheckTask", STATUS_STACK_SIZE, NULL, STATUS_PRI, &statusCheckHandle); // cant use PSRAM as SPI conflict with flash storage
  } 
  return res;
}

// catch software resets due to brownouts
//https://github.com/espressif/esp-idf/blob/master/components/esp_system/port/brownout.c

#include "esp_private/system_internal.h"
#include "esp_private/rtc_ctrl.h"
#include "hal/brownout_ll.h"

#include "soc/rtc_periph.h"
#include "hal/brownout_hal.h"

#define BROWNOUT_DET_LVL 7

IRAM_ATTR static void notifyBrownout(void *arg) {
  esp_cpu_stall(!xPortGetCoreID());  // Stop the other core.
  esp_reset_reason_set_hint(ESP_RST_BROWNOUT);
  brownoutStatus = 'B';
  esp_restart_noos(); // dirty reboot
}

static void initBrownout() {
  // brownout warning only output once to prevent bootloop
  if (brownoutStatus == 'R') LOG_WRN("Brownout warning previously notified");
  else if (brownoutStatus == 'B') {
    LOG_WRN("Brownout occurred due to inadequate power supply");
    brownoutStatus = 'R';
  } else {
    brownout_hal_config_t cfg = {
      .threshold = BROWNOUT_DET_LVL,
      .enabled = true,
      .reset_enabled = false,
      .flash_power_down = true,
      .rf_power_down = true,
    };
    brownout_hal_config(&cfg);
    brownout_ll_intr_clear();
    rtc_isr_register(notifyBrownout, NULL, RTC_CNTL_BROWN_OUT_INT_ENA_M, RTC_INTR_FLAG_IRAM);
    brownout_ll_intr_enable(true);
    brownoutStatus = 0; 
  }
}
