/**
 * @file dtg_nocode.h
 * @brief Hub no-code I/O runtime (down/cfg, full_sync, cfg_ack, uplink, digital/analog PWM down/c).
 *
 * Matches Hub-generated sketches and the **ESP32_DtgHubGateway** library example.
 * Call DtgClient::enableNoCodeIo() once after begin(); requires ArduinoJson + Preferences.
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"
#include "dtg_types.h"

class DtgClient;

#if DTG_FEATURE_NOCODE

namespace detail
{

class HubNoCodeIo
{
public:
  static HubNoCodeIo &instance();

  static void setOtaStartHandler(DTG_OtaStartHandler handler);

  void attach(DtgClient *client);
  void detach();

  void pollUplink();

  /** Gateway-wide uplink period (ms). Default 5000. */
  uint32_t periodUplink() const;
  void setPeriodUplink(uint32_t ms);

  static void configCb(const char *topic, const uint8_t *payload, unsigned int len, void *user);
  static void commandCb(const char *deviceAlias, const char *streamId, const uint8_t *payload,
                        unsigned int len, void *user);

private:
  HubNoCodeIo() = default;
  HubNoCodeIo(const HubNoCodeIo &) = delete;
  HubNoCodeIo &operator=(const HubNoCodeIo &) = delete;

  DtgClient *client_ = nullptr;

  void loadVersion();
  void saveVersion(uint32_t v);
  void onCfgPayload(const char *payload, unsigned int len);
  void handleDownlinkCommand(const char *topic, const uint8_t *payload, unsigned int length);
  bool publishAck(uint32_t version, const char *status, const char *dsId, const char *msg);
  bool publishCmdAck(const char *cmdId, const char *streamSeg, const char *status, int level);
  void publishTeleRetained(const char *seg, float v);
  void queueTeleSample(const char *seg, float v);
  void flushTeleBatch();
};

} // namespace detail

#else

namespace detail
{

class HubNoCodeIo
{
public:
  static HubNoCodeIo &instance()
  {
    static HubNoCodeIo inst;
    return inst;
  }
  static void setOtaStartHandler(DTG_OtaStartHandler) {}
  void attach(DtgClient *) {}
  void detach() {}
  void pollUplink() {}
  uint32_t periodUplink() const { return DTG_PERIOD_UPLINK_DEFAULT_MS; }
  void setPeriodUplink(uint32_t) {}
  static void configCb(const char *, const uint8_t *, unsigned int, void *) {}
  static void commandCb(const char *, const char *, const uint8_t *, unsigned int, void *) {}
};

} // namespace detail

#endif
