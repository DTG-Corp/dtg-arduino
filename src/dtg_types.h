/**
 * @file dtg_types.h
 * @brief Callback types and topic path helpers for MQTT contract v2.
 *
 * Topic shape: dtg/{projectId}/{gwAlias}/{plane}/{deviceAlias}/{resource}
 * - Telemetry: tele/{device}/{streamId}  payload often {"v":..., "ts":...}
 * - GPS:       tele/_/gps_data            payload {"lat","lon","spd","ts"}
 * - Status:    up/status                  retained {"ol":0|1}
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"

// Downlink command: .../down/c/{deviceAlias}/{streamId}
typedef void (*DTG_CommandCallback)(const char *deviceAlias, const char *streamId,
                                       const uint8_t *payload, unsigned int length,
                                       void *userData);

// Hub no-code config push on .../down/cfg (QoS 1) - raw JSON; caller parses (ArduinoJson).
typedef void (*DTG_ConfigCallback)(const char *topic, const uint8_t *payload,
                                      unsigned int length, void *userData);

/** Hub FOTA on .../down/c/{device}/ota - JSON action "start_ota" with jobId + url; register via HubNoCodeIo::setOtaStartHandler. */
typedef void (*DTG_OtaStartHandler)(const char *jobId, const char *url);

/** Optional hooks for up/info and tele/snap (stack buffers only - no heap). */
typedef bool (*DTG_RuntimeStringFn)(char *buf, size_t bufLen, void *userData);
typedef bool (*DTG_RuntimeRssiFn)(int *rssiDbm, void *userData);

struct DtgRuntimeInfoProvider
{
  DTG_RuntimeStringFn getIp = nullptr;
  DTG_RuntimeStringFn getMac = nullptr;
  DTG_RuntimeStringFn getSsid = nullptr; // STA SSID when net=wifi; optional
  DTG_RuntimeRssiFn getRssi = nullptr;
  const char *networkType = "unknown";
  void *userData = nullptr;
};

namespace detail
{

/** Build prefix "dtg/{pid}/{gw}/" into buf; returns false if truncated. */
inline bool buildTopicPrefix(char *buf, size_t bufLen, const char *projectId,
                             const char *gwAlias)
{
  if (!buf || bufLen == 0 || !projectId || !gwAlias)
    return false;
  int n = snprintf(buf, bufLen, "%s/%s/%s/", DTG_TOPIC_ROOT, projectId, gwAlias);
  return n > 0 && (size_t)n < bufLen;
}

/** Append sub-path after prefix (e.g. "tele/_/temp"). */
inline bool appendTopic(char *buf, size_t bufLen, const char *suffix)
{
  if (!buf || !suffix)
    return false;
  size_t used = strnlen(buf, bufLen);
  if (used >= bufLen)
    return false;
  int n = snprintf(buf + used, bufLen - used, "%s", suffix);
  return n > 0 && (size_t)(used + n) < bufLen;
}

} // namespace detail