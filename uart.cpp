
// Use Uart interface to communicate between client ESP and auxiliary ESP
// to support peripherals that cannot be hosted by client
//
// Connect auxiliary UART_TXD_PIN pin to client UART_RXD_PIN pin
// Connect auxiliary UART_RXD_PIN pin to client UART_TXD_PIN pin
// Also connect a common GND
// The UART id and pins used are defined using the web page
//
// The data exchanged consists of 8 bytes:
// - 2 byte fixed header
// - 1 byte command char
// - 4 bytes are data of any type that fits in 32 bits or less
// - 1 byte checksum
//
// Callbacks:
// - setOutputPeripheral(): on Auxiliary, convert uint32_t data read from uart into appropriate output peripheral data type and write to peripheral
// - getInputPeripheral(): on Auxiliary, read input peripheral and convert input data type to uint32_t to send over uart
// - setInputPeripheral(): on client, convert uint32_t read from UART to input status data type
//
// s60sc 2022, 2024

#include "appGlobals.h"

#if INCLUDE_UART
#include "driver/uart.h"

// UART pins
#define UART_RTS UART_PIN_NO_CHANGE
#define UART_CTS UART_PIN_NO_CHANGE

#define UART_BAUD_RATE 115200
#define BUFF_LEN UART_FIFO_LEN * 2
#define MSG_LEN 8 

// UART connection for Auxiliary
int uartTxdPin;
int uartRxdPin;

TaskHandle_t uartRxHandle = NULL;
static QueueHandle_t uartQueue = NULL;
static SemaphoreHandle_t responseMutex = NULL;
static SemaphoreHandle_t writeMutex = NULL;
static uart_event_t uartEvent;
static byte uartBuffTx[BUFF_LEN];
static byte uartBuffRx[BUFF_LEN];
static const char* uartErr[] = {"FRAME_ERR", "PARITY_ERR", "UART_BREAK", "DATA_BREAK",
  "BUFFER_FULL", "FIFO_OVF", "UART_DATA", "PATTERN_DET", "EVENT_MAX"};
static const uint16_t header = 0x55aa; 
static int uartId;

static bool readUart() {
  // Read data from the UART when available 
  // wait until event occurs
  if (xQueueReceive(uartQueue, (void*)&uartEvent, (TickType_t)portMAX_DELAY)) { 
    if (uartEvent.type != UART_DATA) {
      xQueueReset(uartQueue);
      uart_flush_input(uartId);
      LOG_WRN("Unexpected uart event type: %s", uartErr[uartEvent.type]);
      delay(1000);
      return false;
    } else {
      // uart rx data available, wait till have full message
      int msgLen = 0;
      while (msgLen < MSG_LEN) {
        uart_get_buffered_data_len(uartId, (size_t*)&msgLen);
        delay(10);
      }
      heartBeatDone = true; // implied heartbeat
      msgLen = uart_read_bytes(uartId, uartBuffRx, msgLen, pdMS_TO_TICKS(20));
      uint16_t* rxPtr = (uint16_t*)uartBuffRx;
      if (rxPtr[0] != header) {
        // ignore data that received from client when it reboots if using UART0
        return false;
      } 
      // valid message header, check if content ok
      byte checkSum = 0; // checksum is modulo 256 of data content summation 
      for (int i = 0; i < MSG_LEN - 1; i++) checkSum += uartBuffRx[i];
      if (checkSum != uartBuffRx[MSG_LEN - 1]) {
        LOG_WRN("Invalid message ignored, got checksum %02x, expected %02x", uartBuffRx[MSG_LEN - 1], checkSum);
        return false;
      }
    }
  }
  return true;
}

bool writeUart(uint8_t cmd, uint32_t outputData) {
  // prep and write request to uart
  xSemaphoreTake(writeMutex, portMAX_DELAY);
  // load uart TX buffer with peripheral data to send
  memcpy(uartBuffTx, &header, 2);
  uartBuffTx[2] = cmd;
  memcpy(uartBuffTx + 3, &outputData, 4);
  uartBuffTx[MSG_LEN - 1] = 0; // checksum is modulo 256 of data content summation
  for (int i = 0; i < MSG_LEN - 1; i++) uartBuffTx[MSG_LEN - 1] += uartBuffTx[i];  
  bool res = uart_write_bytes(uartId, uartBuffTx, MSG_LEN) > 0 ? true : false;
  xSemaphoreGive(writeMutex);
  return res;
}

static bool configureUart() { 
  // Configure parameters of UART driver  
  uart_config_t uart_config = {
    .baud_rate = UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .rx_flow_ctrl_thresh = 122,
#if CONFIG_IDF_TARGET_ESP32
    .source_clk = UART_SCLK_REF_TICK,
#endif
  };
  
  // install the driver and configure pins
#if CONFIG_IDF_TARGET_ESP32C3 
  uartId = UART_NUM_1;
#else // ESP32, ESP32S3
  uartId = UART_NUM_2;
#endif
  esp_err_t res = uart_driver_install(uartId, BUFF_LEN, BUFF_LEN, 20, &uartQueue, 0);
  if (res == ESP_OK) res = uart_param_config(uartId, &uart_config);
  if (res == ESP_OK) res = uart_set_pin(uartId, uartTxdPin, uartRxdPin, UART_RTS, UART_CTS);
  if (res != ESP_OK) LOG_WRN("UART config failed: %s", espErrMsg(res));
  return (res == ESP_OK) ? true : false;
}

static void uartRxTask(void *arg) {
  // used by auxiliary to receive data from uart
  while (true) {
    // wait for response to previous request to be processed 
    xSemaphoreTake(responseMutex, portMAX_DELAY); 
    if (readUart()) {
      // update given peripheral status
      uint32_t receivedData;
      memcpy(&receivedData, uartBuffRx + 3, 4); // response data (if relevant)
#ifdef AUXILIARY
      // try output request
      if (!setOutputPeripheral(uartBuffRx[2], receivedData)) {
        // try input request    
        int receivedData = getInputPeripheral(uartBuffRx[2]); // cmd 
        // write response to client
        if (receivedData >= 0) writeUart(uartBuffRx[2], (uint32_t)receivedData); // cmd, data
      }
#else
      // client, process received input
      setInputPeripheral(uartBuffRx[2], receivedData);
#endif
    }
    xSemaphoreGive(responseMutex);
  }
}

void prepUart() {
  // setup uart if Auxiliary being used
  if (useUart) {
    if (uartTxdPin && uartRxdPin) {
      LOG_INF("Prepare UART on pins Tx %d, Rx %d", uartTxdPin, uartRxdPin);
      responseMutex = xSemaphoreCreateMutex();
      writeMutex = xSemaphoreCreateMutex();
      if (configureUart()) {
#ifdef USE_UARTTASK
        xSemaphoreTake(responseMutex, portMAX_DELAY);
        xTaskCreate(uartRxTask, "uartRxTask", UART_STACK_SIZE, NULL, UART_PRI, &uartRxHandle);
#endif
        xSemaphoreGive(responseMutex);
        xSemaphoreGive(writeMutex);
      }
    } else LOG_WRN("At least one uart pin not defined");
  } 
}

#endif
