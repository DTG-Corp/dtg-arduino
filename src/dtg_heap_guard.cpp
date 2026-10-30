/**
 * @file dtg_heap_guard.cpp
 */
#include "dtg_heap_guard.h"

#include "dtg_debug.h"
#include "dtg_heap_guard_logic.h"
#include "dtg_modem_lifecycle.h"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <time.h>

#if defined(ESP32)

namespace
{

static DtgHeapGuardDeferFn s_deferPredicate = nullptr;
static DtgHeapGuardPrepareRestartFn s_prepareHandler = nullptr;
static bool s_otaInProgress = false;
static DtgHeapGuardLogicState s_logicState = {};
static const char *const kNvsNs = "dtg_heap";
static const char *const kCooldownKey = "gr_u32";
static const uint32_t kCooldownNoTimeMagic = 0xFFFFFFFFu;

static void persistGuardRestartCooldown()
{
  Preferences prefs;
  if (!prefs.begin(kNvsNs, false))
    return;
  const time_t now = time(nullptr);
  const uint32_t stamp =
      (now > 100000) ? static_cast<uint32_t>(now) : kCooldownNoTimeMagic;
  prefs.putUInt(kCooldownKey, stamp);
  prefs.end();
}

static bool cooldownActive(unsigned long nowMs)
{
  Preferences prefs;
  if (!prefs.begin(kNvsNs, true))
    return false;
  const uint32_t stamp = prefs.getUInt(kCooldownKey, 0);
  prefs.end();
  if (stamp == 0)
    return false;
  if (stamp == kCooldownNoTimeMagic)
    return nowMs < DTG_HEAP_GUARD_COOLDOWN_MS;
  const time_t now = time(nullptr);
  if (now <= 100000)
    return nowMs < DTG_HEAP_GUARD_COOLDOWN_MS;
  return (static_cast<uint32_t>(now) - stamp) <
         (DTG_HEAP_GUARD_COOLDOWN_MS / 1000UL);
}

} // namespace

void dtgHeapGuardSetDeferPredicate(DtgHeapGuardDeferFn fn) { s_deferPredicate = fn; }

void dtgHeapGuardSetPrepareRestartHandler(DtgHeapGuardPrepareRestartFn fn)
{
  s_prepareHandler = fn;
}

void dtgHeapGuardSetOtaInProgress(bool inProgress) { s_otaInProgress = inProgress; }

bool dtgHeapGuardShouldDefer(unsigned long nowMs)
{
  if (s_otaInProgress)
    return true;
  if (nowMs < DTG_HEAP_GUARD_BOOT_GRACE_MS)
    return true;
  if (cooldownActive(nowMs))
    return true;
  if (s_deferPredicate && s_deferPredicate())
    return true;
  return false;
}

void dtgHeapGuardCheck(unsigned long nowMs)
{
  const bool defer = dtgHeapGuardShouldDefer(nowMs);
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlk = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  const bool low = dtgHeapGuardIsLow(freeHeap, largestBlk, DTG_HEAP_GUARD_MIN_FREE_BYTES,
                                    DTG_HEAP_GUARD_MIN_BLOCK_BYTES);
  bool shouldRestart = false;
  (void)dtgHeapGuardEvaluate(defer, low, DTG_HEAP_GUARD_CONSECUTIVE_CHECKS, &s_logicState,
                             &shouldRestart);
  if (!shouldRestart)
    return;

  DTG_LOG("heap guard: free=%lu largest_blk=%lu - controlled restart",
          static_cast<unsigned long>(freeHeap), static_cast<unsigned long>(largestBlk));
  persistGuardRestartCooldown();
  if (s_prepareHandler)
    s_prepareHandler();
  else
    dtgInvokeModemPrepareForRestart();
  esp_task_wdt_reset();
  delay(500);
  ESP.restart();
}

#else

void dtgHeapGuardSetDeferPredicate(DtgHeapGuardDeferFn fn) { (void)fn; }
void dtgHeapGuardSetPrepareRestartHandler(DtgHeapGuardPrepareRestartFn fn) { (void)fn; }
void dtgHeapGuardSetOtaInProgress(bool inProgress) { (void)inProgress; }
bool dtgHeapGuardShouldDefer(unsigned long) { return true; }
void dtgHeapGuardCheck(unsigned long) {}

#endif
