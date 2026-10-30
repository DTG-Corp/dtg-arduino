/**
 * @file dtg_heap_guard.h
 * @brief Light recovery heap guard - ESP.restart only, with defer/cooldown gates.
 */
#pragma once

#include <Arduino.h>

#include "dtg_config.h"

#ifndef DTG_HEAP_GUARD_BOOT_GRACE_MS
#define DTG_HEAP_GUARD_BOOT_GRACE_MS (5UL * 60UL * 1000UL)
#endif
#ifndef DTG_HEAP_GUARD_COOLDOWN_MS
#define DTG_HEAP_GUARD_COOLDOWN_MS (6UL * 60UL * 60UL * 1000UL)
#endif

typedef bool (*DtgHeapGuardDeferFn)(void);
typedef void (*DtgHeapGuardPrepareRestartFn)(void);

void dtgHeapGuardSetDeferPredicate(DtgHeapGuardDeferFn fn);
void dtgHeapGuardSetPrepareRestartHandler(DtgHeapGuardPrepareRestartFn fn);
void dtgHeapGuardSetOtaInProgress(bool inProgress);

/** True when guard must not count streak or restart (OTA, boot grace, cooldown, predicate). */
bool dtgHeapGuardShouldDefer(unsigned long nowMs);

/** Sample heap; may invoke controlled ESP.restart when tripped. Telemetry callers run separately. */
void dtgHeapGuardCheck(unsigned long nowMs);
