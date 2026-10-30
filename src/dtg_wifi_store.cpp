/**
 * @file dtg_wifi_store.cpp
 */
#include "dtg_wifi_store.h"

#include "dtg_config.h"

#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

namespace
{
Preferences s_prefs;
bool s_open = false;

constexpr uint32_t kCfgVer = 1;
constexpr char kCharset[] = "abcdefghijkmnopqrstuvwxyz23456789";

bool hasControlChar(const char *s)
{
  if (!s)
    return true;
  for (const char *p = s; *p; ++p)
  {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c == 0x7f)
      return true;
  }
  return false;
}

void ensureOpen()
{
  if (!s_open)
  {
    s_prefs.begin("dtg_wifi", false);
    s_open = true;
    if (s_prefs.getUInt("cfg_ver", 0) != kCfgVer)
    {
      s_prefs.putUInt("cfg_ver", kCfgVer);
    }
  }
}

bool loadCred(const char *ssidKey, const char *passKey, const char *setKey,
              DtgWifiCredentials *out)
{
  if (!out)
    return false;
  out->ssid[0] = '\0';
  out->pass[0] = '\0';
  out->configured = false;
  ensureOpen();
  if (!s_prefs.getUChar(setKey, 0))
    return false;
  const size_t sl = s_prefs.getString(ssidKey, out->ssid, sizeof(out->ssid));
  const size_t pl = s_prefs.getString(passKey, out->pass, sizeof(out->pass));
  if (sl == 0 || sl >= sizeof(out->ssid))
    return false;
  out->configured = true;
  (void)pl;
  return true;
}

bool saveCred(const char *ssidKey, const char *passKey, const char *setKey,
              const char *ssid, const char *pass)
{
  if (!dtgWifiValidateSsid(ssid) || !dtgWifiValidatePass(pass))
    return false;
  ensureOpen();
  s_prefs.putString(ssidKey, ssid);
  s_prefs.putString(passKey, pass ? pass : "");
  s_prefs.putUChar(setKey, 1);
  return true;
}

} // namespace

void dtgWifiStoreBegin()
{
  ensureOpen();
}

bool dtgWifiValidateSsid(const char *ssid)
{
  if (!ssid)
    return false;
  const size_t len = strlen(ssid);
  if (len == 0 || len > DTG_WIFI_SSID_MAX)
    return false;
  return !hasControlChar(ssid);
}

bool dtgWifiValidatePass(const char *pass)
{
  if (!pass)
    return true;
  const size_t len = strlen(pass);
  if (len > DTG_WIFI_PASS_MAX)
    return false;
  return !hasControlChar(pass);
}

bool dtgWifiStoreSeedPrimaryIfEmpty(const char *ssid, const char *pass)
{
  DtgWifiCredentials existing{};
  if (loadCred("pri_ssid", "pri_pass", "pri_set", &existing))
    return true;
  if (!dtgWifiValidateSsid(ssid))
    return false;
  if (!dtgWifiValidatePass(pass))
    return false;
  return saveCred("pri_ssid", "pri_pass", "pri_set", ssid, pass ? pass : "");
}

bool dtgWifiStoreLoadPrimary(DtgWifiCredentials *out)
{
  return loadCred("pri_ssid", "pri_pass", "pri_set", out);
}

bool dtgWifiStoreLoadBackup(DtgWifiCredentials *out)
{
  return loadCred("bak_ssid", "bak_pass", "bak_set", out);
}

bool dtgWifiStoreSavePrimary(const char *ssid, const char *pass)
{
  return saveCred("pri_ssid", "pri_pass", "pri_set", ssid, pass);
}

bool dtgWifiStoreSaveBackup(const char *ssid, const char *pass)
{
  return saveCred("bak_ssid", "bak_pass", "bak_set", ssid, pass);
}

void dtgWifiStoreClearBackup()
{
  ensureOpen();
  s_prefs.remove("bak_ssid");
  s_prefs.remove("bak_pass");
  s_prefs.putUChar("bak_set", 0);
}

namespace
{

bool applyConfiguredApPsk(char *out, size_t outLen)
{
  const char *cfg = DTG_WIFI_AP_PSK;
  if (!cfg || cfg[0] == '\0')
    return false;
  if (!dtgWifiValidatePass(cfg))
    return false;
  if (!out || outLen <= strlen(cfg))
    return false;
  ensureOpen();
  if (s_prefs.getString("ap_psk", out, outLen) >= 8)
    return true;
  strlcpy(out, cfg, outLen);
  s_prefs.putString("ap_psk", out);
  return true;
}

} // namespace

bool dtgWifiStoreGetOrCreateApPsk(char *out, size_t outLen)
{
  if (!out || outLen < 9)
    return false;
  ensureOpen();
  if (applyConfiguredApPsk(out, outLen))
    return true;

  const size_t existing = s_prefs.getString("ap_psk", out, outLen);
  if (existing >= 8 && existing < outLen)
    return true;

  const size_t len = DTG_WIFI_AP_PSK_LEN;
  if (outLen <= len)
    return false;
  const size_t csLen = sizeof(kCharset) - 1;
  for (size_t i = 0; i < len; ++i)
  {
    out[i] = kCharset[esp_random() % csLen];
  }
  out[len] = '\0';
  s_prefs.putString("ap_psk", out);
  return true;
}

bool dtgWifiStoreSetApPsk(const char *psk)
{
  if (!psk || strlen(psk) < 8 || strlen(psk) >= DTG_WIFI_AP_PSK_MAX)
    return false;
  if (hasControlChar(psk))
    return false;
  ensureOpen();
  s_prefs.putString("ap_psk", psk);
  return true;
}
