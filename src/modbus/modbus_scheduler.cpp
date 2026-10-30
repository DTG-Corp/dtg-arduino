#include "modbus_scheduler.h"

#include "../dtg_config.h"

namespace detail {

void ModbusScheduler::rebuild(const ModbusRuntimeConfig &cfg) {
  entries_.clear();
  rrCursor_ = 0;
  channelBusy_.assign(cfg.channels.size(), false);

  for (size_t ci = 0; ci < cfg.channels.size(); ++ci) {
    const auto &ch = cfg.channels[ci];
    for (size_t di = 0; di < ch.devices.size(); ++di) {
      const auto &dev = ch.devices[di];
      for (size_t ti = 0; ti < dev.tags.size(); ++ti) {
        const auto &tag = dev.tags[ti];
        Entry e;
        e.ref = {ci, di, ti};
        e.pollingMs = tag.pollingMs > 0 ? tag.pollingMs : 1000;
        e.nextAtMs = 0;
        e.inFlightSinceMs = 0;
        e.inFlight = false;
        entries_.push_back(e);
      }
    }
  }
}

bool ModbusScheduler::acquireDue(uint32_t nowMs, ModbusPollRef &out) {
  if (entries_.empty())
    return false;

  for (size_t i = 0; i < entries_.size(); ++i) {
    const size_t idx = (rrCursor_ + i) % entries_.size();
    Entry &e = entries_[idx];
    if (e.inFlight) {
      if ((int32_t)(nowMs - e.inFlightSinceMs) >= (int32_t)DTG_MODBUS_INFLIGHT_MS) {
        e.inFlight = false;
        e.inFlightSinceMs = 0;
        if (e.ref.channelIndex < channelBusy_.size())
          channelBusy_[e.ref.channelIndex] = false;
        e.nextAtMs = nowMs + e.pollingMs;
      } else {
        continue;
      }
    }
    if (e.ref.channelIndex < channelBusy_.size() && channelBusy_[e.ref.channelIndex])
      continue;
    if ((int32_t)(nowMs - e.nextAtMs) < 0)
      continue;

    e.inFlight = true;
    e.inFlightSinceMs = nowMs;
    if (e.ref.channelIndex < channelBusy_.size())
      channelBusy_[e.ref.channelIndex] = true;
    out = e.ref;
    rrCursor_ = (idx + 1) % entries_.size();
    return true;
  }
  return false;
}

void ModbusScheduler::release(const ModbusPollRef &ref, uint32_t nextAtMs) {
  for (auto &e : entries_) {
    if (e.ref.channelIndex == ref.channelIndex && e.ref.deviceIndex == ref.deviceIndex &&
        e.ref.tagIndex == ref.tagIndex) {
      e.inFlight = false;
      e.inFlightSinceMs = 0;
      e.nextAtMs = nextAtMs;
      break;
    }
  }
  if (ref.channelIndex < channelBusy_.size())
    channelBusy_[ref.channelIndex] = false;
}

} // namespace detail
