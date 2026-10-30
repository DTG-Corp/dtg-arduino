/**
 * @file dtg_hub_cmd.h
 * @brief Hub OTA + remote reboot + set_wifi on down/c/{device}/ota|reboot|wifi.
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"

#if DTG_FEATURE_HUB_CMD

#include "dtg_types.h"

class DtgClient;

namespace detail
{

class HubCmd
{
public:
  static HubCmd &instance();

  void attach(DtgClient *client);
  void setCommandChain(DTG_CommandCallback cb, void *userData);

  /** After enableHubCmd(), custom downlink handlers must use setCommandChain (not onCommand). */
  static void commandCb(const char *deviceAlias, const char *streamId,
                        const uint8_t *payload, unsigned int len, void *user);

private:
  HubCmd() = default;

  /** Friend of DtgClient - must live on HubCmd, not free helpers (protected mqtt/topic). */
  static void publishWifiCmdAck(DtgClient *client, const char *cmdId, const char *st,
                                const char *reason, const char *ip);
  static bool waitMqttConnected(DtgClient *client, unsigned long deadlineMs);
  static void handleSetWifi(DtgClient *client, const uint8_t *payload, unsigned int len);

  DtgClient *client_ = nullptr;
  DTG_CommandCallback chainCb_ = nullptr;
  void *chainUser_ = nullptr;
};

} // namespace detail

#endif // DTG_FEATURE_HUB_CMD
