/**
 * @file dtg_modem_lifecycle.h
 * @brief Modem hardware reset and CMUX-safe ESP restart sequencing.
 *
 * HW reset runs only when DtgModemPinConfig.rstPin >= 0 (board-specific wiring).
 * Sketches without a RESET pin should leave rstPin at -1 and use AT+CRESET fallback.
 */
#pragma once

#include <Arduino.h>
#include <PPP.h>

class PubSubClient;

struct DtgModemPinConfig
{
  int rstPin = -1;
  bool rstActiveLow = true;
  uint16_t rstHoldMs = 0;  // 0 = use DTG_MODEM_RST_HOLD_MS
  uint16_t bootWaitMs = 0; // 0 = use DTG_MODEM_BOOT_WAIT_MS
};

/** Optional hook for WiFi-only boards (no-op) or cellular sketches that register prepare logic. */
typedef void (*DtgModemPrepareRestartFn)(void);

/** @param waitBoot false before ESP.restart() - release RST only, skip post-reset wait. */
void dtgModemHardwareReset(const DtgModemPinConfig &cfg, bool waitBoot = true);
bool dtgModemWaitAtReady(PPPClass &ppp, uint16_t timeoutSec, uint16_t intervalMs);
bool dtgModemPrepareForEspRestart(PPPClass &ppp, PubSubClient *mqtt,
                                  const DtgModemPinConfig &cfg);
/** RX pull-up and UART line invert for boards with open-drain / level-shifter wiring. */
struct DtgModemUartLineConfig
{
  int rxPin = -1;
  bool rxPullUp = false;
  bool uartInvert = false;
  uint8_t uartNum = 1;
};

void dtgModemPrepareRxPin(const DtgModemUartLineConfig &uartLine);
void dtgModemApplyUartInvert(const DtgModemUartLineConfig &uartLine);

bool dtgModemBootWithRetry(PPPClass &ppp, ppp_modem_model_t model,
                           const DtgModemPinConfig &cfg, uint8_t maxAttempts,
                           bool softBoot, const DtgModemUartLineConfig *uartLine = nullptr,
                           bool modemAlreadyPowered = false);

bool dtgModemIsSoftResetBoot();
void dtgModemLogResetReason();

void dtgSetModemPrepareForRestartHandler(DtgModemPrepareRestartFn fn);
void dtgInvokeModemPrepareForRestart();
