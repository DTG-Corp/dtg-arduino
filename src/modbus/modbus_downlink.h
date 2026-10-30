#pragma once

#include <Arduino.h>

namespace detail {

class ModbusManager;

/** MQTT downlink handler for down/modbus/{ch}/{dev}/{tag}/w */
class ModbusDownlink {
public:
  static ModbusDownlink &instance();

  void dispatch(const char *topic, const uint8_t *payload, unsigned int len);

private:
  ModbusDownlink() = default;
};

} // namespace detail
