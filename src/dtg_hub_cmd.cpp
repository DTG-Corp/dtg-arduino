/**
 * @file dtg_hub_cmd.cpp
 */
#include "dtg_hub_cmd.h"

#if DTG_FEATURE_HUB_CMD

#include "dtg_client.h"
#include "dtg_debug.h"
#include "dtg_modem_lifecycle.h"
#include "dtg_ota.h"
#include "dtg_wifi_store.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <string.h>

namespace detail
{

namespace
{
static DTG_OtaStartHandler s_otaStartHandler = nullptr;
static JsonDocument s_otaCmdDoc;
static JsonDocument s_rebootCmdDoc;
static JsonDocument s_wifiCmdDoc;

static bool restoreCreds(const DtgWifiCredentials &oldPri,
                         const DtgWifiCredentials &oldBak, bool hadBak)
{
  if (!oldPri.configured)
    return false;
  if (!dtgWifiStoreSavePrimary(oldPri.ssid, oldPri.pass))
    return false;
  if (hadBak)
    (void)dtgWifiStoreSaveBackup(oldBak.ssid, oldBak.pass);
  else
    dtgWifiStoreClearBackup();
  return true;
}

static bool waitStaLinked(unsigned long deadlineMs)
{
  while (static_cast<long>(deadlineMs - millis()) > 0)
  {
    esp_task_wdt_reset();
    if (WiFi.status() == WL_CONNECTED)
      return true;
    delay(100);
  }
  return false;
}

} // namespace

void HubCmd::publishWifiCmdAck(DtgClient *client, const char *cmdId, const char *st,
                               const char *reason, const char *ip)
{
  if (!client || !cmdId || !cmdId[0] || !st)
    return;
  char t[128];
  snprintf(t, sizeof(t), "%sup/cmd_ack", client->topicPrefix_);
  JsonDocument ack;
  ack["id"] = cmdId;
  ack["st"] = st;
  ack["stream"] = "wifi";
  if (reason && reason[0])
    ack["reason"] = reason;
  if (ip && ip[0])
    ack["ip"] = ip;
  char buf[192];
  size_t n = serializeJson(ack, buf, sizeof(buf));
  (void)client->mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf),
                             static_cast<unsigned int>(n), false);
}

bool HubCmd::waitMqttConnected(DtgClient *client, unsigned long deadlineMs)
{
  if (!client)
    return false;
  while (static_cast<long>(deadlineMs - millis()) > 0)
  {
    esp_task_wdt_reset();
    if (client->connectMqttSession())
      return true;
    delay(200);
  }
  return false;
}

void HubCmd::handleSetWifi(DtgClient *client, const uint8_t *payload, unsigned int len)
{
  if (!client || len == 0 || len > 512)
    return;
  // SoftAP setup has no MQTT path; still reject WIFI_AP-only mode.
  if (WiFi.getMode() == WIFI_AP)
    return;

  s_wifiCmdDoc.clear();
  DeserializationError derr = deserializeJson(s_wifiCmdDoc, payload, len);
  if (derr)
    return;

  const char *action = s_wifiCmdDoc["a"] | s_wifiCmdDoc["action"] | "";
  if (strcasecmp(action, "set_wifi") != 0)
    return;

  const char *cmdId = s_wifiCmdDoc["id"] | "";
  const char *ps = s_wifiCmdDoc["ps"] | "";
  const char *pp = s_wifiCmdDoc["pp"] | "";
  const bool hasBak = !s_wifiCmdDoc["bs"].isNull();
  const char *bs = s_wifiCmdDoc["bs"] | "";
  const char *bp = s_wifiCmdDoc["bp"] | "";
  unsigned long vf = s_wifiCmdDoc["vf"] | 45000UL;
  if (vf < 10000UL)
    vf = 10000UL;
  if (vf > 120000UL)
    vf = 120000UL;

  if (!dtgWifiValidateSsid(ps) || !dtgWifiValidatePass(pp))
  {
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "auth", nullptr);
    return;
  }
  if (hasBak && bs[0] && (!dtgWifiValidateSsid(bs) || !dtgWifiValidatePass(bp)))
  {
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "auth", nullptr);
    return;
  }

  dtgWifiStoreBegin();
  DtgWifiCredentials oldPri{};
  DtgWifiCredentials oldBak{};
  const bool hadPri = dtgWifiStoreLoadPrimary(&oldPri);
  const bool hadBak = dtgWifiStoreLoadBackup(&oldBak);
  if (!hadPri)
    oldPri.configured = false;

  if (cmdId[0])
    publishWifiCmdAck(client, cmdId, "accepted", nullptr, nullptr);
  client->mqtt.loop();
  delay(50);

  if (!dtgWifiStoreSavePrimary(ps, pp ? pp : ""))
  {
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "auth", nullptr);
    return;
  }
  if (hasBak)
  {
    if (bs[0])
      (void)dtgWifiStoreSaveBackup(bs, bp ? bp : "");
    else
      dtgWifiStoreClearBackup();
  }

  // ponytail: blocking verify up to vf ms with WDT feeds; async state machine if loop stalls matter
  client->disconnectMqtt();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(150);
  WiFi.begin(ps, pp ? pp : "");

  const unsigned long startMs = millis();
  const unsigned long deadline = startMs + vf;
  if (!waitStaLinked(deadline))
  {
    DTG_LOG("[WIFI] verify failed - STA link timeout");
    (void)restoreCreds(oldPri, oldBak, hadBak);
    if (oldPri.configured)
    {
      WiFi.disconnect(true, true);
      delay(100);
      WiFi.begin(oldPri.ssid, oldPri.pass);
      (void)waitStaLinked(millis() + 20000UL);
    }
    (void)waitMqttConnected(client, millis() + 15000UL);
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "timeout", nullptr);
    return;
  }

  if (!waitMqttConnected(client, deadline))
  {
    DTG_LOG("[WIFI] verify failed - MQTT timeout");
    (void)restoreCreds(oldPri, oldBak, hadBak);
    WiFi.disconnect(true, true);
    delay(100);
    if (oldPri.configured)
      WiFi.begin(oldPri.ssid, oldPri.pass);
    (void)waitStaLinked(millis() + 20000UL);
    (void)waitMqttConnected(client, millis() + 15000UL);
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "timeout", nullptr);
    return;
  }

  char ipBuf[48] = {};
  const IPAddress ip = WiFi.localIP();
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  if (!ipBuf[0] || strcmp(ipBuf, "0.0.0.0") == 0)
  {
    DTG_LOG("[WIFI] verify failed - no IP");
    (void)restoreCreds(oldPri, oldBak, hadBak);
    WiFi.disconnect(true, true);
    delay(100);
    if (oldPri.configured)
      WiFi.begin(oldPri.ssid, oldPri.pass);
    (void)waitStaLinked(millis() + 20000UL);
    (void)waitMqttConnected(client, millis() + 15000UL);
    if (cmdId[0])
      publishWifiCmdAck(client, cmdId, "fail", "no_ip", nullptr);
    return;
  }

  // Commit: demote previous primary to backup when caller did not set backup.
  if (!hasBak && hadPri && oldPri.configured && strcmp(oldPri.ssid, ps) != 0)
    (void)dtgWifiStoreSaveBackup(oldPri.ssid, oldPri.pass);

  if (cmdId[0])
    publishWifiCmdAck(client, cmdId, "ok", nullptr, ipBuf);
  (void)client->publishDeviceInfo();
  client->mqtt.loop();
  delay(150);
  DTG_LOG("[WIFI] verified - rebooting onto new network");
  dtgInvokeModemPrepareForRestart();
  esp_task_wdt_reset();
  delay(500);
  ESP.restart();
}

HubCmd &HubCmd::instance()
{
  static HubCmd inst;
  return inst;
}

void HubCmd::attach(DtgClient *client)
{
  client_ = client;
#if DTG_FEATURE_OTA
  // After HTTPS OTA, resume MQTT TLS so terminal ota_status reaches Hub before reboot.
  dtgOtaSetMqttResumeHandler([]() -> bool {
    DtgClient *c = HubCmd::instance().client_;
    return c != nullptr && c->connectMqttSession();
  });
#endif
}

void HubCmd::setCommandChain(DTG_CommandCallback cb, void *userData)
{
  chainCb_ = cb;
  chainUser_ = userData;
}

void HubCmd::commandCb(const char *deviceAlias, const char *streamId,
                       const uint8_t *payload, unsigned int len, void *)
{
  HubCmd &self = instance();
  if (!self.client_ || !streamId || !streamId[0])
    return;

#if DTG_FEATURE_OTA
  if (strcasecmp(streamId, "ota") == 0)
  {
    DTG_LOG("[OTA] rx stream=ota dev=%s len=%u", deviceAlias ? deviceAlias : "?",
            static_cast<unsigned int>(len));
    s_otaCmdDoc.clear();
    DeserializationError derr = deserializeJson(s_otaCmdDoc, payload, len);
    if (derr)
    {
      DTG_LOG("[OTA] JSON parse error: %s", derr.c_str());
      return;
    }
    const char *action = s_otaCmdDoc["action"] | "";
    if (strcasecmp(action, "start_ota") != 0)
      return;
    const char *jobId = s_otaCmdDoc["jobId"] | "";
    const char *url = s_otaCmdDoc["url"] | "";
    if (!jobId[0] || !url[0])
      return;
    if (s_otaStartHandler)
    {
      s_otaStartHandler(jobId, url);
    }
    else
    {
      const char *checksum = s_otaCmdDoc["checksum"] | "";
      const uint32_t size = s_otaCmdDoc["size"] | 0U;
      (void)hubOtaPerformFromUrl(self.client_->mqtt, self.client_->topicPrefix(), jobId, url,
                                 checksum, size, self.client_->caCert());
    }
    return;
  }
#endif

  if (strcasecmp(streamId, "reboot") == 0)
  {
    if (len == 0 || len > 256)
      return;
    s_rebootCmdDoc.clear();
    DeserializationError derr = deserializeJson(s_rebootCmdDoc, payload, len);
    if (derr)
      return;
    const char *action = s_rebootCmdDoc["action"] | "";
    if (action[0] && strcasecmp(action, "reboot") != 0)
      return;
    const char *cmdId = s_rebootCmdDoc["id"] | "";
    if (cmdId[0])
    {
      char t[128];
      snprintf(t, sizeof(t), "%sup/cmd_ack", self.client_->topicPrefix_);
      JsonDocument ack;
      ack["id"] = cmdId;
      ack["st"] = "accepted";
      ack["stream"] = "reboot";
      char buf[128];
      size_t n = serializeJson(ack, buf, sizeof(buf));
      (void)self.client_->mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf),
                                       static_cast<unsigned int>(n), false);
    }
    DTG_LOG("[REBOOT] remote reboot requested");
    self.client_->mqtt.loop();
    delay(150);
    dtgInvokeModemPrepareForRestart();
    esp_task_wdt_reset();
    delay(500);
    ESP.restart();
    return;
  }

  if (strcasecmp(streamId, "wifi") == 0)
  {
    handleSetWifi(self.client_, payload, len);
    return;
  }

  if (self.chainCb_)
    self.chainCb_(deviceAlias, streamId, payload, len, self.chainUser_);
}

} // namespace detail

#endif // DTG_FEATURE_HUB_CMD
