/**
 * @file dtg_timer.cpp
 * @brief Static timer pool - no heap, fixed DTG_TIMER_MAX slots.
 */
#include "dtg_timer.h"

// ponytail: one global pool per sketch (single DtgClient). Per-instance pools if multi-client ever lands.
static DtgTimerSlot g_slots[DTG_TIMER_MAX];

static constexpr uint8_t DTG_TIMER_ARMED = 0x08;

static unsigned long g_periodUplinkMs = DTG_PERIOD_UPLINK_DEFAULT_MS;

static bool slotMatches(int8_t id, uint8_t gen)
{
  return id >= 0 && id < DTG_TIMER_MAX && (g_slots[id].flags & DTG_TIMER_USED) &&
         g_slots[id].gen == gen;
}

bool DtgTimerHandle::valid() const { return slotMatches(id_, gen_); }

static void clearSlot(int i)
{
  const uint8_t nextGen = static_cast<uint8_t>(g_slots[i].gen + 1U);
  g_slots[i] = DtgTimerSlot{};
  g_slots[i].gen = nextGen; // invalidate stale handles pointing at this index
}

static unsigned long clampInterval(unsigned long intervalMs)
{
  return intervalMs == 0 ? 1UL : intervalMs;
}

static DtgTimerHandle claimSlot(unsigned long intervalMs, DtgTimerCallback cb, bool oneshot)
{
  if (!cb)
    return DtgTimerHandle();

  const bool followUplink = (intervalMs == DTG_PERIOD_UPLINK);
  if (followUplink)
    intervalMs = clampInterval(g_periodUplinkMs);
  else
    intervalMs = clampInterval(intervalMs);

  for (int i = 0; i < DTG_TIMER_MAX; ++i)
  {
    if (g_slots[i].flags & DTG_TIMER_USED)
      continue;

    g_slots[i].intervalMs = intervalMs;
    g_slots[i].prevMs = 0;
    g_slots[i].cb = cb;
    g_slots[i].flags = static_cast<uint8_t>(
        DTG_TIMER_USED | DTG_TIMER_ENABLED | (oneshot ? DTG_TIMER_ONESHOT : 0) |
        (followUplink ? DTG_TIMER_FOLLOW_UPLINK : 0));
    // gen preserved across clearSlot so reused indices reject stale handles
    return DtgTimerHandle(static_cast<int8_t>(i), g_slots[i].gen);
  }
  return DtgTimerHandle();
}

DtgTimerHandle dtgTimerAddInterval(unsigned long intervalMs, DtgTimerCallback cb)
{
  return claimSlot(intervalMs, cb, false);
}

DtgTimerHandle dtgTimerAddTimeout(unsigned long intervalMs, DtgTimerCallback cb)
{
  return claimSlot(intervalMs, cb, true);
}

void dtgTimerDelete(DtgTimerHandle handle)
{
  handle.deleteTimer();
}

void dtgTimerSetPeriodUplink(unsigned long periodUplinkMs)
{
  g_periodUplinkMs = clampInterval(periodUplinkMs);
  for (int i = 0; i < DTG_TIMER_MAX; ++i)
  {
    DtgTimerSlot &s = g_slots[i];
    if (!(s.flags & DTG_TIMER_USED) || !(s.flags & DTG_TIMER_FOLLOW_UPLINK))
      continue;
    s.intervalMs = g_periodUplinkMs;
  }
}

unsigned long dtgTimerIntervalMs(DtgTimerHandle handle)
{
  if (!handle.valid())
    return 0;
  return g_slots[handle.id()].intervalMs;
}

bool DtgTimerHandle::isEnable() const
{
  if (!slotMatches(id_, gen_))
    return false;
  return (g_slots[id_].flags & DTG_TIMER_ENABLED) != 0;
}

void DtgTimerHandle::enable() const
{
  if (!slotMatches(id_, gen_))
    return;
  g_slots[id_].flags =
      static_cast<uint8_t>(g_slots[id_].flags | DTG_TIMER_ENABLED);
}

void DtgTimerHandle::disable() const
{
  if (!slotMatches(id_, gen_))
    return;
  g_slots[id_].flags =
      static_cast<uint8_t>(g_slots[id_].flags & ~DTG_TIMER_ENABLED);
}

bool DtgTimerHandle::changeInterval(unsigned long intervalMs) const
{
  if (!slotMatches(id_, gen_))
    return false;
  // Fixed override clears Hub follow (intentional hardcode wins).
  g_slots[id_].flags =
      static_cast<uint8_t>(g_slots[id_].flags & ~DTG_TIMER_FOLLOW_UPLINK);
  g_slots[id_].intervalMs = clampInterval(intervalMs);
  return true;
}

void DtgTimerHandle::restart() const
{
  if (!slotMatches(id_, gen_))
    return;
  g_slots[id_].prevMs = 0;
  g_slots[id_].flags =
      static_cast<uint8_t>(g_slots[id_].flags & ~DTG_TIMER_ARMED);
}

void DtgTimerHandle::deleteTimer() const
{
  if (!slotMatches(id_, gen_))
    return;
  clearSlot(id_);
}

int dtgTimerUsedCount()
{
  int n = 0;
  for (int i = 0; i < DTG_TIMER_MAX; ++i)
  {
    if (g_slots[i].flags & DTG_TIMER_USED)
      ++n;
  }
  return n;
}

void dtgTimerTick(unsigned long now)
{
  for (int i = 0; i < DTG_TIMER_MAX; ++i)
  {
    DtgTimerSlot &s = g_slots[i];
    if (!(s.flags & DTG_TIMER_USED) || !s.cb)
      continue;

    if (s.flags & DTG_TIMER_FOLLOW_UPLINK)
      s.intervalMs = g_periodUplinkMs;

    // First observation after claim/restart: arm without firing (works when now==0).
    if (!(s.flags & DTG_TIMER_ARMED))
    {
      s.prevMs = now;
      s.flags = static_cast<uint8_t>(s.flags | DTG_TIMER_ARMED);
      continue;
    }

    const unsigned long elapsed = now - s.prevMs;
    if (elapsed < s.intervalMs)
      continue;

    // One fire per tick; advance by full skipped periods (no multi-loop storm).
    s.prevMs += (elapsed / s.intervalMs) * s.intervalMs;

    if (!(s.flags & DTG_TIMER_ENABLED))
      continue;

    DtgTimerCallback cb = s.cb;
    const bool oneshot = (s.flags & DTG_TIMER_ONESHOT) != 0;
    cb();
    if (oneshot)
      clearSlot(i);
  }
}
