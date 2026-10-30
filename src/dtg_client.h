/**
 * @file dtg_client.h
 * @brief MQTT session management, topic building, and telemetry helpers for DTG IoT Hub.
 *
 * Use DtgWiFi or DtgCellular - they attach the correct network Client and call
 * begin(). Only one client instance should exist per sketch (PubSubClient static callback routing).
 */
#pragma once

#include <Arduino.h>
#include <Client.h>
#include <PubSubClient.h>
#include <Stream.h>

#include "dtg_config.h"
#include "dtg_timer.h"
#include "dtg_types.h"

#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE DTG_MQTT_KEEPALIVE_SEC
#endif

namespace detail
{
class HubNoCodeIo;
class ModbusManager;
class DtgFeatureRuntime;
class HubCmd;
}

class DtgWifiProvisioner;

class DtgClient
{
public:
  /**
   * @param projectId   Hub project id (string, matches dashboard numeric id as text).
   * @param gwAlias     Gateway alias, e.g. gw01 (2-16 chars [a-z0-9_]).
   * @param mqttUser    Gateway MQTT username (= token).
   * @param mqttPass    Gateway MQTT password (= token).
   * @param clientId    MQTT client id, typically "gw-" + gateway UUID from Hub UI.
   */
  DtgClient(const char *projectId, const char *gwAlias, const char *mqttUser,
                  const char *mqttPass, const char *clientId);

  virtual ~DtgClient();

  /** Wire PubSubClient to your transport (WiFiClientSecure, NetworkClient, ...). */
  void attachNetworkClient(Client &client);

  /** Register downlink handler for topics matching .../down/c/{device}/{stream}. */
  void onCommand(DTG_CommandCallback cb, void *userData = nullptr);

  /** Register handler for .../down/cfg JSON payloads (no-code config push). */
  void onConfig(DTG_ConfigCallback cb, void *userData = nullptr);

  /**
   * Send library logs to this stream (Wi-Fi/MQTT/no-code). Call after Serial.begin().
   * Call before begin() if you want messages during Wi-Fi/MQTT connect.
   * Sketch-only `#define DTG_DEBUG 1` does not apply to library .cpp files - use this.
   */
  void setLogStream(Stream *stream);

  /**
   * Send the DTG boot banner to this stream once at the start of begin().
   * Keep separate from setLogStream so the banner can show even when debug logs are off.
   */
  void setBootStream(Stream *stream)
  {
#ifndef DTG_NO_DEFAULT_BANNER
    bootStream_ = stream;
#else
    (void)stream;
    bootStream_ = nullptr;
#endif
  }

  /**
   * Enable built-in Hub no-code I/O (same behavior as the generated no-code sketch).
   * Parses down/cfg (set_datastream / delete_datastream / full_sync), publishes up/cfg_ack,
   * uplink sampling to tele/_/..., digital down/c/... . Requires ArduinoJson (Library Manager).
   * Hub FOTA on stream `ota` (JSON start_ota) uses the built-in detail::hubOtaPerformFromUrl when no
   * custom handler is registered via HubNoCodeIo::setOtaStartHandler.
   * Call once after begin(). Replaces any prior onConfig/onCommand handlers.
   * @deprecated Prefer enablePlatformFeatures() for Hub-driven activation.
   */
  void enableNoCodeIo();

  /**
   * Enable Modbus RTU no-code (down/modbus/cfg, tele/modbus/..., up/modbus/cfg_ack).
   * Requires ArduinoJson. Call after begin(); can be combined with enableNoCodeIo().
   * @deprecated Prefer enablePlatformFeatures() for Hub-driven activation.
   */
  void enableModbus();

  /** OTA + remote reboot on down/c/{device}/ota|reboot - always on for HARDWARE gateways. */
  void enableHubCmd();

  /**
   * Subscribe down/gw/caps and activate down/cfg (period uplink / Hub config).
   * Pair with enableHubCmd() on gateway sketches; do not call enableModbus/enableNoCodeIo manually.
   * Caps may still toggle modbus; GPIO no-code slots stay empty until Hub syncs streams.
   */
  void enablePlatformFeatures();

  /** Gateway-wide uplink period (ms). Used by no-code pollUplink; Hub pushes via down/cfg. */
  uint32_t periodUplink() const;
  void setPeriodUplink(uint32_t ms);

  void setMqttBufferSize(size_t bytes);

  /** Apply rotated MQTT credentials and force reconnect (after NVS write). */
  void updateMqttCredentials(const char *user, const char *pass);

  /** Publish UTF-8 payload to {topicPrefix}{suffix}. */
  bool publishTopicSuffix(const char *suffix, const char *payloadUtf8, bool retain = false,
                          uint8_t qos = 0);

  /** Override default telemetry device alias "_" if needed. */
  void setDefaultDeviceAlias(const char *alias);

  /**
   * Enable ESP32 task watchdog (auto-restart on hang). Default on at begin().
   * Pass 0 to disable. Override timeout via DTG_WATCHDOG_TIMEOUT_S before #include <dtg.h>.
   */
  void enableWatchdog(uint32_t timeoutS = DTG_WATCHDOG_TIMEOUT_S);

  /**
   * MQTT + subscription maintenance; call every loop().
   * Reconnects when the transport reports ready (see netLayerReady() in subclasses).
   * Always ticks DTG_EVERY / addInterval timers (not gated on MQTT).
   */
  virtual bool run();

  /**
   * Register a repeating timer (shared static pool, zero heap).
   * Prefer DTG_EVERY(DTG_PERIOD_UPLINK) to follow Hub periodUplink, or
   * DTG_EVERY(ms) for a fixed interval.
   */
  DtgTimerHandle addInterval(unsigned long intervalMs, DtgTimerCallback cb);

  /** One-shot timer; slot is freed after the callback runs. */
  DtgTimerHandle addTimeout(unsigned long intervalMs, DtgTimerCallback cb);

  /** Not const: PubSubClient::connected() is non-const in the stock library. */
  bool connected() { return mqtt.connected(); }

  /** Optional: connect by IPv4 instead of DTG_MQTT_HOST (plain MQTT recommended). */
  void setBrokerIpv4(const char *dottedIpv4);

  /** Publish {"v":value,"ts":uptimeSeconds} to tele/{defaultDevice}/{streamId}.
   *  Hub ingest uses server time; ts is device uptime for local debug only. */
  bool publish(const char *streamId, float value, bool retain = false);
  bool publish(const char *deviceAlias, const char *streamId, float value,
               bool retain = false);
  bool publish(const char *streamId, double value, bool retain = false)
  {
    return publish(streamId, static_cast<float>(value), retain);
  }
  bool publish(const char *streamId, int value, bool retain = false)
  {
    return publish(streamId, static_cast<float>(value), retain);
  }
  bool publish(const char *streamId, long value, bool retain = false)
  {
    return publish(streamId, static_cast<float>(value), retain);
  }
  bool publish(const char *streamId, unsigned long value, bool retain = false)
  {
    return publish(streamId, static_cast<float>(value), retain);
  }
  bool publish(const char *streamId, bool value, bool retain = false)
  {
    return publish(streamId, value ? 1.0f : 0.0f, retain);
  }

  /** Publish raw JSON object string (caller must supply valid JSON). */
  bool publishJson(const char *streamId, const char *jsonUtf8);
  bool publishJson(const char *deviceAlias, const char *streamId, const char *jsonUtf8);

  /** GPS track point on tele/_/gps_data (matches Hub ingest contract). */
  bool publishGps(float lat, float lon, float spdKmh, unsigned long timestampMs);

  /** Manual coordinate log tele/_/log_toado as {"v":"lat,lon"}. */
  bool publishLogCoord(float lat, float lon);

  /** Retained online payload on up/status. */
  bool publishOnline();

  /**
   * Hooks for up/info and tele/snap (IP, MAC, RSSI). Call before begin() or after attachNetworkClient().
   * Buffers are stack-only in library code; callbacks must not allocate (no Arduino String).
   */
  void setRuntimeInfoProvider(const DtgRuntimeInfoProvider *provider);

  /** Retained static identity on up/info (publish on connect / when values change). */
  bool publishDeviceInfo();

  /** Optional SoftAP fields on up/info (ap=1, apSsid, apIp). Cleared when inactive. */
  void setApSetupReport(bool active, const char *ssid, const char *ip);

  /** Periodic tele/snap (RSSI, uptime) - also invoked from run() on heartbeat. */
  bool publishSnapshot();

  const char *topicPrefix() const { return topicPrefix_; }

  /**
   * Set MQTT broker hostname and port before begin() (Hub-generated sketches, dev brokers).
   * If omitted, WiFi/cellular begin() uses defaults from dtg_config.h and TLS/plain ports.
   */
  void setMqttBroker(const char *host, uint16_t port);

  /**
   * Optional custom CA PEM (null-terminated, may contain several certs).
   * Pointer is stored, not copied - must outlive the client. nullptr restores
   * the built-in Let's Encrypt bundle (DTG_TLS_CA_BUNDLE).
   */
  void setCaCert(const char *pem);
  const char *caCert() const;

protected:
  /** Subclass sets broker host/port/TLS before first connect. */
  void setMqttEndpoint(const char *host, uint16_t port);
  void setMqttEndpoint(const IPAddress &ip, uint16_t port);
  void setUseTls(bool on) { useTls_ = on; }
  void setTlsInsecure(bool on) { tlsInsecure_ = on; }

  /** Hook: apply TLS or plain socket settings to the active client before mqtt.connect(). */
  virtual void configureTransportBeforeMqttConnect() {}

  /** Hook: Wi-Fi connected / PPP has IP, etc. */
  virtual bool netLayerReady() const { return true; }

  bool connectMqttSession();
  void disconnectMqtt();
  void maintainMqttConnection();
  void printBootBannerOnce();
  void maybePublishRuntimeTelemetry();
  bool buildDeviceInfoPayload(char *buf, size_t bufLen);
  bool buildSnapshotPayload(char *buf, size_t bufLen);

  void applyWatchdogDefaultIfUnset();
  void applyPowerDefaultsOnce();
  void feedWatchdog();
  /** Yield so idle task can halt the core (thermal). Call on every run() exit path. */
  static void loopIdle();
  unsigned long mqttBackoffWithJitter(unsigned long baseMs) const;
  void scheduleNextMqttAttempt(unsigned long nowMs, bool success);

  PubSubClient mqtt;
  char topicPrefix_[96] = {};
  unsigned long lastMqttAttemptMs_ = 0;
  unsigned long mqttBackoffMs_ = DTG_MQTT_BACKOFF_BASE_MS;
  unsigned long nextMqttAttemptAfterMs_ = 0;
  int32_t wdtTimeoutS_ = -1;
  bool wdtActive_ = false;

  /** Read by WiFi/cellular subclasses in configureTransportBeforeMqttConnect(). */
  bool tlsInsecure_ = false;

  /** Set by setMqttBroker(); WiFi::begin skips default broker from dtg_config.h. */
  bool mqttBrokerConfigured_ = false;

  friend class ::detail::HubNoCodeIo;
  friend class ::detail::ModbusManager;
  friend class ::detail::DtgFeatureRuntime;
  friend class ::detail::HubCmd;
  friend class DtgWifiProvisioner;

  void activateModbusRuntime();
  void deactivateModbusRuntime();
  void activateNocodeRuntime();
  void deactivateNocodeRuntime();
  bool subscribeTopicSuffix(const char *suffix, uint8_t qos = 1);
  bool unsubscribeTopicSuffix(const char *suffix);
  void refreshMqttBufferForFeatures();

  bool apReportActive_ = false;
  char apReportSsid_[20] = {};
  char apReportIp_[24] = {};

private:
  static void staticMqttCallback(char *topic, byte *payload, unsigned int len);
  void dispatchIncoming(char *topic, const uint8_t *payload, unsigned int len);

  char projectId_[20] = {};
  char gwAlias_[20] = {};
  char mqttUser_[DTG_MQTT_CRED_MAX] = {};
  char mqttPass_[DTG_MQTT_CRED_MAX] = {};
  char clientId_[DTG_MQTT_CRED_MAX] = {};
  char defaultDevice_[18] = "_";

  const char *mqttHost_ = DTG_MQTT_HOST;
  IPAddress mqttIp_;
  bool mqttUseIp_ = false;
  uint16_t mqttPort_ = DTG_MQTT_PORT_TLS;
  bool useTls_ = true;
  char mqttHostBuf_[96] = {};
  const char *customCaPem_ = nullptr;

  DTG_CommandCallback cmdCb_ = nullptr;
  DTG_ConfigCallback cfgCb_ = nullptr;
  void *cmdUser_ = nullptr;
  void *cfgUser_ = nullptr;

  Client *netClient_ = nullptr;
  Stream *bootStream_ = nullptr;

  bool noCodeIoEnabled_ = false;
  bool modbusEnabled_ = false;
  bool hubCmdEnabled_ = false;
  bool platformFeaturesEnabled_ = false;
  bool legacyForceFeatures_ = false;
  DTG_ConfigCallback modbusCfgCb_ = nullptr;
  void *modbusCfgUser_ = nullptr;
  bool bootBannerPrinted_ = false;

  DtgRuntimeInfoProvider runtimeProvider_{};
  bool runtimeProviderSet_ = false;
  char lastInfoPayload_[192] = {};
  unsigned long lastSnapshotMs_ = 0;
  unsigned long snapshotIntervalMs_ = DTG_SNAPSHOT_INTERVAL_MS;
  unsigned long snapshotJitterMs_ = 0;

  static DtgClient *instance_;
};