/**
 * @file dtg_heap_telemetry.h
 * @brief Pure heap telemetry JSON fragment for tele/snap (host-testable, no Arduino).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Writes `,"heap":N,"heapMin":N,"heapBlk":N` into buf. Returns length or -1 on error. */
static inline int dtgFormatHeapTelemetryFields(char *buf, size_t bufLen,
                                               uint32_t freeHeap, uint32_t minFreeHeap,
                                               uint32_t largestBlock)
{
  if (!buf || bufLen == 0)
    return -1;
  const int n = snprintf(buf, bufLen, ",\"heap\":%lu,\"heapMin\":%lu,\"heapBlk\":%lu",
                         static_cast<unsigned long>(freeHeap),
                         static_cast<unsigned long>(minFreeHeap),
                         static_cast<unsigned long>(largestBlock));
  if (n <= 0 || static_cast<size_t>(n) >= bufLen)
    return -1;
  return n;
}

#ifdef __cplusplus
}
#endif
