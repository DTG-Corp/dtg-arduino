/**
 * @file dtg_hub_cfg.cpp
 */
#include "dtg_hub_cfg.h"

#include <Preferences.h>
#include <cstring>

#include "dtg_config.h"

static void copyCapped(char *dst, size_t dstMax, const char *src)
{
  if (!src)
  {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstMax - 1);
  dst[dstMax - 1] = '\0';
}

static bool readStr(Preferences &prefs, const char *key, char *out, size_t outMax)
{
  if (!prefs.isKey(key))
  {
    return false;
  }
  String v = prefs.getString(key, "");
  if (v.length() == 0)
  {
    return false;
  }
  copyCapped(out, outMax, v.c_str());
  return true;
}

bool dtgHubCfgLoad(DtgHubCfg &out, const DtgHubCfg *defaults)
{
  memset(&out, 0, sizeof(out));
  out.mqttPort =
#if defined(MQTT_USE_TLS) && MQTT_USE_TLS
      DTG_MQTT_PORT_TLS;
#else
      DTG_MQTT_PORT_PLAIN;
#endif
  copyCapped(out.mqttHost, sizeof(out.mqttHost), DTG_MQTT_HOST);
  out.enableDebug = true;

  if (defaults)
  {
    copyCapped(out.projectId, sizeof(out.projectId), defaults->projectId);
    copyCapped(out.gwAlias, sizeof(out.gwAlias), defaults->gwAlias);
    copyCapped(out.mqttUser, sizeof(out.mqttUser), defaults->mqttUser);
    copyCapped(out.mqttPass, sizeof(out.mqttPass), defaults->mqttPass);
    copyCapped(out.clientId, sizeof(out.clientId), defaults->clientId);
    if (defaults->mqttHost[0])
    {
      copyCapped(out.mqttHost, sizeof(out.mqttHost), defaults->mqttHost);
    }
    if (defaults->mqttPort)
    {
      out.mqttPort = defaults->mqttPort;
    }
    copyCapped(out.apn, sizeof(out.apn), defaults->apn);
    copyCapped(out.wifiSsid, sizeof(out.wifiSsid), defaults->wifiSsid);
    copyCapped(out.wifiPass, sizeof(out.wifiPass), defaults->wifiPass);
    out.enableDebug = defaults->enableDebug;
  }

  Preferences prefs;
  if (!prefs.begin(DTG_CFG_NVS_NS, true))
  {
    out.loaded = false;
    return false;
  }

  bool any = false;
  any |= readStr(prefs, "pid", out.projectId, sizeof(out.projectId));
  any |= readStr(prefs, "alias", out.gwAlias, sizeof(out.gwAlias));
  any |= readStr(prefs, "user", out.mqttUser, sizeof(out.mqttUser));
  any |= readStr(prefs, "pass", out.mqttPass, sizeof(out.mqttPass));
  any |= readStr(prefs, "cid", out.clientId, sizeof(out.clientId));
  any |= readStr(prefs, "mhost", out.mqttHost, sizeof(out.mqttHost));
  any |= readStr(prefs, "apn", out.apn, sizeof(out.apn));
  any |= readStr(prefs, "ssid", out.wifiSsid, sizeof(out.wifiSsid));
  any |= readStr(prefs, "wpass", out.wifiPass, sizeof(out.wifiPass));
  if (prefs.isKey("mport"))
  {
    const uint16_t p = prefs.getUShort("mport", out.mqttPort);
    if (p >= 1)
    {
      out.mqttPort = p;
      any = true;
    }
  }
  if (prefs.isKey("dbg"))
  {
    out.enableDebug = prefs.getBool("dbg", out.enableDebug);
    any = true;
  }
  prefs.end();
  out.loaded = any;
  return any;
}

bool dtgHubCfgSaveCredentials(const char *user, const char *pass, uint32_t version)
{
  if (!user || !pass || !user[0] || !pass[0])
  {
    return false;
  }
  Preferences prefs;
  if (!prefs.begin(DTG_CFG_NVS_NS, false))
  {
    return false;
  }
  const bool okUser = prefs.putString("user", user) > 0;
  const bool okPass = prefs.putString("pass", pass) > 0;
  prefs.putUInt("cver", version);
  prefs.end();
  return okUser && okPass;
}
