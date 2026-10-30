/**
 * @file dtg_cellular.h
 * @brief SIMcom A7680C-class modem via ESP32 PPP (CMUX) for DTG IoT Hub.
 *
 * Requires ESP32 Arduino core with PPP support. Many A7680C boards work with
 * PPP_MODEM_SIM7600 - pass another model constant from PPP.h if yours differs.
 */
#pragma once

#include <PPP.h>

#include <Network.h>

#if __has_include(<NetworkClientSecure.h>)
#include <NetworkClientSecure.h>
using DTG_CellularTlsClient = NetworkClientSecure;
#else
#include <WiFiClientSecure.h>
using DTG_CellularTlsClient = WiFiClientSecure;
#endif

#include "dtg_client.h"
#include "dtg_modem_lifecycle.h"

class DtgCellular : public DtgClient
{
public:
  DtgCellular(const char *projectId, const char *gwAlias, const char *mqttUser,
                  const char *mqttPass, const char *clientId);

  /**
   * Boot modem, run explicit A7680C attach sequence, enter CMUX data mode, then MQTT.
   * @param apn            Packet data APN (e.g. v-internet).
   * @param pinTx          ESP32 UART TX → modem RX.
   * @param pinRx          ESP32 UART RX ← modem TX.
   * @param pinRst         Modem reset GPIO (active level per rstActiveLow).
   * @param rstActiveLow   true if RST pin is active low (common on A7680C breakouts).
   * @param useTls         true → MQTT TLS 8883; false → plain NetworkClient + 1883.
   * @param tlsInsecure    dev only: skip server certificate verification when useTls.
   * @param pppModemModel  e.g. PPP_MODEM_SIM7600 (ppp_modem_model_t from PPP.h).
   * @param simPin         SIM PIN or nullptr if not required.
   * @param rxPullUp       true → pinMode(rx, INPUT_PULLUP) before UART (custom PCB).
   * @param uartInvert     true → invert RX/TX polarity (matches SimSerial.begin invert).
   */
  void beginCellular(const char *apn, int pinTx, int pinRx, int pinRst,
                     bool rstActiveLow = true, bool useTls = true, bool tlsInsecure = false,
                     ppp_modem_model_t pppModemModel = PPP_MODEM_SIM7600,
                     const char *simPin = nullptr, bool rxPullUp = false,
                     bool uartInvert = false);

  /** When true, PPP stack uses Google DNS after GOT_IP (some carriers block it). */
  void setUseGoogleDns(bool on) { useGoogleDns_ = on; }

  /**
   * Call before beginCellular when the sketch already powered the modem (e.g. PWRKEY pulse).
   * Suppresses the PPP.begin RST pulse on the first boot attempt while keeping RST for recovery.
   */
  void setModemWarmBoot(bool on) { modemWarmBoot_ = on; }

  bool netLayerReady() const override;

  bool run() override;

protected:
  void configureTransportBeforeMqttConnect() override;

private:
  static void onPppEvent(arduino_event_id_t event, arduino_event_info_t info);

  bool initA7680Network();
  void recoverPpp();
  void recoverPppWithHwReset();
  void enterDataModeOrRetry();
  void prepareForEspRestart();
  DtgModemPinConfig modemPinConfig() const;
  static void onPrepareForRestart();

  DTG_CellularTlsClient tlsClient_;
  NetworkClient plainClient_;
  bool useTlsTransport_ = true;
  bool useGoogleDns_ = false;
  bool modemWarmBoot_ = false;

  char apn_[48] = {};
  int pinRst_ = -1;
  bool rstActiveLow_ = true;
  ppp_modem_model_t pppModemModel_ = PPP_MODEM_SIM7600;
  DtgModemUartLineConfig uartLine_{};
  unsigned long lastPppRecoveryMs_ = 0;
  int pppRecoveryAttempts_ = 0;
  DtgRuntimeInfoProvider cellularRuntimeProvider_{};

  void installCellularRuntimeProvider();

  static DtgCellular *eventBinding_;
};
