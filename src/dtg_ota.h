/**
 * @file dtg_ota.h
 * @brief Hub FOTA (HTTP(S) download + ESP32 OTA) - shared by no-code I/O and reference sketches.
 *
 * Publishes progress on {prefix}up/ota_status (JSON jobId, status, progress, optional error).
 * Detailed traces use DTG_LOG as [file:line] [OTA] ... (setLogStream / DTG_DEBUG).
 * Requires ESP32 Arduino core (HTTPClient, Update, WiFiClientSecure).
 *
 * Cellular/PPP: MQTT TLS is released before HTTPS (single TLS). After flash, MQTT is resumed
 * via dtgOtaSetMqttResumeHandler to publish terminal status before reboot.
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"

#if DTG_FEATURE_OTA

class PubSubClient;

namespace detail
{

/** Re-open MQTT after HTTPS OTA (typically DtgClient::connectMqttSession). */
using DtgOtaMqttResumeFn = bool (*)();

void dtgOtaSetMqttResumeHandler(DtgOtaMqttResumeFn fn);

bool hubOtaPerformFromUrl(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                          const char *firmwareUrl, const char *checksumSpec = nullptr,
                          uint32_t expectedSize = 0, const char *caPem = nullptr);

} // namespace detail

#else

class PubSubClient;

namespace detail
{

inline void dtgOtaSetMqttResumeHandler(bool (*)()) {}

inline bool hubOtaPerformFromUrl(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                                 const char *firmwareUrl, const char *checksumSpec = nullptr,
                                 uint32_t expectedSize = 0, const char *caPem = nullptr)
{
  (void)mqtt;
  (void)hubTopicPrefix;
  (void)jobId;
  (void)firmwareUrl;
  (void)checksumSpec;
  (void)expectedSize;
  (void)caPem;
  return false;
}

} // namespace detail

#endif // DTG_FEATURE_OTA
