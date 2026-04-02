
/*********************** Remote loggging ***********************/
/*
 * Log mode selection in user interface: 
 * false : log to serial / web monitor only
 * true  : also saves log on SD card. To download the log generated, either:
 *  - To view the log, press Show Log button on the browser
 * - To clear the log file contents, on log web page press Clear Log link
 */
 
#include "appGlobals.h"
#include "freertos/atomic.h"

bool dbgVerbose = false;

#define LOG_BUF_COUNT 4  // number of pool buffers —  number of concurrent calling tasks
#define LOG_QUEUE_DEPTH LOG_BUF_COUNT
#define HWM_MIN 32 // less than these bytes with debug exception probably indicates stack overflow
#define HWM_MAX 128 // more than these bytes with debug exception probably indicates printf formatting causing break
#define WRITE_CACHE_CYCLE 5

// Pool: fixed array of buffers 
static char (*poolBufs)[MAX_OUT] = NULL;
// Queue storage sized correctly as raw bytes (sizeof(char*) per slot)
static uint8_t freePoolStorage[LOG_BUF_COUNT * sizeof(char*)];
static uint8_t logQueueStorage[LOG_QUEUE_DEPTH * sizeof(char*)];
static StaticQueue_t freePoolStatic;
static StaticQueue_t logQueueStatic;
QueueHandle_t logFreePool = NULL;
QueueHandle_t logQueue = NULL;
TaskHandle_t logHandle = NULL;
static uint32_t dropCount = 0; // Atomic drop counter — safe to increment from any task

bool useLogColors = false;  // true to colorise log messages (eg if using idf.py, but not arduino)
bool sdLog = false; // log to SD
int logType = 0; // which log contents to display (0 : ram, 1 : sd)
static FILE* log_remote_fp = NULL;
static uint32_t counter_write = 0;
// allow any startup failures to be reported via browser for remote devices
char startupFailure[SF_LEN] = {0};

// RAM memory based logging in RTC slow memory (cannot init)
RTC_NOINIT_ATTR char messageLog[RAM_LOG_LEN];
RTC_NOINIT_ATTR uint16_t mlogEnd;
static RTC_NOINIT_ATTR char brownoutStatus;
static RTC_NOINIT_ATTR uint32_t crashLoop;
static RTC_NOINIT_ATTR uint32_t backtrace[60]; // array of backtrace addresses 
static RTC_NOINIT_ATTR size_t btLen; // number of backtrace entries
static RTC_NOINIT_ATTR char btReason[64]; // reason for panic
static RTC_NOINIT_ATTR char btTask[20]; // task name
static RTC_NOINIT_ATTR int btCore; // cpu core id
static RTC_NOINIT_ATTR uint32_t btHWM; // high water mark bytes left
static RTC_NOINIT_ATTR uint32_t haveTrace; // boolean


void resetCrashLoop() {
  crashLoop = 0;
}

void logIncrementDropCount(void) {
  Atomic_Increment_u32(&dropCount);
}

void saveRamLog(const char* ramLogName) {
  // save ramlog to storage 
  File ramFile = STORAGE.open(ramLogName, FILE_WRITE);
  int startPtr, endPtr;
  startPtr = endPtr = mlogEnd;  
  // write log in chunks
  do {
    int maxChunk = startPtr < endPtr ? endPtr - startPtr : RAM_LOG_LEN - startPtr;
    size_t chunkSize = std::min(CHUNKSIZE, maxChunk);    
    if (chunkSize > 0) ramFile.write((uint8_t*)messageLog + startPtr, chunkSize);
    startPtr += chunkSize;
    if (startPtr >= RAM_LOG_LEN) startPtr = 0;
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
#if !CONFIG_IDF_TARGET_ESP32C3
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

static void logTask(void *pvParams) {
  // separate task to reduce stack size in other tasks
  char *msg;
  while (true) {
    if (xQueueReceive(logQueue, &msg, portMAX_DELAY) == pdTRUE) {
      // logTask is sole consumer so safe without locking
      msg[MAX_OUT - 2] = '\n'; 
      msg[MAX_OUT - 1] = 0; // ensure always have ending newline
      // output message to various recipients
      size_t msgLen = strlen(msg);
      if (msgLen > 1) {
#ifdef AUXILIARY
        sendSSE("log", msg);
#else
        wsAsyncSendText(msg); // output to browser over web socket
#endif
        if (msg[msgLen - 2] == '~') msg[msgLen - 2] = ' '; // remove '~' if present
      }
      if (monitorOpen) {
        // output to monitor console if attached
        fputs(msg, stdout);
        fflush(stdout);
      } else delay(10); // allow time for other tasks
      if (msgLen > 1) {
        ramLogStore(msg, msgLen); // store in rtc ram 
        if (sdLog) {
          if (log_remote_fp != NULL) {
            // output to SD if file opened
            fwrite(msg, sizeof(char), msgLen, log_remote_fp); // log.txt
            // periodic sync to SD
            if (counter_write++ % WRITE_CACHE_CYCLE == 0) fsync(fileno(log_remote_fp));
          } 
        }
      }
      // return buffer to pool — use portMAX_DELAY as pool should
      // always have space (only send as many messages as pool slots)
      xQueueSend(logFreePool, &msg, portMAX_DELAY);
    }
  }
}

void logLine() {
  LOG_SEND(" \n");
}

int vprintfRedirect(const char* format, va_list args) {
  // format esp_log() output for LOG_SEND()
  char buffer[256];
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  LOG_SEND("%s", buffer);
  return len;
}

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
  for (int i = 0; i < info->backtrace_len; i++) backtrace[i] = info->backtrace[i];
  haveTrace = MAGIC_NUM; // flag that backtrace available
  esp_rom_delay_us(PANIC_DELAY * 1000 * 1000);
}

static void expandReason() {
  if (!strlen(btReason)) strcpy(btReason, "unknown");
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
    for (int i = 0; i < btLen; i++) 
      snprintf(bt + strlen(bt), sizeof(bt) - strlen(bt) - 11, "0x%08x ", (unsigned int)backtrace[i]); 
    LOG_WRN("Paste backtrace below into Arduino Exception Decoder:\n");
    LOG_SEND("Backtrace: %s\n\n", bt);
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

static void initBrownout(void) {
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

void logSetup() {
  // prep logging environment
  if (crashLoop == MAGIC_NUM) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Crash loop detected, check log %s", (brownoutStatus == 'B' || brownoutStatus == 'R') ? "(brownout)" : " ");
  crashLoop = MAGIC_NUM;
  if (logHandle == NULL) {
    set_arduino_panic_handler(appPanicHandler, NULL);
    Serial.begin(115200);
    Serial.setDebugOutput(DBG_ON);
    printf("\n\n");
    if (DEBUG_MEM) printf("init > Free: heap %lu\n", ESP.getFreeHeap()); 
    (DBG_ON) ? esp_log_level_set("*", DBG_LVL) : esp_log_level_set("*", ESP_LOG_NONE); // suppress esp log messages
    esp_log_set_vprintf(vprintfRedirect); // redirect esp_log output to app log

    UBaseType_t poolMem = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    poolBufs = (char (*)[MAX_OUT])heap_caps_malloc(LOG_BUF_COUNT * MAX_OUT, poolMem);
    if (!poolBufs) snprintf(startupFailure, SF_LEN, STARTUP_FAIL "Failed to alloc poolBufs");
    else {
      // Create pool queue from static storage
      logFreePool = xQueueCreateStatic(
          LOG_BUF_COUNT,
          sizeof(char*),
          freePoolStorage,
          &freePoolStatic
      );
      // Create log queue from static storage
      logQueue = xQueueCreateStatic(
          LOG_QUEUE_DEPTH,
          sizeof(char*),
          logQueueStorage,
          &logQueueStatic
      );
      // Populate the free pool
      for (int i = 0; i < LOG_BUF_COUNT; i++) {
          char *p = poolBufs[i];
          xQueueSend(logFreePool, &p, 0);
      }
      xTaskCreateWithCaps(logTask, "logTask", LOG_STACK_SIZE, NULL, LOG_PRI, &logHandle, STACK_MEM);
      
      if (mlogEnd >= RAM_LOG_LEN) ramLogClear(); // init
      LOG_SEND("\n\n=============== %s %s ===============\n", APP_NAME, APP_VER);
      LOG_INF("Setup RAM based log, size %u, starting from %u", RAM_LOG_LEN, mlogEnd);
      if (!DBG_ON) esp_log_level_set("*", ESP_LOG_ERROR); // show ESP_LOG_ERROR messages during init
      wakeupResetReason();
      initBrownout();
      boardInfo();
      debugMemory("logSetup");
    }
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

      LOG_SEND("\nTask stats interval %ds on %u cores\n", STATS_INTERVAL / 1000, CONFIG_FREERTOS_NUMBER_OF_CORES);
      LOG_SEND("\n| %-16s | %-10s | %-3s | %-4s | %-6s |\n", "Task name", "State", "Pri", "Core", "Core%");
      LOG_SEND("|------------------|------------|-----|------|--------|\n"); 
      // Match each task in start_array to those in the end_array
      for (int i = 0; i < statsArraySize; i++) {
        float percentage_time = ((float)statsArray[i].ulRunTimeCounter * 100.0) / runCounter;
        UBaseType_t coreId = statsArray[i].xCoreID;
        LOG_SEND("| %-16s | %-10s | %3u | %4c | %5.1f%% |\n", 
          statsArray[i].pcTaskName, getTaskStateString(statsArray[i].eCurrentState), (int)statsArray[i].uxCurrentPriority, coreId == tskNO_AFFINITY ? '*' : '0' + (int)coreId, percentage_time);
      }
      LOG_SEND("|------------------|------------|-----|------|--------|\n"); 
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
  LOG_INF("%s Free: heap %lu, block: %lu, min: %lu, pSRAM %lu", strlen(source) ? source : "Setup", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), ESP.getFreePsram());
  if (ESP.getFreeHeap() < WARN_HEAP) LOG_WRN("Free heap only %lu, min %lu", ESP.getFreeHeap(), ESP.getMinFreeHeap());
  if (ESP.getMaxAllocHeap() < WARN_ALLOC) LOG_WRN("Max allocatable heap block is only %lu", ESP.getMaxAllocHeap());
  if (!strlen(source) && DEBUG_MEM) runTaskStats();
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
  LOG_SEND("Assigned GPIO Info:\n");
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
    LOG_SEND("%s\n", gpioInf);
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
  LOG_SEND("%-12s %-6s %-12s %-10s %-12s %-10s", "Partition", "Type", "Subtype", "Address", "Size", "Encrypted\n");
  LOG_SEND("-----------------------------------------------------------------\n");

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
    LOG_SEND("%-12s %-6s %-12s 0x%08lX %-12s %-10s\n",
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
  LOG_SEND("%s v%s, arduino-esp32 v%s\n", APP_NAME, APP_VER, ESP_ARDUINO_VERSION_STR);
  logLine();
  printPartitionTable();
  logLine();
  printGpioInfo();
  logLine();
  runTaskStats(true);
  logLine();
  //gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);
}
