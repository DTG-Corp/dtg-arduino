#pragma once

#include <Arduino.h>

#include <vector>

#include "modbus_config.h"

class DtgClient;

namespace detail {

class ModbusHealth {
public:
  void rebuild(size_t channelCount);
  void setAutoDirection(size_t channelIndex, bool enabled);
  void markOk(size_t channelIndex);
  void markError(size_t channelIndex);
  void markTimeout(size_t channelIndex);
  void setDeviceSkipState(size_t channelIndex, uint8_t consecutiveFails, bool skipActive);
  void maybePublish(DtgClient *client, const ModbusRuntimeConfig &cfg, uint32_t nowMs);

private:
  struct Counter {
    uint32_t ok = 0;
    uint32_t err = 0;
    uint32_t timeout = 0;
    uint32_t lastPublishMs = 0;
    bool autoDir = false;
    uint8_t consecutiveFails = 0;
    bool skipActive = false;
    char lastErr[16] = {0};
  };

  std::vector<Counter> counters_;
};

} // namespace detail
