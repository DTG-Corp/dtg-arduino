/**
 * @file modbus_manager.h
 * @brief Modbus RTU no-code runtime: config ingest, polling scheduler, telemetry + health publish.
 */
#pragma once

#include <Arduino.h>

#include <deque>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "modbus_bus.h"
#include "modbus_codec.h"
#include "modbus_config.h"
#include "modbus_health.h"
#include "modbus_scheduler.h"
#include "../dtg_config.h"

class DtgClient;
class Preferences;

namespace detail {

class ModbusManager {
public:
  static ModbusManager &instance();

  void attach(DtgClient *client);
  void poll();
  void shutdown();
  void applyCfgPayload(const char *payload, unsigned int len);
  void enqueueWrite(const char *channelAlias, const char *deviceAlias, const char *tagAlias,
                    float value, const char *reqId);
  static void configCb(const char *topic, const uint8_t *payload, unsigned int len, void *user);

private:
  ModbusManager() = default;

  struct LastValueRef {
    float value = 0.0f;
    bool hasValue = false;
    uint32_t lastPublishMs = 0;
  };

  struct DevicePollState {
    uint8_t consecutiveFails = 0;
    uint32_t skipUntilMs = 0;
  };

  // Result handed off from the eModbus worker task to the main loop.
  struct CompletedRead {
    ModbusPollRef ref;
    uint32_t version = 0;
    bool ok = false;
    bool isTimeout = false;
    bool isBits = false;
    uint16_t registers[125] = {};
    uint8_t regCount = 0;
    uint8_t bits[250] = {};
    uint8_t bitBytes = 0;
  };

  struct PendingWriteJob {
    size_t writeTagIndex = 0;
    float value = 0.0f;
    char reqId[37] = {0};
  };

  struct CompletedWrite {
    size_t writeTagIndex = 0;
    bool ok = false;
    bool isTimeout = false;
    char reqId[37] = {0};
    char error[24] = {0};
  };

  DtgClient *client_ = nullptr;
  ModbusRuntimeConfig cfg_;
  std::vector<std::unique_ptr<ModbusBus>> buses_;
  std::vector<std::vector<std::vector<LastValueRef>>> lastValues_;
  std::vector<std::vector<DevicePollState>> deviceStates_;
  std::vector<ModbusWriteTagRef> writeTags_;
  ModbusScheduler scheduler_;
  ModbusHealth health_;
  uint32_t appliedVersion_ = 0;

  PendingWriteJob writeQueue_[DTG_MODBUS_WRITE_QUEUE_MAX];
  size_t writeHead_ = 0;
  size_t writeTail_ = 0;
  size_t writeCount_ = 0;
  bool writeInFlight_ = false;
  uint32_t writeInFlightSinceMs_ = 0;
  char activeWriteReqId_[37] = {0};

  std::deque<CompletedRead> completed_;
  std::deque<CompletedWrite> completedWrites_;
  SemaphoreHandle_t completedLock_ = nullptr;

  void onCfgPayload(const char *payload, unsigned int len);
  bool applyConfig(const ModbusRuntimeConfig &cfg, String &error);
  bool loadPersistedConfig();
  void rebuildLastValues();
  void rebuildDeviceStates();
  void rebuildWriteTags();
  void updateChannelSkipHealth(size_t channelIndex);
  void publishCfgAck(uint32_t version, const char *status, const char *msg);
  void publishTele(const char *channelAlias, const char *deviceAlias, const char *tagAlias, float v);
  void publishWriteAck(const ModbusWriteTagRef &tag, const char *reqId, bool ok, const char *err);

  int findWriteTagIndex(const char *channelAlias, const char *deviceAlias,
                        const char *tagAlias) const;
  void pushWriteJob(size_t writeTagIndex, float value, const char *reqId);
  void processWriteQueue();
  void drainCompletedWrites();

  // Producer (worker task): enqueue raw result only.
  void enqueueCompleted(const ModbusPollRef &ref, uint32_t version, const ModbusReadResult &rs);
  void enqueueCompletedWrite(size_t writeTagIndex, const char *reqId, const ModbusReadResult &rs);
  // Consumer (main loop): decode, publish, update health and scheduler.
  void drainCompleted();

  static void onReadDone(const ModbusReadResult &rs, const ModbusPendingCapture &cap);
  static void onWriteDone(const ModbusReadResult &rs, const ModbusPendingCapture &cap);
};

} // namespace detail
