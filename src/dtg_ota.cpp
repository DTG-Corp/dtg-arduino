/**
 * @file dtg_ota.cpp
 */
#include "dtg_config.h"
#if DTG_FEATURE_OTA

#include "dtg_ota.h"

#include "dtg_debug.h"
#include "dtg_heap_guard.h"
#include "dtg_modem_lifecycle.h"

#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

#include "dtg_ota_hash.h"
#include "dtg_tls_roots.h"

#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace detail
{

namespace
{

/** Cellular PPP: TLS alone often exceeds the HTTPClient default (5 s). */
static constexpr int kOtaHttpTimeoutMs = 30000;
static constexpr uint32_t kStreamIdleAbortMs = 60000;
static constexpr int kOtaHttpGetAttempts = 3;
static constexpr uint32_t kOtaHttpRetryDelayMs = 2000;
/** Let PPP/LWIP release the MQTT TLS socket before opening HTTPS. */
static constexpr uint32_t kOtaMqttReleaseSettleMs = 500;

static DtgOtaMqttResumeFn s_mqttResume = nullptr;

/** OTA traces via DTG_LOG: [basename:line] [OTA] ... */
static void otaTrace(const char *fmt, ...)
{
  char msg[180];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  DTG_LOG("[OTA] %s", msg);
}

static void otaTraceErr(const char *ctx)
{
  DTG_LOG("[OTA] %s err=%d %s", ctx ? ctx : "?", static_cast<int>(Update.getError()),
          Update.errorString());
}

/** Log first `maxLen` chars of URL (host/path only for support tickets). */
static void otaTraceUrlHint(const char *url, int maxLen)
{
  if (!url)
  {
    otaTrace("url=(null)");
    return;
  }
  char hint[96];
  const int n = (int)strnlen(url, (size_t)maxLen);
  if (n >= (int)sizeof(hint) - 1)
  {
    memcpy(hint, url, sizeof(hint) - 4);
    hint[sizeof(hint) - 4] = '.';
    hint[sizeof(hint) - 3] = '.';
    hint[sizeof(hint) - 2] = '.';
    hint[sizeof(hint) - 1] = '\0';
  }
  else
  {
    memcpy(hint, url, (size_t)n);
    hint[n] = '\0';
  }
  otaTrace("url_hint=%s", hint);
}

bool publishOtaStatus(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                      const char *status, int progress, const char *errorMsg)
{
  if (!hubTopicPrefix || !jobId)
  {
    otaTrace("publishOtaStatus skip: missing prefix or jobId");
    return false;
  }
  char t[128];
  snprintf(t, sizeof(t), "%sup/ota_status", hubTopicPrefix);
  JsonDocument doc;
  doc["jobId"] = jobId;
  doc["status"] = status;
  doc["progress"] = progress;
  if (errorMsg && errorMsg[0])
    doc["error"] = errorMsg;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  const bool ok =
      mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf), static_cast<unsigned int>(n), false);
  otaTrace("up/ota_status publish %s topic=%s err=%s", ok ? "OK" : "FAIL", t,
           errorMsg && errorMsg[0] ? errorMsg : "-");
  if (!ok)
    otaTrace("mqtt.publish fail: connected=%d bufLen=%u", (int)mqtt.connected(),
             static_cast<unsigned int>(n));
  return ok;
}

static bool resumeMqttIfNeeded(PubSubClient &mqtt)
{
  if (mqtt.connected())
    return true;
  if (!s_mqttResume)
  {
    otaTrace("MQTT resume: no handler registered");
    return false;
  }
  otaTrace("MQTT resume after HTTPS...");
  esp_task_wdt_reset();
  const bool ok = s_mqttResume();
  otaTrace("MQTT resume %s connected=%d heap=%u", ok ? "OK" : "FAIL", (int)mqtt.connected(),
           static_cast<unsigned int>(ESP.getFreeHeap()));
  return ok && mqtt.connected();
}

/** esp32 HTTPClient::~HTTPClient() calls _client->stop() - clients must outlive http. */
struct OtaHttpSession
{
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  explicit OtaHttpSession(const char *caPem)
  {
#if DTG_OTA_TLS_INSECURE
    (void)caPem;
    secure.setInsecure();
#else
    secure.setCACert(caPem && caPem[0] ? caPem : DTG_TLS_CA_BUNDLE);
#endif
    http.setTimeout(kOtaHttpTimeoutMs);
    http.setConnectTimeout(kOtaHttpTimeoutMs);
  }

  ~OtaHttpSession() { http.end(); }

  bool beginUrl(const char *url)
  {
    const bool isHttps = strncmp(url, "https://", 8) == 0;
    otaTrace("HTTPClient isHttps=%d timeout_ms=%d", isHttps ? 1 : 0, kOtaHttpTimeoutMs);
    return isHttps ? http.begin(secure, url) : http.begin(plain, url);
  }
};

/**
 * Download + flash only. Returns nullptr on success, or a stable error code string.
 * Must run with MQTT TLS already released on cellular (caller disconnects first).
 */
static const char *otaDownloadAndFlash(const char *firmwareUrl, size_t *writtenOut,
                                       const uint8_t *expectSha, uint32_t expectedSize,
                                       const char *caPem)
{
  OtaHttpSession sess(caPem);
  int code = -1;
  for (int attempt = 1; attempt <= kOtaHttpGetAttempts; ++attempt)
  {
    if (!sess.beginUrl(firmwareUrl))
    {
      otaTrace("http.begin failed attempt=%d", attempt);
      if (attempt == kOtaHttpGetAttempts)
        return "http_begin_failed";
      delay(kOtaHttpRetryDelayMs);
      esp_task_wdt_reset();
      continue;
    }
    otaTrace("HTTP GET start attempt=%d/%d...", attempt, kOtaHttpGetAttempts);
    code = sess.http.GET();
    otaTrace("HTTP GET code=%d (%s)", code, sess.http.errorToString(code).c_str());
    if (code == HTTP_CODE_OK)
      break;
    sess.http.end();
    if (attempt < kOtaHttpGetAttempts)
    {
      delay(kOtaHttpRetryDelayMs);
      esp_task_wdt_reset();
    }
  }
  if (code != HTTP_CODE_OK)
  {
    // Caller formats http_%d - stash code in static for one shot.
    static char errHttp[24];
    snprintf(errHttp, sizeof(errHttp), "http_%d", code);
    return errHttp;
  }

  const int contentLen = sess.http.getSize();
  otaTrace("response contentLen=%d expectedSize=%lu", contentLen,
           static_cast<unsigned long>(expectedSize));
  WiFiClient *stream = sess.http.getStreamPtr();
  if (!stream)
    return "no_stream";

  const size_t updateSize = expectedSize > 0
                                ? static_cast<size_t>(expectedSize)
                                : (contentLen > 0 ? static_cast<size_t>(contentLen)
                                                  : UPDATE_SIZE_UNKNOWN);
  otaTrace("Update.begin size=%u", static_cast<unsigned int>(updateSize));
  if (!Update.begin(updateSize))
  {
    otaTraceErr("Update.begin FAILED");
    return "ota_begin_failed";
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
#if MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256_starts(&sha, 0);
#else
  mbedtls_sha256_starts_ret(&sha, 0);
#endif

  uint8_t buff[1024];
  size_t written = 0;
  int lastLoggedProgress = -1;
  uint32_t idleMs = 0;
  const size_t expectedBytes = expectedSize > 0
                                   ? static_cast<size_t>(expectedSize)
                                   : (contentLen > 0 ? static_cast<size_t>(contentLen) : 0);

  while (sess.http.connected() && !(expectedBytes > 0 && written >= expectedBytes))
  {
    esp_task_wdt_reset();
    const size_t available = stream->available();
    if (available == 0)
    {
      delay(1);
      idleMs += 1;
      if (expectedBytes > 0 && written >= expectedBytes)
        break;
      if (idleMs >= kStreamIdleAbortMs)
      {
        otaTrace("abort: no stream data for %lu ms (written=%u)",
                 static_cast<unsigned long>(idleMs), static_cast<unsigned int>(written));
        mbedtls_sha256_free(&sha);
        Update.abort();
        return "stream_idle_timeout";
      }
      continue;
    }

    idleMs = 0;
    const int toRead = (available > sizeof(buff)) ? (int)sizeof(buff) : (int)available;
    const int readLen = stream->readBytes(reinterpret_cast<char *>(buff), toRead);
    if (readLen <= 0)
      break;
    if (Update.write(buff, static_cast<size_t>(readLen)) != static_cast<size_t>(readLen))
    {
      otaTraceErr("Update.write");
      mbedtls_sha256_free(&sha);
      Update.abort();
      return "ota_write_failed";
    }
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha256_update(&sha, buff, static_cast<size_t>(readLen));
#else
    mbedtls_sha256_update_ret(&sha, buff, static_cast<size_t>(readLen));
#endif
    written += static_cast<size_t>(readLen);
    if (expectedBytes == 0)
      continue;
    const int progress = static_cast<int>((written * 100UL) / expectedBytes);
    if (progress >= 100)
      break;
    if (progress % 10 == 0 && progress != lastLoggedProgress)
    {
      otaTrace("downloading ~%d%% written=%u", progress, static_cast<unsigned int>(written));
      lastLoggedProgress = progress;
    }
  }

  uint8_t digest[32] = {};
#if MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha256_finish(&sha, digest);
#else
  mbedtls_sha256_finish_ret(&sha, digest);
#endif
  mbedtls_sha256_free(&sha);

  if (expectedSize > 0 && written != static_cast<size_t>(expectedSize))
  {
    Update.abort();
    return "size_mismatch";
  }
  if (expectSha && !dtgOtaSha256Equal(digest, expectSha))
  {
    Update.abort();
    return "checksum_mismatch";
  }

  if (!Update.end())
  {
    otaTraceErr("Update.end() FAILED");
    return "ota_end_failed";
  }
  if (!Update.isFinished())
  {
    otaTraceErr("Update.isFinished()=false");
    return "ota_not_finished";
  }

  if (writtenOut)
    *writtenOut = written;
  return nullptr;
}

} // namespace

void dtgOtaSetMqttResumeHandler(DtgOtaMqttResumeFn fn) { s_mqttResume = fn; }

bool hubOtaPerformFromUrl(PubSubClient &mqtt, const char *hubTopicPrefix, const char *jobId,
                          const char *firmwareUrl, const char *checksumSpec,
                          uint32_t expectedSize, const char *caPem)
{
#if !defined(ESP32)
  (void)mqtt;
  (void)hubTopicPrefix;
  (void)jobId;
  (void)firmwareUrl;
  (void)checksumSpec;
  (void)expectedSize;
  (void)caPem;
  DTG_LOG("[OTA] hubOtaPerformFromUrl is only supported on ESP32");
  return false;
#else
  struct OtaGuardScope
  {
    OtaGuardScope() { dtgHeapGuardSetOtaInProgress(true); }
    ~OtaGuardScope() { dtgHeapGuardSetOtaInProgress(false); }
  } otaGuard;

  struct MqttBufShrink
  {
    PubSubClient &mqtt;
    size_t restoreBytes;
    MqttBufShrink(PubSubClient &m) : mqtt(m), restoreBytes(DTG_MQTT_BUFFER_CORE)
    {
      mqtt.setBufferSize(2048);
    }
    ~MqttBufShrink() { mqtt.setBufferSize(restoreBytes); }
  } mqttBufShrink(mqtt);

  const char *jid = jobId && jobId[0] ? jobId : "";

  otaTrace("hubOtaPerformFromUrl enter jobId=%s mqtt_connected=%d wifi_status=%d free_heap=%u",
           jid[0] ? jid : "(null)", (int)mqtt.connected(), (int)WiFi.status(),
           static_cast<unsigned int>(ESP.getFreeHeap()));
  otaTraceUrlHint(firmwareUrl, 80);

  if (!mqtt.connected())
  {
    otaTrace("abort: MQTT not connected (cannot publish ota_status to Hub)");
    publishOtaStatus(mqtt, hubTopicPrefix, jid, "failed", 0, "mqtt_disconnected");
    return false;
  }
  if (!firmwareUrl || !firmwareUrl[0])
  {
    otaTrace("abort: missing firmwareUrl");
    publishOtaStatus(mqtt, hubTopicPrefix, jid, "failed", 0, "missing_url");
    return false;
  }

  // Status first while MQTT TLS is still up.
  publishOtaStatus(mqtt, hubTopicPrefix, jid, "downloading", 1, "");

  // One TLS at a time on PPP: MQTT+HTTPS → DNS -54 / errno 113.
  otaTrace("release MQTT TLS before HTTPS heap=%u",
           static_cast<unsigned int>(ESP.getFreeHeap()));
  mqtt.disconnect();
  delay(kOtaMqttReleaseSettleMs);
  esp_task_wdt_reset();
  otaTrace("MQTT released heap=%u", static_cast<unsigned int>(ESP.getFreeHeap()));

  uint8_t expectSha[32] = {};
  const bool haveSha = dtgOtaParseSha256Hex(checksumSpec, expectSha);

  size_t written = 0;
  const char *err =
      otaDownloadAndFlash(firmwareUrl, &written, haveSha ? expectSha : nullptr, expectedSize,
                          caPem);
  // HTTPS session destroyed here - safe to resume MQTT TLS.

  if (err)
  {
    (void)resumeMqttIfNeeded(mqtt);
    if (mqtt.connected())
      publishOtaStatus(mqtt, hubTopicPrefix, jid, "failed", 0, err);
    else
      otaTrace("fail %s (mqtt down; Hub stall/timeout)", err);
    return false;
  }

  otaTrace("flash OK written=%u - resume MQTT for success", static_cast<unsigned int>(written));
  if (resumeMqttIfNeeded(mqtt))
  {
    publishOtaStatus(mqtt, hubTopicPrefix, jid, "installing", 95, "");
    publishOtaStatus(mqtt, hubTopicPrefix, jid, "success", 100, "");
    delay(800);
    mqtt.loop();
  }
  else
  {
    otaTrace("success but MQTT resume failed - device updated; Hub may show stall");
  }

  otaTrace("success written=%u -> reboot", static_cast<unsigned int>(written));
  esp_task_wdt_reset();
  dtgInvokeModemPrepareForRestart();
  esp_task_wdt_reset();
  ESP.restart();
  return true;
#endif
}

} // namespace detail

#endif // DTG_FEATURE_OTA
