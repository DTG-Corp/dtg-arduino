/**
 * @file dtg_sketch_log.h
 * @brief Optional Serial helpers for examples (topic + JSON payload traces).
 *
 * For library-level traces on every MQTT event, also call:
 *   dtg.setLogStream(&Serial);
 *
 * Lines use DTG_LOG format: [basename:line] [Tag] ...
 */
#pragma once

#include <Arduino.h>
#include <stdio.h>

#include "dtg_client.h"
#include "dtg_debug.h"

inline void dtgSerialLogMqtt(const char *event, const char *topic, const char *payload)
{
  DTG_LOG("%s topic=%s payload=%s", dtgLogMqttTag(event), topic ? topic : "",
          payload ? payload : "");
}

inline void dtgSerialLogUplink(DtgClient &dtg, const char *deviceAlias, const char *streamId,
                               float value)
{
  char topic[160];
  char json[80];
  snprintf(topic, sizeof(topic), "%stele/%s/%s", dtg.topicPrefix(),
           deviceAlias ? deviceAlias : "_", streamId ? streamId : "");
  snprintf(json, sizeof(json), "{\"v\":%.6f,\"ts\":%lu}", static_cast<double>(value),
           static_cast<unsigned long>(millis() / 1000UL));
  dtgSerialLogMqtt("uplink", topic, json);
}

inline void dtgSerialLogDownlink(const char *topicPrefix, const char *deviceAlias,
                                 const char *streamId, const char *rawPayload)
{
  char topic[160];
  snprintf(topic, sizeof(topic), "%sdown/c/%s/%s", topicPrefix ? topicPrefix : "",
           deviceAlias ? deviceAlias : "_", streamId ? streamId : "");
  dtgSerialLogMqtt("downlink", topic, rawPayload ? rawPayload : "");
}

inline void dtgSerialLogConfig(const char *topic, const char *jsonPayload)
{
  dtgSerialLogMqtt("config", topic, jsonPayload);
}

inline void dtgSerialLogConnected(DtgClient &dtg, const char *clientId)
{
  DTG_LOG("[MQTT] connected clientId=%s", clientId ? clientId : "");
  DTG_LOG("[MQTT] topicPrefix=%s", dtg.topicPrefix());
  char sub[128];
  snprintf(sub, sizeof(sub), "%sdown/c/#", dtg.topicPrefix());
  DTG_LOG("[MQTT] subscribe topic=%s", sub);
  snprintf(sub, sizeof(sub), "%sdown/cfg", dtg.topicPrefix());
  DTG_LOG("[MQTT] subscribe topic=%s", sub);
}
