
// Use Uart interface to communicate between client ESP and IO extender ESP
// to allow IO extender to provide additional pins to client
// to support peripherals defined in peripherals.cpp
//
// Connect extender UART_TXD_PIN pin to client UART_RXD_PIN pin
// Connect extender UART_RXD_PIN pin to client UART_TXD_PIN pin
// Also connect a common GND
// The UART id and pins used are defined using the web page
// If UART0 is used, the Arduino serial monitor is not available
// use the web monitor instead.
//
// The data exchanged consists of 8 bytes:
// - 2 byte fixed header
// - 1 byte pin number, 
// - 4 bytes are data of any type that fits in 32 bits or less
// - 1 byte checksum
//
// s60sc 2022

#include "appGlobals.h"
#include "driver/uart.h"

// UART pins
#define UART_RTS UART_PIN_NO_CHANGE
#define UART_CTS UART_PIN_NO_CHANGE

#define UART_BAUD_RATE 115200
#define BUFF_LEN UART_FIFO_LEN * 2
#define MSG_LEN 8 

TaskHandle_t uartClientHandle = NULL;
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
      msgLen = uart_read_bytes(uartId, uartBuffRx, msgLen, 20 / portTICK_PERIOD_MS);
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

static void writeUart() {
  // prep and write request or response data to other device
  memcpy(uartBuffTx, &header, 2);
  uartBuffTx[MSG_LEN - 1] = 0; // checksum is modulo 256 of data content summation 
  for (int i = 0; i < MSG_LEN - 1; i++) uartBuffTx[MSG_LEN - 1] += uartBuffTx[i]; 
  uart_write_bytes(uartId, uartBuffTx, MSG_LEN);
}

static void configureUart() { 
  // Configure parameters of UART driver
  if (useUART0) {
    uartId = UART_NUM_0;
    // disable serial monitor
    LOG_INF("detach UART0 from serial monitor");
    delay(100);
    monitorOpen = false;
    esp_log_level_set("*", ESP_LOG_NONE);
    uart_driver_delete(UART_NUM_0);
  } else uartId = UART_NUM_1;
  
  uart_config_t uart_config = {
    .baud_rate = UART_BAUD_RATE,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
#if CONFIG_IDF_TARGET_ESP32
    .source_clk = UART_SCLK_REF_TICK,
#endif
  };
  
  // install the driver and configure pins
  uart_driver_install(uartId, BUFF_LEN, BUFF_LEN, 20, &uartQueue, 0);
  uart_param_config(uartId, &uart_config);
  uart_set_pin(uartId, uartTxdPin, uartRxdPin, UART_RTS, UART_CTS);
}

static void getExtenderResponse() {
  // client gets extender response for peripheral and actions
  if (readUart()) {
    // update given peripheral status
    uint32_t responseData;
    memcpy(&responseData, uartBuffRx + 3, 4); // response data (if relevant)
    setPeripheralResponse(uartBuffRx[2], responseData);
  }
}

void uartClientTask(void *arg) {
  // task for client, eg MJPEG2SD
  delay(2000); // time to complete startup
  configureUart();
  while (true) {
    // woken by request to get external peripheral response
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // wait for response to previous request to be processed 
    xSemaphoreTake(responseMutex, portMAX_DELAY); 
    getExtenderResponse();
    xSemaphoreGive(responseMutex);
  }
}

bool externalPeripheral(byte pinNum, uint32_t outputData) {
  // used by client to communicate with external peripheral
  if (pinNum >= EXTPIN) {
    if (useIOextender && !IS_IO_EXTENDER ) {
      xSemaphoreTake(writeMutex, portMAX_DELAY);
      // load uart TX buffer with peripheral data to send
      uartBuffTx[2] = pinNum;
      memcpy(uartBuffTx + 3, &outputData, 4);
      writeUart();
      xSemaphoreGive(writeMutex);
      // wake uartClientTask to get response from IO Extender
      if (uartClientHandle != NULL) xTaskNotifyGive(uartClientHandle);
      return true;
    } else LOG_WRN("IO Extender not enabled for external pin %i", pinNum);
  }
  return false;
}

void getPeripheralsRequest() {
  // used by IO Extender to receive peripheral request from client
  if (uartQueue == NULL) {
    LOG_ERR("Interface UART not defined");
    delay(30000); // allow time for user to rectify
  } else {
    if (readUart()) { 
      // client data arrived, loaded into uartBuffRx
      uint32_t receivedData;
      memcpy(&receivedData, uartBuffRx + 3, 4);
      // interact with peripheral, supplying any data and receiving response
      uint32_t responseData = usePeripheral(uartBuffRx[2] - EXTPIN, receivedData); 
      // write response to client
      uartBuffTx[2] = uartBuffRx[2];
      memcpy(uartBuffTx + 3, &responseData, 4);
      writeUart();
    }
    delay(10);
  }
}

void prepUart() {
  // setup uart if IO_Extender being used, or if this is IO_Extender
  if (useIOextender || IS_IO_EXTENDER) {
    if (uartTxdPin && uartRxdPin) {
      LOG_INF("Prepare IO Extender");
      responseMutex = xSemaphoreCreateMutex();
      writeMutex = xSemaphoreCreateMutex();
      if (!IS_IO_EXTENDER) xTaskCreate(uartClientTask, "uartClientTask", 2048, NULL, 1, &uartClientHandle);
      else configureUart();
      xSemaphoreGive(responseMutex);
      xSemaphoreGive(writeMutex);
    } else {
      useIOextender = false;
      LOG_WRN("At least one uart pin not defined");
    }
  } 
}
