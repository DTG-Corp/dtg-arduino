/**
 * @file modbus_manager.cpp
 * @brief Modbus RTU runtime manager with config persistence, scheduling, and telemetry publishing.
 */
#include "modbus_manager.h"

#if DTG_FEATURE_MODBUS

#include "../dtg_client.h"
#include "../dtg_config.h"
#include "../dtg_debug.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include <string.h>

namespace detail {

namespace {
constexpr size_t kModbusCfgMax = DTG_MODBUS_CFG_BUF_MAX;
static char *s_modbusCfgBuf = nullptr;
static Preferences s_modbusPrefs;
static bool s_loggedFirstPoll = false;
static bool s_loggedFirstResult = false;

bool ensureModbusCfgBuf()
{
  if (!s_modbusCfgBuf)
    s_modbusCfgBuf = static_cast<char *>(malloc(kModbusCfgMax));
  return s_modbusCfgBuf != nullptr;
}

void copyModbusAlias(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0)
    return;
  if (!src)
    src = "";
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

void copyModbusAlias(char *dst, size_t dstLen, const String &src) {
  copyModbusAlias(dst, dstLen, src.c_str());
}
} // namespace

ModbusManager &ModbusManager::instance() {
  static ModbusManager mgr;
  return mgr;
}

void ModbusManager::attach(DtgClient *client) {
  client_ = client;
  if (!ensureModbusCfgBuf())
    return;
  if (!completedLock_)
    completedLock_ = xSemaphoreCreateMutex();
  s_modbusPrefs.begin("dtg_modbus", false);
  appliedVersion_ = s_modbusPrefs.getUInt("cfg_ver", 0);
  loadPersistedConfig();
}

bool ModbusManager::loadPersistedConfig() {
  if (!ensureModbusCfgBuf())
    return false;
  const size_t n = s_modbusPrefs.getBytesLength("cfg");
  if (n == 0 || n >= kModbusCfgMax)
    return false;
  if (s_modbusPrefs.getBytes("cfg", s_modbusCfgBuf, n) != n)
    return false;
  s_modbusCfgBuf[n] = '\0';

  ModbusRuntimeConfig parsed;
  String error;
  if (!parseModbusConfigPayload(s_modbusCfgBuf, n, parsed, error)) {
    DTG_LOG("Modbus persisted cfg parse failed: %s", error.c_str());
    return false;
  }

  if (!applyConfig(parsed, error)) {
    DTG_LOG("Modbus persisted cfg apply failed: %s", error.c_str());
    return false;
  }

  DTG_LOG("Modbus persisted cfg loaded v%u", (unsigned)cfg_.version);
  return true;
}

bool ModbusManager::applyConfig(const ModbusRuntimeConfig &cfg, String &error) {
  for (auto &bus : buses_) {
    if (bus)
      bus->shutdown();
  }
  buses_.clear();

  if (completedLock_) {
    xSemaphoreTake(completedLock_, portMAX_DELAY);
    completed_.clear();
    completedWrites_.clear();
    xSemaphoreGive(completedLock_);
  }

  std::vector<std::unique_ptr<ModbusBus>> newBuses;
  newBuses.reserve(cfg.channels.size());

  for (const auto &ch : cfg.channels) {
    auto bus = std::unique_ptr<ModbusBus>(new ModbusBus());
    if (!bus->begin(ch, error)) {
      for (auto &started : newBuses) {
        if (started)
          started->shutdown();
      }
      return false;
    }
    newBuses.push_back(std::move(bus));
  }

  cfg_ = cfg;
  buses_ = std::move(newBuses);
  scheduler_.rebuild(cfg_);
  health_.rebuild(cfg_.channels.size());
  for (size_t i = 0; i < cfg_.channels.size(); ++i) {
    const auto &ch = cfg_.channels[i];
    health_.setAutoDirection(i, ch.dePin < 0 && ch.rePin < 0);
  }
  rebuildLastValues();
  rebuildDeviceStates();
  rebuildWriteTags();
  writeHead_ = 0;
  writeTail_ = 0;
  writeCount_ = 0;
  writeInFlight_ = false;
  writeInFlightSinceMs_ = 0;
  activeWriteReqId_[0] = '\0';
  s_loggedFirstPoll = false;
  s_loggedFirstResult = false;
  return true;
}

void ModbusManager::rebuildWriteTags() {
  writeTags_.clear();
  for (size_t ci = 0; ci < cfg_.channels.size(); ++ci) {
    const auto &ch = cfg_.channels[ci];
    for (size_t di = 0; di < ch.devices.size(); ++di) {
      const auto &dev = ch.devices[di];
      for (const auto &tag : dev.writeTags) {
        ModbusWriteTagRef ref;
        ref.channelIndex = ci;
        ref.deviceIndex = di;
        ref.slaveId = dev.slaveId;
        ref.functionCode = tag.functionCode;
        ref.address = tag.address;
        copyModbusAlias(ref.transformer, sizeof(ref.transformer), tag.transformer);
        ref.scale = tag.scale;
        ref.offset = tag.offset;
        copyModbusAlias(ref.channelAlias, sizeof(ref.channelAlias), ch.alias);
        copyModbusAlias(ref.deviceAlias, sizeof(ref.deviceAlias), dev.alias);
        copyModbusAlias(ref.tagAlias, sizeof(ref.tagAlias), tag.alias);
        writeTags_.push_back(ref);
      }
    }
  }
}

void ModbusManager::rebuildDeviceStates() {
  deviceStates_.clear();
  deviceStates_.resize(cfg_.channels.size());
  for (size_t ci = 0; ci < cfg_.channels.size(); ++ci) {
    const auto &ch = cfg_.channels[ci];
    deviceStates_[ci].resize(ch.devices.size());
  }
}

void ModbusManager::updateChannelSkipHealth(size_t channelIndex) {
  if (channelIndex >= deviceStates_.size())
    return;
  uint8_t maxFails = 0;
  bool anySkip = false;
  const uint32_t now = millis();
  for (const auto &ds : deviceStates_[channelIndex]) {
    if (ds.consecutiveFails > maxFails)
      maxFails = ds.consecutiveFails;
    if (ds.skipUntilMs > 0 && (int32_t)(now - ds.skipUntilMs) < 0)
      anySkip = true;
  }
  health_.setDeviceSkipState(channelIndex, maxFails, anySkip);
}

void ModbusManager::rebuildLastValues() {
  lastValues_.clear();
  lastValues_.resize(cfg_.channels.size());
  for (size_t ci = 0; ci < cfg_.channels.size(); ++ci) {
    const auto &ch = cfg_.channels[ci];
    lastValues_[ci].resize(ch.devices.size());
    for (size_t di = 0; di < ch.devices.size(); ++di) {
      lastValues_[ci][di].resize(ch.devices[di].tags.size());
    }
  }
}

void ModbusManager::configCb(const char *topic, const uint8_t *payload, unsigned int len,
                             void *user) {
  (void)topic;
  (void)user;
  if (!payload || len == 0)
    return;
  if (!ensureModbusCfgBuf())
    return;
  if (len >= kModbusCfgMax)
    len = kModbusCfgMax - 1;
  memcpy(s_modbusCfgBuf, payload, len);
  s_modbusCfgBuf[len] = '\0';
  instance().onCfgPayload(s_modbusCfgBuf, len);
}

void ModbusManager::onCfgPayload(const char *payload, unsigned int len) {
  ModbusRuntimeConfig parsed;
  String error;
  if (!parseModbusConfigPayload(payload, len, parsed, error)) {
    publishCfgAck(appliedVersion_, "failed", error.c_str());
    return;
  }

  if (parsed.version > 0 && parsed.version <= appliedVersion_) {
    publishCfgAck(appliedVersion_, "applied", "noop_version");
    return;
  }

  if (!applyConfig(parsed, error)) {
    publishCfgAck(appliedVersion_, "failed", error.c_str());
    return;
  }

  if (!saveModbusConfigBlob(s_modbusPrefs, payload, len)) {
    publishCfgAck(appliedVersion_, "failed", "persist_blob_failed");
    return;
  }

  appliedVersion_ = parsed.version;
  s_modbusPrefs.putUInt("cfg_ver", appliedVersion_);
  DTG_LOG("Modbus cfg applied v%u with %u channel(s)", (unsigned)appliedVersion_,
          (unsigned)cfg_.channels.size());
  publishCfgAck(appliedVersion_, "applied", nullptr);
}

void ModbusManager::publishCfgAck(uint32_t version, const char *status, const char *msg) {
  if (!client_)
    return;
  JsonDocument doc;
  doc["version"] = version;
  doc["status"] = status;
  if (msg)
    doc["error"] = msg;
  char buf[192];
  serializeJson(doc, buf, sizeof(buf));
  client_->publishTopicSuffix("up/modbus/cfg_ack", buf, true, 1);
}

void ModbusManager::publishTele(const char *channelAlias, const char *deviceAlias,
                                const char *tagAlias, float v) {
  if (!client_)
    return;

  char suffix[96];
  snprintf(suffix, sizeof(suffix), "tele/modbus/%s/%s/%s", channelAlias, deviceAlias, tagAlias);

  JsonDocument doc;
  doc["v"] = v;
  doc["t"] = (uint32_t)millis();
  doc["q"] = 0;

  char buf[96];
  serializeJson(doc, buf, sizeof(buf));
  client_->publishTopicSuffix(suffix, buf, false, 0);
}

void ModbusManager::enqueueCompleted(const ModbusPollRef &ref, uint32_t version,
                                     const ModbusReadResult &rs) {
  // Runs on the eModbus worker task: do not touch MQTT/scheduler/health here.
  if (!completedLock_)
    return;
  CompletedRead cr;
  cr.ref = ref;
  cr.version = version;
  cr.ok = rs.ok;
  cr.isTimeout = rs.isTimeout;
  cr.isBits = rs.isBits;
  cr.regCount = rs.regCount > 125 ? 125 : rs.regCount;
  memcpy(cr.registers, rs.registers, (size_t)cr.regCount * sizeof(uint16_t));
  cr.bitBytes = rs.bitBytes > 250 ? 250 : rs.bitBytes;
  memcpy(cr.bits, rs.bits, cr.bitBytes);

  xSemaphoreTake(completedLock_, portMAX_DELAY);
  if (completed_.size() >= DTG_MODBUS_COMPLETED_MAX)
    completed_.pop_front();
  completed_.push_back(std::move(cr));
  xSemaphoreGive(completedLock_);
}

void ModbusManager::drainCompleted() {
  if (!completedLock_)
    return;

  std::deque<CompletedRead> batch;
  xSemaphoreTake(completedLock_, portMAX_DELAY);
  batch.swap(completed_);
  xSemaphoreGive(completedLock_);

  for (const auto &cr : batch) {
    if (!s_loggedFirstResult) {
      s_loggedFirstResult = true;
      DTG_LOG("Modbus first result ok=%d timeout=%d regs=%u",
              cr.ok ? 1 : 0, cr.isTimeout ? 1 : 0,
              (unsigned)cr.regCount);
    }

    // Drop results that belong to a superseded configuration.
    if (cr.version != cfg_.version)
      continue;

    const size_t ci = cr.ref.channelIndex;
    const size_t di = cr.ref.deviceIndex;
    const size_t ti = cr.ref.tagIndex;
    if (ci >= cfg_.channels.size() || di >= cfg_.channels[ci].devices.size() ||
        ti >= cfg_.channels[ci].devices[di].tags.size())
      continue;

    const auto &rch = cfg_.channels[ci];
    const auto &rdev = rch.devices[di];
    const auto &rtag = rdev.tags[ti];
    const uint32_t now = millis();
    const uint32_t nextAt = now + rtag.pollingMs;

    if (!cr.ok) {
      if (ci < deviceStates_.size() && di < deviceStates_[ci].size()) {
        auto &ds = deviceStates_[ci][di];
        if (ds.consecutiveFails < 255)
          ds.consecutiveFails++;
        if (ds.consecutiveFails >= DTG_MODBUS_FAIL_THRESHOLD)
          ds.skipUntilMs = now + DTG_MODBUS_FAIL_BACKOFF_MS;
        updateChannelSkipHealth(ci);
      }
      if (cr.isTimeout)
        health_.markTimeout(ci);
      else
        health_.markError(ci);
      scheduler_.release(cr.ref, nextAt);
      continue;
    }

    if (ci < deviceStates_.size() && di < deviceStates_[ci].size()) {
      deviceStates_[ci][di].consecutiveFails = 0;
      deviceStates_[ci][di].skipUntilMs = 0;
      updateChannelSkipHealth(ci);
    }

    float value = 0.0f;
    bool decoded = false;
    if (cr.isBits)
      decoded = decodeModbusBits(cr.bits, cr.bitBytes, rtag.length, rtag.scale, rtag.offset, value);
    else
      decoded = decodeModbusRegisters(cr.registers, cr.regCount, rtag.transformer.c_str(),
                                      rtag.scale, rtag.offset, value);

    if (!decoded) {
      health_.markError(ci);
      scheduler_.release(cr.ref, nextAt);
      continue;
    }

    auto &last = lastValues_[ci][di][ti];
    const bool valueChanged =
        !last.hasValue || fabsf(last.value - value) >= rtag.deltaThreshold;
    bool shouldPublish = valueChanged || rtag.important;
    if (rtag.maxPublishMs > 0 && last.hasValue &&
        (now - last.lastPublishMs) >= rtag.maxPublishMs)
      shouldPublish = true;
    if (shouldPublish && rtag.minPublishMs > 0 && last.hasValue &&
        (now - last.lastPublishMs) < rtag.minPublishMs)
      shouldPublish = false;

    if (shouldPublish) {
      publishTele(rch.alias.c_str(), rdev.alias.c_str(), rtag.alias.c_str(), value);
      last.value = value;
      last.hasValue = true;
      last.lastPublishMs = now;
    } else if (!last.hasValue) {
      last.value = value;
      last.hasValue = true;
    }

    health_.markOk(ci);
    scheduler_.release(cr.ref, nextAt + rdev.delayMs);
  }
}

void ModbusManager::publishWriteAck(const ModbusWriteTagRef &tag, const char *reqId, bool ok,
                                    const char *err) {
  if (!client_ || !reqId || !reqId[0])
    return;

  char suffix[128];
  snprintf(suffix, sizeof(suffix), "up/modbus/%s/%s/%s/w_ack", tag.channelAlias, tag.deviceAlias,
           tag.tagAlias);

  JsonDocument doc;
  doc["reqId"] = reqId;
  doc["ok"] = ok ? 1 : 0;
  doc["t"] = (uint32_t)millis();
  if (!ok && err && err[0])
    doc["err"] = err;

  char buf[128];
  serializeJson(doc, buf, sizeof(buf));
  client_->publishTopicSuffix(suffix, buf, false, 0);
}

int ModbusManager::findWriteTagIndex(const char *channelAlias, const char *deviceAlias,
                                     const char *tagAlias) const {
  if (!channelAlias || !deviceAlias || !tagAlias)
    return -1;
  for (size_t i = 0; i < writeTags_.size(); ++i) {
    const ModbusWriteTagRef &t = writeTags_[i];
    if (strcmp(t.channelAlias, channelAlias) == 0 && strcmp(t.deviceAlias, deviceAlias) == 0 &&
        strcmp(t.tagAlias, tagAlias) == 0)
      return static_cast<int>(i);
  }
  return -1;
}

void ModbusManager::pushWriteJob(size_t writeTagIndex, float value, const char *reqId) {
  if (!reqId || !reqId[0] || writeTagIndex >= writeTags_.size())
    return;

  for (size_t i = 0; i < writeCount_; ++i) {
    const size_t idx = (writeHead_ + i) % DTG_MODBUS_WRITE_QUEUE_MAX;
    if (writeQueue_[idx].writeTagIndex == writeTagIndex) {
      if (writeQueue_[idx].reqId[0] && strcmp(writeQueue_[idx].reqId, reqId) != 0 &&
          writeTagIndex < writeTags_.size())
        publishWriteAck(writeTags_[writeTagIndex], writeQueue_[idx].reqId, false, "superseded");
      writeQueue_[idx].value = value;
      strncpy(writeQueue_[idx].reqId, reqId, sizeof(writeQueue_[idx].reqId) - 1);
      writeQueue_[idx].reqId[sizeof(writeQueue_[idx].reqId) - 1] = '\0';
      return;
    }
  }

  if (writeCount_ >= DTG_MODBUS_WRITE_QUEUE_MAX) {
    writeHead_ = (writeHead_ + 1) % DTG_MODBUS_WRITE_QUEUE_MAX;
    writeCount_--;
  }

  PendingWriteJob &job = writeQueue_[writeTail_];
  job.writeTagIndex = writeTagIndex;
  job.value = value;
  strncpy(job.reqId, reqId, sizeof(job.reqId) - 1);
  job.reqId[sizeof(job.reqId) - 1] = '\0';
  writeTail_ = (writeTail_ + 1) % DTG_MODBUS_WRITE_QUEUE_MAX;
  writeCount_++;
}

void ModbusManager::enqueueWrite(const char *channelAlias, const char *deviceAlias,
                                 const char *tagAlias, float value, const char *reqId) {
  const int idx = findWriteTagIndex(channelAlias, deviceAlias, tagAlias);
  if (idx < 0) {
    DTG_LOG("Modbus write unknown tag %s/%s/%s", channelAlias ? channelAlias : "?",
            deviceAlias ? deviceAlias : "?", tagAlias ? tagAlias : "?");
    return;
  }
  pushWriteJob(static_cast<size_t>(idx), value, reqId);
}

void ModbusManager::enqueueCompletedWrite(size_t writeTagIndex, const char *reqId,
                                          const ModbusReadResult &rs) {
  if (!completedLock_)
    return;

  CompletedWrite cw;
  cw.writeTagIndex = writeTagIndex;
  cw.ok = rs.ok;
  cw.isTimeout = rs.isTimeout;
  if (reqId) {
    strncpy(cw.reqId, reqId, sizeof(cw.reqId) - 1);
    cw.reqId[sizeof(cw.reqId) - 1] = '\0';
  }
  if (!rs.ok && rs.error[0]) {
    strncpy(cw.error, rs.error, sizeof(cw.error) - 1);
    cw.error[sizeof(cw.error) - 1] = '\0';
  } else if (rs.isTimeout) {
    strncpy(cw.error, "timeout", sizeof(cw.error) - 1);
    cw.error[sizeof(cw.error) - 1] = '\0';
  }

  xSemaphoreTake(completedLock_, portMAX_DELAY);
  if (completedWrites_.size() >= DTG_MODBUS_COMPLETED_MAX)
    completedWrites_.pop_front();
  completedWrites_.push_back(cw);
  xSemaphoreGive(completedLock_);
}

void ModbusManager::drainCompletedWrites() {
  if (!completedLock_)
    return;

  std::deque<CompletedWrite> batch;
  xSemaphoreTake(completedLock_, portMAX_DELAY);
  batch.swap(completedWrites_);
  xSemaphoreGive(completedLock_);

  for (const auto &cw : batch) {
    if (cw.writeTagIndex >= writeTags_.size())
      continue;
    const ModbusWriteTagRef &tag = writeTags_[cw.writeTagIndex];
    const char *err = cw.ok ? nullptr : (cw.error[0] ? cw.error : "error");
    publishWriteAck(tag, cw.reqId, cw.ok, err);
    if (cw.ok)
      health_.markOk(tag.channelIndex);
    else if (cw.isTimeout)
      health_.markTimeout(tag.channelIndex);
    else
      health_.markError(tag.channelIndex);
  }
  if (!batch.empty()) {
    writeInFlight_ = false;
    writeInFlightSinceMs_ = 0;
  }
}

void ModbusManager::processWriteQueue() {
  if (writeInFlight_ || writeCount_ == 0)
    return;

  const PendingWriteJob &job = writeQueue_[writeHead_];
  if (job.writeTagIndex >= writeTags_.size())
    return;

  const ModbusWriteTagRef &tag = writeTags_[job.writeTagIndex];
  if (tag.channelIndex >= buses_.size() || !buses_[tag.channelIndex])
    return;

  writeInFlight_ = true;
  writeInFlightSinceMs_ = millis();
  strncpy(activeWriteReqId_, job.reqId, sizeof(activeWriteReqId_) - 1);
  activeWriteReqId_[sizeof(activeWriteReqId_) - 1] = '\0';
  writeHead_ = (writeHead_ + 1) % DTG_MODBUS_WRITE_QUEUE_MAX;
  writeCount_--;

  const size_t ci = tag.channelIndex;
  ModbusPendingCapture cap;
  cap.writeTagIndex = job.writeTagIndex;
  strncpy(cap.reqId, activeWriteReqId_, sizeof(cap.reqId) - 1);
  cap.reqId[sizeof(cap.reqId) - 1] = '\0';

  if (tag.functionCode == 5) {
    const bool on = (job.value != 0.0f);
    buses_[ci]->writeSingleCoil(tag.slaveId, tag.address, on, onWriteDone, cap);
    return;
  }

  if (tag.functionCode != 6) {
    writeInFlight_ = false;
    writeInFlightSinceMs_ = 0;
    publishWriteAck(tag, cap.reqId, false, "unsupported_function_code");
    return;
  }

  uint16_t raw = 0;
  if (!encodeModbusRegisterValue(job.value, tag.transformer, tag.scale, tag.offset, raw)) {
    writeInFlight_ = false;
    writeInFlightSinceMs_ = 0;
    publishWriteAck(tag, cap.reqId, false, "encode_failed");
    return;
  }

  buses_[ci]->writeSingleRegister(tag.slaveId, tag.address, raw, onWriteDone, cap);
}

void ModbusManager::poll() {
  if (!client_ || cfg_.channels.empty())
    return;

  drainCompleted();
  drainCompletedWrites();
  processWriteQueue();

  const uint32_t nowMs = millis();
  if (writeInFlight_ &&
      (int32_t)(nowMs - writeInFlightSinceMs_) >= (int32_t)DTG_MODBUS_INFLIGHT_MS) {
    writeInFlight_ = false;
    writeInFlightSinceMs_ = 0;
  }
  if (writeInFlight_) {
    health_.maybePublish(client_, cfg_, nowMs);
    return;
  }

  // 2) Issue new read requests: one outstanding transaction per bus.
  const uint32_t version = cfg_.version;
  ModbusPollRef ref;
  size_t guard = 0;
  const uint32_t now = millis();
  while (guard++ < 256 && scheduler_.acquireDue(now, ref)) {
    const size_t ci = ref.channelIndex;
    const size_t di = ref.deviceIndex;
    const size_t ti = ref.tagIndex;
    if (ci >= cfg_.channels.size() || di >= cfg_.channels[ci].devices.size() ||
        ti >= cfg_.channels[ci].devices[di].tags.size() || ci >= buses_.size() || !buses_[ci]) {
      scheduler_.release(ref, now + 1000);
      continue;
    }

    if (ci < deviceStates_.size() && di < deviceStates_[ci].size()) {
      const auto &ds = deviceStates_[ci][di];
      if (ds.skipUntilMs > 0 && (int32_t)(now - ds.skipUntilMs) < 0) {
        scheduler_.release(ref, now + 500);
        continue;
      }
    }

    const auto &dev = cfg_.channels[ci].devices[di];
    const auto &tag = dev.tags[ti];
    if (!s_loggedFirstPoll) {
      s_loggedFirstPoll = true;
      DTG_LOG("Modbus poll started %s/%s/%s slave=%u fc=%u addr=%u qty=%u",
              cfg_.channels[ci].alias.c_str(), dev.alias.c_str(), tag.alias.c_str(),
              (unsigned)dev.slaveId, (unsigned)tag.functionCode, (unsigned)tag.address,
              (unsigned)tag.length);
    }
    ModbusPendingCapture cap;
    cap.channelIndex = ref.channelIndex;
    cap.deviceIndex = ref.deviceIndex;
    cap.tagIndex = ref.tagIndex;
    cap.version = version;
    buses_[ci]->readRegisters(tag.functionCode, dev.slaveId, tag.address, tag.length, onReadDone,
                              cap);
  }

  health_.maybePublish(client_, cfg_, now);
}

void ModbusManager::applyCfgPayload(const char *payload, unsigned int len)
{
  if (!payload || len == 0)
    return;
  if (!ensureModbusCfgBuf())
    return;
  if (len >= kModbusCfgMax)
    len = kModbusCfgMax - 1;
  memcpy(s_modbusCfgBuf, payload, len);
  s_modbusCfgBuf[len] = '\0';
  onCfgPayload(s_modbusCfgBuf, len);
}

void ModbusManager::shutdown()
{
  buses_.clear();
  cfg_ = ModbusRuntimeConfig{};
  lastValues_.clear();
  deviceStates_.clear();
  writeTags_.clear();
  scheduler_.rebuild(cfg_);
  health_.rebuild(0);
  writeHead_ = 0;
  writeTail_ = 0;
  writeCount_ = 0;
  writeInFlight_ = false;
  writeInFlightSinceMs_ = 0;
  if (completedLock_)
  {
    xSemaphoreTake(completedLock_, portMAX_DELAY);
    completed_.clear();
    completedWrites_.clear();
    xSemaphoreGive(completedLock_);
  }
  s_loggedFirstPoll = false;
  s_loggedFirstResult = false;
  DTG_LOG("Modbus runtime shutdown");
}

void ModbusManager::onReadDone(const ModbusReadResult &rs, const ModbusPendingCapture &cap) {
  ModbusPollRef ref;
  ref.channelIndex = cap.channelIndex;
  ref.deviceIndex = cap.deviceIndex;
  ref.tagIndex = cap.tagIndex;
  instance().enqueueCompleted(ref, cap.version, rs);
}

void ModbusManager::onWriteDone(const ModbusReadResult &rs, const ModbusPendingCapture &cap) {
  instance().enqueueCompletedWrite(cap.writeTagIndex, cap.reqId, rs);
}

} // namespace detail

#endif // DTG_FEATURE_MODBUS
