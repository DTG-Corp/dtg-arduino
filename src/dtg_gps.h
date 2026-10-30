/**
 * @file dtg_gps.h
 * @brief BN-880 / u-blox NMEA UART reader using TinyGPS++.
 *
 * Uses ESP32 HardwareSerial Serial2: connect module TX -> gpioRx, module RX -> gpioTx.
 * Optional GPIO for a momentary button to trigger log callback (same contract as legacy
 * gateway firmware: only fires when fix is fresh).
 */
#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

#ifndef DTG_GPS_PUBLISH_INTERVAL_MS
#define DTG_GPS_PUBLISH_INTERVAL_MS 2000UL
#endif
#ifndef DTG_GPS_WARN_INTERVAL_MS
#define DTG_GPS_WARN_INTERVAL_MS 5000UL
#endif
#ifndef DTG_GPS_FIX_STALE_MS
#define DTG_GPS_FIX_STALE_MS 5000UL
#endif
#ifndef DTG_GPS_BUTTON_DEBOUNCE_MS
#define DTG_GPS_BUTTON_DEBOUNCE_MS 250UL
#endif

typedef void (*DTG_GpsDataCallback)(float lat, float lon, float spdKmh,
                                       unsigned long timestampMs);
typedef void (*DTG_GpsLogCoordCallback)(float lat, float lon);

class DtgGps
{
public:
  DtgGps() = default;

  /**
   * @param gpioRx  ESP32 pin connected to GPS TX.
   * @param gpioTx  ESP32 pin connected to GPS RX (optional for RX-only use).
   * @param logButtonPin  INPUT_PULLUP button to log coords, or -1 to disable.
   */
  void begin(int gpioRx, int gpioTx, int logButtonPin = -1);

  void loop();

  void setDataCallback(DTG_GpsDataCallback cb);
  void setLogCoordCallback(DTG_GpsLogCoordCallback cb);

  bool hasFix() const { return hasValidGps_; }
  float latitude() const { return latestLat_; }
  float longitude() const { return latestLon_; }
  float speedKmh() const { return latestSpd_; }

private:
  uint32_t detectGpsBaud();
  void handleLogButton();

  TinyGPSPlus tinyGps_;

  DTG_GpsDataCallback onData_ = nullptr;
  DTG_GpsLogCoordCallback onLog_ = nullptr;

  bool initialized_ = false;
  bool hasValidGps_ = false;
  float latestLat_ = 0.0f;
  float latestLon_ = 0.0f;
  float latestSpd_ = 0.0f;

  unsigned long lastPublishMs_ = 0;
  unsigned long lastWarnMs_ = 0;
  unsigned long lastValidFixMs_ = 0;
  unsigned long lastButtonMs_ = 0;
  int lastButtonState_ = HIGH;

  int rxPin_ = -1;
  int txPin_ = -1;
  int logButtonPin_ = -1;
};
