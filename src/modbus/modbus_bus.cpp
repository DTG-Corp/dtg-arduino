#include "modbus_bus.h"

#include <ModbusClientRTU.h>
#include <ModbusError.h>

#include "../dtg_config.h"
#include "../dtg_debug.h"

namespace detail {
namespace {

void setBusError(char *dst, const char *msg) {
  if (!dst)
    return;
  strncpy(dst, msg ? msg : "", DTG_MODBUS_ERROR_MAX - 1);
  dst[DTG_MODBUS_ERROR_MAX - 1] = '\0';
}

#if DTG_MODBUS_TRACE
void logHexTrace(const char *prefix, const uint8_t *data, size_t len) {
  if (!data || len == 0) {
    DTG_LOG("Modbus %s (0 bytes)", prefix);
    return;
  }
  char buf[192];
  size_t pos = 0;
  for (size_t i = 0; i < len && pos + 4 < sizeof(buf); ++i) {
    if (i > 0)
      buf[pos++] = ' ';
    pos += (size_t)snprintf(buf + pos, sizeof(buf) - pos, "%02X", data[i]);
  }
  buf[pos] = '\0';
  DTG_LOG("Modbus %s %s", prefix, buf);
}
#endif

struct TokenBind {
  uint32_t token = 0;
  ModbusBus *bus = nullptr;
  bool used = false;
};

TokenBind gBinds[DTG_MODBUS_TOKEN_SLOTS];
SemaphoreHandle_t gTokenLock = nullptr;
uint32_t gNextToken = 1;

void ensureTokenLock() {
  if (!gTokenLock)
    gTokenLock = xSemaphoreCreateMutex();
}

uint32_t allocToken() {
  ensureTokenLock();
  xSemaphoreTake(gTokenLock, portMAX_DELAY);
  const uint32_t token = gNextToken++;
  if (gNextToken == 0)
    gNextToken = 1;
  xSemaphoreGive(gTokenLock);
  return token;
}

bool bindToken(uint32_t token, ModbusBus *bus) {
  ensureTokenLock();
  xSemaphoreTake(gTokenLock, portMAX_DELAY);
  bool ok = false;
  for (size_t i = 0; i < DTG_MODBUS_TOKEN_SLOTS; ++i) {
    if (!gBinds[i].used) {
      gBinds[i].used = true;
      gBinds[i].token = token;
      gBinds[i].bus = bus;
      ok = true;
      break;
    }
  }
  xSemaphoreGive(gTokenLock);
  return ok;
}

void unbindToken(uint32_t token) {
  if (!gTokenLock)
    return;
  xSemaphoreTake(gTokenLock, portMAX_DELAY);
  for (size_t i = 0; i < DTG_MODBUS_TOKEN_SLOTS; ++i) {
    if (gBinds[i].used && gBinds[i].token == token) {
      gBinds[i].used = false;
      gBinds[i].bus = nullptr;
      gBinds[i].token = 0;
      break;
    }
  }
  xSemaphoreGive(gTokenLock);
}

ModbusBus *ownerOf(uint32_t token) {
  if (!gTokenLock)
    return nullptr;
  xSemaphoreTake(gTokenLock, portMAX_DELAY);
  ModbusBus *owner = nullptr;
  for (size_t i = 0; i < DTG_MODBUS_TOKEN_SLOTS; ++i) {
    if (gBinds[i].used && gBinds[i].token == token) {
      owner = gBinds[i].bus;
      break;
    }
  }
  xSemaphoreGive(gTokenLock);
  return owner;
}

void onDataStatic(ModbusMessage msg, uint32_t token) {
  ModbusBus *owner = ownerOf(token);
  if (owner)
    owner->handleData(msg, token);
}

void onErrorStatic(Error err, uint32_t token) {
  ModbusBus *owner = ownerOf(token);
  if (owner)
    owner->handleError((int)err, token);
}

} // namespace

ModbusBus::ModbusBus() = default;

ModbusBus::~ModbusBus() { shutdown(); }

void ModbusBus::failImmediate(ModbusReadCallback cb, const ModbusPendingCapture &cap,
                              const char *msg) {
  ModbusReadResult rs;
  setBusError(rs.error, msg);
  if (cb)
    cb(rs, cap);
}

int ModbusBus::allocSlot() {
  for (int i = 0; i < (int)DTG_MODBUS_PENDING_MAX; ++i) {
    if (!pool_[i].used)
      return i;
  }
  return -1;
}

size_t ModbusBus::pendingCount() const {
  size_t n = 0;
  for (size_t i = 0; i < DTG_MODBUS_PENDING_MAX; ++i) {
    if (pool_[i].used)
      ++n;
  }
  return n;
}

bool ModbusBus::takePending(uint32_t token, PendingRead &out) {
  out = PendingRead{};
  if (!lock_)
    return false;
  xSemaphoreTake(lock_, portMAX_DELAY);
  bool found = false;
  for (size_t i = 0; i < DTG_MODBUS_PENDING_MAX; ++i) {
    if (pool_[i].used && pool_[i].token == token) {
      out = pool_[i];
      pool_[i] = PendingRead{};
      found = true;
      break;
    }
  }
  xSemaphoreGive(lock_);
  return found;
}

HardwareSerial *ModbusBus::resolveSerial() {
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S3
  if (portNum_ == 2)
    return &Serial2;
#endif
  return &Serial1;
}

bool ModbusBus::beginClient(String &error) {
  if (dePin_ >= 0 && rePin_ >= 0 && dePin_ != rePin_) {
    error = "separate_de_re_not_supported";
    return false;
  }
  if (dePin_ >= 0) {
    client_ = std::make_unique<ModbusClientRTU>(dePin_, 32);
  } else {
    client_ = std::make_unique<ModbusClientRTU>();
  }

  if (!client_) {
    error = "modbus_client_alloc_failed";
    return false;
  }

  client_->onDataHandler(&onDataStatic);
  client_->onErrorHandler(&onErrorStatic);
  client_->setTimeout(1200);

  HardwareSerial *ser = resolveSerial();
  if (!ser) {
    error = "serial_unavailable";
    return false;
  }
  client_->begin(*ser);
  return true;
}

void ModbusBus::prepareBusForRequest() {
  if (!autoDirection_)
    return;
  HardwareSerial *ser = resolveSerial();
  if (!ser)
    return;
#if DTG_MODBUS_AUTO_DIR_DRAIN_RX
  while (ser->available())
    (void)ser->read();
#endif
#if DTG_MODBUS_INTER_FRAME_US > 0
  delayMicroseconds(DTG_MODBUS_INTER_FRAME_US);
#endif
}

void ModbusBus::shutdown() {
  if (lock_) {
    const unsigned long t0 = millis();
    while (pendingCount() > 0 && (millis() - t0) < (unsigned long)(DTG_MODBUS_INFLIGHT_MS))
      delay(10);
    xSemaphoreTake(lock_, portMAX_DELAY);
    for (size_t i = 0; i < DTG_MODBUS_PENDING_MAX; ++i) {
      if (pool_[i].used)
        unbindToken(pool_[i].token);
      pool_[i] = PendingRead{};
    }
    xSemaphoreGive(lock_);
  }
  client_.reset();
  initialized_ = false;
  if (lock_) {
    vSemaphoreDelete(lock_);
    lock_ = nullptr;
  }
}

bool ModbusBus::begin(const ModbusChannelConfig &cfg, String &error) {
  shutdown();
  alias_ = cfg.alias;
  baud_ = cfg.baud;
  dePin_ = cfg.dePin;
  rePin_ = cfg.rePin;
  autoDirection_ = (dePin_ < 0 && rePin_ < 0);

  portNum_ = (strcmp(cfg.port.c_str(), "UART2") == 0) ? 2 : 1;

  HardwareSerial *ser = resolveSerial();
  if (!ser) {
    error = "serial_unavailable";
    return false;
  }

  if (dePin_ >= 0) {
    pinMode(dePin_, OUTPUT);
    digitalWrite(dePin_, LOW);
  }
  if (rePin_ >= 0) {
    pinMode(rePin_, OUTPUT);
    digitalWrite(rePin_, LOW);
  }

  int serialMode = SERIAL_8N1;
  if (strcmp(cfg.parity.c_str(), "even") == 0)
    serialMode = (cfg.stopBits == 2) ? SERIAL_8E2 : SERIAL_8E1;
  else if (strcmp(cfg.parity.c_str(), "odd") == 0)
    serialMode = (cfg.stopBits == 2) ? SERIAL_8O2 : SERIAL_8O1;
  else
    serialMode = (cfg.stopBits == 2) ? SERIAL_8N2 : SERIAL_8N1;

  const int defaultRx = (portNum_ == 2) ? DTG_MODBUS_UART2_RX : DTG_MODBUS_UART1_RX;
  const int defaultTx = (portNum_ == 2) ? DTG_MODBUS_UART2_TX : DTG_MODBUS_UART1_TX;
  const int rx = (cfg.rxPin >= 0) ? cfg.rxPin : defaultRx;
  const int tx = (cfg.txPin >= 0) ? cfg.txPin : defaultTx;
  ser->begin(baud_, serialMode, rx, tx);

  if (!beginClient(error))
    return false;

  lock_ = xSemaphoreCreateMutex();
  if (!lock_) {
    error = "bus_lock_create_failed";
    client_.reset();
    return false;
  }
  initialized_ = true;
  return true;
}

bool ModbusBus::readRegisters(uint8_t functionCode, uint8_t slaveId, uint16_t address,
                              uint16_t quantity, ModbusReadCallback cb,
                              const ModbusPendingCapture &cap) {
  if (!initialized_ || !client_ || !lock_ || !cb) {
    failImmediate(cb, cap, "bus_not_ready");
    return false;
  }
  if (functionCode != READ_COIL && functionCode != READ_DISCR_INPUT &&
      functionCode != READ_HOLD_REGISTER && functionCode != READ_INPUT_REGISTER) {
    failImmediate(cb, cap, "unsupported_function_code");
    return false;
  }

  xSemaphoreTake(lock_, portMAX_DELAY);
  const int slot = allocSlot();
  if (slot < 0) {
    xSemaphoreGive(lock_);
    failImmediate(cb, cap, "pending_full");
    return false;
  }
  const uint32_t token = allocToken();
  pool_[slot].used = true;
  pool_[slot].token = token;
  pool_[slot].quantity = quantity;
  pool_[slot].cap = cap;
  pool_[slot].callback = cb;
  xSemaphoreGive(lock_);

  if (!bindToken(token, this)) {
    bool found = false;
    PendingRead pending;
    found = takePending(token, pending);
    (void)found;
    failImmediate(cb, cap, "token_bind_failed");
    return false;
  }

  prepareBusForRequest();

#if DTG_MODBUS_TRACE
  DTG_LOG("Modbus TX slave=%u fc=%u addr=%u qty=%u", (unsigned)slaveId, (unsigned)functionCode,
          (unsigned)address, (unsigned)quantity);
#endif

  const Error err = client_->addRequest(token, slaveId, functionCode, address, quantity);
  if (err != SUCCESS) {
    PendingRead pending;
    const bool found = takePending(token, pending);
    unbindToken(token);
    if (found && pending.callback)
      failImmediate(pending.callback, pending.cap, (const char *)ModbusError(err));
    else
      failImmediate(cb, cap, (const char *)ModbusError(err));
    return false;
  }
  return true;
}

bool ModbusBus::writeSingleCoil(uint8_t slaveId, uint16_t address, bool on, ModbusReadCallback cb,
                                const ModbusPendingCapture &cap) {
  if (!initialized_ || !client_ || !lock_ || !cb) {
    failImmediate(cb, cap, "bus_not_ready");
    return false;
  }

  xSemaphoreTake(lock_, portMAX_DELAY);
  const int slot = allocSlot();
  if (slot < 0) {
    xSemaphoreGive(lock_);
    failImmediate(cb, cap, "pending_full");
    return false;
  }
  const uint32_t token = allocToken();
  pool_[slot].used = true;
  pool_[slot].token = token;
  pool_[slot].quantity = 1;
  pool_[slot].cap = cap;
  pool_[slot].callback = cb;
  xSemaphoreGive(lock_);
  if (!bindToken(token, this)) {
    PendingRead pending;
    (void)takePending(token, pending);
    failImmediate(cb, cap, "token_bind_failed");
    return false;
  }

  prepareBusForRequest();
  const uint16_t wireValue = on ? 0xFF00 : 0x0000;
  const Error err = client_->addRequest(token, slaveId, WRITE_COIL, address, wireValue);
  if (err != SUCCESS) {
    PendingRead pending;
    const bool found = takePending(token, pending);
    unbindToken(token);
    if (found && pending.callback)
      failImmediate(pending.callback, pending.cap, (const char *)ModbusError(err));
    else
      failImmediate(cb, cap, (const char *)ModbusError(err));
    return false;
  }
  return true;
}

bool ModbusBus::writeSingleRegister(uint8_t slaveId, uint16_t address, uint16_t value,
                                    ModbusReadCallback cb, const ModbusPendingCapture &cap) {
  if (!initialized_ || !client_ || !lock_ || !cb) {
    failImmediate(cb, cap, "bus_not_ready");
    return false;
  }

  xSemaphoreTake(lock_, portMAX_DELAY);
  const int slot = allocSlot();
  if (slot < 0) {
    xSemaphoreGive(lock_);
    failImmediate(cb, cap, "pending_full");
    return false;
  }
  const uint32_t token = allocToken();
  pool_[slot].used = true;
  pool_[slot].token = token;
  pool_[slot].quantity = 1;
  pool_[slot].cap = cap;
  pool_[slot].callback = cb;
  xSemaphoreGive(lock_);
  if (!bindToken(token, this)) {
    PendingRead pending;
    (void)takePending(token, pending);
    failImmediate(cb, cap, "token_bind_failed");
    return false;
  }

  prepareBusForRequest();
  const Error err = client_->addRequest(token, slaveId, WRITE_HOLD_REGISTER, address, value);
  if (err != SUCCESS) {
    PendingRead pending;
    const bool found = takePending(token, pending);
    unbindToken(token);
    if (found && pending.callback)
      failImmediate(pending.callback, pending.cap, (const char *)ModbusError(err));
    else
      failImmediate(cb, cap, (const char *)ModbusError(err));
    return false;
  }
  return true;
}

void ModbusBus::handleData(ModbusMessage msg, uint32_t token) {
  PendingRead pending;
  const bool found = takePending(token, pending);
  unbindToken(token);
  if (!found || !pending.callback)
    return;

  ModbusReadResult rs;
  const size_t n = msg.size();
  if (n < 3) {
    setBusError(rs.error, "bad_response");
    pending.callback(rs, pending.cap);
    return;
  }

  const uint8_t fc = msg[1];
  if (fc == WRITE_HOLD_REGISTER || fc == WRITE_COIL) {
    rs.ok = true;
    pending.callback(rs, pending.cap);
    return;
  }

  const uint8_t byteCount = msg[2];
  if (n < (size_t)(3 + byteCount)) {
    setBusError(rs.error, "bad_byte_count");
    pending.callback(rs, pending.cap);
    return;
  }

  if (fc == READ_COIL || fc == READ_DISCR_INPUT) {
    rs.isBits = true;
    const uint8_t copyN = byteCount > 250 ? 250 : byteCount;
    for (uint8_t i = 0; i < copyN; ++i)
      rs.bits[i] = msg[3 + i];
    rs.bitBytes = copyN;
    rs.ok = true;
    pending.callback(rs, pending.cap);
    return;
  }

  if ((byteCount % 2) != 0) {
    setBusError(rs.error, "odd_register_bytes");
    pending.callback(rs, pending.cap);
    return;
  }

  const size_t registerCount = byteCount / 2;
  const size_t copyN = registerCount > 125 ? 125 : registerCount;
  for (size_t i = 0; i < copyN; ++i) {
    const uint8_t hi = msg[3 + i * 2];
    const uint8_t lo = msg[3 + i * 2 + 1];
    rs.registers[i] = (uint16_t)((hi << 8) | lo);
  }
  rs.regCount = (uint8_t)copyN;
  rs.ok = true;
  pending.callback(rs, pending.cap);
}

void ModbusBus::handleError(int errCode, uint32_t token) {
  PendingRead pending;
  const bool found = takePending(token, pending);
  unbindToken(token);
  if (!found || !pending.callback)
    return;

  ModbusReadResult rs;
  rs.ok = false;
  rs.isTimeout = (errCode == TIMEOUT);
  setBusError(rs.error, (const char *)ModbusError((Error)errCode));
#if DTG_MODBUS_TRACE
  DTG_LOG("Modbus RX error code=%d (%s)", errCode, rs.error);
#endif
  pending.callback(rs, pending.cap);
}

} // namespace detail
