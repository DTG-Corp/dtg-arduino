/**
 * @file dtg_wifi_provision.h
 * @brief SoftAP dtg-gw-* + local HTTP portal for Wi-Fi primary/backup (ESP32).
 */
#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "dtg_wifi_store.h"

class DtgWiFi;

enum class DtgWifiState : uint8_t
{
  StaPrimary,
  StaBackup,
  OfflineWatch,
  ApSetup,
};

class DtgWifiProvisioner
{
public:
  void attach(DtgWiFi *owner);

  DtgWifiState state() const { return state_; }
  void setState(DtgWifiState s) { state_ = s; }

  void setBootConfigPin(uint8_t pin) { bootPin_ = pin; }

  /** Call each run() - BOOT hold, offline timer, AP timeout. */
  void tick(unsigned long nowMs, bool staLinkUp, bool mqttConnected);

  /** HTTP + AP lifecycle when state==ApSetup. Returns false to skip MQTT. */
  bool handleApSetup(unsigned long nowMs);

  void requestApSetup();
  bool apSetupActive() const { return state_ == DtgWifiState::ApSetup; }

  void buildApSsid(char *out, size_t outLen) const;

private:
  void startAp();
  void stopAp();
  void setupRoutes();
  void publishApInfo();

  static void handleRoot();
  static void handleSave();
  static void handleNotFound();

  static bool constantTimeEq(const char *a, const char *b);

  DtgWiFi *owner_ = nullptr;
  WebServer server_{80};
  DtgWifiState state_ = DtgWifiState::StaPrimary;

  uint8_t bootPin_ = 0;
  bool bootDebounced_ = false;
  unsigned long bootLowSinceMs_ = 0;
  unsigned long bootHoldStartMs_ = 0;
  unsigned long offlineSinceMs_ = 0;
  unsigned long apEnteredMs_ = 0;
  unsigned long apLastPostMs_ = 0;
  uint8_t postCountWindow_ = 0;
  unsigned long postLockoutUntilMs_ = 0;

  char apSsid_[20] = {};
  char apPsk_[DTG_WIFI_AP_PSK_MAX + 1] = {};
  char csrfToken_[33] = {};
  bool routesRegistered_ = false;
  bool apRunning_ = false;
  unsigned long apLastStartAttemptMs_ = 0;

  static DtgWifiProvisioner *instance_;
};
