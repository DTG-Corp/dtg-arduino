/**
 * @file dtg_client.cpp
 */
#include "dtg_client.h"

#include "dtg_banner.h"
#include "dtg_debug.h"
#include "dtg_dispatch.h"
#include "dtg_feature_runtime.h"
#include "dtg_hub_cmd.h"
#if DTG_FEATURE_NOCODE
#include "dtg_nocode.h"
#endif
#if DTG_FEATURE_MODBUS
#include "modbus/modbus_manager.h"
#include "modbus/modbus_downlink.h"
#endif

#include <esp_random.h>
#include <esp_task_wdt.h>
#ifdef ESP32
#include <esp_heap_caps.h>
#endif
#include <string.h>

#if defined(ESP32)
#include <esp_heap_caps.h>
#include <esp_system.h>
#endif

#include "dtg_heap_telemetry.h"
#include "dtg_heap_guard.h"

extern void dtgOnConnected();

DtgClient *DtgClient::instance_ = nullptr;

#if defined(ESP32)
static uint8_t s_snapResetReason = 255;
static size_t s_mqttBufferBytes = DTG_MQTT_BUFFER_CORE;

static void captureSnapResetReasonOnce()
{
  if (s_snapResetReason != 255)
    return;
  s_snapResetReason = static_cast<uint8_t>(esp_reset_reason());
}
#endif

/** Print MQTT topic + payload via DTG_LOG ([file:line] + tag). */
static void dtgTraceMqtt(const char *event, const char *topic, const uint8_t *payload,
                         unsigned int len, unsigned int previewMax = 96)
{
  if (!event)
    return;

  const char *tag = dtgLogMqttTag(event);
  const char *t = topic ? topic : "(null)";

  if (!payload || len == 0)
  {
    DTG_LOG("%s topic=%s payload=(empty)", tag, t);
    return;
  }

  char pay[97];
  unsigned int n = (len < previewMax) ? len : previewMax;
  if (n >= sizeof(pay))
    n = sizeof(pay) - 1U;
  memcpy(pay, payload, n);
  pay[n] = '\0';
  DTG_LOG("%s topic=%s payload=%s%s", tag, t, pay, (len > n) ? "..." : "");
}

static void copyStr(char *dst, size_t dstLen, const char *src)
{
  if (!dst || dstLen == 0)
    return;
  if (!src)
  {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

/** Escape a JSON string value (no surrounding quotes). Rejects control chars. */
static bool jsonEscapeField(const char *src, char *dst, size_t dstLen)
{
  if (!dst || dstLen == 0)
    return false;
  if (!src)
  {
    dst[0] = '\0';
    return true;
  }
  size_t j = 0;
  for (const char *p = src; *p; ++p)
  {
    const char c = *p;
    if (c < 0x20)
      return false;
    if (c == '"' || c == '\\')
    {
      if (j + 2 >= dstLen)
        return false;
      dst[j++] = '\\';
      dst[j++] = c;
    }
    else
    {
      if (j + 1 >= dstLen)
        return false;
      dst[j++] = c;
    }
  }
  dst[j] = '\0';
  return true;
}

DtgClient::DtgClient(const char *projectId, const char *gwAlias,
                                 const char *mqttUser, const char *mqttPass,
                                 const char *clientId)
{
  copyStr(projectId_, sizeof(projectId_), projectId);
  copyStr(gwAlias_, sizeof(gwAlias_), gwAlias);
  copyStr(mqttUser_, sizeof(mqttUser_), mqttUser);
  copyStr(mqttPass_, sizeof(mqttPass_), mqttPass);
  copyStr(clientId_, sizeof(clientId_), clientId);
  instance_ = this;
  mqtt.setBufferSize(DTG_MQTT_BUFFER_CORE);
  mqtt.setCallback(staticMqttCallback);
  snprintf(topicPrefix_, sizeof(topicPrefix_), "%s/%s/%s/", DTG_TOPIC_ROOT, projectId_,
           gwAlias_);
}

DtgClient::~DtgClient()
{
  if (instance_ == this)
  {
    instance_ = nullptr;
  }
}

void DtgClient::attachNetworkClient(Client &client)
{
  netClient_ = &client;
  mqtt.setClient(client);
}

void DtgClient::onCommand(DTG_CommandCallback cb, void *userData)
{
  cmdCb_ = cb;
  cmdUser_ = userData;
}

void DtgClient::onConfig(DTG_ConfigCallback cb, void *userData)
{
  cfgCb_ = cb;
  cfgUser_ = userData;
}

void DtgClient::setLogStream(Stream *stream)
{
  dtgLogStream = stream;
}

void DtgClient::printBootBannerOnce()
{
#ifndef DTG_NO_DEFAULT_BANNER
  if (!bootStream_ || bootBannerPrinted_)
    return;
  detail::printBootBanner(*bootStream_);
  bootBannerPrinted_ = true;
#endif
}

void DtgClient::setCaCert(const char *pem)
{
  customCaPem_ = (pem && pem[0]) ? pem : nullptr;
}

const char *DtgClient::caCert() const
{
  return (customCaPem_ && customCaPem_[0]) ? customCaPem_ : DTG_TLS_CA_BUNDLE;
}

void DtgClient::setMqttBroker(const char *host, uint16_t port)
{
  if (host && host[0])
  {
    copyStr(mqttHostBuf_, sizeof(mqttHostBuf_), host);
    mqttBrokerConfigured_ = true;
    setMqttEndpoint(mqttHostBuf_, port);
  }
  else
  {
    mqttHostBuf_[0] = '\0';
    mqttBrokerConfigured_ = false;
    setMqttEndpoint(DTG_MQTT_HOST, port);
  }
}

void DtgClient::setMqttBufferSize(size_t bytes)
{
  if (bytes < 256)
    bytes = 256;
  mqtt.setBufferSize(bytes);
#if defined(ESP32)
  s_mqttBufferBytes = bytes;
#endif
}

void DtgClient::refreshMqttBufferForFeatures()
{
  size_t need = DTG_MQTT_BUFFER_CORE;
#if DTG_FEATURE_MODBUS
  if (modbusEnabled_)
    need = DTG_MQTT_BUFFER_MODBUS;
#endif
#if DTG_FEATURE_NOCODE
  if (noCodeIoEnabled_ && need < DTG_MQTT_BUFFER_NOCODE)
    need = DTG_MQTT_BUFFER_NOCODE;
#endif
  setMqttBufferSize(need);
}

bool DtgClient::subscribeTopicSuffix(const char *suffix, uint8_t qos)
{
  if (!suffix || !mqtt.connected())
    return false;
  char sub[128];
  snprintf(sub, sizeof(sub), "%s%s", topicPrefix_, suffix);
  return mqtt.subscribe(sub, qos);
}

bool DtgClient::unsubscribeTopicSuffix(const char *suffix)
{
  if (!suffix || !mqtt.connected())
    return false;
  char sub[128];
  snprintf(sub, sizeof(sub), "%s%s", topicPrefix_, suffix);
  return mqtt.unsubscribe(sub);
}

#if DTG_FEATURE_HUB_CMD
  void DtgClient::enableHubCmd()
  {
    if (hubCmdEnabled_)
      return;
    hubCmdEnabled_ = true;
    detail::HubCmd::instance().attach(this);
    onCommand(detail::HubCmd::commandCb, nullptr);
    // Listen-first chain so DTG_LISTEN works with or without no-code.
    detail::HubCmd::instance().setCommandChain(dtgListenCommandChain, nullptr);
    DTG_LOG("Hub command channel enabled (OTA/reboot)");
  }
#else
  void DtgClient::enableHubCmd() {}
#endif

void DtgClient::enablePlatformFeatures()
{
  if (platformFeaturesEnabled_)
    return;
  platformFeaturesEnabled_ = true;
  detail::DtgFeatureRuntime::instance().attach(this);
  // Always own down/cfg (period uplink / credentials / full_sync). Do not wait for
  // caps.nocode - Hub pushes set_period_uplink even with zero GPIO streams.
  activateNocodeRuntime();
  DTG_LOG("Platform feature runtime enabled (down/gw/caps + down/cfg)");
}

#if DTG_FEATURE_NOCODE
void DtgClient::enableNoCodeIo()
{
  if (noCodeIoEnabled_)
    return;
  legacyForceFeatures_ = true;
  activateNocodeRuntime();
  DTG_LOG("Hub no-code I/O enabled (legacy force-on)");
}
#else
void DtgClient::enableNoCodeIo() {}
#endif

uint32_t DtgClient::periodUplink() const
{
#if DTG_FEATURE_NOCODE
  return detail::HubNoCodeIo::instance().periodUplink();
#else
  return DTG_PERIOD_UPLINK_DEFAULT_MS;
#endif
}

void DtgClient::setPeriodUplink(uint32_t ms)
{
#if DTG_FEATURE_NOCODE
  detail::HubNoCodeIo::instance().setPeriodUplink(ms);
#else
  (void)ms;
#endif
}

void DtgClient::activateNocodeRuntime()
{
#if DTG_FEATURE_NOCODE
  if (noCodeIoEnabled_)
    return;
  detail::HubNoCodeIo::instance().attach(this);
  onConfig(detail::HubNoCodeIo::configCb, nullptr);
#if DTG_FEATURE_HUB_CMD
  // Listen handlers own matching streams; unmatched fall through to no-code GPIO.
  detail::HubCmd::instance().setCommandChain(dtgListenCommandChain, nullptr);
#else
  onCommand(dtgListenCommandChain, nullptr);
#endif
  noCodeIoEnabled_ = true;
  refreshMqttBufferForFeatures();
  if (mqtt.connected())
  {
    subscribeTopicSuffix("down/cfg", 1);
    if (!hubCmdEnabled_)
      subscribeTopicSuffix("down/c/#", 1);
  }
  DTG_LOG("No-code runtime active");
#endif
}

void DtgClient::deactivateNocodeRuntime()
{
#if DTG_FEATURE_NOCODE
  if (!noCodeIoEnabled_)
    return;
  unsubscribeTopicSuffix("down/cfg");
  detail::HubNoCodeIo::instance().detach();
  if (!hubCmdEnabled_)
    unsubscribeTopicSuffix("down/c/#");
  onConfig(nullptr, nullptr);
#if DTG_FEATURE_HUB_CMD
  // Keep listen dispatcher after no-code off (DTG_LISTEN still works).
  detail::HubCmd::instance().setCommandChain(dtgListenCommandChain, nullptr);
#else
  onCommand(dtgListenCommandChain, nullptr);
#endif
  noCodeIoEnabled_ = false;
  refreshMqttBufferForFeatures();
  DTG_LOG("No-code runtime deactivated");
#endif
}

#if DTG_FEATURE_MODBUS
void DtgClient::enableModbus()
{
  if (modbusEnabled_)
    return;
  legacyForceFeatures_ = true;
  activateModbusRuntime();
  DTG_LOG("Modbus no-code enabled (legacy force-on)");
}
#else
void DtgClient::enableModbus() {}
#endif

void DtgClient::activateModbusRuntime()
{
#if DTG_FEATURE_MODBUS
  if (modbusEnabled_)
    return;
  detail::ModbusManager::instance().attach(this);
  modbusCfgCb_ = detail::ModbusManager::configCb;
  modbusCfgUser_ = nullptr;
  modbusEnabled_ = true;
  refreshMqttBufferForFeatures();
  if (mqtt.connected())
    subscribeTopicSuffix("down/modbus/#", 1);
  DTG_LOG("Modbus runtime active");
#endif
}

void DtgClient::deactivateModbusRuntime()
{
#if DTG_FEATURE_MODBUS
  if (!modbusEnabled_)
    return;
  unsubscribeTopicSuffix("down/modbus/#");
  detail::ModbusManager::instance().shutdown();
  modbusCfgCb_ = nullptr;
  modbusCfgUser_ = nullptr;
  modbusEnabled_ = false;
  refreshMqttBufferForFeatures();
  DTG_LOG("Modbus runtime deactivated");
#endif
}

bool DtgClient::publishTopicSuffix(const char *suffix, const char *payloadUtf8,
                                   bool retain, uint8_t qos)
{
  if (!suffix || !payloadUtf8 || !mqtt.connected())
    return false;
  char topic[160];
  snprintf(topic, sizeof(topic), "%s%s", topicPrefix_, suffix);
  return mqtt.publish(topic, payloadUtf8, retain);
}

void DtgClient::setDefaultDeviceAlias(const char *alias)
{
  copyStr(defaultDevice_, sizeof(defaultDevice_), alias ? alias : "_");
}

void DtgClient::setBrokerIpv4(const char *dottedIpv4)
{
  if (!dottedIpv4 || !dottedIpv4[0])
  {
    mqttUseIp_ = false;
    return;
  }
  if (mqttIp_.fromString(dottedIpv4))
  {
    mqttUseIp_ = true;
    mqtt.setServer(mqttIp_, mqttPort_);
  }
}

void DtgClient::setMqttEndpoint(const char *host, uint16_t port)
{
  mqttHost_ = host;
  mqttPort_ = port;
  mqttUseIp_ = false;
  mqtt.setServer(mqttHost_, mqttPort_);
}

void DtgClient::setMqttEndpoint(const IPAddress &ip, uint16_t port)
{
  mqttIp_ = ip;
  mqttPort_ = port;
  mqttUseIp_ = true;
  mqtt.setServer(mqttIp_, mqttPort_);
}

bool DtgClient::publishOnline()
{
  if (!mqtt.connected())
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%sup/status", topicPrefix_);
  return mqtt.publish(t, "{\"ol\":1}", true);
}

void DtgClient::setRuntimeInfoProvider(const DtgRuntimeInfoProvider *provider)
{
  if (!provider)
  {
    runtimeProviderSet_ = false;
    memset(&runtimeProvider_, 0, sizeof(runtimeProvider_));
    return;
  }
  runtimeProvider_ = *provider;
  runtimeProviderSet_ = true;
}

void DtgClient::setApSetupReport(bool active, const char *ssid, const char *ip)
{
  apReportActive_ = active;
  apReportSsid_[0] = '\0';
  apReportIp_[0] = '\0';
  if (active && ssid)
    copyStr(apReportSsid_, sizeof(apReportSsid_), ssid);
  if (active && ip)
    copyStr(apReportIp_, sizeof(apReportIp_), ip);
  lastInfoPayload_[0] = '\0';
}

bool DtgClient::buildDeviceInfoPayload(char *buf, size_t bufLen)
{
  if (!buf || bufLen < 32)
    return false;

  char ipRaw[48] = {};
  char macRaw[20] = {};
  char ip[96] = {};
  char mac[40] = {};
  char fw[64] = {};
  char netEsc[32] = {};
  const char *net =
      (runtimeProviderSet_ && runtimeProvider_.networkType &&
       runtimeProvider_.networkType[0])
          ? runtimeProvider_.networkType
          : "unknown";

  if (runtimeProviderSet_ && runtimeProvider_.getIp)
  {
    runtimeProvider_.getIp(ipRaw, sizeof(ipRaw), runtimeProvider_.userData);
  }
  if (runtimeProviderSet_ && runtimeProvider_.getMac)
  {
    runtimeProvider_.getMac(macRaw, sizeof(macRaw), runtimeProvider_.userData);
  }

  if (ipRaw[0] && !jsonEscapeField(ipRaw, ip, sizeof(ip)))
    ip[0] = '\0';
  if (macRaw[0] && !jsonEscapeField(macRaw, mac, sizeof(mac)))
    mac[0] = '\0';
  if (!jsonEscapeField(DTG_FIRMWARE_VERSION, fw, sizeof(fw)))
    return false;
  if (!jsonEscapeField(net, netEsc, sizeof(netEsc)))
    copyStr(netEsc, sizeof(netEsc), "unknown");

  int pos = snprintf(buf, bufLen, "{\"v\":1");
  if (pos <= 0 || (size_t)pos >= bufLen)
    return false;
  if (ip[0])
  {
    const int n = snprintf(buf + pos, bufLen - (size_t)pos, ",\"ip\":\"%s\"", ip);
    if (n <= 0)
      return false;
    pos += n;
  }
  if (mac[0])
  {
    const int n = snprintf(buf + pos, bufLen - (size_t)pos, ",\"mac\":\"%s\"", mac);
    if (n <= 0)
      return false;
    pos += n;
  }
  {
    int n = snprintf(
        buf + pos, bufLen - (size_t)pos,
        ",\"fw\":\"%s\",\"net\":\"%s\",\"profile\":\"%s\"",
        fw, netEsc, DTG_PROFILE_NAME);
    if (n <= 0)
      return false;
    pos += n;
  }
  // Connected STA SSID (wifi only) - distinct from SoftAP apSsid.
  if (runtimeProviderSet_ && runtimeProvider_.getSsid &&
      runtimeProvider_.networkType &&
      strcmp(runtimeProvider_.networkType, "wifi") == 0)
  {
    char ssidRaw[33] = {};
    char ssidEsc[40] = {};
    if (runtimeProvider_.getSsid(ssidRaw, sizeof(ssidRaw),
                                 runtimeProvider_.userData) &&
        ssidRaw[0] && jsonEscapeField(ssidRaw, ssidEsc, sizeof(ssidEsc)))
    {
      const int n =
          snprintf(buf + pos, bufLen - (size_t)pos, ",\"ssid\":\"%s\"", ssidEsc);
      if (n <= 0)
        return false;
      pos += n;
    }
  }
  if (apReportActive_ && apReportSsid_[0] && apReportIp_[0])
  {
    char ssidEsc[40] = {};
    char ipEsc[48] = {};
    if (!jsonEscapeField(apReportSsid_, ssidEsc, sizeof(ssidEsc)) ||
        !jsonEscapeField(apReportIp_, ipEsc, sizeof(ipEsc)))
      return false;
    const int n = snprintf(buf + pos, bufLen - (size_t)pos,
                           ",\"ap\":1,\"apSsid\":\"%s\",\"apIp\":\"%s\"}", ssidEsc,
                           ipEsc);
    if (n <= 0 || (size_t)(pos + n) >= bufLen)
      return false;
  }
  else
  {
    const int n = snprintf(buf + pos, bufLen - (size_t)pos, "}");
    if (n <= 0 || (size_t)(pos + n) >= bufLen)
      return false;
  }
  return true;
}

bool DtgClient::buildSnapshotPayload(char *buf, size_t bufLen)
{
  if (!buf || bufLen < 32)
    return false;

  if (!runtimeProviderSet_ || !runtimeProvider_.getRssi)
    return false;

  int rssi = 0;
  if (!runtimeProvider_.getRssi(&rssi, runtimeProvider_.userData) || rssi >= 0)
    return false;

  const char *net =
      (runtimeProviderSet_ && runtimeProvider_.networkType &&
       runtimeProvider_.networkType[0])
          ? runtimeProvider_.networkType
          : "unknown";
  char netEsc[32] = {};
  const unsigned long upSec = millis() / 1000UL;

  if (!jsonEscapeField(net, netEsc, sizeof(netEsc)))
    copyStr(netEsc, sizeof(netEsc), "unknown");

  char base[160] = {};
  const int baseLen = snprintf(base, sizeof(base),
                               "{\"v\":1,\"rssi\":%d,\"rssiUnit\":\"dBm\",\"net\":"
                               "\"%s\",\"up\":%lu",
                               rssi, netEsc, upSec);
  if (baseLen <= 0 || (size_t)baseLen >= sizeof(base))
    return false;

#if defined(ESP32)
  captureSnapResetReasonOnce();
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t minHeap = ESP.getMinFreeHeap();
  const uint32_t largestBlk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  char heapFields[96] = {};
  if (dtgFormatHeapTelemetryFields(heapFields, sizeof(heapFields), freeHeap, minHeap,
                                   largestBlk) < 0)
    return false;
  char tail[48] = {};
  int tailLen = snprintf(tail, sizeof(tail), ",\"mqttBuf\":%u,\"rst\":%u",
                         static_cast<unsigned>(s_mqttBufferBytes),
                         static_cast<unsigned>(s_snapResetReason));
  if (tailLen <= 0 || (size_t)tailLen >= sizeof(tail))
    return false;
  const int n = snprintf(buf, bufLen, "%s%s%s}", base, heapFields, tail);
#else
  const int n = snprintf(buf, bufLen, "%s}", base);
#endif
  return n > 0 && (size_t)n < bufLen;
}

bool DtgClient::publishDeviceInfo()
{
  if (!mqtt.connected())
    return false;
  char payload[256];
  if (!buildDeviceInfoPayload(payload, sizeof(payload)))
    return false;
  if (strncmp(payload, lastInfoPayload_, sizeof(lastInfoPayload_)) == 0)
    return true;
  copyStr(lastInfoPayload_, sizeof(lastInfoPayload_), payload);
  return publishTopicSuffix("up/info", payload, true, 1);
}

bool DtgClient::publishSnapshot()
{
  if (!mqtt.connected())
    return false;
  char payload[256];
  if (!buildSnapshotPayload(payload, sizeof(payload)))
    return false;
  return publishTopicSuffix("tele/snap", payload, false, 0);
}

void DtgClient::maybePublishRuntimeTelemetry()
{
  if (!mqtt.connected())
    return;
  const unsigned long now = millis();
  if (lastSnapshotMs_ == 0)
  {
    snapshotJitterMs_ = static_cast<unsigned long>(random(0, 5000));
    lastSnapshotMs_ = now;
    (void)publishDeviceInfo();
    (void)publishSnapshot();
#if defined(ESP32)
    dtgHeapGuardCheck(now);
#endif
    return;
  }
  const unsigned long interval = snapshotIntervalMs_ + snapshotJitterMs_;
  if (now - lastSnapshotMs_ < interval)
    return;
  lastSnapshotMs_ = now;
  snapshotJitterMs_ = static_cast<unsigned long>(random(0, 3000));
  (void)publishDeviceInfo();
  (void)publishSnapshot();
#if defined(ESP32)
  dtgHeapGuardCheck(now);
#endif
}

bool DtgClient::publish(const char *streamId, float value, bool retain)
{
  return publish(defaultDevice_, streamId, value, retain);
}

bool DtgClient::publish(const char *deviceAlias, const char *streamId, float value,
                        bool retain)
{
  if (!mqtt.connected() || !streamId || !deviceAlias)
    return false;
  char t[160];
  snprintf(t, sizeof(t), "%stele/%s/%s", topicPrefix_, deviceAlias, streamId);
  char buf[128];
  const int n =
      snprintf(buf, sizeof(buf), "{\"v\":%.6f,\"ts\":%lu}", static_cast<double>(value),
               static_cast<unsigned long>(millis() / 1000));
  const bool ok = n > 0 && n < static_cast<int>(sizeof(buf)) &&
                  mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf),
                               static_cast<unsigned int>(n), retain);
  if (ok)
    dtgTraceMqtt("uplink", t, reinterpret_cast<const uint8_t *>(buf),
                 static_cast<unsigned int>(n));
  return ok;
}

bool DtgClient::publishJson(const char *streamId, const char *jsonUtf8)
{
  return publishJson(defaultDevice_, streamId, jsonUtf8);
}

bool DtgClient::publishJson(const char *deviceAlias, const char *streamId,
                                  const char *jsonUtf8)
{
  if (!mqtt.connected() || !streamId || !deviceAlias || !jsonUtf8)
    return false;
  char t[160];
  snprintf(t, sizeof(t), "%stele/%s/%s", topicPrefix_, deviceAlias, streamId);
  return mqtt.publish(t, jsonUtf8, false);
}

bool DtgClient::publishGps(float lat, float lon, float spdKmh,
                                 unsigned long timestampMs)
{
  if (!mqtt.connected())
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%stele/_/gps_data", topicPrefix_);
  char buf[160];
  snprintf(buf, sizeof(buf),
           "{\"lat\":%.6f,\"lon\":%.6f,\"spd\":%.2f,\"ts\":%lu}",
           static_cast<double>(lat), static_cast<double>(lon),
           static_cast<double>(spdKmh), timestampMs);
  return mqtt.publish(t, buf, false);
}

bool DtgClient::publishLogCoord(float lat, float lon)
{
  if (!mqtt.connected())
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%stele/_/log_toado", topicPrefix_);
  char buf[96];
  snprintf(buf, sizeof(buf), "{\"v\":\"%.6f,%.6f\"}",
           static_cast<double>(lat), static_cast<double>(lon));
  return mqtt.publish(t, buf, false);
}

void DtgClient::enableWatchdog(uint32_t timeoutS)
{
  wdtTimeoutS_ = static_cast<int32_t>(timeoutS);

  if (wdtActive_)
  {
    esp_task_wdt_delete(NULL);
    wdtActive_ = false;
  }

  if (timeoutS == 0)
  {
    return;
  }

#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
  const esp_task_wdt_config_t wdtCfg = {
      .timeout_ms = timeoutS * 1000U,
      .idle_core_mask = 0,
      .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdtCfg);
#else
  esp_task_wdt_init(timeoutS, true);
#endif
  esp_task_wdt_add(NULL);
  wdtActive_ = true;
}

void DtgClient::applyWatchdogDefaultIfUnset()
{
  if (wdtTimeoutS_ < 0)
  {
    enableWatchdog(DTG_WATCHDOG_TIMEOUT_S);
  }
  applyPowerDefaultsOnce();
}

void DtgClient::applyPowerDefaultsOnce()
{
  static bool done = false;
  if (done)
    return;
  done = true;

#if defined(ESP32) && (DTG_CPU_FREQ_MHZ) > 0
  if (!setCpuFrequencyMhz(static_cast<uint32_t>(DTG_CPU_FREQ_MHZ)))
  {
    DTG_LOG("power: setCpuFrequencyMhz(%u) failed",
            static_cast<unsigned>(DTG_CPU_FREQ_MHZ));
  }
#endif
  DTG_LOG("power: cpu=%uMHz loopIdle=%ums",
#if defined(ESP32)
          static_cast<unsigned>(getCpuFrequencyMhz()),
#else
          0u,
#endif
          static_cast<unsigned>(DTG_LOOP_IDLE_MS));
}

void DtgClient::loopIdle()
{
#if DTG_LOOP_IDLE_MS > 0
  // ponytail: delay() = vTaskDelay on ESP32 Arduino; core halt vs busy-spin heat.
  delay(DTG_LOOP_IDLE_MS);
#endif
}

void DtgClient::feedWatchdog()
{
  if (wdtActive_)
  {
    esp_task_wdt_reset();
  }
}

unsigned long DtgClient::mqttBackoffWithJitter(unsigned long baseMs) const
{
  const uint32_t r = esp_random();
  const long jitter = static_cast<long>(baseMs * DTG_MQTT_BACKOFF_JITTER_PCT / 100);
  if (jitter <= 0)
  {
    return baseMs;
  }
  const long span = (jitter * 2L) + 1L;
  const long offset = static_cast<long>(r % static_cast<uint32_t>(span)) - jitter;
  const long out = static_cast<long>(baseMs) + offset;
  return (out > 100) ? static_cast<unsigned long>(out) : 100UL;
}

void DtgClient::scheduleNextMqttAttempt(unsigned long nowMs, bool success)
{
  if (success)
  {
    mqttBackoffMs_ = DTG_MQTT_BACKOFF_BASE_MS;
    nextMqttAttemptAfterMs_ = nowMs + DTG_MQTT_RECONNECT_INTERVAL_MS;
    return;
  }

  if (mqttBackoffMs_ < DTG_MQTT_BACKOFF_BASE_MS)
  {
    mqttBackoffMs_ = DTG_MQTT_BACKOFF_BASE_MS;
  }
  else if (mqttBackoffMs_ < DTG_MQTT_BACKOFF_MAX_MS)
  {
    const unsigned long doubled = mqttBackoffMs_ * 2UL;
    mqttBackoffMs_ = (doubled > DTG_MQTT_BACKOFF_MAX_MS) ? DTG_MQTT_BACKOFF_MAX_MS : doubled;
  }

  nextMqttAttemptAfterMs_ = nowMs + mqttBackoffWithJitter(mqttBackoffMs_);
}

void DtgClient::maintainMqttConnection()
{
  if (!netClient_)
    return;
  if (mqtt.connected())
    return;
  if (!netLayerReady())
    return;
  const unsigned long now = millis();
  if (now < nextMqttAttemptAfterMs_)
    return;
  lastMqttAttemptMs_ = now;
  netClient_->stop();
  const bool ok = connectMqttSession();
  scheduleNextMqttAttempt(now, ok);
}

DtgTimerHandle DtgClient::addInterval(unsigned long intervalMs, DtgTimerCallback cb)
{
  DtgTimerHandle h = dtgTimerAddInterval(intervalMs, cb);
  if (!h.valid())
    DTG_LOG("timer_pool_full max=%d", DTG_TIMER_MAX);
  return h;
}

DtgTimerHandle DtgClient::addTimeout(unsigned long intervalMs, DtgTimerCallback cb)
{
  DtgTimerHandle h = dtgTimerAddTimeout(intervalMs, cb);
  if (!h.valid())
    DTG_LOG("timer_pool_full max=%d", DTG_TIMER_MAX);
  return h;
}

bool DtgClient::run()
{
  feedWatchdog();
  // Timers always tick (local GPIO / sensors); sketch gates publish with connected().
  // Sync DTG_EVERY(DTG_PERIOD_UPLINK) slots to Hub periodUplink before tick.
  dtgTimerSetPeriodUplink(periodUplink());
  dtgTimerTick(millis());
  maintainMqttConnection();
  if (modbusEnabled_)
#if DTG_FEATURE_MODBUS
    detail::ModbusManager::instance().poll();
#else
    ;
#endif
  bool ok = false;
  if (mqtt.connected())
  {
    mqtt.loop();
    maybePublishRuntimeTelemetry();
#if DTG_FEATURE_NOCODE
    if (noCodeIoEnabled_)
      detail::HubNoCodeIo::instance().pollUplink();
#endif
    ok = true;
  }
  loopIdle();
  return ok;
}

void DtgClient::disconnectMqtt()
{
  mqtt.disconnect();
}

void DtgClient::updateMqttCredentials(const char *user, const char *pass)
{
  copyStr(mqttUser_, sizeof(mqttUser_), user);
  copyStr(mqttPass_, sizeof(mqttPass_), pass);
  if (mqtt.connected())
  {
    disconnectMqtt();
  }
  lastMqttAttemptMs_ = 0;
  nextMqttAttemptAfterMs_ = 0;
  mqttBackoffMs_ = DTG_MQTT_BACKOFF_BASE_MS;
}

bool DtgClient::connectMqttSession()
{
  if (!netClient_)
    return false;

  configureTransportBeforeMqttConnect();

  if (mqttUseIp_)
  {
    mqtt.setServer(mqttIp_, mqttPort_);
  }
  else
  {
    mqtt.setServer(mqttHost_, mqttPort_);
  }

  char willTopic[128];
  snprintf(willTopic, sizeof(willTopic), "%sup/status", topicPrefix_);

  if (mqtt.connect(clientId_, mqttUser_, mqttPass_, willTopic, 1, true, "{\"ol\":0}"))
  {
    publishOnline();
    lastSnapshotMs_ = 0;
    lastInfoPayload_[0] = '\0';
    (void)publishDeviceInfo();
    (void)publishSnapshot();
    char sub[128];
    if (platformFeaturesEnabled_)
    {
      snprintf(sub, sizeof(sub), "%sdown/gw/caps", topicPrefix_);
      mqtt.subscribe(sub, 1);
    }
    if (hubCmdEnabled_ || (noCodeIoEnabled_ && !hubCmdEnabled_))
    {
      snprintf(sub, sizeof(sub), "%sdown/c/#", topicPrefix_);
      mqtt.subscribe(sub, 1);
    }
    if (!platformFeaturesEnabled_ || legacyForceFeatures_ || noCodeIoEnabled_)
    {
      if (noCodeIoEnabled_)
      {
        snprintf(sub, sizeof(sub), "%sdown/cfg", topicPrefix_);
        mqtt.subscribe(sub, 1);
      }
    }
    if (modbusEnabled_)
    {
      snprintf(sub, sizeof(sub), "%sdown/modbus/#", topicPrefix_);
      mqtt.subscribe(sub, 1);
    }

    if (dtgLogStream)
    {
      if (mqttUseIp_)
      {
        DTG_LOG("mqtt connected ip=%s port=%u clientId=%s", mqttIp_.toString().c_str(),
                static_cast<unsigned>(mqttPort_), clientId_);
      }
      else
      {
        DTG_LOG("mqtt connected host=%s port=%u clientId=%s", mqttHost_,
                static_cast<unsigned>(mqttPort_), clientId_);
      }
      dtgTraceMqtt("mqtt will", willTopic,
                   reinterpret_cast<const uint8_t *>("{\"ol\":0}"), 9);
      if (platformFeaturesEnabled_)
      {
        snprintf(sub, sizeof(sub), "%sdown/gw/caps", topicPrefix_);
        DTG_LOG("mqtt subscribe qos=1 topic=%s", sub);
      }
      if (hubCmdEnabled_ || (noCodeIoEnabled_ && !hubCmdEnabled_))
      {
        snprintf(sub, sizeof(sub), "%sdown/c/#", topicPrefix_);
        DTG_LOG("mqtt subscribe qos=1 topic=%s", sub);
      }
      if (noCodeIoEnabled_)
      {
        snprintf(sub, sizeof(sub), "%sdown/cfg", topicPrefix_);
        DTG_LOG("mqtt subscribe qos=1 topic=%s", sub);
      }
      if (modbusEnabled_)
      {
        snprintf(sub, sizeof(sub), "%sdown/modbus/#", topicPrefix_);
        DTG_LOG("mqtt subscribe qos=1 topic=%s", sub);
      }
      DTG_LOG("mqtt online payload topic=%sup/status json={\"ol\":1}", topicPrefix_);
    }

    dtgOnConnected();
    return true;
  }
  return false;
}

void DtgClient::staticMqttCallback(char *topic, byte *payload, unsigned int len)
{
  if (instance_)
  {
    instance_->dispatchIncoming(topic, payload, len);
  }
}

static bool topicEndsWith(const char *topic, const char *suffix)
{
  if (!topic || !suffix)
    return false;
  size_t tl = strlen(topic);
  size_t sl = strlen(suffix);
  return tl >= sl && strcmp(topic + tl - sl, suffix) == 0;
}

void DtgClient::dispatchIncoming(char *topic, const uint8_t *payload,
                                       unsigned int len)
{
  if (topicEndsWith(topic, "down/gw/caps"))
  {
    dtgTraceMqtt("gw_caps", topic, payload, len);
    if (platformFeaturesEnabled_)
      detail::DtgFeatureRuntime::instance().onCapsPayload(payload, len);
    return;
  }

  if (topicEndsWith(topic, "down/cfg"))
  {
    dtgTraceMqtt("config", topic, payload, len);
    if (cfgCb_)
    {
      cfgCb_(topic, payload, len, cfgUser_);
    }
    return;
  }

  if (topicEndsWith(topic, "down/modbus/cfg"))
  {
    dtgTraceMqtt("modbus_config", topic, payload, len);
    if (platformFeaturesEnabled_ && !modbusEnabled_)
    {
      detail::DtgFeatureRuntime::instance().stashModbusCfg(payload, len);
      return;
    }
    if (modbusCfgCb_)
    {
      modbusCfgCb_(topic, payload, len, modbusCfgUser_);
    }
    return;
  }

#if DTG_FEATURE_MODBUS
  if (modbusEnabled_ && strstr(topic, "/down/modbus/") && topicEndsWith(topic, "/w"))
  {
    dtgTraceMqtt("modbus_write", topic, payload, len);
    detail::ModbusDownlink::instance().dispatch(topic, payload, len);
    return;
  }
#endif

  const char *p = strstr(topic, "/down/c/");
  if (p && cmdCb_)
  {
    dtgTraceMqtt("downlink", topic, payload, len);
    p += 8;
    const char *slash = strchr(p, '/');
    if (!slash || !slash[1])
      return;
    char deviceBuf[20] = {};
    size_t devLen = static_cast<size_t>(slash - p);
    if (devLen >= sizeof(deviceBuf))
      return;
    memcpy(deviceBuf, p, devLen);
    deviceBuf[devLen] = '\0';
    cmdCb_(deviceBuf, slash + 1, payload, len, cmdUser_);
  }
}