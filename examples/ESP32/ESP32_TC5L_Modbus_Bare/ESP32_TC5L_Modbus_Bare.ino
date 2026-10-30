/**
 * @file ESP32_TC5L_Modbus_Bare.ino
 * @brief Standalone Modbus RTU bench sketch for TC5-L (no WiFi, MQTT, or dtg library).
 *
 * Purpose: isolate ESP32 + Serial2 + RS485-TTL module from the IoT Platform stack.
 * Open Serial Monitor @ 115200 and compare TX/RX hex with Modbus Poll.
 *
 * Wiring (UART2 - match your board; many bench setups need TX=25 RX=14):
 *   ESP32 GPIO25 (TX) -> module DI / RXD
 *   ESP32 GPIO14 (RX) <- module RO / TXD
 *   ESP32 GND         -> module GND
 *   module A/B        -> TC5-L terminal 4 (A+) / 3 (B-)
 *
 * Test matrix (change #defines below, re-flash):
 *   MODBUS_PARITY_MODE 0 = 8N1 (USB Modbus default)
 *   MODBUS_PARITY_MODE 1 = 8E1 (many TC5-L units)
 *   MODBUS_PARITY_MODE 2 = 8O1
 *   Swap MODBUS_RX_PIN / MODBUS_TX_PIN if TTL TX/RX are reversed
 */
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Compile-time test knobs
// ---------------------------------------------------------------------------
#ifndef MODBUS_RX_PIN
#define MODBUS_RX_PIN 14
#endif
#ifndef MODBUS_TX_PIN
#define MODBUS_TX_PIN 25
#endif
#ifndef MODBUS_BAUD
#define MODBUS_BAUD 9600
#endif
#ifndef MODBUS_SLAVE
#define MODBUS_SLAVE 1
#endif
// 0 = 8N1, 1 = 8E1, 2 = 8O1
#ifndef MODBUS_PARITY_MODE
#define MODBUS_PARITY_MODE 0
#endif
#ifndef MODBUS_POLL_MS
#define MODBUS_POLL_MS 5000
#endif
#ifndef MODBUS_RX_TIMEOUT_MS
#define MODBUS_RX_TIMEOUT_MS 500
#endif
// Gap after TX before listening (auto-direction RS485 modules)
#ifndef MODBUS_TURNAROUND_US
#define MODBUS_TURNAROUND_US 2000
#endif
#ifndef MODBUS_DECIMAL_PLACES
// Match TC5 panel [dP]: 0=factory default, 1=one decimal (25.5 -> raw 255)
#define MODBUS_DECIMAL_PLACES 0
#endif
#ifndef MODBUS_SCAN_ADDR_MAX
#define MODBUS_SCAN_ADDR_MAX 4
#endif

static constexpr size_t kTxBufLen = 8;
static constexpr size_t kRxBufLen = 64;

static uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

static int serialModeFromParity() {
#if MODBUS_PARITY_MODE == 1
  return SERIAL_8E1;
#elif MODBUS_PARITY_MODE == 2
  return SERIAL_8O1;
#else
  return SERIAL_8N1;
#endif
}

static const char *parityLabel() {
#if MODBUS_PARITY_MODE == 1
  return "8E1";
#elif MODBUS_PARITY_MODE == 2
  return "8O1";
#else
  return "8N1";
#endif
}

static void printHexLine(const char *prefix, const uint8_t *buf, size_t len) {
  Serial.print(prefix);
  if (len == 0) {
    Serial.println("(0 bytes)");
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    if (buf[i] < 0x10)
      Serial.print('0');
    Serial.print(buf[i], HEX);
    if (i + 1 < len)
      Serial.print(' ');
  }
  Serial.println();
}

static void drainRx() {
  while (Serial2.available())
    (void)Serial2.read();
}

static size_t buildReadHoldingRequest(uint8_t *out, uint8_t slave, uint16_t address,
                                      uint16_t quantity) {
  out[0] = slave;
  out[1] = 0x03;
  out[2] = (uint8_t)(address >> 8);
  out[3] = (uint8_t)(address & 0xFF);
  out[4] = (uint8_t)(quantity >> 8);
  out[5] = (uint8_t)(quantity & 0xFF);
  const uint16_t crc = modbusCrc16(out, 6);
  out[6] = (uint8_t)(crc & 0xFF);
  out[7] = (uint8_t)(crc >> 8);
  return 8;
}

static size_t expectedModbusRtuLen(const uint8_t *buf, size_t count) {
  if (count < 2)
    return 0;
  const uint8_t fc = buf[1];
  if (fc & 0x80)
    return 5;
  if (count < 3)
    return 0;
  if (fc == 0x03 || fc == 0x04 || fc == 0x01 || fc == 0x02)
    return 3u + (size_t)buf[2] + 2u;
  return 0;
}

static size_t readResponse(uint8_t *out, size_t maxLen, uint32_t timeoutMs) {
  uint32_t idleDeadline = millis() + timeoutMs;
  size_t count = 0;
  size_t need = 0;

  while ((int32_t)(millis() - idleDeadline) < 0) {
    while (Serial2.available() && count < maxLen) {
      out[count++] = (uint8_t)Serial2.read();
      idleDeadline = millis() + 80;
      need = expectedModbusRtuLen(out, count);
    }
    if (need > 0 && count >= need)
      break;
    delay(1);
  }

  uint32_t tailDeadline = millis() + 30;
  while ((int32_t)(millis() - tailDeadline) < 0 && Serial2.available() && count < maxLen) {
    out[count++] = (uint8_t)Serial2.read();
    tailDeadline = millis() + 30;
  }
  return count;
}

static bool verifyResponseCrc(const uint8_t *buf, size_t len) {
  if (len < 4)
    return false;
  const uint16_t rxCrc = (uint16_t)buf[len - 2] | ((uint16_t)buf[len - 1] << 8);
  const uint16_t calc = modbusCrc16(buf, len - 2);
  return rxCrc == calc;
}

static void printScaledVariants(int16_t raw) {
  Serial.print("CRC: OK  raw=");
  Serial.print(raw);
  Serial.print(" | dP0=");
  Serial.print(raw);
  Serial.print(" dP1=");
  Serial.print(raw / 10.0f, 1);
  Serial.print(" dP2=");
  Serial.print(raw / 100.0f, 2);
  Serial.println();
}

static const char *tc5RegisterHint(uint16_t addr) {
  switch (addr) {
  case 0:
    return "PV (measured temp) - Sinny 0000H / 40001";
  case 1:
    return "SV (setpoint) - many TC5 firmware - try match screen";
  case 2:
    return "Often MV/output% or status - NOT SV on many units";
  case 3:
    return "Alarm/status (varies by firmware)";
  default:
    return "See COM_ZH_S_231026_INST register map";
  }
}

static bool decodeHoldingValue(const uint8_t *buf, size_t len, int16_t &valueOut) {
  if (len < 7)
    return false;
  if (buf[0] != MODBUS_SLAVE)
    return false;
  if (buf[1] == 0x83)
    return false;
  if (buf[1] != 0x03)
    return false;
  if (buf[2] != 2)
    return false;
  if (!verifyResponseCrc(buf, len))
    return false;
  valueOut = (int16_t)((buf[3] << 8) | buf[4]);
  return true;
}

static void pollRegister(uint16_t address) {
  uint8_t tx[kTxBufLen];
  uint8_t rx[kRxBufLen];

  const size_t txLen = buildReadHoldingRequest(tx, MODBUS_SLAVE, address, 1);

  drainRx();
  Serial.println();
  Serial.print("[poll] FC03 addr=");
  Serial.print(address);
  Serial.print(" - ");
  Serial.println(tc5RegisterHint(address));

  printHexLine("TX: ", tx, txLen);
  Serial2.write(tx, txLen);
  Serial2.flush();
  delayMicroseconds(MODBUS_TURNAROUND_US);

  const size_t rxLen = readResponse(rx, kRxBufLen, MODBUS_RX_TIMEOUT_MS);
  printHexLine("RX: ", rx, rxLen);

  int16_t raw = 0;
  if (decodeHoldingValue(rx, rxLen, raw)) {
    printScaledVariants(raw);
  } else if (rxLen > 0 && rx[1] == 0x83) {
    Serial.print("MODBUS exception code=");
    Serial.println(rx[2]);
  } else if (rxLen > 0) {
    Serial.print("CRC: FAIL or incomplete frame (got ");
    Serial.print(rxLen);
    Serial.print(" bytes, need ");
    Serial.print(expectedModbusRtuLen(rx, rxLen));
    Serial.println(")");
  } else {
    Serial.println("RESULT: timeout (0 bytes) - check module power, A/B, TTL TX/RX");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial2.begin(MODBUS_BAUD, serialModeFromParity(), MODBUS_RX_PIN, MODBUS_TX_PIN);
  drainRx();

  Serial.println();
  Serial.println("=== ESP32 TC5-L Modbus Bare (no IoT Platform) ===");
  Serial.print("UART2 RX=");
  Serial.print(MODBUS_RX_PIN);
  Serial.print(" TX=");
  Serial.print(MODBUS_TX_PIN);
  Serial.print(" baud=");
  Serial.print(MODBUS_BAUD);
  Serial.print(" parity=");
  Serial.print(parityLabel());
  Serial.print(" slave=");
  Serial.println(MODBUS_SLAVE);
  Serial.println("Scan addr 0..N - match raw/dP* with TC5 screen (panel [dP] sets decimal)");
  Serial.println("SV is often addr 1 (40002), NOT addr 2. Addr 2 may be output %.");
}

void loop() {
  for (uint16_t addr = 0; addr <= MODBUS_SCAN_ADDR_MAX; ++addr) {
    pollRegister(addr);
    delay(200);
  }
  delay(MODBUS_POLL_MS);
}
