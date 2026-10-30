#pragma once

#include <Arduino.h>

#include <vector>

#include "modbus_config.h"

namespace detail {

struct ModbusPollRef {
  size_t channelIndex = 0;
  size_t deviceIndex = 0;
  size_t tagIndex = 0;
};

// Round-robin scheduler with per-tag in-flight guarding and at most one
// outstanding transaction per channel (RS485 bus is half-duplex / serial).
class ModbusScheduler {
public:
  void rebuild(const ModbusRuntimeConfig &cfg);

  // Returns the next due tag that is not already in flight and whose channel
  // has no outstanding transaction. Marks it in flight on success.
  bool acquireDue(uint32_t nowMs, ModbusPollRef &out);

  // Clears the in-flight state and schedules the next poll instant.
  void release(const ModbusPollRef &ref, uint32_t nextAtMs);

private:
  struct Entry {
    ModbusPollRef ref;
    uint32_t pollingMs = 1000;
    uint32_t nextAtMs = 0;
    uint32_t inFlightSinceMs = 0;
    bool inFlight = false;
  };

  std::vector<Entry> entries_;
  std::vector<bool> channelBusy_;
  size_t rrCursor_ = 0;
};

} // namespace detail
