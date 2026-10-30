/**
 * @file dtg_heap_guard_logic.h
 * @brief Host-testable heap guard streak logic (no Arduino).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint8_t lowStreak;
} DtgHeapGuardLogicState;

static inline bool dtgHeapGuardIsLow(uint32_t freeHeap, uint32_t largestBlk,
                                     uint32_t minFreeBytes, uint32_t minBlockBytes)
{
  return freeHeap < minFreeBytes || largestBlk < minBlockBytes;
}

/** Returns true when consecutive low checks reached and not deferred. */
static inline bool dtgHeapGuardEvaluate(bool defer, bool low, uint8_t consecutiveChecks,
                                        DtgHeapGuardLogicState *state, bool *shouldRestart)
{
  if (!state || !shouldRestart)
    return false;
  *shouldRestart = false;
  if (defer)
    return false;
  if (!low)
  {
    state->lowStreak = 0;
    return false;
  }
  if (state->lowStreak < 255)
    ++state->lowStreak;
  if (state->lowStreak >= consecutiveChecks)
  {
    *shouldRestart = true;
    state->lowStreak = 0;
    return true;
  }
  return false;
}

#ifdef __cplusplus
}
#endif
