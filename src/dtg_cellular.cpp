/**
 * @file dtg_cellular.cpp
 */
#include "dtg_config.h"
#if DTG_FEATURE_CELLULAR

#include "dtg_cellular.h"

#include <esp_task_wdt.h>

#include <cstring>

#include "dtg_debug.h"
#include "dtg_heap_guard.h"
#include "dtg_modem_lifecycle.h"

#include <WiFi.h>

#include <stdio.h>

DtgCellular *DtgCellular::eventBinding_ = nullptr;

static bool s_cellularPppRecovering = false;

static bool cellularHeapGuardDefer()
{
  return s_cellularPppRecovering;
}

namespace
{

bool dtgCellularGetIp(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 8 || !PPP.connected())
    return false;
  const IPAddress ip = PPP.localIP();
  const int n = snprintf(buf, bufLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgCellularGetMac(char *buf, size_t bufLen, void *userData)
{
  (void)userData;
  if (!buf || bufLen < 18)
    return false;
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  const int n = snprintf(buf, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                         mac[1], mac[2], mac[3], mac[4], mac[5]);
  return n > 0 && (size_t)n < bufLen;
}

bool dtgCellularGetRssi(int *rssiDbm, void *userData)
{
  (void)userData;
  if (!rssiDbm || !PPP.connected())
    return false;
  const int rssi = PPP.RSSI();
  if (rssi >= 0)
    return false;
  *rssiDbm = rssi;
  return true;
}

} // namespace

void DtgCellular::installCellularRuntimeProvider()
{
  cellularRuntimeProvider_.getIp = dtgCellularGetIp;
  cellularRuntimeProvider_.getMac = dtgCellularGetMac;
  cellularRuntimeProvider_.getRssi = dtgCellularGetRssi;
  cellularRuntimeProvider_.networkType = "cellular";
  cellularRuntimeProvider_.userData = nullptr;
  setRuntimeInfoProvider(&cellularRuntimeProvider_);
}

namespace
{
void copyApn(char *dst, size_t len, const char *apn)
{
  if (!dst || len == 0)
    return;
  if (!apn)
  {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, apn, len - 1);
  dst[len - 1] = '\0';
}
} // namespace

DtgCellular::DtgCellular(const char *projectId, const char *gwAlias,
                                 const char *mqttUser, const char *mqttPass,
                                 const char *clientId)
    : DtgClient(projectId, gwAlias, mqttUser, mqttPass, clientId)
{
}

void DtgCellular::onPppEvent(arduino_event_id_t event, arduino_event_info_t /*info*/)
{
  switch (event)
  {
  case ARDUINO_EVENT_PPP_START:
    DTG_LOG("[PPP] Started");
    break;
  case ARDUINO_EVENT_PPP_CONNECTED:
    DTG_LOG("[PPP] Connected");
    break;
  case ARDUINO_EVENT_PPP_GOT_IP:
    DTG_LOG("[PPP] Got IP");
    if (eventBinding_ && eventBinding_->useGoogleDns_)
    {
      IPAddress dnsPrimary(DTG_DNS_PRIMARY_A, DTG_DNS_PRIMARY_B, DTG_DNS_PRIMARY_C,
                           DTG_DNS_PRIMARY_D);
      IPAddress dnsBackup(DTG_DNS_SECONDARY_A, DTG_DNS_SECONDARY_B, DTG_DNS_SECONDARY_C,
                          DTG_DNS_SECONDARY_D);
      const bool okP = PPP.dnsIP(0, dnsPrimary);
      const bool okS = PPP.dnsIP(1, dnsBackup);
      DTG_LOG("[PPP] Google DNS pri=%s sec=%s", okP ? "OK" : "FAIL", okS ? "OK" : "FAIL");
    }
    break;
  case ARDUINO_EVENT_PPP_LOST_IP:
    DTG_LOG("[PPP] Lost IP");
    break;
  case ARDUINO_EVENT_PPP_DISCONNECTED:
    DTG_LOG("[PPP] Disconnected");
    break;
  case ARDUINO_EVENT_PPP_STOP:
    DTG_LOG("[PPP] Stopped");
    break;
  default:
    break;
  }
}

bool DtgCellular::initA7680Network()
{
  DTG_LOG("[MODEM] A7680C network init");
  PPP.cmd("ATE0", 3000);

  String r = PPP.cmd("AT+CFUN?", 5000);
  DTG_LOG("[MODEM] CFUN? -> %s", r.c_str());

  if (r.indexOf("+CFUN: 1") < 0)
  {
    DTG_LOG("[MODEM] CFUN=1");
    r = PPP.cmd("AT+CFUN=1", 15000);
    DTG_LOG("[MODEM] %s", r.c_str());
    delay(3000);
  }

  bool simReady = false;
  for (int i = 0; i < 30; i++)
  {
    esp_task_wdt_reset();
    r = PPP.cmd("AT+CPIN?", 5000);
    if (r.indexOf("READY") >= 0)
    {
      simReady = true;
      DTG_LOG("[MODEM] SIM ready %ds", i);
      break;
    }
    delay(1000);
  }
  if (!simReady)
  {
    DTG_LOG("[MODEM] SIM not ready");
    return false;
  }

  bool hasSignal = false;
  for (int i = 0; i < 30; i++)
  {
    esp_task_wdt_reset();
    r = PPP.cmd("AT+CSQ", 5000);
    if (r.length() > 0 && r.indexOf("99,") < 0)
    {
      hasSignal = true;
      break;
    }
    delay(1000);
  }
  if (!hasSignal)
  {
    DTG_LOG("[MODEM] weak CSQ - continuing");
  }

  r = PPP.cmd("AT+CREG?", 5000);
  const bool alreadyRegistered = (r.indexOf(",1") >= 0 || r.indexOf(",5") >= 0);

  char cgdcont[96];
  snprintf(cgdcont, sizeof(cgdcont), "AT+CGDCONT=1,\"IP\",\"%s\"", apn_);
  r = PPP.cmd(cgdcont, 5000);
  DTG_LOG("[MODEM] CGDCONT -> %s", r.c_str());

  if (!alreadyRegistered)
  {
    r = PPP.cmd("AT+COPS=0", 60000);
    DTG_LOG("[MODEM] COPS=0 -> %s", r.c_str());
  }

  r = PPP.cmd("AT+CGATT=1", 30000);
  DTG_LOG("[MODEM] CGATT=1 -> %s", r.c_str());

  for (int i = 0; i < 120; i++)
  {
    esp_task_wdt_reset();
    if (PPP.attached())
    {
      DTG_LOG("[MODEM] Attached %d ms", i * 500);
      return true;
    }
    delay(500);
    if (i > 0 && i % 20 == 0)
    {
      PPP.cmd("AT+CSQ", 3000);
      PPP.cmd("AT+CGATT=1", 10000);
    }
  }
  DTG_LOG("[MODEM] Attach timeout");
  return false;
}

DtgModemPinConfig DtgCellular::modemPinConfig() const
{
  DtgModemPinConfig cfg{};
  cfg.rstPin = pinRst_;
  cfg.rstActiveLow = rstActiveLow_;
  cfg.rstHoldMs = static_cast<uint16_t>(DTG_MODEM_RST_HOLD_MS);
  cfg.bootWaitMs = static_cast<uint16_t>(DTG_MODEM_BOOT_WAIT_MS);
  return cfg;
}

void DtgCellular::prepareForEspRestart()
{
  dtgModemPrepareForEspRestart(PPP, &mqtt, modemPinConfig());
}

void DtgCellular::onPrepareForRestart()
{
  if (eventBinding_)
  {
    eventBinding_->prepareForEspRestart();
  }
}

void DtgCellular::recoverPppWithHwReset()
{
  struct PppRecoveringScope
  {
    PppRecoveringScope() { s_cellularPppRecovering = true; }
    ~PppRecoveringScope() { s_cellularPppRecovering = false; }
  } recovering;
  const DtgModemPinConfig cfg = modemPinConfig();
  dtgModemHardwareReset(cfg);
  esp_task_wdt_reset();
  PPP.begin(pppModemModel_);
  dtgModemApplyUartInvert(uartLine_);
  esp_task_wdt_reset();
  if (!dtgModemWaitAtReady(PPP, 15, 1000))
  {
    DTG_LOG("[PPP] modem not responding after HW reset");
    return;
  }
  if (!initA7680Network())
  {
    DTG_LOG("[PPP] network init failed after HW reset");
    return;
  }
  PPP.mode(ESP_MODEM_MODE_CMUX);
  if (PPP.waitStatusBits(ESP_NETIF_CONNECTED_BIT, DTG_PPP_CONNECTED_WAIT_MS))
  {
    DTG_LOG("[PPP] recovered after HW reset");
    pppRecoveryAttempts_ = 0;
    installCellularRuntimeProvider();
  }
  else
  {
    DTG_LOG("[PPP] CMUX/IP failed after HW reset");
  }
}

void DtgCellular::recoverPpp()
{
  struct PppRecoveringScope
  {
    PppRecoveringScope() { s_cellularPppRecovering = true; }
    ~PppRecoveringScope() { s_cellularPppRecovering = false; }
  } recovering;
  pppRecoveryAttempts_++;
  DTG_LOG("[PPP] recovery #%d", pppRecoveryAttempts_);

  if (pppRecoveryAttempts_ >= static_cast<int>(DTG_PPP_HARD_RESET_AFTER))
  {
    DTG_LOG("[PPP] max recovery - prepare and restart ESP");
    prepareForEspRestart();
    delay(500);
    ESP.restart();
    return;
  }

  if (pppRecoveryAttempts_ >= 2)
  {
    DTG_LOG("[PPP] stage 2 HW reset + re-begin");
    recoverPppWithHwReset();
    return;
  }

  PPP.mode(ESP_MODEM_MODE_COMMAND);
  delay(2000);
  esp_task_wdt_reset();
  PPP.cmd("ATE0", 3000);
  // Not PPP.cmd("AT"): it strips the final OK, so a healthy echo-off modem returns "".
  if (!PPP.sync())
  {
    DTG_LOG("[PPP] modem silent - escalating to HW reset");
    recoverPppWithHwReset();
    return;
  }

  if (!initA7680Network())
  {
    return;
  }

  PPP.mode(ESP_MODEM_MODE_CMUX);
  if (PPP.waitStatusBits(ESP_NETIF_CONNECTED_BIT, DTG_PPP_CONNECTED_WAIT_MS))
  {
    DTG_LOG("[PPP] recovered");
    pppRecoveryAttempts_ = 0;
    installCellularRuntimeProvider();
  }
}

void DtgCellular::enterDataModeOrRetry()
{
  PPP.mode(ESP_MODEM_MODE_CMUX);
  if (!PPP.waitStatusBits(ESP_NETIF_CONNECTED_BIT, DTG_PPP_CONNECTED_WAIT_MS))
  {
    DTG_LOG("[PPP] CMUX / IP timeout");
  }
}

void DtgCellular::beginCellular(const char *apn, int pinTx, int pinRx, int pinRst,
                                    bool rstActiveLow, bool useTls, bool tlsInsecure,
                                    ppp_modem_model_t pppModemModel, const char *simPin,
                                    bool rxPullUp, bool uartInvert)
{
  applyWatchdogDefaultIfUnset();
  eventBinding_ = this;
  copyApn(apn_, sizeof(apn_), apn);
  useTlsTransport_ = useTls;
  setUseTls(useTls);
  setTlsInsecure(tlsInsecure);
  printBootBannerOnce();

  // Cellular gateway: kill Wi-Fi radio entirely (heat + ~20-40mA idle).
  WiFi.mode(WIFI_OFF);

  const uint16_t port =
      useTls ? static_cast<uint16_t>(DTG_MQTT_PORT_TLS)
             : static_cast<uint16_t>(DTG_MQTT_PORT_PLAIN);
  setMqttEndpoint(DTG_MQTT_HOST, port);

  if (useTlsTransport_)
  {
    attachNetworkClient(tlsClient_);
  }
  else
  {
    attachNetworkClient(plainClient_);
  }

  Network.onEvent(onPppEvent);

  pinRst_ = pinRst;
  rstActiveLow_ = rstActiveLow;
  pppModemModel_ = pppModemModel;
  uartLine_.rxPin = pinRx;
  uartLine_.rxPullUp = rxPullUp;
  uartLine_.uartInvert = uartInvert;
  uartLine_.uartNum = 1;
  dtgSetModemPrepareForRestartHandler(&DtgCellular::onPrepareForRestart);
  dtgHeapGuardSetDeferPredicate(cellularHeapGuardDefer);
  dtgHeapGuardSetPrepareRestartHandler(&DtgCellular::onPrepareForRestart);

  dtgModemLogResetReason();
  const bool softBoot = dtgModemIsSoftResetBoot();
  const DtgModemPinConfig modemCfg = modemPinConfig();

  PPP.setApn(apn_);
  PPP.setPin(simPin);
  PPP.setResetPin(pinRst, rstActiveLow, DTG_MODEM_RST_DELAY_MS);
  dtgModemPrepareRxPin(uartLine_);
  PPP.setPins(pinTx, pinRx, -1, -1, ESP_MODEM_FLOW_CONTROL_NONE);

  delay(2000);

  const bool modemAtOk =
      dtgModemBootWithRetry(PPP, pppModemModel, modemCfg,
                            static_cast<uint8_t>(DTG_MODEM_BOOT_AT_RETRIES), softBoot,
                            &uartLine_, modemWarmBoot_);

  if (modemAtOk && initA7680Network())
  {
    enterDataModeOrRetry();
  }
  else if (!modemAtOk)
  {
    DTG_LOG("[MODEM] Skipping network init - modem offline");
  }

  esp_task_wdt_reset();

  if (PPP.connected())
  {
    installCellularRuntimeProvider();
    for (int a = 0; a < 5; ++a)
    {
      esp_task_wdt_reset();
      if (connectMqttSession())
      {
        DTG_LOG("MQTT up (cellular)");
        break;
      }
      delay(2000);
    }
  }

  lastPppRecoveryMs_ = millis();
}

bool DtgCellular::netLayerReady() const
{
  return PPP.connected();
}

bool DtgCellular::run()
{
  feedWatchdog();
  const unsigned long now = millis();

  if (!PPP.connected())
  {
    if (connected())
    {
      disconnectMqtt();
      DTG_LOG("[MQTT] dropped (PPP down)");
    }
    if (now - lastPppRecoveryMs_ >= DTG_PPP_RECOVERY_INTERVAL_MS)
    {
      lastPppRecoveryMs_ = now;
      recoverPpp();
      esp_task_wdt_reset();
    }
    loopIdle();
    return false;
  }

  pppRecoveryAttempts_ = 0;
  return DtgClient::run();
}

void DtgCellular::configureTransportBeforeMqttConnect()
{
  if (!useTlsTransport_)
    return;
  if (tlsInsecure_)
  {
    tlsClient_.setInsecure();
  }
  else
  {
    tlsClient_.setCACert(caCert());
  }
}

#endif // DTG_FEATURE_CELLULAR
