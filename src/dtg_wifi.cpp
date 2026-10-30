/**
 * @file dtg_wifi.cpp
 */
#include "dtg_wifi.h"

#include "dtg_debug.h"
#include "dtg_wifi_store.h"

#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>

namespace
{

bool dtgWifiGetIp(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 8 || WiFi.status() != WL_CONNECTED)
    return false;
  const IPAddress ip = WiFi.localIP();
  const int n = snprintf(buf, bufLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgWifiGetMac(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 18)
    return false;
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  const int n = snprintf(buf, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                         mac[1], mac[2], mac[3], mac[4], mac[5]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgWifiGetRssi(int *rssiDbm, void *userData)
{
  (void)userData;
  if (!rssiDbm || WiFi.status() != WL_CONNECTED)
    return false;
  const int rssi = WiFi.RSSI();
  if (rssi >= 0)
    return false;
  *rssiDbm = rssi;
  return true;
}

bool dtgWifiGetSsid(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 2 || WiFi.status() != WL_CONNECTED)
    return false;
  wifi_ap_record_t info{};
  if (esp_wifi_sta_get_ap_info(&info) != ESP_OK)
    return false;
  const size_t n = strnlen(reinterpret_cast<const char *>(info.ssid),
                           DTG_WIFI_SSID_MAX);
  if (n == 0 || n >= bufLen)
    return false;
  memcpy(buf, info.ssid, n);
  buf[n] = '\0';
  return true;
}

} // namespace

void DtgWiFi::installWifiRuntimeProvider()
{
  wifiRuntimeProvider_.getIp = dtgWifiGetIp;
  wifiRuntimeProvider_.getMac = dtgWifiGetMac;
  wifiRuntimeProvider_.getSsid = dtgWifiGetSsid;
  wifiRuntimeProvider_.getRssi = dtgWifiGetRssi;
  wifiRuntimeProvider_.networkType = "wifi";
  wifiRuntimeProvider_.userData = nullptr;
  setRuntimeInfoProvider(&wifiRuntimeProvider_);
}

DtgWiFi::DtgWiFi(const char *projectId, const char *gwAlias, const char *mqttUser,
                 const char *mqttPass, const char *clientId)
    : DtgClient(projectId, gwAlias, mqttUser, mqttPass, clientId)
{
  provisioner_.attach(this);
}

void DtgWiFi::setBootConfigPin(uint8_t pin)
{
  provisioner_.setBootConfigPin(pin);
  pinMode(pin, INPUT_PULLUP);
}

void DtgWiFi::applyPreferredDns()
{
  IPAddress dns1(DTG_DNS_PRIMARY_A, DTG_DNS_PRIMARY_B, DTG_DNS_PRIMARY_C,
                 DTG_DNS_PRIMARY_D);
  const bool ok =
      WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1);
  DTG_LOG("Wi-Fi DNS override %s", ok ? "ok" : "failed");
}

void DtgWiFi::beginStaProfile(const DtgWifiCredentials &cred)
{
  if (!cred.configured)
    return;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.begin(cred.ssid, cred.pass);
  staFailSinceMs_ = millis();
  dnsApplied_ = false;
  DTG_LOG("Wi-Fi connecting to %s...", cred.ssid);
}

void DtgWiFi::begin(const char *wifiSsid, const char *wifiPass, bool useTls,
                    bool tlsInsecure)
{
  applyWatchdogDefaultIfUnset();
  useTlsTransport_ = useTls;
  setUseTls(useTls);
  setTlsInsecure(tlsInsecure);
  printBootBannerOnce();

  if (!mqttBrokerConfigured_)
  {
    const uint16_t port =
        useTls ? static_cast<uint16_t>(DTG_MQTT_PORT_TLS)
               : static_cast<uint16_t>(DTG_MQTT_PORT_PLAIN);
    setMqttEndpoint(DTG_MQTT_HOST, port);
  }

  if (useTlsTransport_)
    attachNetworkClient(tlsClient_);
  else
    attachNetworkClient(plainClient_);

  dtgWifiStoreBegin();
#if DTG_WIFI_FORCE_SKETCH_CREDS
  if (wifiSsid && wifiSsid[0] != '\0' && dtgWifiValidateSsid(wifiSsid) &&
      dtgWifiValidatePass(wifiPass))
  {
    (void)dtgWifiStoreSavePrimary(wifiSsid, wifiPass ? wifiPass : "");
    DTG_LOG("Wi-Fi credentials synced from sketch (force)");
  }
#else
  (void)dtgWifiStoreSeedPrimaryIfEmpty(wifiSsid, wifiPass);
#endif

  DtgWifiCredentials pri{};
  if (!dtgWifiStoreLoadPrimary(&pri))
  {
    DTG_LOG("No primary Wi-Fi in NVS - using sketch credentials");
    strlcpy(pri.ssid, wifiSsid ? wifiSsid : "", sizeof(pri.ssid));
    strlcpy(pri.pass, wifiPass ? wifiPass : "", sizeof(pri.pass));
    pri.configured = pri.ssid[0] != '\0';
  }

  provisioner_.setState(DtgWifiState::StaPrimary);
  beginStaProfile(pri);
  // Modem sleep (DTIM) - keep radio cool when STA is idle between beacons.
  WiFi.setSleep(true);
  installWifiRuntimeProvider();
#if DTG_WIFI_BOOT_PIN != 255
  setBootConfigPin(DTG_WIFI_BOOT_PIN);
#endif
}

void DtgWiFi::maintainSta(unsigned long nowMs)
{
  const DtgWifiState st = provisioner_.state();
  if (st == DtgWifiState::ApSetup)
    return;

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!dnsApplied_)
    {
      applyPreferredDns();
      dnsApplied_ = true;
    }
    staFailSinceMs_ = 0;

    if (st == DtgWifiState::OfflineWatch)
    {
      provisioner_.setState(DtgWifiState::StaPrimary);
    }
    return;
  }

  if (staFailSinceMs_ == 0)
    staFailSinceMs_ = nowMs;

  if (st == DtgWifiState::StaPrimary &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials bak{};
    if (dtgWifiStoreLoadBackup(&bak))
    {
      DTG_LOG("Primary timeout - failing over to backup Wi-Fi");
      provisioner_.setState(DtgWifiState::StaBackup);
      beginStaProfile(bak);
      return;
    }
    provisioner_.setState(DtgWifiState::OfflineWatch);
    staFailSinceMs_ = nowMs;
    DTG_LOG("Primary timeout - no backup, offline watch");
    return;
  }

  if (st == DtgWifiState::StaBackup &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials pri{};
    if (dtgWifiStoreLoadPrimary(&pri))
    {
      DTG_LOG("Backup timeout - retrying primary Wi-Fi");
      provisioner_.setState(DtgWifiState::StaPrimary);
      beginStaProfile(pri);
      return;
    }
    provisioner_.setState(DtgWifiState::OfflineWatch);
    staFailSinceMs_ = nowMs;
    DTG_LOG("Backup timeout - no primary, offline watch");
    return;
  }

  if (st == DtgWifiState::OfflineWatch &&
      nowMs - staFailSinceMs_ >= DTG_WIFI_FAILOVER_MS)
  {
    DtgWifiCredentials pri{};
    if (dtgWifiStoreLoadPrimary(&pri))
    {
      DTG_LOG("Offline watch - retry primary Wi-Fi (%s)", pri.ssid);
      provisioner_.setState(DtgWifiState::StaPrimary);
      beginStaProfile(pri);
      staFailSinceMs_ = nowMs;
    }
  }
}

bool DtgWiFi::netLayerReady() const
{
  return WiFi.status() == WL_CONNECTED;
}

bool DtgWiFi::run()
{
  const unsigned long now = millis();
  const bool link = netLayerReady();
  const bool mqttUp = connected();

  provisioner_.tick(now, link, mqttUp);

  if (provisioner_.state() == DtgWifiState::ApSetup)
  {
    (void)provisioner_.handleApSetup(now);
    loopIdle();
    return false;
  }

  maintainSta(now);
  return DtgClient::run();
}

void DtgWiFi::configureTransportBeforeMqttConnect()
{
  if (!useTlsTransport_)
    return;
  if (tlsInsecure_)
    tlsClient_.setInsecure();
  else
    tlsClient_.setCACert(caCert());
}
