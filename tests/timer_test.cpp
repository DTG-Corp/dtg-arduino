/**
 * Host self-check for dtg_timer pool / tick math.
 * Build (PowerShell):
 *   g++ -std=c++17 -DDTG_TIMER_MAX=4 -I../src -o timer_test.exe timer_test.cpp ../src/dtg_timer.cpp
 *   .\timer_test.exe
 */
#include <cassert>
#include <cstdio>

#ifndef DTG_TIMER_MAX
#define DTG_TIMER_MAX 4
#endif
#include "../src/dtg_timer.h"

static int g_fires = 0;
static void onFire() { ++g_fires; }

static int g_fires2 = 0;
static void onFire2() { ++g_fires2; }

int main()
{
  assert(dtgTimerUsedCount() == 0);

  DtgTimerHandle a = dtgTimerAddInterval(1000UL, onFire);
  assert(a.valid());
  assert(dtgTimerUsedCount() == 1);

  // Arm at t=0 (must not fire; must not re-arm forever)
  dtgTimerTick(0);
  assert(g_fires == 0);
  dtgTimerTick(0);
  assert(g_fires == 0);

  dtgTimerTick(999);
  assert(g_fires == 0);

  dtgTimerTick(1000);
  assert(g_fires == 1);

  dtgTimerTick(1000);
  assert(g_fires == 1);

  dtgTimerTick(2000);
  assert(g_fires == 2);

  // Large jump: one fire only
  dtgTimerTick(10000);
  assert(g_fires == 3);

  a.disable();
  dtgTimerTick(11000);
  assert(g_fires == 3);
  a.enable();
  dtgTimerTick(12000);
  assert(g_fires == 4);

  a.changeInterval(500UL);
  dtgTimerTick(12500);
  assert(g_fires == 5);

  // Oneshot
  g_fires2 = 0;
  DtgTimerHandle once = dtgTimerAddTimeout(100UL, onFire2);
  assert(once.valid());
  dtgTimerTick(12500);
  dtgTimerTick(12600);
  assert(g_fires2 == 1);
  assert(!once.valid()); // oneshot cleared + gen bumped
  once.enable();         // stale handle must not touch a recycled slot
  dtgTimerTick(12800);
  assert(g_fires2 == 1);
  assert(dtgTimerUsedCount() == 1); // only `a` remains

  // Fill pool (max 4): a + 3 more
  DtgTimerHandle b = dtgTimerAddInterval(50UL, onFire);
  DtgTimerHandle c = dtgTimerAddInterval(50UL, onFire);
  DtgTimerHandle d = dtgTimerAddInterval(50UL, onFire);
  assert(b.valid() && c.valid() && d.valid());
  assert(dtgTimerUsedCount() == 4);
  DtgTimerHandle full = dtgTimerAddInterval(50UL, onFire);
  assert(!full.valid());

  a.deleteTimer();
  b.deleteTimer();
  c.deleteTimer();
  d.deleteTimer();
  assert(dtgTimerUsedCount() == 0);

  // Unsigned wrap: arm near max, then cross wrap boundary
  g_fires = 0;
  DtgTimerHandle w = dtgTimerAddInterval(100UL, onFire);
  assert(w.valid());
  const unsigned long nearMax = static_cast<unsigned long>(0) - 50UL; // ULONG_MAX-49
  dtgTimerTick(nearMax);
  dtgTimerTick(nearMax + 50UL); // still < 100 elapsed
  assert(g_fires == 0);
  dtgTimerTick(nearMax + 100UL);
  assert(g_fires == 1);
  w.deleteTimer();

  // DTG_PERIOD_UPLINK follows dtgTimerSetPeriodUplink (Hub periodUplink).
  g_fires = 0;
  dtgTimerSetPeriodUplink(1000UL);
  DtgTimerHandle up = dtgTimerAddInterval(DTG_PERIOD_UPLINK, onFire);
  assert(up.valid());
  assert(dtgTimerIntervalMs(up) == 1000UL);
  dtgTimerTick(0);
  dtgTimerTick(1000);
  assert(g_fires == 1);
  dtgTimerSetPeriodUplink(500UL);
  assert(dtgTimerIntervalMs(up) == 500UL);
  dtgTimerTick(1500);
  assert(g_fires == 2);
  up.deleteTimer();

  // Literal ms stays fixed when Hub period changes.
  g_fires = 0;
  DtgTimerHandle fixed = dtgTimerAddInterval(2000UL, onFire);
  assert(fixed.valid());
  dtgTimerTick(1500);
  dtgTimerSetPeriodUplink(100UL);
  assert(dtgTimerIntervalMs(fixed) == 2000UL);
  dtgTimerTick(3500);
  assert(g_fires == 1);

  // changeInterval clears follow (hardcode wins).
  DtgTimerHandle up2 = dtgTimerAddInterval(DTG_PERIOD_UPLINK, onFire);
  assert(up2.valid());
  up2.changeInterval(3000UL);
  assert(dtgTimerIntervalMs(up2) == 3000UL);
  dtgTimerSetPeriodUplink(100UL);
  assert(dtgTimerIntervalMs(up2) == 3000UL);

  up2.deleteTimer();
  fixed.deleteTimer();

  std::puts("timer_test ok");
  return 0;
}
