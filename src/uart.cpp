// Uart interface to communicate between client ESP and auxiliary ESP
//
// Connect auxiliary UART_TXD_PIN pin to client UART_RXD_PIN pin
// Connect auxiliary UART_RXD_PIN pin to client UART_TXD_PIN pin
// Also connect a common GND
// The UART id and pins used are defined using the web page
//
// Frame format: [0x55][len][payload x len][crc8 over len+payload][0xAA]
//
// - call initUart() during setup
// - to send over uart:
//   - call uartTxPayload() with pointer to payload array 
//     - queues payload to uartTxTask() (1-slot queue, newest unsent payload wins)
//     - uartTxTask() calls writeFrame()
//       - writeFrame() calls serializePacket() to build the frame
//         - serializePacket() calls calculateCrc8()
//       - writeFrame() writes the frame to the uart (mutex protected)
// - to receive over uart:
//   - uartRxTask() wakes on uart events (or a 20 ms idle timeout)
//     - drains every buffered byte through parserFeed()
//       - parserFeed() calls calculateCrc8() to check the CRC
//     - heartbeat frames are handled in place and never published
//     - any other valid payload (with its length) is published to rx_msg_queue
//       (1-slot queue, overwrite: the consumer always sees the newest payload)
//   - payloadConsumerTask() blocks on rx_msg_queue and calls app specific function 
//
// Heartbeat (runs inside uartRxTask, no extra task):
// - client sends ping [HB_PING][seq] every HB_INTERVAL_MS
// - auxiliary replies with pong [HB_PONG][seq]
// - on both sides, any valid frame counts as proof of life
// - link is down if no valid frame received for HB_TIMEOUT_MS
// - first payload bytes HB_PING and HB_PONG are reserved, do not use in application payloads
// - call uartLinkUp() to check link status
//
// Error handling: on FIFO overflow, ring buffer full, framing/parity errors or
// break, the input is flushed and the parser is reset so it hunts for a new 0x55.
// An idle gap of RX_IDLE_TIMEOUT_MS in the middle of a frame also resets the parser.
//
//
// s60sc 2022, 2024, 2026

#include "appGlobals.h"

#if INCLUDE_UART
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 1, 0)
#error uart.cpp must be compiled with arduino-esp32 core v3.1.0 or higher
#endif
#include "driver/uart.h"

#define BUF_SIZE 256
#define PAYLOAD_MAX_LEN 32
#define FRAME_START_BYTE 0x55
#define FRAME_END_BYTE 0xAA
#define RX_IDLE_TIMEOUT_MS 20
#define UART_BAUD_RATE 921600

// Heartbeat
#define HB_INTERVAL_MS 1000
#define HB_TIMEOUT_MS 3000   // should be at least 2-3 x HB_INTERVAL_MS

// UART connection for Auxiliary
int uartTxdPin;
int uartRxdPin;
static uart_port_t uartId;

typedef struct {
  uint8_t data[PAYLOAD_MAX_LEN];
  uint8_t length;
} tx_payload_t;

typedef struct {
  uint8_t data[PAYLOAD_MAX_LEN];
  uint8_t length;
} rx_msg_t;

// Communication structures
static QueueHandle_t uart_event_queue = NULL;
static QueueHandle_t tx_packet_queue = NULL;  // depth 1, written with xQueueOverwrite
static QueueHandle_t rx_msg_queue = NULL;     // depth 1, written with xQueueOverwrite
static SemaphoreHandle_t uartTxMutex = NULL;  // serialises uart writes from uartTxTask and uartRxTask

// Heartbeat state. Only uartRxTask() writes these.
static volatile bool linkUp = false;
static volatile uint32_t lastRxMs = 0;  // time of last valid frame
static uint8_t hbSeq = 0;               // client: last ping sequence number
static uint32_t hbSentMs = 0;           // client: time last ping was sent
static uint32_t hbRttMs = 0;            // client: last measured round trip time

#define CHECK_ALLOC(ptr) ESP_ERROR_CHECK((ptr) != NULL ? ESP_OK : ESP_ERR_NO_MEM)

static uint8_t calculateCrc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x80) crc = (crc << 1) ^ 0x31;
      else crc <<= 1;
    }
  }
  return crc;
}

static size_t serializePacket(uint8_t *buffer, const uint8_t* payload, uint8_t payload_len) {
  if (payload_len == 0 || payload_len > PAYLOAD_MAX_LEN) return 0;
  buffer[0] = FRAME_START_BYTE;
  buffer[1] = payload_len;
  memcpy(&buffer[2], payload, payload_len);
  buffer[2 + payload_len] = calculateCrc8(&buffer[1], (size_t)payload_len + 1);
  buffer[3 + payload_len] = FRAME_END_BYTE;
  return (size_t)4 + payload_len;
}

// Builds a frame and writes it to the uart. Called from uartTxTask() and uartRxTask().
// With a 0-byte driver TX buffer uart_write_bytes() blocks until every byte is in the
// hardware FIFO, so no extra uart_wait_tx_done() is needed. The mutex stops frames
// from the two tasks interleaving.
static void writeFrame(const uint8_t *payload, uint8_t len) {
  uint8_t wire_frame[PAYLOAD_MAX_LEN + 4];
  size_t bytes_to_send = serializePacket(wire_frame, payload, len);
  if (bytes_to_send == 0 || uartTxMutex == NULL) return;
  xSemaphoreTake(uartTxMutex, portMAX_DELAY);
  int bytes_written = uart_write_bytes(uartId, (const void *)wire_frame, bytes_to_send);
  xSemaphoreGive(uartTxMutex);
  if (bytes_written < 0) {
    LOG_ERR("Driver error: TX failed completely");
  } else if ((size_t)bytes_written < bytes_to_send) {
    LOG_WRN("Short TX write: %u of %u", (unsigned)bytes_written, (unsigned)bytes_to_send);
  }
}

// --------------------------------------------------------------------
// Frame parser. State is file-static, so it is NOT reentrant: only
// uartRxTask() may call parserFeed()/parserReset()/parserIsIdle().
// --------------------------------------------------------------------
typedef enum { STATE_START, STATE_LEN, STATE_PAYLOAD, STATE_CRC, STATE_END } parser_state_t;

static parser_state_t p_state = STATE_START;
static uint8_t p_crc_buffer[PAYLOAD_MAX_LEN + 1];
static uint8_t p_payload_index = 0;
static uint8_t p_expected_len = 0;
static uint8_t p_received_crc = 0;

static void parserReset(void) {
  p_state = STATE_START;
  p_payload_index = 0;
  p_expected_len = 0;
  p_received_crc = 0;
}

static bool parserIsIdle(void) {
  return p_state == STATE_START;
}

// Feeds one byte. Writes payload bytes into msg->data as they arrive (before the
// CRC is known), so msg must only be used when this returns true.
static bool parserFeed(uint8_t incoming, rx_msg_t *msg) {
  switch (p_state) {
    case STATE_START:
      if (incoming == FRAME_START_BYTE) p_state = STATE_LEN;
    break;
    case STATE_LEN:
      if (incoming > 0 && incoming <= PAYLOAD_MAX_LEN) {
        p_expected_len = incoming;
        p_crc_buffer[0] = incoming;
        p_payload_index = 0;
        p_state = STATE_PAYLOAD;
      } else if (incoming == FRAME_START_BYTE) {
        // Back-to-back start bytes: this one may be the real start, stay put
      } else {
        p_state = STATE_START;
      }
    break;
    case STATE_PAYLOAD:
      msg->data[p_payload_index] = incoming;
      p_crc_buffer[1 + p_payload_index] = incoming;
      p_payload_index++;
      if (p_payload_index >= p_expected_len) p_state = STATE_CRC;
    break;
    case STATE_CRC:
      p_received_crc = incoming;
      p_state = STATE_END;
    break;
    case STATE_END:
      p_state = STATE_START;
      if (incoming == FRAME_END_BYTE) {
        uint8_t calculated_crc = calculateCrc8(p_crc_buffer, (size_t)p_expected_len + 1);
        if (calculated_crc == p_received_crc) {
          msg->length = p_expected_len;
          return true;
        }
        LOG_WRN("CRC mismatch");
      } else if (incoming == FRAME_START_BYTE) {
        // Bad end byte, but this byte may begin the next frame: resync on it
        p_state = STATE_LEN;
      }
    break;
  }
  return false;
}

// --------------------------------------------------------------------
// TX
// --------------------------------------------------------------------
static void uartTxTask(void *pvParameters) {
  tx_payload_t OutgoingPacket;
  while (1) {
    if (xQueueReceive(tx_packet_queue, &OutgoingPacket, portMAX_DELAY) == pdTRUE)
      writeFrame(OutgoingPacket.data, OutgoingPacket.length);
  }
}

// --------------------------------------------------------------------
// Heartbeat
// --------------------------------------------------------------------

// Called from uartRxTask() only, so the heartbeat state needs no locking.
// Client: sends a ping every HB_INTERVAL_MS.
// Both roles: update link status and log transitions.
static void heartbeatTick() {
  uint32_t now = millis();

  if (now - hbSentMs >= HB_INTERVAL_MS) {
    hbSentMs = now;
    uint8_t ping[2] = { HB_PING, ++hbSeq };
    writeFrame(ping, sizeof(ping));
  }

  bool up = (now - lastRxMs) < HB_TIMEOUT_MS;
  if (up != linkUp) {
    linkUp = up;
    if (up) LOG_INF("UART link UP (rtt %lu ms)", (unsigned long)hbRttMs);
    else LOG_WRN("UART link DOWN: no frames for %d ms", HB_TIMEOUT_MS);
  }
}

// True if a valid frame was received within HB_TIMEOUT_MS (client and auxiliary)
bool uartLinkUp(void) {
  return linkUp;
}

// --------------------------------------------------------------------
// RX
// --------------------------------------------------------------------

// Reads everything currently buffered in the driver and run it through the parser.
// Draining by buffered length (not event.size) keeps in sync even if the driver
// dropped earlier UART_DATA events. Returns the number of bytes consumed.
static size_t drainRxBuffer(rx_msg_t *msg) {
  static uint8_t rx_chunk[BUF_SIZE];  // static: keeps it off the task stack
  size_t total = 0;
  while (1) {
    size_t buffered = 0;
    if (uart_get_buffered_data_len(uartId, &buffered) != ESP_OK || buffered == 0) break;
    if (buffered > sizeof(rx_chunk)) buffered = sizeof(rx_chunk);

    int n = uart_read_bytes(uartId, rx_chunk, (uint32_t)buffered, 0);  // data is already there
    if (n <= 0) break;
    total += (size_t)n;

    for (int i = 0; i < n; i++) {
      if (parserFeed(rx_chunk[i], msg)) {
        lastRxMs = millis();  // any valid frame is proof of life
        if (msg->length == 2 && msg->data[0] == HB_PING) {
          // echo the sequence number back
          uint8_t pong[2] = { HB_PONG, msg->data[1] };
          writeFrame(pong, sizeof(pong));
        } else if (msg->length == 2 && msg->data[0] == HB_PONG) {
          // pong for the latest ping gives the round trip time
          if (msg->data[1] == hbSeq) hbRttMs = lastRxMs - hbSentMs;
        } else {
          LOG_VRB("Valid packet received! Length: %u", (unsigned)msg->length);
          xQueueOverwrite(rx_msg_queue, msg);  // latest wins; consumer gets data + length
        }
      }
    }
  }
  return total;
}

static void uartRxTask(void *pvParameters) {
  uart_event_t event;
  rx_msg_t msg;
  memset(&msg, 0, sizeof(msg));
  parserReset();

  // start with link down, first ping goes out immediately
  lastRxMs = millis() - HB_TIMEOUT_MS;
  hbSentMs = millis() - HB_INTERVAL_MS;
  while (1) {
    if (xQueueReceive(uart_event_queue, (void *)&event, pdMS_TO_TICKS(RX_IDLE_TIMEOUT_MS)) == pdTRUE) {
      switch (event.type) {
        case UART_DATA:
          drainRxBuffer(&msg);
        break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
          LOG_WRN("RX overflow (type %d), resyncing", (int)event.type);
          uart_flush_input(uartId);
          xQueueReset(uart_event_queue);
          parserReset();
        break;
        case UART_FRAME_ERR:
        case UART_PARITY_ERR:
        case UART_BREAK:
          LOG_VRB("RX line error (type %d), resyncing", (int)event.type);
          uart_flush_input(uartId);
          xQueueReset(uart_event_queue);
          parserReset();
        break;
        default:
        break;
      }
    } else {
      // Idle timeout. Catch any bytes whose UART_DATA event was dropped; if the
      // line is truly silent and we are mid-frame, abandon the partial frame.
      if (drainRxBuffer(&msg) == 0 && !parserIsIdle()) {
        LOG_WRN("RX idle mid-frame, parser reset");
        parserReset();
      }
    }
#ifdef AUXILIARY
  heartbeatTick();  // also runs during traffic
#endif
  }
}

// ====================================================================
// CONSUMER TASK: blocks until a valid payload is published, always sees the newest
// ====================================================================

static void payloadConsumerTask(void *pvParameters) {
  // call app specific handlers to process request and provide response
  rx_msg_t local;
  while (1) {
    if (xQueueReceive(rx_msg_queue, &local, portMAX_DELAY) == pdTRUE) {
#ifdef AUXILIARY
    // auxiliary device processes request and responds if required
      auxProcess(local.data, local.length);
#else
      // client device processes any response from auxiliary
      auxResponse(local.data, local.length);
#endif
    }
  }
}

// ====================================================================
// Public API
// ====================================================================
void prepUart(void) {
  // setup uart if Auxiliary being used
  if (useUart) {
    if (uartTxdPin && uartRxdPin) {
      LOG_INF("Prepare UART on pins Tx %d, Rx %d", uartTxdPin, uartRxdPin);

      uart_config_t uart_config = {};
      uart_config.baud_rate = UART_BAUD_RATE;
      uart_config.data_bits = UART_DATA_8_BITS;
      uart_config.parity = UART_PARITY_DISABLE;
      uart_config.stop_bits = UART_STOP_BITS_1;
      uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
      uart_config.source_clk = UART_SCLK_DEFAULT;

      // install the driver and configure pins
#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32S2
      uartId = UART_NUM_1;
#else // ESP32, ESP32S3 (UART_NUM_1 default pins on ESP32 clash with flash)
      uartId = UART_NUM_2;
#endif

      // 0-byte TX buffer: uart_write_bytes() blocks until the data is in the HW FIFO
      esp_err_t res = uart_driver_install(uartId, BUF_SIZE, 0, 20, &uart_event_queue, 0);
      if (res == ESP_OK) res = uart_param_config(uartId, &uart_config);
      if (res == ESP_OK) res = uart_set_pin(uartId, uartTxdPin, uartRxdPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
      // Create all queues and the mutex before any task that uses them starts
      tx_packet_queue = xQueueCreate(1, sizeof(tx_payload_t));
      CHECK_ALLOC(tx_packet_queue);
      rx_msg_queue = xQueueCreate(1, sizeof(rx_msg_t));
      CHECK_ALLOC(rx_msg_queue);
      uartTxMutex = xSemaphoreCreateMutex();
      CHECK_ALLOC(uartTxMutex);

      // Stack sizes are in bytes on ESP-IDF. 
      // uartRx runs the heartbeat and writeFrame(), so needs a more than a pure receive task
      BaseType_t ok;
      ok = xTaskCreate(uartRxTask, "uartRx", 2560, NULL, 5, NULL);
      ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
      ok = xTaskCreate(uartTxTask, "uartTx", 2048, NULL, 5,  NULL);
      ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
      ok = xTaskCreate(payloadConsumerTask, "msgConsumer", 2048, NULL, 5,  NULL);
      ESP_ERROR_CHECK(ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    } else LOG_WRN("At least one uart pin not defined");
  }
}

// Queues a payload for transmission. Returns false on bad arguments or if not initialised.
// NOTE: the TX queue has depth 1 and uses overwrite, so an unsent older payload is
// replaced by a newer one. That suits state/telemetry; for commands that must all
// arrive, use a deeper queue with xQueueSend() and return its result instead.
// The first payload byte must not be HB_PING or HB_PONG (reserved for the heartbeat).
bool uartTxPayload(const uint8_t *payload, uint8_t length) {
  if (payload == NULL || length == 0 || length > PAYLOAD_MAX_LEN || tx_packet_queue == NULL) return false;
  tx_payload_t item;
  item.length = length;
  memcpy(item.data, payload, length);
  xQueueOverwrite(tx_packet_queue, &item);
  return true;
}

#endif // INCLUDE_UART
