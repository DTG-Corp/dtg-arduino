/**
 * @file dtg_feature_runtime.h
 * @brief Platform-driven capability activation (down/gw/caps retained from Hub).
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"

class DtgClient;

namespace detail
{

class DtgFeatureRuntime
{
public:
  static DtgFeatureRuntime &instance();

  void attach(DtgClient *client);
  void onCapsPayload(const uint8_t *payload, unsigned int len);
  void stashModbusCfg(const uint8_t *payload, unsigned int len);
  bool modbusActive() const { return modbusActive_; }
  bool nocodeActive() const { return nocodeActive_; }

private:
  DtgFeatureRuntime() = default;

  DtgClient *client_ = nullptr;
  uint32_t lastSeq_ = 0;
  bool modbusActive_ = false;
  bool nocodeActive_ = false;
  bool modbusCfgStashed_ = false;
  char *modbusCfgStash_ = nullptr;
  size_t modbusCfgStashLen_ = 0;

  void setModbus(bool on);
  void setNocode(bool on);
  void applyStashedModbusCfg();
  void releaseModbusStash();
};

} // namespace detail
