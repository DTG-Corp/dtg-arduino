/**
 * @file dtg_timer.h
 * @brief Cooperative interval/timeout scheduler for DTG sketches (zero heap).
 *
 * Preferred sketch API (file scope, like DTG_LISTEN):
 *   // Follow Hub gateway periodUplink (library default + down/cfg):
 *   DTG_EVERY(DTG_PERIOD_UPLINK) {
 *     if (dtg.connected()) dtg.publish("temp", readTemp());
 *   }
 *   // Fixed ms (intentional - Hub will not change this interval):
 *   DTG_EVERY(1000) { ... }
 *
 * PERIOD_UPLINK lives in the library (DTG_PERIOD_UPLINK_DEFAULT_MS) and Hub
 * down/cfg - do not put a sketch PERIOD_UPLINK_MS const. Use DTG_PERIOD_UPLINK
 * as the DTG_EVERY argument to follow Hub.
 *
 * Runtime API:
 *   DtgTimerHandle t = dtgTimerAddInterval(1000UL, myFn);
 *   t.disable(); t.enable(); t.changeInterval(2000UL);
 *
 * Call dtgTimerSetPeriodUplink(periodUplink()) then dtgTimerTick(millis())
 * from DtgClient::run() every loop. Keep callbacks short - no delay().
 *
 * Override pool size before #include <dtg.h>:
 *   #define DTG_TIMER_MAX 16
 */
#pragma once

#include <stdint.h>

#ifndef DTG_TIMER_MAX
#define DTG_TIMER_MAX 8
#endif

#if DTG_TIMER_MAX < 1
#error DTG_TIMER_MAX must be >= 1
#endif
#if DTG_TIMER_MAX > 32
#error DTG_TIMER_MAX must be <= 32
#endif

/**
 * Pass to DTG_EVERY / dtgTimerAddInterval to follow Hub periodUplink at runtime.
 * Not a real millisecond value - do not use as a delay() argument.
 */
#ifndef DTG_PERIOD_UPLINK
#define DTG_PERIOD_UPLINK 0xFFFFFFFFu
#endif

/** Library default uplink period (ms). Canonical define also in dtg_config.h. */
#ifndef DTG_PERIOD_UPLINK_DEFAULT_MS
#define DTG_PERIOD_UPLINK_DEFAULT_MS 5000U
#endif

typedef void (*DtgTimerCallback)(void);

enum DtgTimerFlag : uint8_t
{
  DTG_TIMER_USED = 0x01,
  DTG_TIMER_ENABLED = 0x02,
  DTG_TIMER_ONESHOT = 0x04,
  /** Slot interval tracks dtgTimerSetPeriodUplink() (Hub periodUplink). */
  DTG_TIMER_FOLLOW_UPLINK = 0x10,
};

struct DtgTimerSlot
{
  unsigned long intervalMs = 0;
  unsigned long prevMs = 0;
  DtgTimerCallback cb = nullptr;
  uint8_t flags = 0;
  uint8_t gen = 0;
};

class DtgTimerHandle
{
public:
  DtgTimerHandle() : id_(-1), gen_(0) {}
  DtgTimerHandle(int8_t id, uint8_t gen) : id_(id), gen_(gen) {}

  bool valid() const;
  int8_t id() const { return id_; }

  bool isEnable() const;
  void enable() const;
  void disable() const;
  bool changeInterval(unsigned long intervalMs) const;
  void restart() const;
  void deleteTimer() const;

private:
  int8_t id_;
  uint8_t gen_;
};

/** Repeating timer. Pass DTG_PERIOD_UPLINK to follow Hub period. */
DtgTimerHandle dtgTimerAddInterval(unsigned long intervalMs, DtgTimerCallback cb);

/** One-shot timer. Returns invalid handle if the static pool is full. */
DtgTimerHandle dtgTimerAddTimeout(unsigned long intervalMs, DtgTimerCallback cb);

/** Free a slot (safe if handle invalid). */
void dtgTimerDelete(DtgTimerHandle handle);

/**
 * Push current Hub periodUplink (ms) for DTG_EVERY(DTG_PERIOD_UPLINK) slots.
 * Call from DtgClient::run() before dtgTimerTick().
 */
void dtgTimerSetPeriodUplink(unsigned long periodUplinkMs);

/**
 * Advance timers. Pass millis() (or a test clock). At most one fire per slot
 * per call; catch-up uses prevMs += intervalMs (no burst storm).
 */
void dtgTimerTick(unsigned long now);

/** How many slots are currently USED (for tests / diagnostics). */
int dtgTimerUsedCount();

/** Current interval of a valid handle (0 if invalid). Test / diagnostics. */
unsigned long dtgTimerIntervalMs(DtgTimerHandle handle);

#ifndef DTG__JOIN_INNER
#define DTG__JOIN_INNER(a, b) a##b
#define DTG__JOIN(a, b) DTG__JOIN_INNER(a, b)
#define DTG__UNIQUE(prefix) DTG__JOIN(prefix, __COUNTER__)
#endif

struct DtgTimerRegistrar
{
  DtgTimerRegistrar(unsigned long intervalMs, DtgTimerCallback cb)
  {
    (void)dtgTimerAddInterval(intervalMs, cb);
  }
};

/**
 * File-scope periodic callback. Do NOT place inside loop()/setup().
 * Body runs from dtgTimerTick (via dtg.run()), even when MQTT is offline.
 * Use DTG_PERIOD_UPLINK to follow Hub; any other ms is fixed forever.
 */
#define DTG_EVERY(intervalMs)                                                                  \
  DTG_EVERY_IMPL(intervalMs, DTG__UNIQUE(dtgEveryFn_), DTG__UNIQUE(dtgEveryReg_))

#define DTG_EVERY_IMPL(intervalMs, fnName, regName)                                            \
  static void fnName();                                                                        \
  static DtgTimerRegistrar regName((unsigned long)(intervalMs), fnName);                       \
  static void fnName()
