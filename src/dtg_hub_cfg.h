/**
 * @file dtg_hub_cfg.h
 * @brief Load Hub gateway credentials from NVS namespace dtg_cfg (browser factory flash).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "dtg_config.h"

#ifndef DTG_CFG_NVS_NS
#define DTG_CFG_NVS_NS "dtg_cfg"
#endif

struct DtgHubCfg {
  char projectId[16];
  char gwAlias[17];
  char mqttUser[DTG_MQTT_CRED_MAX];
  char mqttPass[DTG_MQTT_CRED_MAX];
  char clientId[DTG_MQTT_CRED_MAX];
  char mqttHost[64];
  uint16_t mqttPort;
  char apn[32];
  char wifiSsid[33];
  char wifiPass[65];
  bool enableDebug;
  bool loaded;
};

/** Seed defaults then overlay NVS keys. Returns true if any key came from NVS. */
bool dtgHubCfgLoad(DtgHubCfg &out, const DtgHubCfg *defaults = nullptr);

/** Persist MQTT credentials after Hub rotate (down/cfg set_mqtt_credential). */
bool dtgHubCfgSaveCredentials(const char *user, const char *pass, uint32_t version);

