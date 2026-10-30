/**
 * @file dtg_feature_runtime.cpp
 */
#include "dtg_feature_runtime.h"

#include "dtg_client.h"
#include "dtg_debug.h"
#if defined(ESP32)
#include <esp_heap_caps.h>
#endif
#if DTG_FEATURE_MODBUS
#include "modbus/modbus_manager.h"
#endif

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

namespace detail
{

DtgFeatureRuntime &DtgFeatureRuntime::instance()
{
  static DtgFeatureRuntime inst;
  return inst;
}

void DtgFeatureRuntime::attach(DtgClient *client) { client_ = client; }

void DtgFeatureRuntime::releaseModbusStash()
{
  if (modbusCfgStash_)
  {
    free(modbusCfgStash_);
    modbusCfgStash_ = nullptr;
  }
  modbusCfgStashLen_ = 0;
  modbusCfgStashed_ = false;
}

void DtgFeatureRuntime::stashModbusCfg(const uint8_t *payload, unsigned int len)
{
  if (!payload || len == 0 || len >= DTG_MODBUS_CFG_BUF_MAX)
    return;
#if defined(ESP32)
  const uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
#endif
  if (!modbusCfgStash_)
    modbusCfgStash_ = static_cast<char *>(malloc(DTG_MODBUS_CFG_BUF_MAX));
  if (!modbusCfgStash_)
    return;
  memcpy(modbusCfgStash_, payload, len);
  modbusCfgStash_[len] = '\0';
  modbusCfgStashLen_ = len;
  modbusCfgStashed_ = true;
#if defined(ESP32)
  DTG_LOG("Modbus cfg stashed len=%u heap before=%lu after=%lu", len,
          static_cast<unsigned long>(heapBefore),
          static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
#else
  DTG_LOG("Modbus cfg stashed len=%u (awaiting caps modbus=true)", len);
#endif
}

void DtgFeatureRuntime::applyStashedModbusCfg()
{
#if DTG_FEATURE_MODBUS
  if (!modbusCfgStashed_ || !modbusCfgStash_ || !client_)
    return;
  ModbusManager::instance().applyCfgPayload(modbusCfgStash_,
                                            static_cast<unsigned int>(modbusCfgStashLen_));
  releaseModbusStash();
#endif
}

void DtgFeatureRuntime::setModbus(bool on)
{
  if (!client_)
    return;
  if (on == modbusActive_)
    return;
#if defined(ESP32)
  const uint32_t heapBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
#endif
  modbusActive_ = on;
  if (on)
  {
    client_->activateModbusRuntime();
    applyStashedModbusCfg();
  }
  else
  {
    client_->deactivateModbusRuntime();
    releaseModbusStash();
  }
#if defined(ESP32)
  DTG_LOG("Modbus runtime %s heap before=%lu after=%lu", on ? "on" : "off",
          static_cast<unsigned long>(heapBefore),
          static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT)));
#endif
}

void DtgFeatureRuntime::setNocode(bool on)
{
  if (!client_)
    return;
  if (on)
  {
    if (nocodeActive_)
      return;
    nocodeActive_ = true;
    client_->activateNocodeRuntime();
    return;
  }
  // ponytail: never unsubscribe down/cfg when caps.nocode=false - Hub still pushes
  // set_period_uplink / credentials there. GPIO slots simply stay empty.
  nocodeActive_ = false;
}

void DtgFeatureRuntime::onCapsPayload(const uint8_t *payload, unsigned int len)
{
  if (!payload || len == 0 || len >= 256)
    return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len))
    return;

  const int v = doc["v"] | 0;
  if (v != 1)
    return;

  const uint32_t seq = doc["seq"] | 0U;
  if (seq > 0 && seq <= lastSeq_)
    return;
  if (seq > 0)
    lastSeq_ = seq;

  JsonObject features = doc["features"];
  if (features.isNull())
    return;

  const bool modbus = features["modbus"] | false;
  const bool nocode = features["nocode"] | false;

  DTG_LOG("Caps seq=%u modbus=%d nocode=%d", static_cast<unsigned>(seq),
          modbus ? 1 : 0, nocode ? 1 : 0);

  setModbus(modbus);
  setNocode(nocode);
}

} // namespace detail
