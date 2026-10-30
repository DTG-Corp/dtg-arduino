#include "modbus_config.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

#include "../dtg_pin.h"

namespace detail {
namespace {

ModbusByteOrder parseByteOrder(const char *s) {
  if (!s || !s[0] || strcmp(s, "BE") == 0)
    return ModbusByteOrder::BE;
  if (strcmp(s, "LE") == 0)
    return ModbusByteOrder::LE;
  if (strcmp(s, "MID_BE") == 0)
    return ModbusByteOrder::MID_BE;
  if (strcmp(s, "MID_LE") == 0)
    return ModbusByteOrder::MID_LE;
  return ModbusByteOrder::BE;
}

bool parsePinField(JsonObject cfg, const char *key, bool asOutput, int16_t &out, String &error) {
  if (cfg[key].isNull()) {
    out = -1;
    return true;
  }
  const int pin = cfg[key] | -1;
  if (pin < -1 || pin > 48) {
    error = "invalid_pin";
    return false;
  }
  if (pin >= 0 && !dtgPinUsable(pin, asOutput)) {
    error = "invalid_pin";
    return false;
  }
  out = (int16_t)pin;
  return true;
}

void clampReadLength(ModbusTagConfig &tag) {
  if (tag.length < 1)
    tag.length = 1;
  if (tag.functionCode == 1 || tag.functionCode == 2) {
    if (tag.length > 2000)
      tag.length = 2000;
  } else if (tag.length > 125) {
    tag.length = 125;
  }
}

bool parseWriteTag(JsonObject obj, ModbusTagConfig &tag, String &error) {
  const char *alias = obj["alias"] | "";
  if (!dtgAliasValid(alias, DTG_MODBUS_ALIAS_MAX)) {
    error = "invalid_alias";
    return false;
  }
  tag.alias = alias;
  tag.kind = "write";

  JsonObject cfg = obj["config"].as<JsonObject>();
  if (cfg.isNull()) {
    error = "tag_config_missing";
    return false;
  }
  tag.functionCode = (uint8_t)(cfg["functionCode"] | 6);
  if (tag.functionCode != 5 && tag.functionCode != 6) {
    error = "unsupported_write_function_code";
    return false;
  }
  tag.address = (uint16_t)(cfg["address"] | 0);
  tag.length = 1;
  tag.transformer = (const char *)(cfg["transformer"] | "INT16");
  tag.scale = (float)(cfg["scale"] | 1.0f);
  tag.offset = (float)(cfg["offset"] | 0.0f);
  return true;
}

bool parseTag(JsonObject obj, ModbusTagConfig &tag, String &error) {
  const char *alias = obj["alias"] | "";
  if (!dtgAliasValid(alias, DTG_MODBUS_ALIAS_MAX)) {
    error = "invalid_alias";
    return false;
  }
  tag.alias = alias;
  tag.kind = (const char *)(obj["kind"] | "read");

  JsonObject cfg = obj["config"].as<JsonObject>();
  if (cfg.isNull()) {
    error = "tag_config_missing";
    return false;
  }
  tag.functionCode = (uint8_t)(cfg["functionCode"] | 3);
  if (tag.functionCode < 1 || tag.functionCode > 4) {
    error = "unsupported_read_function_code";
    return false;
  }
  tag.address = (uint16_t)(cfg["address"] | 0);
  tag.length = (uint16_t)(cfg["length"] | 1);
  clampReadLength(tag);
  tag.transformer = (const char *)(cfg["transformer"] | "INT16");
  tag.pollingMs = (uint32_t)(cfg["pollingMs"] | 1000);
  tag.scale = (float)(cfg["scale"] | 1.0f);
  tag.offset = (float)(cfg["offset"] | 0.0f);
  tag.deltaThreshold = (float)(cfg["deltaThreshold"] | 0.0f);
  tag.minPublishMs = (uint32_t)(cfg["minPublishMs"] | 0);
  tag.maxPublishMs = (uint32_t)(cfg["maxPublishMs"] | 0);
  tag.important = (bool)(cfg["important"] | false);
  return true;
}

bool parseDevice(JsonObject obj, ModbusDeviceConfig &dev, String &error) {
  const char *alias = obj["alias"] | "";
  if (!dtgAliasValid(alias, DTG_MODBUS_ALIAS_MAX)) {
    error = "invalid_alias";
    return false;
  }
  dev.alias = alias;

  JsonObject cfg = obj["config"].as<JsonObject>();
  if (cfg.isNull()) {
    error = "device_config_missing";
    return false;
  }
  dev.slaveId = (uint8_t)(cfg["slaveId"] | 1);
  dev.delayMs = (uint32_t)(cfg["delayMs"] | 0);
  dev.connectionTimeoutSec = (uint32_t)(cfg["connectionTimeoutSec"] | 60);
  dev.byteOrder = parseByteOrder(cfg["byteOrder"] | "BE");

  JsonArray tags = obj["tags"].as<JsonArray>();
  if (tags.isNull())
    return true;
  dev.tags.reserve(tags.size());
  dev.writeTags.reserve(tags.size());
  for (JsonVariant v : tags) {
    JsonObject tagObj = v.as<JsonObject>();
    if (tagObj.isNull())
      continue;
    const char *kind = tagObj["kind"] | "read";
    if (String(kind).equalsIgnoreCase("write")) {
      ModbusTagConfig tag;
      String tagError;
      if (!parseWriteTag(tagObj, tag, tagError))
        continue;
      dev.writeTags.push_back(tag);
      continue;
    }
    ModbusTagConfig tag;
    String tagError;
    if (!parseTag(tagObj, tag, tagError))
      continue;
    dev.tags.push_back(tag);
  }
  return true;
}

bool parseChannel(JsonObject obj, ModbusChannelConfig &ch, String &error) {
  const char *alias = obj["alias"] | "";
  if (!dtgAliasValid(alias, DTG_MODBUS_ALIAS_MAX)) {
    error = "invalid_alias";
    return false;
  }
  ch.alias = alias;

  JsonObject cfg = obj["config"].as<JsonObject>();
  if (cfg.isNull()) {
    error = "channel_config_missing";
    return false;
  }
  ch.port = (const char *)(cfg["port"] | "UART1");
  ch.baud = (uint32_t)(cfg["baud"] | 9600);
  ch.parity = (const char *)(cfg["parity"] | "none");
  ch.dataBits = (uint8_t)(cfg["dataBits"] | 8);
  ch.stopBits = (uint8_t)(cfg["stopBits"] | 1);
  if (!parsePinField(cfg, "dePin", true, ch.dePin, error))
    return false;
  if (!parsePinField(cfg, "rePin", true, ch.rePin, error))
    return false;
  if (!parsePinField(cfg, "rxPin", false, ch.rxPin, error))
    return false;
  if (!parsePinField(cfg, "txPin", true, ch.txPin, error))
    return false;
  if (ch.rePin >= 0 && ch.dePin < 0) {
    error = "invalid_pin";
    return false;
  }

  JsonArray devices = obj["devices"].as<JsonArray>();
  if (devices.isNull())
    return true;
  ch.devices.reserve(devices.size());
  for (JsonVariant v : devices) {
    JsonObject devObj = v.as<JsonObject>();
    if (devObj.isNull())
      continue;
    ModbusDeviceConfig dev;
    String devError;
    if (!parseDevice(devObj, dev, devError))
      continue;
    if (dev.tags.empty() && dev.writeTags.empty())
      continue;
    ch.devices.push_back(dev);
  }
  return true;
}

} // namespace

bool parseModbusConfigPayload(const char *payload, size_t len, ModbusRuntimeConfig &out,
                              String &error) {
  if (!payload || len == 0) {
    error = "empty_payload";
    return false;
  }
  if (len >= DTG_MODBUS_CFG_BUF_MAX)
    len = DTG_MODBUS_CFG_BUF_MAX - 1;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    error = "json_parse";
    return false;
  }

  out = ModbusRuntimeConfig{};
  out.version = (uint32_t)(doc["version"] | 0);

  JsonArray channels = doc["channels"].as<JsonArray>();
  if (channels.isNull()) {
    error = "channels_missing";
    return false;
  }

  out.channels.reserve(channels.size());
  for (JsonVariant v : channels) {
    JsonObject chObj = v.as<JsonObject>();
    if (chObj.isNull())
      continue;
    ModbusChannelConfig ch;
    String chError;
    if (!parseChannel(chObj, ch, chError)) {
      if (chError == "invalid_pin" || chError == "invalid_alias") {
        error = chError;
        return false;
      }
      continue;
    }
    if (ch.devices.empty())
      continue;
    for (const auto &existing : out.channels) {
      if (existing.port == ch.port) {
        error = "duplicate_uart_port";
        return false;
      }
    }
    out.channels.push_back(ch);
  }

  if (out.channels.empty()) {
    error = "channels_empty";
    return false;
  }

  return true;
}

bool saveModbusConfigBlob(Preferences &prefs, const char *payload, size_t len) {
  if (!payload || len == 0)
    return false;
  return prefs.putBytes("cfg", payload, len) == len;
}

} // namespace detail
