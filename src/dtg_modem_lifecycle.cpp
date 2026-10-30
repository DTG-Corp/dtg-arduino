/**
 * @file dtg_modem_lifecycle.cpp
 */
#include "dtg_modem_lifecycle.h"

#include "dtg_config.h"
#include "dtg_debug.h"

#include <PubSubClient.h>
#include <driver/uart.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

static DtgModemPrepareRestartFn s_prepareRestartHandler = nullptr;

void dtgSetModemPrepareForRestartHandler(DtgModemPrepareRestartFn fn)
{
  s_prepareRestartHandler = fn;
}

void dtgInvokeModemPrepareForRestart()
{
  if (s_prepareRestartHandler)
  {
    s_prepareRestartHandler();
  }
}

static uint16_t resolveHoldMs(const DtgModemPinConfig &cfg)
{
  return cfg.rstHoldMs ? cfg.rstHoldMs
                       : static_cast<uint16_t>(DTG_MODEM_RST_HOLD_MS);
}

static uint16_t resolveBootWaitMs(const DtgModemPinConfig &cfg)
{
  return cfg.bootWaitMs ? cfg.bootWaitMs
                        : static_cast<uint16_t>(DTG_MODEM_BOOT_WAIT_MS);
}

/** Chunked delay so long RST hold (2.5s) does not starve the task WDT. */
static void delayWithWdt(uint32_t totalMs)
{
  while (totalMs > 0)
  {
    const uint32_t step = (totalMs > 200U) ? 200U : totalMs;
    delay(step);
    esp_task_wdt_reset();
    totalMs -= step;
  }
}

/** PPP.begin() pulses RST when setResetPin() is configured - skip on soft boot. */
void dtgModemPrepareRxPin(const DtgModemUartLineConfig &uartLine)
{
  if (uartLine.rxPullUp && uartLine.rxPin >= 0)
  {
    pinMode(uartLine.rxPin, INPUT_PULLUP);
  }
}

void dtgModemApplyUartInvert(const DtgModemUartLineConfig &uartLine)
{
  if (!uartLine.uartInvert)
  {
    return;
  }

  const esp_err_t err = uart_set_line_inverse(
      static_cast<uart_port_t>(uartLine.uartNum),
      static_cast<uint32_t>(UART_SIGNAL_RXD_INV | UART_SIGNAL_TXD_INV));
  if (err != ESP_OK)
  {
    DTG_LOG("[MODEM] uart_set_line_inverse failed err=%d", static_cast<int>(err));
  }
}

static void beginModem(PPPClass &ppp, ppp_modem_model_t model,
                       const DtgModemPinConfig &cfg, bool suppressHwReset,
                       const DtgModemUartLineConfig *uartLine)
{
  if (suppressHwReset)
  {
    DTG_LOG("[MODEM] begin: suppressing PPP internal RST pulse");
    ppp.setResetPin(-1, cfg.rstActiveLow, 0);
  }

  ppp.begin(model);

  if (uartLine)
  {
    dtgModemApplyUartInvert(*uartLine);
  }

  if (suppressHwReset && cfg.rstPin >= 0)
  {
    const uint32_t holdMs =
        cfg.rstHoldMs ? cfg.rstHoldMs
                      : static_cast<uint32_t>(DTG_MODEM_RST_HOLD_MS);
    ppp.setResetPin(cfg.rstPin, cfg.rstActiveLow, holdMs);
  }
}

void dtgModemHardwareReset(const DtgModemPinConfig &cfg, bool waitBoot)
{
  if (cfg.rstPin < 0)
  {
    return;
  }

  const int activeLevel = cfg.rstActiveLow ? LOW : HIGH;
  const int releaseLevel = cfg.rstActiveLow ? HIGH : LOW;
  const uint16_t holdMs = resolveHoldMs(cfg);
  const uint16_t bootWaitMs = resolveBootWaitMs(cfg);

  DTG_LOG("[MODEM] HW reset pin=%d holdMs=%u waitBoot=%d", cfg.rstPin, (unsigned)holdMs,
          waitBoot ? 1 : 0);

  pinMode(cfg.rstPin, OUTPUT);
  digitalWrite(cfg.rstPin, activeLevel);
  delayWithWdt(holdMs);
  digitalWrite(cfg.rstPin, releaseLevel);
  if (waitBoot)
  {
    delayWithWdt(bootWaitMs);
  }
}

/** After ESP.restart() the DTE is fresh but the modem may still be in CMUX. */
static bool dtgModemResyncCommandMode(PPPClass &ppp, bool afterSoftBoot,
                                      uint16_t extraDelayMs)
{
  DTG_LOG("[MODEM] resync: COMMAND mode");
  ppp.mode(ESP_MODEM_MODE_COMMAND);

  const uint16_t baseDelay = afterSoftBoot
                                 ? static_cast<uint16_t>(DTG_MODEM_SOFT_RESYNC_DELAY_MS)
                                 : 800;
  delay(baseDelay + extraDelayMs);
  esp_task_wdt_reset();

  ppp.cmd("ATE0", 3000);
  esp_task_wdt_reset();

  const String at = ppp.cmd("AT", 5000);
  if (at.indexOf("OK") >= 0 || at.length() > 0)
  {
    DTG_LOG("[MODEM] resync: AT ok");
    return true;
  }

  DTG_LOG("[MODEM] resync: AT silent");
  return false;
}

bool dtgModemWaitAtReady(PPPClass &ppp, uint16_t timeoutSec, uint16_t intervalMs)
{
  if (timeoutSec == 0)
  {
    return false;
  }

  for (uint16_t i = 0; i < timeoutSec; ++i)
  {
    esp_task_wdt_reset();
    const String at = ppp.cmd("AT", 3000);
    if (at.indexOf("OK") >= 0 || at.length() > 0)
    {
      DTG_LOG("[MODEM] Modem responded after %us", (unsigned)i);
      return true;
    }
    delay(intervalMs);
  }
  return false;
}

bool dtgModemPrepareForEspRestart(PPPClass &ppp, PubSubClient *mqtt,
                                  const DtgModemPinConfig &cfg)
{
  if (mqtt && mqtt->connected())
  {
    mqtt->disconnect();
    mqtt->loop();
    delay(50);
    mqtt->loop();
  }

  DTG_LOG("[MODEM] prepare: COMMAND mode");
  ppp.mode(ESP_MODEM_MODE_COMMAND);
  delay(500);
  esp_task_wdt_reset();

  if (cfg.rstPin >= 0)
  {
    DTG_LOG("[MODEM] prepare: HW reset (datasheet 2.5s pulse)");
    dtgModemHardwareReset(cfg, false);
    esp_task_wdt_reset();
    DTG_LOG("[MODEM] prepare: modem HW reset issued");
    return true;
  }

  ppp.cmd("ATE0", 3000);
  esp_task_wdt_reset();

  const String at = ppp.cmd("AT", 5000);
  if (at.length() > 0)
  {
    DTG_LOG("[MODEM] prepare: no RST pin - AT+CRESET fallback");
    ppp.cmd("AT+CRESET", 3000);
    delay(DTG_MODEM_CRESET_SETTLE_MS);
    esp_task_wdt_reset();
    DTG_LOG("[MODEM] prepare: modem reboot issued");
  }
  else
  {
    DTG_LOG("[MODEM] prepare: modem silent and no RST pin");
  }
  return true;
}

bool dtgModemBootWithRetry(PPPClass &ppp, ppp_modem_model_t model,
                           const DtgModemPinConfig &cfg, uint8_t maxAttempts,
                           bool softBoot, const DtgModemUartLineConfig *uartLine,
                           bool modemAlreadyPowered)
{
  if (maxAttempts == 0)
  {
    maxAttempts = static_cast<uint8_t>(DTG_MODEM_BOOT_AT_RETRIES);
  }

  DTG_LOG("[MODEM] Waiting for modem boot...");
  if (softBoot)
  {
    DTG_LOG("[MODEM] soft boot - wait for modem then UART resync");
    DTG_LOG("[MODEM] soft boot initial wait %lums",
            static_cast<unsigned long>(DTG_MODEM_SOFT_BOOT_INITIAL_WAIT_MS));
    delayWithWdt(DTG_MODEM_SOFT_BOOT_INITIAL_WAIT_MS);
  }
  else if (modemAlreadyPowered)
  {
    DTG_LOG("[MODEM] warm boot - PWRKEY already powered modem, skip PPP RST pulse");
  }

  for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt)
  {
    if (attempt > 0)
    {
      DTG_LOG("[MODEM] HW reset before begin attempt %u", (unsigned)(attempt + 1));
      dtgModemHardwareReset(cfg);
    }

    esp_task_wdt_reset();
    const bool suppressPppRstPulse =
        (softBoot || modemAlreadyPowered) && attempt == 0;
    beginModem(ppp, model, cfg, suppressPppRstPulse, uartLine);
    esp_task_wdt_reset();

    const bool extendedAtWait = softBoot || modemAlreadyPowered;
    const uint16_t extraDelay =
        softBoot ? static_cast<uint16_t>(attempt) * 1500U : 0U;
    if (dtgModemResyncCommandMode(ppp, softBoot, extraDelay))
    {
      return true;
    }

    if (dtgModemWaitAtReady(ppp, extendedAtWait ? 25U : 15U, 1000))
    {
      return true;
    }

    DTG_LOG("[MODEM] AT timeout attempt %u", (unsigned)(attempt + 1));
  }

  DTG_LOG("[MODEM] WARNING: Modem not responding after retries");
  return false;
}

bool dtgModemIsSoftResetBoot()
{
  const esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_SW || reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
         reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT;
}

void dtgModemLogResetReason()
{
  const esp_reset_reason_t reason = esp_reset_reason();
  DTG_LOG("[MODEM] esp_reset_reason=%d sdk=%s", static_cast<int>(reason), ESP.getSdkVersion());
}
