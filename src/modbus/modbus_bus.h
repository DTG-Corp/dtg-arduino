#pragma once

#include <Arduino.h>

#include <memory>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "modbus_config.h"

class ModbusClientRTU;
class ModbusMessage;

namespace detail {

struct ModbusReadResult {
  bool ok = false;
  bool isTimeout = false;
  bool isBits = false;
  uint16_t registers[125] = {};
  uint8_t regCount = 0;
  uint8_t bits[250] = {};
  uint8_t bitBytes = 0;
  char error[DTG_MODBUS_ERROR_MAX] = {0};
};

struct ModbusPendingCapture {
  size_t channelIndex = 0;
  size_t deviceIndex = 0;
  size_t tagIndex = 0;
  uint32_t version = 0;
  size_t writeTagIndex = 0;
  char reqId[37] = {0};
};

using ModbusReadCallback = void (*)(const ModbusReadResult &rs, const ModbusPendingCapture &cap);

class ModbusBus {
public:
  ModbusBus();
  ~ModbusBus();

  ModbusBus(const ModbusBus &) = delete;
  ModbusBus &operator=(const ModbusBus &) = delete;
  ModbusBus(ModbusBus &&) = delete;
  ModbusBus &operator=(ModbusBus &&) = delete;

  bool begin(const ModbusChannelConfig &cfg, String &error);
  void shutdown();
  size_t pendingCount() const;

  bool readRegisters(uint8_t functionCode, uint8_t slaveId, uint16_t address, uint16_t quantity,
                     ModbusReadCallback cb, const ModbusPendingCapture &cap);
  bool writeSingleCoil(uint8_t slaveId, uint16_t address, bool on, ModbusReadCallback cb,
                       const ModbusPendingCapture &cap);
  bool writeSingleRegister(uint8_t slaveId, uint16_t address, uint16_t value, ModbusReadCallback cb,
                           const ModbusPendingCapture &cap);

  const String &alias() const { return alias_; }

  void handleData(ModbusMessage msg, uint32_t token);
  void handleError(int errCode, uint32_t token);

private:
  struct PendingRead {
    bool used = false;
    uint32_t token = 0;
    uint16_t quantity = 0;
    ModbusPendingCapture cap;
    ModbusReadCallback callback = nullptr;
  };

  String alias_;
  int portNum_ = 1;
  uint32_t baud_ = 9600;
  int16_t dePin_ = -1;
  int16_t rePin_ = -1;
  bool autoDirection_ = false;
  bool initialized_ = false;
  std::unique_ptr<ModbusClientRTU> client_;
  PendingRead pool_[DTG_MODBUS_PENDING_MAX];
  SemaphoreHandle_t lock_ = nullptr;

  HardwareSerial *resolveSerial();
  bool beginClient(String &error);
  void prepareBusForRequest();
  int allocSlot();
  bool takePending(uint32_t token, PendingRead &out);
  void failImmediate(ModbusReadCallback cb, const ModbusPendingCapture &cap, const char *msg);
};

} // namespace detail
