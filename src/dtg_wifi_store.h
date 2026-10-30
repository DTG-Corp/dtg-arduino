/**
 * @file dtg_wifi_store.h
 * @brief NVS persistence for primary/backup Wi-Fi and SoftAP PSK (dtg_wifi namespace).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef DTG_WIFI_SSID_MAX
#define DTG_WIFI_SSID_MAX 32
#endif
#ifndef DTG_WIFI_PASS_MAX
#define DTG_WIFI_PASS_MAX 64
#endif
#ifndef DTG_WIFI_AP_PSK_MAX
#define DTG_WIFI_AP_PSK_MAX 16
#endif

struct DtgWifiCredentials
{
  char ssid[DTG_WIFI_SSID_MAX + 1];
  char pass[DTG_WIFI_PASS_MAX + 1];
  bool configured;
};

/** Open NVS namespace (idempotent). */
void dtgWifiStoreBegin();

/** If primary not set, copy sketch begin() credentials into NVS. */
bool dtgWifiStoreSeedPrimaryIfEmpty(const char *ssid, const char *pass);

bool dtgWifiStoreLoadPrimary(DtgWifiCredentials *out);
bool dtgWifiStoreLoadBackup(DtgWifiCredentials *out);

bool dtgWifiStoreSavePrimary(const char *ssid, const char *pass);
bool dtgWifiStoreSaveBackup(const char *ssid, const char *pass);
void dtgWifiStoreClearBackup();

/** AP PSK: existing NVS value, else DTG_WIFI_AP_PSK if set, else random once in NVS. */
bool dtgWifiStoreGetOrCreateApPsk(char *out, size_t outLen);

/** Optional factory override (>= 8 chars). */
bool dtgWifiStoreSetApPsk(const char *psk);

bool dtgWifiValidateSsid(const char *ssid);
bool dtgWifiValidatePass(const char *pass);
