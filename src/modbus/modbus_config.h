#pragma once

#include <Arduino.h>

#include <vector>

#include "../dtg_config.h"

class Preferences;

namespace detail {

enum class ModbusByteOrder : uint8_t {
  BE = 0,
  LE = 1,
  MID_BE = 2,
  MID_LE = 3,
};

struct ModbusTagConfig {
  String alias;
  String kind;
  uint8_t functionCode = 3;
  uint16_t address = 0;
  uint16_t length = 1;
  String transformer = "INT16";
  uint32_t pollingMs = 1000;
  float scale = 1.0f;
  float offset = 0.0f;
  float deltaThreshold = 0.0f;
  uint32_t minPublishMs = 0;
  uint32_t maxPublishMs = 0;
  bool important = false;
};

struct ModbusDeviceConfig {
  String alias;
  uint8_t slaveId = 1;
  uint32_t delayMs = 0;
  uint32_t connectionTimeoutSec = 60;
  ModbusByteOrder byteOrder = ModbusByteOrder::BE;
  std::vector<ModbusTagConfig> tags;
  std::vector<ModbusTagConfig> writeTags;
};

struct ModbusChannelConfig {
  String alias;
  String port = "UART1";
  uint32_t baud = 9600;
  String parity = "none";
  uint8_t dataBits = 8;
  uint8_t stopBits = 1;
  int16_t dePin = -1;
  int16_t rePin = -1;
  int16_t rxPin = -1;
  int16_t txPin = -1;
  std::vector<ModbusDeviceConfig> devices;
};

struct ModbusRuntimeConfig {
  uint32_t version = 0;
  std::vector<ModbusChannelConfig> channels;
};

/** Resolved write tag for MQTT downlink lookup (built in ModbusManager::applyConfig). */
struct ModbusWriteTagRef {
  size_t channelIndex = 0;
  size_t deviceIndex = 0;
  uint8_t slaveId = 1;
  uint8_t functionCode = 6;
  uint16_t address = 0;
  char transformer[DTG_MODBUS_TRANSFORMER_MAX] = "INT16";
  float scale = 1.0f;
  float offset = 0.0f;
  char channelAlias[DTG_MODBUS_ALIAS_MAX] = {0};
  char deviceAlias[DTG_MODBUS_ALIAS_MAX] = {0};
  char tagAlias[DTG_MODBUS_ALIAS_MAX] = {0};
};

bool parseModbusConfigPayload(const char *payload, size_t len, ModbusRuntimeConfig &out,
                              String &error);
bool saveModbusConfigBlob(Preferences &prefs, const char *payload, size_t len);

} // namespace detail
