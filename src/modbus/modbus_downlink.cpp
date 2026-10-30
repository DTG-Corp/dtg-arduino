#include "modbus_downlink.h"

#include "modbus_manager.h"

#include <ArduinoJson.h>
#include <string.h>

namespace detail {

ModbusDownlink &ModbusDownlink::instance() {
  static ModbusDownlink dl;
  return dl;
}

void ModbusDownlink::dispatch(const char *topic, const uint8_t *payload, unsigned int len) {
  if (!topic || !payload || len == 0)
    return;

  const char *marker = strstr(topic, "/down/modbus/");
  if (!marker)
    return;
  marker += strlen("/down/modbus/");
  if (!marker[0])
    return;

  const size_t markerLen = strlen(marker);
  if (markerLen < 2 || marker[markerLen - 2] != '/' || marker[markerLen - 1] != 'w')
    return;
  const char *wSuffix = marker + markerLen - 2;

  char pathBuf[96];
  const size_t pathLen = static_cast<size_t>(wSuffix - marker);
  if (pathLen == 0 || pathLen >= sizeof(pathBuf))
    return;
  memcpy(pathBuf, marker, pathLen);
  pathBuf[pathLen] = '\0';

  char *save = nullptr;
  char *chAlias = strtok_r(pathBuf, "/", &save);
  char *devAlias = strtok_r(nullptr, "/", &save);
  char *tagAlias = strtok_r(nullptr, "/", &save);
  if (!chAlias || !devAlias || !tagAlias)
    return;

  if (len >= 256)
    len = 255;
  char jsonBuf[256];
  memcpy(jsonBuf, payload, len);
  jsonBuf[len] = '\0';

  JsonDocument doc;
  if (deserializeJson(doc, jsonBuf))
    return;

  const char *reqId = doc["reqId"] | "";
  if (!reqId[0])
    return;

  if (!doc["v"].is<float>() && !doc["v"].is<int>() && !doc["v"].is<double>())
    return;

  const float value = doc["v"].as<float>();
  ModbusManager::instance().enqueueWrite(chAlias, devAlias, tagAlias, value, reqId);
}

} // namespace detail
