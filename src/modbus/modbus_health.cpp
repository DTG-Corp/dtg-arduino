#include "modbus_health.h"

#include <ArduinoJson.h>
#include <string.h>

#include "../dtg_client.h"

namespace detail {

void ModbusHealth::rebuild(size_t channelCount) {
  counters_.assign(channelCount, Counter{});
}

void ModbusHealth::setAutoDirection(size_t channelIndex, bool enabled) {
  if (channelIndex < counters_.size())
    counters_[channelIndex].autoDir = enabled;
}

void ModbusHealth::markOk(size_t channelIndex) {
  if (channelIndex < counters_.size()) {
    counters_[channelIndex].ok++;
    counters_[channelIndex].lastErr[0] = '\0';
  }
}

void ModbusHealth::markError(size_t channelIndex) {
  if (channelIndex < counters_.size()) {
    counters_[channelIndex].err++;
    strncpy(counters_[channelIndex].lastErr, "error", sizeof(counters_[channelIndex].lastErr) - 1);
    counters_[channelIndex].lastErr[sizeof(counters_[channelIndex].lastErr) - 1] = '\0';
  }
}

void ModbusHealth::markTimeout(size_t channelIndex) {
  if (channelIndex < counters_.size()) {
    counters_[channelIndex].err++;
    counters_[channelIndex].timeout++;
    strncpy(counters_[channelIndex].lastErr, "timeout", sizeof(counters_[channelIndex].lastErr) - 1);
    counters_[channelIndex].lastErr[sizeof(counters_[channelIndex].lastErr) - 1] = '\0';
  }
}

void ModbusHealth::setDeviceSkipState(size_t channelIndex, uint8_t consecutiveFails,
                                      bool skipActive) {
  if (channelIndex < counters_.size()) {
    counters_[channelIndex].consecutiveFails = consecutiveFails;
    counters_[channelIndex].skipActive = skipActive;
  }
}

void ModbusHealth::maybePublish(DtgClient *client, const ModbusRuntimeConfig &cfg,
                                uint32_t nowMs) {
  if (!client)
    return;

  for (size_t i = 0; i < counters_.size() && i < cfg.channels.size(); ++i) {
    Counter &c = counters_[i];
    if ((nowMs - c.lastPublishMs) < 10000)
      continue;

    char suffix[128];
    snprintf(suffix, sizeof(suffix), "up/modbus/%s/health", cfg.channels[i].alias.c_str());

    JsonDocument doc;
    doc["ok"] = c.ok;
    doc["err"] = c.err;
    doc["timeout"] = c.timeout;
    doc["t"] = nowMs;
    doc["autoDir"] = c.autoDir;
    if (c.consecutiveFails > 0)
      doc["consecutiveFails"] = c.consecutiveFails;
    if (c.skipActive)
      doc["skipActive"] = true;
    if (c.lastErr[0])
      doc["lastErr"] = c.lastErr;

    char payload[160];
    serializeJson(doc, payload, sizeof(payload));
    client->publishTopicSuffix(suffix, payload, false, 0);
    c.lastPublishMs = nowMs;
  }
}

} // namespace detail
