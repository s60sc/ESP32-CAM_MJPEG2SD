
/*********************** Remote loggging ***********************/
/*
 * Log mode selection in user interface: 
 * false : log to serial / web monitor only
 * true  : also saves log on SD card. To download the log generated, either:
 *  - To view the log, press Show Log button on the browser
 * - To clear the log file contents, on log web page press Clear Log link
 *
 * s60sc 2026
 */

#include "appGlobals.h"
#include "esp_memory_utils.h"

// configuration
#define MAX_OUT_BODY 128
#define MAX_OUT_FULL 192
#define LOG_POOL_SIZE 8  // messages that can be in flight

volatile uint8_t g_logLevel = LOG_LEVEL_DEBUG;
static logEntry_t s_pool[LOG_POOL_SIZE];       // .bss, not stack
static QueueHandle_t s_free_q = NULL;
static QueueHandle_t s_filled_q = NULL;

#define HWM_MIN 32 // less than these bytes with debug exception probably indicates stack overflow
#define HWM_MAX 128 // more than these bytes with debug exception probably indicates printf formatting causing break
#define WRITE_CACHE_CYCLE 5
TaskHandle_t logHandle = NULL;

bool sdLog = false; // log to SD
int logType = 0; // which log contents to display (0 : ram, 1 : sd)
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;
bool monitorOpen = true;
bool dbgVerbose = false;
// allow any startup failures to be reported via browser for remote devices
char startupFailure[SF_LEN] = {0};
static char brownoutStatus;

// RAM memory based logging in RTC slow memory (cannot init)
RTC_NOINIT_ATTR char messageLog[RAM_LOG_LEN];
RTC_NOINIT_ATTR uint16_t mlogEnd;
static RTC_NOINIT_ATTR uint32_t crashLoop;
static RTC_NOINIT_ATTR uint32_t backtrace[60]; // array of backtrace addresses 
static RTC_NOINIT_ATTR size_t btLen; // number of backtrace entries
static RTC_NOINIT_ATTR char btReason[64]; // reason for panic
static RTC_NOINIT_ATTR char btTask[20]; // task name
static RTC_NOINIT_ATTR int btCore; // cpu core id
static RTC_NOINIT_ATTR uint32_t btHWM; // high water mark bytes left
static RTC_NOINIT_ATTR uint32_t haveTrace; // boolean

// ====================================================================
// Per-level decoration
// ====================================================================
struct levelStyle_t {
  const char* identifier;
  const char* color;     // ANSI start sequence, "" for none
  bool useFunc; // true for function name, false for filename:lineno
};

static const levelStyle_t k_style[] = {
              // identifier, color, function or file:line
  /* RAW     */ { "", "", false},
  /* CMD     */ { "", "", false},
  /* ERROR   */ { "ERROR ", "\033[0;31m", false},   // red
  /* WARN    */ { "WARN ",  "\033[0;33m", true},   // yellow
  /* INFO    */ { "", "", true},   
  /* ALERT   */ { "", "", true},
  /* DEBUG   */ { "### DEBUG ", "\033[0;34m", false},   // blue
  /* VERBOSE */ { "VERBOSE ", "\033[0;36m", true},   // cyan
};
static_assert(sizeof(k_style) / sizeof(k_style[0]) == LOG_LEVEL_VERBOSE + 1, "style table size");


// persistent log management

void resetCrashLoop() {
  crashLoop = 0;
}

void logLine() {
  LOG_RAW(" \n");
}

int getLogStartPtr(int mlogEnd) {
  // skip over nulls
  int startPtr = mlogEnd;
  while (messageLog[startPtr] == 0) {
    startPtr++;
    if (startPtr >= RAM_LOG_LEN) startPtr = 0;
    if (startPtr == mlogEnd) break;
  }
  return startPtr;
}

void saveRamLog(const char* ramLogName) {
  // save ramlog to storage 
  File ramFile = STORAGE.open(ramLogName, FILE_WRITE);
  int startPtr = getLogStartPtr(mlogEnd);
  int endPtr = mlogEnd;  

  // write log in chunks
  do {
    int maxChunk = startPtr < endPtr ? endPtr - startPtr : RAM_LOG_LEN - startPtr;
    size_t chunkSize = std::min(CHUNKSIZE, maxChunk);    
    if (chunkSize > 0) ramFile.write((uint8_t*)messageLog + startPtr, chunkSize);
    startPtr += chunkSize;
    if (startPtr >= RAM_LOG_LEN) startPtr = 0;
    delay(1);
  } while (startPtr != endPtr);
  ramFile.close();
}

static void ramLogClear() {
  mlogEnd = 0;
  memset(messageLog, 0, RAM_LOG_LEN);
}
  
static void ramLogStore(const char* outBuf, size_t msgLen) {
  // save log entry in ram buffer
  if (mlogEnd + msgLen >= RAM_LOG_LEN) {
    // log needs to roll around cyclic buffer
    uint16_t firstPart = RAM_LOG_LEN - mlogEnd;
    memcpy(messageLog + mlogEnd, outBuf, firstPart);
    msgLen -= firstPart;
    memcpy(messageLog, outBuf + firstPart, msgLen);
    mlogEnd = 0;
  } else memcpy(messageLog + mlogEnd, outBuf, msgLen);
  mlogEnd += msgLen;
}

void flush_log(bool andClose) {
  if (log_remote_fp != NULL) {
    fsync(fileno(log_remote_fp));  
    fflush(log_remote_fp);
    if (andClose) {
      LOG_INF("Closed SD file for logging");
      fclose(log_remote_fp);
      log_remote_fp = NULL;
    } else delay(1000);
  }  
}

static void remote_log_init_SD() {
#if (!CONFIG_IDF_TARGET_ESP32C3 && !CONFIG_IDF_TARGET_ESP32S2)
  STORAGE.mkdir(DATA_DIR);
  // Open remote file
  log_remote_fp = NULL;
  log_remote_fp = fopen("/sdcard" LOG_FILE_PATH, "a");
  if (log_remote_fp == NULL) {LOG_WRN("Failed to open SD log file %s", LOG_FILE_PATH);}
  else {
    logLine();
    LOG_INF("Opened SD file for logging");
  }
#endif
}

void reset_log() {
  if (logType == 0) ramLogClear();
  if (logType == 1) {
    if (log_remote_fp != NULL) flush_log(true); // Close log file
    STORAGE.remove(LOG_FILE_PATH);
    remote_log_init_SD();
  }
  LOG_INF("Cleared %s log file", logType == 0 ? "RAM" : "SD"); 
}

void remote_log_init() {
  // setup required log mode
  if (sdLog) {
    flush_log(false);
    remote_log_init_SD(); // store log on sd card
  } else flush_log(true);
}

// helpers

void formatHex(const char* inData, size_t inLen) {
  // format data as hex bytes for output
  char formatted[(inLen * 3) + 1];
  for (int i=0; i<inLen; i++) sprintf(formatted + (i*3), "%02x ", inData[i]);
  formatted[(inLen * 3)] = 0; // terminator
  LOG_INF("Hex: %s", formatted);
}

const char* espErrMsg(esp_err_t errCode) {
  // convert esp error code to text
  // https://github.com/espressif/esp-idf/blob/master/components/esp_common/include/esp_err.h
  static char errText[100];
  esp_err_to_name_r(errCode, errText, 100);
  return errText;
}

static void appPanicHandler(arduino_panic_info_t *info, void *arg) {
  // store crash backtrace and delay reboot to avoid thrashing
  // https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-misc.c
  TaskHandle_t task =  xTaskGetCurrentTaskHandleForCore(info->core);
  btHWM = uxTaskGetStackHighWaterMark(task);
  const char* taskName = task ? pcTaskGetName(task) : "idle";
  strncpy(btTask, taskName, sizeof(btTask) - 1);
  strncpy(btReason, info->reason, sizeof(btReason) - 1);
  btCore = info->core;
  btLen = info->backtrace_len;
  int start = (btLen > 60) ? btLen - 60 : 0; // later entries more important
  for (int i = start; i < info->backtrace_len; i++) backtrace[i - start] = info->backtrace[i];
  haveTrace = MAGIC_NUM; // flag that backtrace available
  esp_rom_delay_us(PANIC_DELAY * 1000 * 1000);
}

static void expandReason() {
  if (!btReason[0]) strcpy(btReason, "unknown");
#if CONFIG_IDF_TARGET_ARCH_RISCV
  // riscV
  else if (strstr(btReason, "Breakpoint") != NULL) sprintf(btReason, "probably printf format"); // usually misplaced or misformatted vsnprintf()
  else if (strstr(btReason, "Stack protection fault") != NULL) sprintf(btReason, "stack overflow after HWM: %lu bytes", btHWM);  
  else if (!strcmp(btReason, "LoadAccessFault") || !strcmp(btReason, "StoreAccessFault") || !strcmp(btReason, "InstructionAccessFault")) strcat(btReason, " (pointer issue)");
#else
  // Xtensa
  else if (strstr(btReason, "Unhandled debug exception") != NULL) {
    if (btHWM < HWM_MIN) sprintf(btReason, "probably stack overflow @ HWM: %lu bytes", btHWM);
    else if (btHWM > HWM_MAX) sprintf(btReason, "probably printf format"); // usually misplaced or misformatted vsnprintf()
    else sprintf(btReason, "stack overflow / printf format. HWM: %lu bytes", btHWM); 
  }
  else if (!strcmp(btReason, "LoadProhibited") || !strcmp(btReason, "StoreProhibited") || !strcmp(btReason, "InstructionFetchError")) strcat(btReason, " (pointer issue)");
#endif
}

static void showBacktrace() {
  // display reason and backtrace following restart after a panic
  if (haveTrace == MAGIC_NUM) {
    haveTrace = 0;
    expandReason();
    LOG_WRN("Core %d task: %s - %s", btCore, btTask, btReason); 
    char bt[(11 * btLen) + 1]; // 11 is size of each trace hex
    bt[0] = 0;
    int offset = 0;
    for (int i = 0; i < btLen; i++) {
      int remaining = sizeof(bt) - offset;
      if (remaining < 12) break; // Need space for 11 chars + null terminator
      int written = snprintf(bt + offset, remaining, "0x%08x ", (unsigned int)backtrace[i]);
      if (written > 0 && written < remaining) offset += written;
      else  break;
    }
    LOG_WRN("Paste backtrace below into Arduino Exception Decoder:\n");
    LOG_RAW("Backtrace: %s\n\n", bt);
  }
}

static esp_sleep_wakeup_cause_t printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : LOG_INF("Wakeup by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : LOG_INF("Wakeup by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : LOG_INF("Wakeup by internal timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : LOG_INF("Wakeup by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : LOG_INF("Wakeup by ULP program"); break;
    case ESP_SLEEP_WAKEUP_GPIO: LOG_INF("Wakeup by GPIO"); break;    
    case ESP_SLEEP_WAKEUP_UART: LOG_INF("Wakeup by UART"); break; 
    default : LOG_INF("Wakeup by reset"); break;
  }
  return wakeup_reason;
}

static esp_reset_reason_t printResetReason() {
  esp_reset_reason_t bootReason = esp_reset_reason();
  switch (bootReason) {
    case ESP_RST_UNKNOWN: LOG_INF("Reset for unknown reason"); break;
    case ESP_RST_POWERON: {
      LOG_INF("Power on reset");
      brownoutStatus = 0;
      messageLog[0] = 0; 
      break;
    }
    case ESP_RST_EXT: LOG_INF("Reset from external pin"); break;
    case ESP_RST_SW: LOG_INF("Software reset via esp_restart"); break;
    case ESP_RST_PANIC: LOG_INF("Software reset due to exception/panic"); break;
    case ESP_RST_INT_WDT: LOG_INF("Reset due to interrupt watchdog"); break;
    case ESP_RST_TASK_WDT: LOG_INF("Reset due to task watchdog"); break;
    case ESP_RST_WDT: LOG_INF("Reset due to other watchdogs"); break;
    case ESP_RST_DEEPSLEEP: LOG_INF("Reset after exiting deep sleep mode"); break;
    case ESP_RST_BROWNOUT: LOG_INF("Software reset due to brownout"); break;
    case ESP_RST_SDIO: LOG_INF("Reset over SDIO"); break;
    default: LOG_WRN("Unhandled reset reason"); break;
  }
  showBacktrace();
  return bootReason;
}

esp_sleep_wakeup_cause_t wakeupResetReason() {
  printResetReason();
  esp_sleep_wakeup_cause_t wakeupReason = printWakeupReason();
  return wakeupReason;
}

static void boardInfo() {
  LOG_INF("Chip %s, %u cores @ %luMhz, rev %u", ESP.getChipModel(), ESP.getChipCores(), ESP.getCpuFreqMHz(), ESP.getChipRevision() / 100);
  FlashMode_t ideMode = ESP.getFlashChipMode();
  LOG_INF("Flash %s, mode %s @ %luMhz", fmtSize(ESP.getFlashChipSize()), (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"), ESP.getFlashChipSpeed() / OneMHz);

#if defined(CONFIG_SPIRAM_MODE_OCT)
  const char* psramMode = "OPI";
#else 
  const char* psramMode = "QSPI";
#endif
  char memInfo[100] = "none";
#if !CONFIG_IDF_TARGET_ESP32C3
  if (psramFound()) sprintf(memInfo, "%s, mode %s @ %dMhz", fmtSize(ESP.getPsramSize()), psramMode, CONFIG_SPIRAM_SPEED);
#endif
  LOG_INF("PSRAM %s", memInfo);
}

// Logging API

logEntry_t* logAcquire(logLevel_t level, const char* timestamp, const char* file, int line, const char* func, const char* fmt);
void logPost(logEntry_t* e);

void logCopyStr(logEntry_t* e, logArg_t &a, const char* s) {
  if (!s) s = "(null)";
  uint16_t off = e->str_used;
  size_t room = (size_t)LOG_STR_AREA - off;       // bytes left incl. NUL
  if (room == 0) {               // area full: point at the terminator
    a.v.str_off = LOG_STR_AREA;
    return;
  }
  size_t n = 0;
  while (s[n] && n < room - 1) { e->strs[off + n] = s[n]; n++; }
  e->strs[off + n] = '\0';
  a.v.str_off = off;
  e->str_used = (uint16_t)(off + n + 1);
}

// ====================================================================
// Argument coercion: the format spec decides how a stored arg is printed,
// so a mismatched type degrades gracefully instead of corrupting the stack
// like a real printf would.
// ====================================================================
static int64_t asInt(const logArg_t *a) {
  switch (a->type) {
    case LOG_ARG_INT:    return a->v.i;
    case LOG_ARG_UINT:   return (int64_t)a->v.u;
    case LOG_ARG_DOUBLE: return (int64_t)a->v.d;
    case LOG_ARG_PTR:    return (int64_t)a->v.p;
    default:               return 0;
  }
}

static uint64_t asUInt(const logArg_t *a) {
  uint64_t u;
  switch (a->type) {
    case LOG_ARG_INT:    u = (uint64_t)a->v.i; break;
    case LOG_ARG_UINT:   u = a->v.u; break;
    case LOG_ARG_DOUBLE: u = (uint64_t)(int64_t)a->v.d; break;
    case LOG_ARG_PTR:    u = (uint64_t)a->v.p; break;
    default:               u = 0; break;
  }
  // %x/%u/%o of a negative 32-bit int must print 32 bits, as real printf does
  if (a->type == LOG_ARG_INT && a->size < 8) u &= ((1ULL << (a->size * 8)) - 1ULL);
  return u;
}

static double asDouble(const logArg_t *a) {
  switch (a->type) {
    case LOG_ARG_INT:    return (double)a->v.i;
    case LOG_ARG_UINT:   return (double)a->v.u;
    case LOG_ARG_DOUBLE: return a->v.d;
    default:               return 0.0;
  }
}

static const char *asStr(const logEntry_t *e, const logArg_t *a) {
  return (a->type == LOG_ARG_STR) ? &e->strs[a->v.str_off] : "<?>";
}

static const logArg_t *nextArg(const logEntry_t *e, uint8_t *idx) {
  return (*idx < e->nargs) ? &e->args[(*idx)++] : NULL;
}

// ====================================================================
// Message body: walk the format string, format each conversion on its own
// ====================================================================
#define EMIT(...)                                                              \
  do {                                                                         \
    size_t room_ = cap - pos;                                                  \
    int r_ = snprintf(out + pos, room_, __VA_ARGS__);                          \
    if (r_ < 0) r_ = 0;                                                        \
    if ((size_t)r_ >= room_) { pos = cap - 1; trunc = true; }                  \
    else pos += (size_t)r_;                                                    \
  } while (0)

// renderBody builds each conversion spec at runtime, so snprintf legitimately gets a
// non-literal format here, so exempt warning
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static size_t renderBody(const logEntry_t *e, char *out, size_t cap) {
  size_t pos = 0;
  bool trunc = false;
  uint8_t ai = 0;
  const char *p = e->fmt ? e->fmt : "";
  out[0] = '\0';

  while (*p && !trunc) {
    // ---- literal text up to the next '%'
    if (*p != '%') {
      size_t run = strcspn(p, "%");
      size_t room = cap - 1 - pos;
      size_t c = run < room ? run : room;
      memcpy(out + pos, p, c);
      pos += c;
      out[pos] = '\0';
      if (c < run) trunc = true;
      p += run;
      continue;
    }

    p++; // skip '%'
    if (*p == '%') { EMIT("%%"); p++; continue; }

    // ---- rebuild the conversion spec (flags, width, precision) in `spec`
    char spec[24];
    size_t sl = 0;
    auto put = [&](char c) { if (sl < sizeof(spec) - 5) spec[sl++] = c; };  // keep room for "ll"+conv+NUL
    auto putInt = [&](int v) { char t[12]; snprintf(t, sizeof t, "%d", v); for (char *q = t; *q; q++) put(*q); };

    put('%');
    while (*p && strchr("-+ #0", *p)) put(*p++);

    if (*p == '*') {  // width from argument
      const logArg_t *w = nextArg(e, &ai);
      putInt(w ? (int)asInt(w) : 0);
      p++;
    } else {
      while (isdigit((unsigned char)*p)) put(*p++);
    }

    if (*p == '.') {
      p++;
      if (*p == '*') {  // precision from argument (negative = none)
        const logArg_t *pr = nextArg(e, &ai);
        int v = pr ? (int)asInt(pr) : -1;
        if (v >= 0) { put('.'); putInt(v); }
        p++;
      } else {
        put('.');
        while (isdigit((unsigned char)*p)) put(*p++);
      }
    }

    // length modifiers are dropped: width is decided by the stored argument type
    while (*p && strchr("hljztLq", *p)) p++;

    char conv = *p;
    if (!conv) break;
    p++;

    auto finish = [&](const char *lenmod, char c) {
      for (const char *m = lenmod; *m; m++) spec[sl++] = *m;
      spec[sl++] = c;
      spec[sl] = '\0';
    };

    const logArg_t *a = nextArg(e, &ai);
    if (!a) { EMIT("<?>"); continue; }     // more conversions than arguments

    switch (conv) {
      case 'd': case 'i':
        finish("ll", 'd');
        EMIT(spec, (long long)asInt(a));
      break;
      case 'u': case 'x': case 'X': case 'o':
        finish("ll", conv);
        EMIT(spec, (unsigned long long)asUInt(a));
      break;
      case 'c':
        finish("", 'c');
        EMIT(spec, (int)asInt(a));
      break;
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        finish("", conv);
        EMIT(spec, asDouble(a));
      break;
      case 's':
        finish("", 's');
        EMIT(spec, asStr(e, a));
      break;
      case 'p':
        finish("", 'p');
        EMIT(spec, (void *)(uintptr_t)asUInt(a));
      break;
      default:  // %n and anything unknown are not supported
        EMIT("<%%%c?>", conv);
      break;
    }
  }

  if (trunc && pos >= 3) { out[pos - 3] = '.'; out[pos - 2] = '.'; out[pos - 1] = '.'; }
  return pos;
}
#pragma GCC diagnostic pop

// ====================================================================
// Full output line
// ====================================================================
static size_t renderLine(char* out, size_t cap, uint8_t level, const char* timestamp, const char* file, int line, const char* func, const char* body) {
  const levelStyle_t &st = k_style[level];
//#define LOG_USE_COLOR // uncomment if serial monitor supports colors - eg platformio, but not arduino
#ifdef LOG_USE_COLOR
  const char* color = st.color;
  const char* reset = st.color[0] ? "\033[0m" : "";
#else
  const char* color = "";
  const char* reset = "";
#endif
  int n = 0;
  if (st.useFunc) n = snprintf(out, cap, "%s[%s %s%s] %s%s", color, timestamp, st.identifier, func, body, reset);
  else n = snprintf(out, cap, "%s[%s %s@ %s:%u] %s%s", color, timestamp, st.identifier, pathToFileName(file), line, body, reset);
  if (level < LOG_LEVEL_INFO) out[n++] = '~';
  out[n++] = '\n';
  if (n < 0) n = 0;
  if ((size_t)n >= cap) {  // overlong msg, keep the line terminated
    n = (int)cap - 1;
    out[n - 1] = '\n';
  }
  return (size_t)n;
}

// ====================================================================
//
//  Log output Mssage formatting. 
//
// Caller -> logAcquire() -> fill entry -> logPost() -> [s_filled_q]
// logTask: receive -> renderBody() -> renderLine() -> output -> return entry to [s_free_q]
//
// Both queues carry entry pointers into s_pool, so nothing is copied and an entry
// is owned by exactly one party at a time (free queue, a caller, filled queue, or the log task).

// The Calling task does almost no work and uses almost no stack:
//   1. grabs a pre-allocated entry from a fixed pool (non-blocking)
//   2. stores tag/fmt pointers, timestamp, task name, core and the raw, typed
//      argument values (strings are copied)
//   3. posts the entry pointer to the log task (non-blocking)
// No vsnprintf, no float formatting and no large buffer ever runs on the caller's stack.
//
// The LOG Task (own, larger stack) walks the format string, formats each
// argument and prints the line with per-level decoration 
//
// Rules:
//  - fmt` must have static lifetime (string literals): only the pointers
//    are stored. %s ARGUMENTS are copied, so those may be temporary buffers.
//  - Up to LOG_MAX_ARGS arguments (compile-time checked).
//  - NOT callable from an ISR.
//  - Format strings are checked by the compiler (-Wformat) exactly like printf.
// ====================================================================

static void logTask(void *pvParameters) {
  // static: these (and printf's internals) are why this task has a big stack
  static char msgBody[MAX_OUT_BODY];
  static char msgFull[MAX_OUT_FULL];
  size_t msgLen = 0;

  while (1) {
    logEntry_t *e = NULL;
    if (xQueueReceive(s_filled_q, &e, portMAX_DELAY) != pdTRUE || e == NULL) continue;
    msgLen = renderBody(e, msgBody, sizeof msgBody);
    if (e->raw) {   
       // no additional decoration required
      if (msgLen >= sizeof msgBody) msgLen = sizeof msgBody - 1;
      memcpy(msgFull, msgBody, msgLen);
    } else {
      // apply required decoration
      msgLen = renderLine(msgFull, sizeof msgFull, e->level, e->timestamp, e->file, e->line, e->func, msgBody);
    }
    if (e->level == LOG_LEVEL_COMMAND) {
      msgFull[msgLen] = 0;
      sendSSE("cmd", msgFull); // send command to browser
    } else {
      // ensure have newline and terminator for overlong msg
      msgFull[MAX_OUT_FULL - 2] = '\n'; 
      msgFull[MAX_OUT_FULL - 1] = 0; 
      // output message to various recipients
      if (msgLen > 1) { // skips progress markers
        sendSSE("log", msgFull); // output  log mesdage to browser 
        if (msgFull[msgLen - 2] == '~') msgFull[msgLen - 2] = ' '; // remove '~' if present
      }
      if (monitorOpen && msgLen) {
        // output to monitor console if attached
        fwrite(msgFull, 1, msgLen, stdout); 
        fflush(stdout);
      } else delay(10); // allow time for other tasks
      if (msgLen > 1) {
        ramLogStore(msgFull, msgLen); // store in rtc ram 
        if (sdLog) {
          if (log_remote_fp != NULL) {
            // output to SD if file opened
            fwrite(msgFull, sizeof(char), msgLen, log_remote_fp); // log.txt
            // periodic sync to SD
            if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
          } 
        }
      }
    }
    if (e->level == LOG_LEVEL_DEBUG) delay(FLUSH_DELAY);
    xQueueSend(s_free_q, &e, 0);  // hand the entry back; cannot fail (capacity == pool size)
  }
}

// ====================================================================
// Producer side (runs on the caller's task, kept tiny and non-blocking)
// ====================================================================
logEntry_t* logAcquire(logLevel_t level, const char* timestamp, const char* file, int line, const char* func, const char* fmt) {
  if (s_free_q == NULL) return NULL;       // not initialised

  logEntry_t* e = NULL;
  if (xQueueReceive(s_free_q, &e, 0) != pdTRUE) return NULL;

  e->fmt = fmt;
  e->file = file;
  e->line = (uint16_t)line;
  e->func = func;
  e->level = (uint8_t)level;
  e->timestamp = timestamp;
  e->nargs = 0;
  e->raw = 0;
  e->str_used = 0;
  e->strs[LOG_STR_AREA] = '\0';
  return e;
}

void logPost(logEntry_t* e) {
  xQueueSend(s_filled_q, &e, 0);  // capacity == pool size, so this never fails
}

// ====================================================================
// esp_log capture: replaces esp_log_set_vprintf(vprintfRedirect).
// The hook cannot hand its va_list to another task (it points into the caller's
// stack), so it walks the format string and stores the typed values instead.
// Formatting still happens in the log task.
// ====================================================================
static vprintf_like_t s_prev_vprintf = NULL;

static void releaseEntry(logEntry_t* e) { xQueueSend(s_free_q, &e, 0); }

static bool captureStar(logEntry_t* e, va_list &ap) {
  if (e->nargs >= LOG_MAX_ARGS) return false;
  logPush(e, va_arg(ap, int));
  return true;
}

// Reads `ap` as dictated by `fmt`. Returns false if it can't (too many args, %n, unknown conversion).
static bool captureVaList(logEntry_t* e, const char* fmt, va_list &ap) {
  const char* p = fmt;
  while (*p) {
    if (*p++ != '%') continue;
    if (*p == '%') { p++; continue; }

    while (*p && strchr("-+ #0", *p)) p++;
    if (*p == '*') { if (!captureStar(e, ap)) return false; p++; }
    else while (isdigit((unsigned char)*p)) p++;
    if (*p == '.') {
      p++;
      if (*p == '*') { if (!captureStar(e, ap)) return false; p++; }
      else while (isdigit((unsigned char)*p)) p++;
    }

    int lm = 0;  // 0 none/h/hh, 2 l, 3 ll, 4 j, 5 z, 6 t, 7 L
    for (;; p++) {
      if (*p == 'h') { /* promoted to int */ }
      else if (*p == 'l') lm = (lm == 2) ? 3 : 2;
      else if (*p == 'j') lm = 4;
      else if (*p == 'z') lm = 5;
      else if (*p == 't') lm = 6;
      else if (*p == 'L') lm = 7;
      else break;
    }

    char conv = *p;
    if (!conv) return false;
    p++;
    if (e->nargs >= LOG_MAX_ARGS) return false;

    switch (conv) {
      case 'd': case 'i':
        if (lm == 3)                logPush(e, va_arg(ap, long long));
        else if (lm == 2)           logPush(e, va_arg(ap, long));
        else if (lm == 4)           logPush(e, va_arg(ap, intmax_t));
        else if (lm == 5 || lm == 6) logPush(e, va_arg(ap, ptrdiff_t));
        else                        logPush(e, va_arg(ap, int));
      break;
      case 'u': case 'x': case 'X': case 'o':
        if (lm == 3)                logPush(e, va_arg(ap, unsigned long long));
        else if (lm == 2)           logPush(e, va_arg(ap, unsigned long));
        else if (lm == 4)           logPush(e, va_arg(ap, uintmax_t));
        else if (lm == 5 || lm == 6) logPush(e, va_arg(ap, size_t));
        else                        logPush(e, va_arg(ap, unsigned int));
      break;
      case 'c':
        logPush(e, va_arg(ap, int));
      break;
      case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
        if (lm == 7) logPush(e, (double)va_arg(ap, long double));
        else         logPush(e, va_arg(ap, double));
      break;
      case 's':
        logPush(e, va_arg(ap, const char *));   // copied into the entry
      break;
      case 'p':
        logPush(e, va_arg(ap, void *));
      break;
      default:
        return false;  // %n or unknown
    }
  }
  return true;
}

static int logVprintfHook(const char* fmt, va_list ap) {
  vprintf_like_t prev = s_prev_vprintf ? s_prev_vprintf : vprintf;

  // Not initialised, or ISR context (log queue calls are task-only): behave as before
  if (s_free_q == NULL || xPortInIsrContext()) return prev(fmt, ap);

  logEntry_t* e = logAcquire(LOG_LEVEL_INFO, NULL, NULL, 0, NULL, fmt);
  if (!e) return 0;  // pool exhausted: never blocks
  e->raw = 1;

  // Normally fmt is a literal in flash/rodata and only the pointer is kept. If it lives in
  // DRAM (stack/heap/.data) it may not outlive this call, so copy it into the entry.
  if (esp_ptr_in_dram(fmt)) {
    logArg_t tmp;
    logCopyStr(e, tmp, fmt);
    e->fmt = &e->strs[tmp.v.str_off];
  }

  va_list copy;
  va_copy(copy, ap);
  bool ok = captureVaList(e, fmt, copy);
  va_end(copy);

  if (!ok) {  // rare: fall back to the old handler (uses caller stack)
    releaseEntry(e);
    return prev(fmt, ap);
  }
  logPost(e);
  return 0;
}

static void logCaptureEspLog() {
  if (s_prev_vprintf != NULL) return;  // already installed
  s_prev_vprintf = esp_log_set_vprintf(logVprintfHook);
}

void logReleaseEspLog() {
  if (s_prev_vprintf == NULL) return;

  esp_log_set_vprintf(s_prev_vprintf);
  s_prev_vprintf = NULL;
}

void logSetLevel(logLevel_t level) { g_logLevel = (uint8_t)level; }

bool logSetup() {
  // prep logging environment
  if (crashLoop == MAGIC_NUM) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Crash loop detected, check log %s", (brownoutStatus == 'B' || brownoutStatus == 'R') ? "(brownout)" : " ");
  crashLoop = MAGIC_NUM;
  if (logHandle == NULL) {
    set_arduino_panic_handler(appPanicHandler, NULL);
    Serial.begin(115200);
    Serial.setDebugOutput(DBG_ON);
    esp_rom_printf("\n\n");
    if (DEBUG_MEM) esp_rom_printf("init > Free: heap %lu\n", ESP.getFreeHeap()); 
    (DBG_ON) ? esp_log_level_set("*", DBG_LVL) : esp_log_level_set("*", ESP_LOG_ERROR); // show ESP_LOG_ERROR messages during init
    logSetLevel(LOG_LEVEL_DEBUG);
    logCaptureEspLog(); // redirect esp_log output to app log

    QueueHandle_t fq = xQueueCreate(LOG_POOL_SIZE, sizeof(logEntry_t *));
    QueueHandle_t lq = xQueueCreate(LOG_POOL_SIZE, sizeof(logEntry_t *));
    if (!fq || !lq) {
      if (fq) vQueueDelete(fq);
      if (lq) vQueueDelete(lq);
      return false;
    }

    for (int i = 0; i < LOG_POOL_SIZE; i++) {
      logEntry_t *p = &s_pool[i];
      xQueueSend(fq, &p, 0);
    }

    s_filled_q = lq;  // set before the task can run
    if (xTaskCreateWithCaps(logTask, "logTask", LOG_STACK_SIZE, NULL, LOG_PRI, &logHandle, STACK_MEM) != pdPASS) {
      s_filled_q = NULL;
      vQueueDelete(fq);
      vQueueDelete(lq);
      return false;
    }
    if (esp_reset_reason() == ESP_RST_POWERON || mlogEnd >= RAM_LOG_LEN) ramLogClear(); // init

    s_free_q = fq;  // enables producer

    LOG_RAW("\n\n=============== %s %s ===============\n", APP_NAME, APP_VER);
    LOG_INF("Setup RAM based log, size %u, starting from %u", RAM_LOG_LEN, mlogEnd);
    wakeupResetReason();
    boardInfo();
    debugMemory("logSetup");
  }
  return true;
}

// Returns true once every entry is back in the free pool (i.e. everything queued has been printed).
bool logFlush(uint32_t timeout_ms) {
  if (s_free_q == NULL) return true;
  TickType_t start = xTaskGetTickCount();
  TickType_t limit = pdMS_TO_TICKS(timeout_ms);
  while (uxQueueMessagesWaiting(s_free_q) < LOG_POOL_SIZE) {
    if ((TickType_t)(xTaskGetTickCount() - start) >= limit) return false;
    vTaskDelay(1);
  }
  return true;
}

void debugMemory(const char* caller) {
  if (DEBUG_MEM) {
    LOG_RAW("%s > Free: heap %lu, block: %lu, min: %lu, pSRAM %lu\n", caller, ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
    delay(FLUSH_DELAY);
  }
}

void showProgress(const char* marker) {
  // show progess as dots or supplied single char marker
  static uint8_t dotCnt = 0;
  uart_write_bytes(UART_NUM_0, marker, 1); // progress marker
  if (++dotCnt >= DOT_MAX) {
    dotCnt = 0;
    uart_write_bytes(UART_NUM_0, "\n", 1);
  }
}


/************** task monitoring ***************/

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 1, 0)

static const char* getTaskStateString(eTaskState state) {
  // 
  switch (state) { 
    case eRunning: return "Running"; 
    case eReady: return "Ready"; 
    case eBlocked: return "Blocked"; 
    case eSuspended: return "Suspended"; 
    case eDeleted: return "Deleted"; 
    case eInvalid: return "Invalid"; 
    default: return "Unknown";
  }
}

static void statsTask(void *arg) { 
  // Output real time task stats periodically
  #define STATS_TASK_PRIO     10
  #define STATS_INTERVAL      30000 // ms
  #define ARRAY_SIZE_OFFSET   40   // Increase this if ESP_ERR_INVALID_SIZE

  bool onceOnly = *(bool*)arg; 
  esp_err_t ret = ESP_OK;  
  TaskStatus_t *statsArray = NULL;
  UBaseType_t statsArraySize;
  static configRUN_TIME_COUNTER_TYPE prevRunCounter = 0;
  configRUN_TIME_COUNTER_TYPE runCounter;
  
  do {
    delay(STATS_INTERVAL);

    do { // fake loop for breaks
      // Allocate array to store current task states
      statsArraySize = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
      statsArray = (TaskStatus_t *)malloc(sizeof(TaskStatus_t) * statsArraySize);
      if (statsArray == NULL) {
        ret = ESP_ERR_NO_MEM;
        break;
      }

      // Get current task states
      statsArraySize = uxTaskGetSystemState(statsArray, statsArraySize, &runCounter);
      if (statsArraySize == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        break;
      }

      // Calculate total_elapsed_time in units of run time stats clock period
      if (runCounter - prevRunCounter == 0) {
        ret = ESP_ERR_INVALID_STATE;
        break;
      }

      LOG_RAW("\nTask stats interval %ds on %u cores\n", STATS_INTERVAL / 1000, CONFIG_FREERTOS_NUMBER_OF_CORES);
      LOG_RAW("\n| %-16s | %-10s | %-3s | %-4s | %-6s |\n", "Task name", "State", "Pri", "Core", "Core%");
      LOG_RAW("|------------------|------------|-----|------|--------|\n"); 
      // Match each task in start_array to those in the end_array
      for (int i = 0; i < statsArraySize; i++) {
        float percentage_time = ((float)statsArray[i].ulRunTimeCounter * 100.0f) / runCounter;
        UBaseType_t coreId = statsArray[i].xCoreID;
        delay(5);
        LOG_RAW("| %-16s | %-10s | %3u | %4c | %5.1f%% |\n", 
          statsArray[i].pcTaskName, getTaskStateString(statsArray[i].eCurrentState), (int)statsArray[i].uxCurrentPriority, coreId == tskNO_AFFINITY ? '*' : '0' + (int)coreId, percentage_time);
      }
      LOG_RAW("|------------------|------------|-----|------|--------|\n"); 
    } while (false);

    prevRunCounter = runCounter;
    free(statsArray);
    if (ret != ESP_OK) LOG_WRN("Failed to start task monitoring %s", espErrMsg(ret));
  } while (!onceOnly);
  vTaskDelete(NULL);
}

void runTaskStats(bool _onceOnly) {
  // invoke task stats monitoring
  static bool onceOnly = _onceOnly;
  // Allow other core to finish initialization
  vTaskDelay(pdMS_TO_TICKS(100));
  // Create and start stats task
  xTaskCreatePinnedToCore(statsTask, "statsTask", 4096, &onceOnly, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
}
#endif

void checkMemory(const char* source) {
  LOG_INF("%s Free: heap %lu, block: %lu, min: %lu, pSRAM %lu", source[0] ? source : "Setup", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
  if (ESP.getFreeHeap() < WARN_HEAP) LOG_WRN("Free heap only %lu, min %lu", ESP.getFreeHeap(), ESP.getMinFreeHeap());
  if (ESP.getMaxAllocHeap() < WARN_ALLOC) LOG_WRN("Max allocatable heap block is only %lu", ESP.getMaxAllocHeap());
  if (!source[0] && DEBUG_MEM) runTaskStats();
}

uint32_t checkStackUse(TaskHandle_t thisTask, int taskIdx) {
  // get minimum free stack size for task since started
  // taskIdx used to index minStack[] array
  static uint32_t minStack[20]; 
  uint32_t freeStack = 0;
  if (thisTask != NULL) {
    freeStack = (uint32_t)uxTaskGetStackHighWaterMark(thisTask);
    if (!minStack[taskIdx]) {
      minStack[taskIdx] = freeStack; // initialise
      LOG_INF("Task %s on core %d, initial stack space %lu", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
    if (freeStack < minStack[taskIdx]) {
      minStack[taskIdx] = freeStack;
      if (freeStack < MIN_STACK_FREE) LOG_WRN("Task %s on core %d, stack space only: %lu", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
      else LOG_INF("Task %s on core %d, stack space reduced to %lu", pcTaskGetTaskName(thisTask), xPortGetCoreID(), freeStack);
    }
  }
  return freeStack;
}

/************************ system info **************************/

#include <driver/gpio.h>
#include "esp32-hal-periman.h"

static void printGpioInfo() {
  // from https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/chip-debug-report.cpp
  LOG_RAW("Assigned GPIO Info:\n");
  for (uint8_t i = 0; i < SOC_GPIO_PIN_COUNT; i++) {
    if (!perimanPinIsValid(i)) continue;  //invalid pin
    peripheral_bus_type_t type = perimanGetPinBusType(i);
    if (type == ESP32_BUS_TYPE_INIT) continue;  //unused pin

    char gpioInf[100];
    char* p = gpioInf;
#if defined(BOARD_HAS_PIN_REMAP)
    int dpin = gpioNumberToDigitalPin(i);
    if (dpin < 0) continue;  //pin is not exported
    else p+= sprintf(p, "  D%-3d|%4u : ", dpin, i);
#else
    p+= sprintf(p, "  %4u : ", i);
#endif
    const char *extra_type = perimanGetPinBusExtraType(i);
    if (extra_type) p+= sprintf(p, "%s", extra_type);
    else p+= sprintf(p, "%s", perimanGetTypeName(type));
    int8_t bus_number = perimanGetPinBusNum(i);
    if (bus_number != -1) p+= sprintf(p, "[%u]", bus_number);

    int8_t bus_channel = perimanGetPinBusChannel(i);
    if (bus_channel != -1) p+= sprintf(p, "[%u]", bus_channel);
    *p = 0;
    delay(5);
    LOG_RAW("%s\n", gpioInf);
  }
}

// display partition map
const char* partitionTypeToStr(uint8_t type) {
  // Map type to string
  switch (type) {
    case ESP_PARTITION_TYPE_APP: return "APP";
    case ESP_PARTITION_TYPE_DATA: return "DATA";
    default: return "UNKNOWN";
  }
}

const char* partitionSubtypeToStr(uint8_t type, uint8_t subtype) {
  // Map subtype to string based on type
  if (type == ESP_PARTITION_TYPE_APP) {
    switch (subtype) {
      case ESP_PARTITION_SUBTYPE_APP_FACTORY: return "Factory";
      case ESP_PARTITION_SUBTYPE_APP_OTA_0: return "OTA_0";
      case ESP_PARTITION_SUBTYPE_APP_OTA_1: return "OTA_1";
      case ESP_PARTITION_SUBTYPE_APP_OTA_2: return "OTA_2";
      case ESP_PARTITION_SUBTYPE_APP_OTA_3: return "OTA_3";
      case ESP_PARTITION_SUBTYPE_APP_OTA_4: return "OTA_4";
      case ESP_PARTITION_SUBTYPE_APP_OTA_5: return "OTA_5";
      default: return "App_Other";
    }
  } else if (type == ESP_PARTITION_TYPE_DATA) {
    switch (subtype) {
      case ESP_PARTITION_SUBTYPE_DATA_OTA: return "OTA_Data";
      case ESP_PARTITION_SUBTYPE_DATA_PHY: return "PHY";
      case ESP_PARTITION_SUBTYPE_DATA_NVS: return "NVS";
      case ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS: return "NVS_Keys";
      case ESP_PARTITION_SUBTYPE_DATA_SPIFFS: return "SPIFFS";
      case ESP_PARTITION_SUBTYPE_DATA_FAT: return "FAT";
      default: return "Data_Other";
    }
  }
  return "Unknown";
}

static void printPartitionTable() {
  // print all partitions
  delay(10);
  LOG_RAW("%-12s %-6s %-12s %-10s %-12s %-10s", "Partition", "Type", "Subtype", "Address", "Size", "Encrypted\n");
  LOG_RAW("-----------------------------------------------------------------\n");

  // Get iterator for all partitions
  esp_partition_iterator_t iter = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);

  if (iter == NULL) {
    LOG_ERR("No partitions found");
    return;
  }

  // Iterate through all partitions
  do {
    const esp_partition_t* part = esp_partition_get(iter);
    const char* typeStr = partitionTypeToStr(part->type);
    const char* subtypeStr = partitionSubtypeToStr(part->type, part->subtype);
    const char* label = part->label;
    delay(5);
    LOG_RAW("%-12s %-6s %-12s 0x%08lX %-12s %-10s\n",
      label, typeStr, subtypeStr, part->address, fmtSize(part->size), part->encrypted ? "Yes" : "No");
    iter = esp_partition_next(iter);
  } while (iter != NULL);

  // Free iterator
  esp_partition_iterator_release(iter);
}

void showSys() {
  // output system details to web log
  logLine();
  boardInfo();
  logLine();
  LOG_RAW("%s v%s, arduino-esp32 v%s\n", APP_NAME, APP_VER, ESP_ARDUINO_VERSION_STR);
  logLine();
  printPartitionTable();
  logLine();
  printGpioInfo();
  logLine();
  runTaskStats(true);
  logLine();
  //gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
}

void showHWM(const char* msg) {
  uint32_t freeStack = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
  LOG_DBG("%s: stack for task %s has min %lu bytes",  msg, pcTaskGetName(NULL), freeStack);
  delay(250);
}

