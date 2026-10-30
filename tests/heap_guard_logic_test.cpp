#include <cassert>

#include "../src/dtg_heap_guard_logic.h"

int main()
{
  DtgHeapGuardLogicState st = {};
  bool restart = false;

  assert(!dtgHeapGuardIsLow(25000, 10000, 20000, 8000));
  assert(dtgHeapGuardIsLow(15000, 10000, 20000, 8000));
  assert(dtgHeapGuardIsLow(25000, 4000, 20000, 8000));

  for (int i = 0; i < 2; ++i)
  {
    restart = false;
    assert(!dtgHeapGuardEvaluate(false, true, 3, &st, &restart));
    assert(!restart);
  }
  restart = false;
  assert(dtgHeapGuardEvaluate(false, true, 3, &st, &restart));
  assert(restart);
  assert(st.lowStreak == 0);

  st.lowStreak = 2;
  restart = false;
  assert(!dtgHeapGuardEvaluate(true, true, 3, &st, &restart));
  assert(!restart);
  assert(st.lowStreak == 2);

  st.lowStreak = 0;
  restart = false;
  dtgHeapGuardEvaluate(false, false, 3, &st, &restart);
  assert(st.lowStreak == 0);

  return 0;
}
