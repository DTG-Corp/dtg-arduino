/**
 * @file dtg_nocode.cpp
 * @brief No-code agent logic aligned with Hub sketch export and ESP32_DtgHubGateway example.
 */
#include "dtg_nocode.h"

#if DTG_FEATURE_NOCODE

#include "dtg_client.h"
#include "dtg_debug.h"
#include "dtg_hub_cfg.h"
#include "dtg_ota.h"
#include "dtg_modem_lifecycle.h"
#include "dtg_pin.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <ctype.h>
#include <esp_task_wdt.h>
#include <soc/soc_caps.h>
#include <stdlib.h>
#include <string.h>

namespace detail
{

namespace
{

constexpr int MAX_SLOTS = 16;

enum
{
  PM_INPUT = 0,
  PM_INPUT_PULLUP = 1,
  PM_OUTPUT = 2
};

struct Slot
{
  bool active;
  char id[40];
  char mqttSeg[32];
  uint8_t protocol;
  uint8_t direction;
  int16_t gpio;
  uint8_t pinModeEnum;
  float scale;
  float offset;
  int rawMin;
  int rawMax;
  uint32_t pwmFreq;
  uint8_t pwmResolution;
  uint8_t ledcChannel;
  bool pwmAttached;
  int32_t actionOn;
  int32_t actionOff;
  bool hasActionOn;
  bool hasActionOff;
  uint32_t lastReadMs;
  bool hasDesiredValue;
  float desiredValue;
};

constexpr uint8_t kLedcChannelFree = 0xFF;
constexpr uint32_t kDefaultPwmFreq = 5000;
constexpr uint8_t kDefaultPwmResolution = 8;

Slot slots[MAX_SLOTS];
static Preferences prefs;
static uint32_t appliedVersion = 0;
static bool prefsOpen = false;

// ponytail: gateway-wide uplink cadence; Hub overrides via down/cfg periodUplink
static constexpr uint32_t kPeriodUplinkDefaultMs = DTG_PERIOD_UPLINK_DEFAULT_MS;
static constexpr uint32_t kPeriodUplinkMinMs = 1000U;
static constexpr uint32_t kPeriodUplinkMaxMs = 3600000U;
static uint32_t periodUplinkMs = kPeriodUplinkDefaultMs;

static uint32_t clampPeriodUplink(uint32_t ms)
{
  if (ms < kPeriodUplinkMinMs)
    return kPeriodUplinkMinMs;
  if (ms > kPeriodUplinkMaxMs)
    return kPeriodUplinkMaxMs;
  return ms;
}

static void persistPeriodUplink(uint32_t ms)
{
  if (prefsOpen)
  {
    prefs.putUInt("pup", ms);
    return;
  }
  Preferences p;
  if (!p.begin("dtg_cfg", false))
    return;
  p.putUInt("pup", ms);
  p.end();
}

static constexpr size_t kCfgPayloadMax = DTG_NOCODE_CFG_BUF_MAX;
static char s_cfgPayloadCopy[kCfgPayloadMax];
static JsonDocument s_cfgJsonDoc;
static JsonDocument s_otaCmdDoc;
static JsonDocument s_rebootCmdDoc;
static Slot s_fullSyncScratch[MAX_SLOTS];
static DTG_OtaStartHandler s_otaStartHandler = nullptr;

struct TeleBatchEntry
{
  char seg[32];
  float v;
};

static TeleBatchEntry s_teleBatch[DTG_TELE_BATCH_MAX];
static int s_teleBatchCount = 0;
static uint32_t s_teleBatchWindowStartMs = 0;

static bool isLedcChannelInUse(uint8_t ch)
{
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if (slots[i].active && slots[i].pwmAttached && slots[i].ledcChannel == ch)
      return true;
  }
  return false;
}

static uint8_t allocLedcChannel()
{
  for (uint8_t ch = 0; ch < SOC_LEDC_CHANNEL_NUM; ch++)
  {
    if (!isLedcChannelInUse(ch))
      return ch;
  }
  return kLedcChannelFree;
}

static void releasePwm(Slot &s)
{
  if (!s.pwmAttached || s.gpio < 0)
  {
    s.pwmAttached = false;
    s.ledcChannel = kLedcChannelFree;
    return;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(s.gpio);
#else
  ledcDetachPin(s.gpio);
#endif
  s.pwmAttached = false;
  s.ledcChannel = kLedcChannelFree;
}

static void releaseSlotHardware(Slot &s)
{
  if (s.protocol == 1 && s.direction == 1)
    releasePwm(s);
}

static void clearSlot(int i)
{
  releaseSlotHardware(slots[i]);
  memset(&slots[i], 0, sizeof(Slot));
}

static void clearAllSlots()
{
  for (int i = 0; i < MAX_SLOTS; i++)
    clearSlot(i);
}

static int findSlotById(const char *id)
{
  for (int i = 0; i < MAX_SLOTS; i++)
    if (slots[i].active && strncmp(slots[i].id, id, sizeof(slots[i].id)) == 0)
      return i;
  return -1;
}

static int allocSlot()
{
  for (int i = 0; i < MAX_SLOTS; i++)
    if (!slots[i].active)
      return i;
  return -1;
}

static bool hasAnyActiveSlot()
{
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if (slots[i].active)
      return true;
  }
  return false;
}

static uint8_t parsePinMode(const char *m)
{
  if (!m)
    return PM_INPUT;
  if (strcasecmp(m, "INPUT_PULLUP") == 0)
    return PM_INPUT_PULLUP;
  if (strcasecmp(m, "OUTPUT") == 0)
    return PM_OUTPUT;
  return PM_INPUT;
}

static void applyPinMode(int16_t gpio, uint8_t pm)
{
  if (gpio < 0)
    return;
  if (pm == PM_OUTPUT)
    pinMode(gpio, OUTPUT);
  else if (pm == PM_INPUT_PULLUP)
    pinMode(gpio, INPUT_PULLUP);
  else
    pinMode(gpio, INPUT);
}

static const char *parseSetInto(JsonObject root, Slot &out)
{
  memset(&out, 0, sizeof(out));
  out.active = true;
  const char *dsId = root["datastreamId"] | "";
  const char *proto = root["protocol"] | "";
  const char *dir = root["direction"] | "uplink";
  const char *seg = root["mqttSegment"] | root["alias"] | "";
  if (!dsId[0] || !seg[0])
    return "missing_id_or_segment";

  if (!dtgAliasValid(seg, sizeof(out.mqttSeg)))
    return "invalid_alias";
  strncpy(out.id, dsId, sizeof(out.id) - 1);
  strncpy(out.mqttSeg, seg, sizeof(out.mqttSeg) - 1);
  out.scale = root["scaleFactor"] | 1.0f;
  out.offset = root["offset"] | 0.0f;
  out.direction = strcasecmp(dir, "downlink") == 0 ? 1 : 0;

  JsonObject binding = root["binding"];
  if (strcasecmp(proto, "digital") == 0)
  {
    out.protocol = 0;
    out.pinModeEnum = parsePinMode(binding["pinMode"] | "INPUT");
  }
  else if (strcasecmp(proto, "analog") == 0)
  {
    out.protocol = 1;
    out.rawMin = binding["rawMin"] | 0;
    if (out.direction == 1)
    {
      out.rawMax = binding["rawMax"] | 255;
      out.pinModeEnum = PM_OUTPUT;
    }
    else
    {
      out.rawMax = binding["rawMax"] | 4095;
      out.pinModeEnum = parsePinMode(binding["pinMode"] | "INPUT");
    }
    out.pwmFreq = binding["pwmFrequency"] | kDefaultPwmFreq;
    out.pwmResolution = binding["pwmResolution"] | kDefaultPwmResolution;
    out.ledcChannel = kLedcChannelFree;
    out.pwmAttached = false;
    if (out.pwmResolution < 1)
      out.pwmResolution = kDefaultPwmResolution;
    if (out.pwmResolution > 16)
      out.pwmResolution = 16;
    if (out.pwmFreq < 1)
      out.pwmFreq = kDefaultPwmFreq;
  }
  else
  {
    return "unsupported_protocol";
  }

  const int gpioRaw = binding["gpioPin"] | -1;
  if (gpioRaw < 0 || gpioRaw > 48)
    return "invalid_pin";
  const bool asOutput = (out.direction == 1) || (out.pinModeEnum == PM_OUTPUT);
  if (!dtgPinUsable(gpioRaw, asOutput))
    return "invalid_pin";
  out.gpio = (int16_t)gpioRaw;

  if (out.direction == 1)
  {
    out.hasActionOn = !root["actionOnValue"].isNull();
    out.hasActionOff = !root["actionOffValue"].isNull();
    if (root["actionOnValue"].is<int>())
      out.actionOn = root["actionOnValue"].as<int>();
    else if (root["actionOnValue"].is<float>())
      out.actionOn = (int32_t)root["actionOnValue"].as<float>();
    if (root["actionOffValue"].is<int>())
      out.actionOff = root["actionOffValue"].as<int>();
    else     if (root["actionOffValue"].is<float>())
      out.actionOff = (int32_t)root["actionOffValue"].as<float>();

    if (!root["desiredValue"].isNull())
    {
      out.hasDesiredValue = true;
      if (root["desiredValue"].is<int>() || root["desiredValue"].is<long>())
        out.desiredValue = (float)root["desiredValue"].as<long>();
      else if (root["desiredValue"].is<float>() || root["desiredValue"].is<double>())
        out.desiredValue = root["desiredValue"].as<float>();
      else if (root["desiredValue"].is<bool>())
        out.desiredValue = root["desiredValue"].as<bool>() ? 1.0f : 0.0f;
      else if (root["desiredValue"].is<const char *>())
        out.desiredValue = strtof(root["desiredValue"].as<const char *>(), nullptr);
      else
        out.hasDesiredValue = false;
    }
  }
  return nullptr;
}

static bool attachAnalogPwm(Slot &s)
{
  if (s.gpio < 0 || s.protocol != 1 || s.direction != 1)
    return false;

  if (s.pwmAttached)
    releasePwm(s);

  uint8_t ch = allocLedcChannel();
  if (ch == kLedcChannelFree)
  {
    DTG_LOG("[PWM] no free LEDC channel for GPIO%d", (int)s.gpio);
    return false;
  }

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ledcAttach(s.gpio, s.pwmFreq, s.pwmResolution))
  {
    DTG_LOG("[PWM] ledcAttach failed GPIO%d", (int)s.gpio);
    return false;
  }
  s.ledcChannel = ch;
#else
  // ledcSetup returns the actual frequency configured; 0 means the freq/resolution
  // pair is invalid for this SoC (e.g. high resolution + high frequency). Fall back.
  double actualFreq = ledcSetup(ch, s.pwmFreq, s.pwmResolution);
  if (actualFreq == 0)
  {
    DTG_LOG("[PWM] ledcSetup invalid freq=%lu res=%u GPIO%d",
               (unsigned long)s.pwmFreq, (unsigned)s.pwmResolution, (int)s.gpio);
    return false;
  }
  ledcAttachPin(s.gpio, ch);
  s.ledcChannel = ch;
#endif
  s.pwmAttached = true;
  DTG_LOG("[PWM] GPIO%d ch=%u freq=%lu res=%u", (int)s.gpio, (unsigned)ch,
             (unsigned long)s.pwmFreq, (unsigned)s.pwmResolution);
  return true;
}

static void applyHardware(Slot &s)
{
  if (s.protocol == 0)
    applyPinMode(s.gpio, s.pinModeEnum);
  else if (s.protocol == 1)
  {
    if (s.direction == 1)
    {
      if (!attachAnalogPwm(s))
        applyPinMode(s.gpio, PM_OUTPUT);
    }
    else
      applyPinMode(s.gpio, s.pinModeEnum);
  }
}

static bool copyEnvelopeFieldValue(JsonVariant vVar, char *valueBuf, size_t valueBufLen)
{
  if (vVar.isNull() || !valueBuf || valueBufLen == 0)
    return false;
  valueBuf[0] = 0;
  if (vVar.is<const char *>())
  {
    const char *s = vVar.as<const char *>();
    if (!s || !s[0])
      return false;
    strncpy(valueBuf, s, valueBufLen - 1);
    valueBuf[valueBufLen - 1] = 0;
    return true;
  }
  if (vVar.is<bool>())
  {
    strncpy(valueBuf, vVar.as<bool>() ? "1" : "0", valueBufLen - 1);
    valueBuf[valueBufLen - 1] = 0;
    return true;
  }
  if (vVar.is<int>() || vVar.is<long>() || vVar.is<long long>())
  {
    snprintf(valueBuf, valueBufLen, "%ld", (long)vVar.as<long>());
    return valueBuf[0] != 0;
  }
  if (vVar.is<float>() || vVar.is<double>())
  {
    snprintf(valueBuf, valueBufLen, "%.9g", (double)vVar.as<double>());
    return valueBuf[0] != 0;
  }
  return false;
}

static int parseDownlinkLevel(const char *p, unsigned int len)
{
  if (!p || len == 0)
    return LOW;
  char buf[48];
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, p, len);
  buf[len] = 0;
  char *a = buf;
  while (*a && isspace((unsigned char)*a))
    a++;
  char *b = a + strlen(a);
  while (b > a && isspace((unsigned char)b[-1]))
    *--b = 0;
  if (!a[0])
    return LOW;
  if (!strcasecmp(a, "1") || !strcasecmp(a, "on") || !strcasecmp(a, "true") || !strcasecmp(a, "high"))
    return HIGH;
  if (!strcasecmp(a, "0") || !strcasecmp(a, "off") || !strcasecmp(a, "false") || !strcasecmp(a, "low"))
    return LOW;
  float v = strtof(a, nullptr);
  return (v >= 0.5f) ? HIGH : LOW;
}

static float parseDownlinkAnalogFloat(const char *p, unsigned int len, const Slot &s)
{
  if (!p || len == 0)
    return (float)s.rawMin;
  char buf[48];
  if (len >= sizeof(buf))
    len = sizeof(buf) - 1;
  memcpy(buf, p, len);
  buf[len] = 0;
  char *a = buf;
  while (*a && isspace((unsigned char)*a))
    a++;
  char *b = a + strlen(a);
  while (b > a && isspace((unsigned char)b[-1]))
    *--b = 0;
  if (!a[0])
    return (float)s.rawMin;
  if (!strcasecmp(a, "on") || !strcasecmp(a, "true") || !strcasecmp(a, "high"))
    return (float)(s.hasActionOn ? s.actionOn : s.rawMax);
  if (!strcasecmp(a, "off") || !strcasecmp(a, "false") || !strcasecmp(a, "low"))
    return (float)(s.hasActionOff ? s.actionOff : s.rawMin);
  return strtof(a, nullptr);
}

static int scaleToPwmDuty(const Slot &s, float value)
{
  int rmin = s.rawMin;
  int rmax = s.rawMax;
  if (rmax <= rmin)
    rmax = rmin + 1;
  float clamped = value;
  if (clamped < (float)rmin)
    clamped = (float)rmin;
  if (clamped > (float)rmax)
    clamped = (float)rmax;
  uint8_t res = s.pwmResolution > 0 ? s.pwmResolution : kDefaultPwmResolution;
  if (res > 16)
    res = 16;
  int pwmMax = (1 << res) - 1;
  int duty = (int)(((clamped - (float)rmin) * (float)pwmMax) / (float)(rmax - rmin) + 0.5f);
  if (duty < 0)
    duty = 0;
  if (duty > pwmMax)
    duty = pwmMax;
  return duty;
}

static bool writeAnalogPwm(Slot &s, int duty)
{
  if (s.gpio < 0)
    return false;
  if (!s.pwmAttached && !attachAnalogPwm(s))
    return false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(s.gpio, duty);
#else
  if (s.ledcChannel != kLedcChannelFree)
    ledcWrite(s.ledcChannel, duty);
  else
    return false;
#endif
  return true;
}

static bool applyDesiredOutput(Slot &s)
{
  if (!s.active || s.direction != 1)
    return true;
  if (!s.hasDesiredValue)
  {
    DTG_LOG("[CFG] %s downlink: no desiredValue in snapshot", s.mqttSeg);
    return true;
  }
  if (s.protocol == 0 && s.gpio >= 0)
  {
    int level = (s.desiredValue >= 0.5f) ? HIGH : LOW;
    digitalWrite(s.gpio, level);
    DTG_LOG("[CFG] apply desired %s GPIO%d <- %s", s.mqttSeg, (int)s.gpio,
               level == HIGH ? "HIGH" : "LOW");
    return true;
  }
  if (s.protocol == 1 && s.gpio >= 0)
  {
    int duty = scaleToPwmDuty(s, s.desiredValue);
    if (!writeAnalogPwm(s, duty))
    {
      DTG_LOG("[CFG] apply desired %s GPIO%d PWM failed", s.mqttSeg, (int)s.gpio);
      return false;
    }
    DTG_LOG("[CFG] apply desired %s GPIO%d <- duty=%d (raw=%.2f)", s.mqttSeg,
               (int)s.gpio, duty, s.desiredValue);
    return true;
  }
  return true;
}

} // namespace

HubNoCodeIo &HubNoCodeIo::instance()
{
  static HubNoCodeIo inst;
  return inst;
}

void HubNoCodeIo::setOtaStartHandler(DTG_OtaStartHandler handler) { s_otaStartHandler = handler; }

void HubNoCodeIo::attach(DtgClient *client)
{
  client_ = client;
  clearAllSlots();
  loadVersion();
}

void HubNoCodeIo::detach()
{
  clearAllSlots();
  client_ = nullptr;
}

void HubNoCodeIo::loadVersion()
{
  prefs.begin("dtg_cfg", false);
  prefsOpen = true;
  appliedVersion = prefs.getUInt("ver", 0);
  periodUplinkMs = clampPeriodUplink(prefs.getUInt("pup", kPeriodUplinkDefaultMs));
}

void HubNoCodeIo::saveVersion(uint32_t v)
{
  appliedVersion = v;
  prefs.putUInt("ver", v);
}

uint32_t HubNoCodeIo::periodUplink() const { return periodUplinkMs; }

void HubNoCodeIo::setPeriodUplink(uint32_t ms)
{
  periodUplinkMs = clampPeriodUplink(ms);
  persistPeriodUplink(periodUplinkMs);
  DTG_LOG("[CFG] periodUplink=%u", (unsigned)periodUplinkMs);
}

bool HubNoCodeIo::publishAck(uint32_t version, const char *status, const char *dsId,
                            const char *msg)
{
  if (!client_ || !client_->mqtt.connected())
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%sup/cfg_ack", client_->topicPrefix_);
  JsonDocument doc;
  doc["version"] = version;
  doc["status"] = status;
  if (dsId && dsId[0])
    doc["datastreamId"] = dsId;
  if (msg && msg[0])
    doc["msg"] = msg;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  return client_->mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf),
                                static_cast<unsigned int>(n), true);
}

bool HubNoCodeIo::publishCmdAck(const char *cmdId, const char *streamSeg, const char *status,
                                int level)
{
  if (!client_ || !client_->mqtt.connected() || !cmdId || !cmdId[0])
    return false;
  char t[128];
  snprintf(t, sizeof(t), "%sup/cmd_ack", client_->topicPrefix_);
  JsonDocument doc;
  doc["id"] = cmdId;
  doc["st"] = status;
  if (streamSeg && streamSeg[0])
    doc["stream"] = streamSeg;
  doc["v"] = level;
  char buf[192];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  return client_->mqtt.publish(t, reinterpret_cast<const uint8_t *>(buf),
                                static_cast<unsigned int>(n), false);
}

void HubNoCodeIo::publishTeleRetained(const char *seg, float v)
{
  if (client_)
    client_->publish(seg, v, true);
}

void HubNoCodeIo::flushTeleBatch()
{
  if (!client_ || s_teleBatchCount <= 0)
    return;

  JsonDocument doc;
  doc["v"] = 1;
  doc["ts"] = static_cast<uint32_t>(millis() / 1000UL);
  JsonArray samples = doc["samples"].to<JsonArray>();
  for (int i = 0; i < s_teleBatchCount; i++)
  {
    JsonObject row = samples.add<JsonObject>();
    row["s"] = s_teleBatch[i].seg;
    row["v"] = s_teleBatch[i].v;
  }

  char buf[512];
  const size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf))
  {
    (void)client_->publishTopicSuffix("tele/batch", buf, false, 0);
  }

  s_teleBatchCount = 0;
  s_teleBatchWindowStartMs = millis();
}

void HubNoCodeIo::queueTeleSample(const char *seg, float v)
{
  if (!seg || !seg[0])
    return;

  if (s_teleBatchCount == 0)
  {
    s_teleBatchWindowStartMs = millis();
  }

  if (s_teleBatchCount >= DTG_TELE_BATCH_MAX)
  {
    flushTeleBatch();
    s_teleBatchWindowStartMs = millis();
  }

  TeleBatchEntry &row = s_teleBatch[s_teleBatchCount++];
  strncpy(row.seg, seg, sizeof(row.seg) - 1);
  row.seg[sizeof(row.seg) - 1] = '\0';
  row.v = v;
}

void HubNoCodeIo::onCfgPayload(const char *payload, unsigned int len)
{
  if (!client_)
    return;

  s_cfgJsonDoc.clear();
  DeserializationError derr = deserializeJson(s_cfgJsonDoc, payload, len);
  if (derr)
  {
    publishAck(0, "error", "", "json_parse");
    return;
  }
  JsonObject root = s_cfgJsonDoc.as<JsonObject>();
  const char *action = root["action"] | "";
  DTG_LOG("[CFG] rx action=%s", action ? action : "");

  if (strcasecmp(action, "set_mqtt_credential") == 0)
  {
    uint32_t ver = root["version"] | 0;
    const char *user = root["mqttUser"] | "";
    const char *pass = root["mqttPass"] | "";
    if (!user[0] || !pass[0])
    {
      publishAck(ver, "error", "", "missing_credential");
      return;
    }
    if (!dtgHubCfgSaveCredentials(user, pass, ver))
    {
      publishAck(ver, "error", "", "nvs_write");
      return;
    }
    client_->updateMqttCredentials(user, pass);
    publishAck(ver, "ok", "", "credential_rotated");
    return;
  }

  if (strcasecmp(action, "set_period_uplink") == 0)
  {
    uint32_t ver = root["version"] | 0;
    uint32_t ms = root["periodUplink"] | kPeriodUplinkDefaultMs;
    setPeriodUplink(ms);
    if (ver > appliedVersion)
      saveVersion(ver);
    publishAck(ver, "ok", "", "period_uplink");
    return;
  }

  if (strcasecmp(action, "set_datastream") == 0)
  {
    uint32_t ver = root["version"] | 0;
    if (ver <= appliedVersion && hasAnyActiveSlot())
    {
      publishAck(ver, "ignored", "", "stale_version");
      return;
    }
    const char *dsId = root["datastreamId"] | "";
    Slot tmp;
    const char *err = parseSetInto(root, tmp);
    if (err)
    {
      publishAck(ver, "error", dsId, err);
      return;
    }
    int idx = findSlotById(tmp.id);
    if (idx < 0)
    {
      idx = allocSlot();
      if (idx < 0)
      {
        publishAck(ver, "error", dsId, "max_slots");
        return;
      }
    }
    else
    {
      // Reconfiguring an existing slot: free old PWM channel before overwrite
      // to avoid LEDC channel leak / dangling attach on the same pin.
      releaseSlotHardware(slots[idx]);
    }
    memcpy(&slots[idx], &tmp, sizeof(Slot));
    applyHardware(slots[idx]);
    applyDesiredOutput(slots[idx]);
    saveVersion(ver);
    publishAck(ver, "ok", dsId, "");
    return;
  }

  if (strcasecmp(action, "delete_datastream") == 0)
  {
    uint32_t ver = root["version"] | 0;
    const char *dsId = root["datastreamId"] | "";
    if (ver <= appliedVersion)
    {
      publishAck(ver, "ignored", "", "stale_version");
      return;
    }
    int idx = findSlotById(dsId);
    if (idx >= 0)
      clearSlot(idx); // clearSlot already releases PWM hardware
    saveVersion(ver);
    publishAck(ver, "ok", dsId, "");
    return;
  }

  if (strcasecmp(action, "full_sync") == 0)
  {
    uint32_t ver = root["version"] | 0;
    if (ver <= appliedVersion && hasAnyActiveSlot())
    {
      publishAck(ver, "ignored", "", "stale_version");
      return;
    }
    if (!root["periodUplink"].isNull())
      setPeriodUplink(root["periodUplink"] | kPeriodUplinkDefaultMs);
    int n = 0;
    const char *err = nullptr;
    JsonArray streams = root["streams"];
    for (JsonObject s : streams)
    {
      if (n >= MAX_SLOTS)
      {
        err = "max_slots";
        break;
      }
      const char *e = parseSetInto(s, s_fullSyncScratch[n]);
      if (e)
      {
        err = e;
        break;
      }
      n++;
    }
    if (err)
    {
      publishAck(ver, "error", "", err);
      return;
    }
    clearAllSlots();
    for (int i = 0; i < n; i++)
    {
      memcpy(&slots[i], &s_fullSyncScratch[i], sizeof(Slot));
      applyHardware(slots[i]);
      applyDesiredOutput(slots[i]);
    }
    saveVersion(ver);
    publishAck(ver, "ok", "", "");
    return;
  }

  publishAck(root["version"] | 0, "error", "", "unknown_action");
}

void HubNoCodeIo::handleDownlinkCommand(const char *topic, const uint8_t *payload,
                                        unsigned int length)
{
  const char *p = strstr(topic, "/down/c/");
  if (!p)
    return;
  p += 8;
  const char *slash = strchr(p, '/');
  if (!slash)
    return;
  const char *streamSeg = slash + 1;
  if (!streamSeg[0])
    return;

  const uint8_t *cmdPayload = payload;
  unsigned int cmdLen = length;
  char cmdIdBuf[48];
  cmdIdBuf[0] = 0;
  char valueBuf[64];
  valueBuf[0] = 0;

  if (length > 0 && payload[0] == '{')
  {
    s_cfgJsonDoc.clear();
    DeserializationError derr = deserializeJson(s_cfgJsonDoc, payload, length);
    if (!derr)
    {
      const char *id = s_cfgJsonDoc["id"] | "";
      if (id[0])
      {
        strncpy(cmdIdBuf, id, sizeof(cmdIdBuf) - 1);
        cmdIdBuf[sizeof(cmdIdBuf) - 1] = 0;
      }
      if (copyEnvelopeFieldValue(s_cfgJsonDoc["v"], valueBuf, sizeof(valueBuf)))
      {
        cmdPayload = reinterpret_cast<const uint8_t *>(valueBuf);
        cmdLen = static_cast<unsigned int>(strlen(valueBuf));
      }
    }
  }

  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if (!slots[i].active || slots[i].direction != 1)
      continue;
    if (strcasecmp(slots[i].mqttSeg, streamSeg) != 0)
      continue;
    if (slots[i].gpio < 0)
    {
      if (cmdIdBuf[0])
        publishCmdAck(cmdIdBuf, streamSeg, "fail", 0);
      return;
    }
    if (slots[i].protocol == 1)
    {
      float rawVal = parseDownlinkAnalogFloat(reinterpret_cast<const char *>(cmdPayload), cmdLen,
                                              slots[i]);
      int duty = scaleToPwmDuty(slots[i], rawVal);
      if (!writeAnalogPwm(slots[i], duty))
      {
        DTG_LOG("[DOWN] %s GPIO%d PWM write failed", streamSeg, (int)slots[i].gpio);
        if (cmdIdBuf[0])
          publishCmdAck(cmdIdBuf, streamSeg, "fail", 0);
        return;
      }
      DTG_LOG("[DOWN] %s GPIO%d <- duty=%d (raw=%.2f)", streamSeg, (int)slots[i].gpio, duty,
                 rawVal);
      slots[i].hasDesiredValue = true;
      slots[i].desiredValue = rawVal;
      publishTeleRetained(streamSeg, rawVal);
      if (cmdIdBuf[0])
        publishCmdAck(cmdIdBuf, streamSeg, "ok", duty);
      return;
    }
    if (slots[i].protocol != 0)
    {
      if (cmdIdBuf[0])
        publishCmdAck(cmdIdBuf, streamSeg, "fail", 0);
      return;
    }
    int level = parseDownlinkLevel(reinterpret_cast<const char *>(cmdPayload), cmdLen);
    digitalWrite(slots[i].gpio, level);
    DTG_LOG("[DOWN] %s GPIO%d <- %s", streamSeg, (int)slots[i].gpio,
               level == HIGH ? "HIGH" : "LOW");
    slots[i].hasDesiredValue = true;
    slots[i].desiredValue = (float)level;
    publishTeleRetained(streamSeg, (float)level);
    if (cmdIdBuf[0])
      publishCmdAck(cmdIdBuf, streamSeg, "ok", level);
    return;
  }
  if (cmdIdBuf[0])
    publishCmdAck(cmdIdBuf, streamSeg, "fail", 0);
  DTG_LOG("[DOWN] no downlink slot for %s", streamSeg);
}

void HubNoCodeIo::pollUplink()
{
  if (!client_ || !client_->mqtt.connected())
    return;
  uint32_t now = millis();
  for (int i = 0; i < MAX_SLOTS; i++)
  {
    if (!slots[i].active || slots[i].direction != 0)
      continue;
    if (now - slots[i].lastReadMs < periodUplinkMs)
      continue;

    float rawF = 0;
    if (slots[i].protocol == 0 && slots[i].gpio >= 0)
      rawF = (float)digitalRead(slots[i].gpio);
    else if (slots[i].protocol == 1 && slots[i].gpio >= 0)
      // Keep raw ADC counts; engineering conversion is done via scale/offset
      // (set by the Hub) to preserve backward-compatible telemetry semantics.
      rawF = (float)analogRead(slots[i].gpio);
    else
    {
      slots[i].lastReadMs = now;
      continue;
    }
    float v = rawF * slots[i].scale + slots[i].offset;
    queueTeleSample(slots[i].mqttSeg, v);
    slots[i].lastReadMs = now;
  }

  if (s_teleBatchCount > 0 &&
      (now - s_teleBatchWindowStartMs >= static_cast<uint32_t>(DTG_TELE_BATCH_MS)))
  {
    flushTeleBatch();
  }
}

void HubNoCodeIo::configCb(const char *, const uint8_t *payload, unsigned int len, void *)
{
  unsigned int n = len;
  if (n >= kCfgPayloadMax)
    n = kCfgPayloadMax - 1;
  memcpy(s_cfgPayloadCopy, payload, n);
  s_cfgPayloadCopy[n] = 0;
  instance().onCfgPayload(s_cfgPayloadCopy, n);
}

void HubNoCodeIo::commandCb(const char *deviceAlias, const char *streamId, const uint8_t *payload,
                            unsigned int len, void *)
{
  HubNoCodeIo &self = instance();
  if (!self.client_ || !streamId || !streamId[0])
    return;

#if !DTG_FEATURE_HUB_CMD
  // Hub FOTA uses stream id "ota" on .../down/c/{device}/ota (not a GPIO slot).
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
    DTG_LOG("[OTA] action=%s", action ? action : "");
    if (strcasecmp(action, "start_ota") != 0)
    {
      DTG_LOG("[OTA] ignored (not start_ota)");
      return;
    }
    const char *jobId = s_otaCmdDoc["jobId"] | "";
    const char *url = s_otaCmdDoc["url"] | "";
    if (!jobId[0] || !url[0])
    {
      DTG_LOG("[OTA] start_ota missing jobId or url (jobId_len=%u url_len=%u)",
                 static_cast<unsigned int>(strlen(jobId)),
                 static_cast<unsigned int>(strlen(url)));
      return;
    }
    if (s_otaStartHandler)
    {
      DTG_LOG("[OTA] invoking custom handler jobId=%s", jobId);
      s_otaStartHandler(jobId, url);
    }
    else
    {
      const char *checksum = s_otaCmdDoc["checksum"] | "";
      const uint32_t size = s_otaCmdDoc["size"] | 0U;
      DTG_LOG("[OTA] invoking built-in hubOtaPerformFromUrl jobId=%s", jobId);
      (void)hubOtaPerformFromUrl(self.client_->mqtt, self.client_->topicPrefix(), jobId, url,
                                 checksum, size, self.client_->caCert());
    }
    return;
  }

  // Hub remote soft reboot: stream id "reboot" on .../down/c/{device}/reboot (not a GPIO slot).
  if (strcasecmp(streamId, "reboot") == 0)
  {
    if (len == 0 || len > 256)
    {
      DTG_LOG("[REBOOT] rejected len=%u", static_cast<unsigned int>(len));
      return;
    }
    s_rebootCmdDoc.clear();
    DeserializationError derr = deserializeJson(s_rebootCmdDoc, payload, len);
    if (derr)
    {
      DTG_LOG("[REBOOT] JSON parse error: %s", derr.c_str());
      return;
    }
    const char *action = s_rebootCmdDoc["action"] | "";
    if (action[0] && strcasecmp(action, "reboot") != 0)
    {
      DTG_LOG("[REBOOT] ignored action=%s", action);
      return;
    }
    const char *cmdId = s_rebootCmdDoc["id"] | "";
    if (!cmdId[0])
    {
      DTG_LOG("[REBOOT] missing id");
      return;
    }
    DTG_LOG("[REBOOT] ack then restart cmdId=%s", cmdId);
    self.publishCmdAck(cmdId, "reboot", "ok", 1);
    self.client_->mqtt.loop();
    delay(150);
    esp_task_wdt_reset();
    dtgInvokeModemPrepareForRestart();
    esp_task_wdt_reset();
    ESP.restart();
    return;
  }
#endif // !DTG_FEATURE_HUB_CMD

  const char *dev = (deviceAlias && deviceAlias[0]) ? deviceAlias : "_";
  char synthetic[192];
  snprintf(synthetic, sizeof(synthetic), "%sdown/c/%s/%s", self.client_->topicPrefix_, dev,
           streamId);
  self.handleDownlinkCommand(synthetic, payload, len);
}

} // namespace detail

#endif // DTG_FEATURE_NOCODE