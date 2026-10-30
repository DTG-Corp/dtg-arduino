/**
 * @file dtg_gps.cpp
 */
#include "dtg_config.h"
#if DTG_FEATURE_GPS

#include "dtg_gps.h"

#include "dtg_debug.h"

namespace
{
constexpr uint32_t kBaudCandidates[] = {9600, 38400, 115200, 57600, 19200, 4800};
}

uint32_t DtgGps::detectGpsBaud()
{
  for (uint32_t baud : kBaudCandidates)
  {
    DTG_LOG("GPS try baud %lu", static_cast<unsigned long>(baud));
    Serial2.end();
    delay(50);
    Serial2.begin(baud, SERIAL_8N1, rxPin_, txPin_);

    const unsigned long start = millis();
    int trialBytes = 0;
    while (millis() - start < 1200)
    {
      if (Serial2.available() > 0)
      {
        const int c = Serial2.read();
        trialBytes++;
        if (c == '$')
        {
          DTG_LOG("GPS NMEA at %lu", static_cast<unsigned long>(baud));
          return baud;
        }
      }
    }
    (void)trialBytes;
  }

  Serial2.end();
  delay(50);
  Serial2.begin(9600, SERIAL_8N1, rxPin_, txPin_);
  return 9600;
}

void DtgGps::begin(int gpioRx, int gpioTx, int logButtonPin)
{
  if (initialized_)
    return;
  rxPin_ = gpioRx;
  txPin_ = gpioTx;
  logButtonPin_ = logButtonPin;

  if (logButtonPin_ >= 0)
  {
    pinMode(logButtonPin_, INPUT_PULLUP);
  }

  const uint32_t baud = detectGpsBaud();
  DTG_LOG("GPS using baud %lu", static_cast<unsigned long>(baud));
  initialized_ = true;
}

void DtgGps::setDataCallback(DTG_GpsDataCallback cb)
{
  onData_ = cb;
}

void DtgGps::setLogCoordCallback(DTG_GpsLogCoordCallback cb)
{
  onLog_ = cb;
}

void DtgGps::loop()
{
  if (!initialized_)
    return;

  while (Serial2.available() > 0)
    tinyGps_.encode(static_cast<char>(Serial2.read()));

  const unsigned long now = millis();
  if (!tinyGps_.location.isValid())
  {
    hasValidGps_ = false;
    if (now - lastWarnMs_ >= DTG_GPS_WARN_INTERVAL_MS)
    {
      lastWarnMs_ = now;
      DTG_LOG("[GPS] waiting for fix");
    }
    handleLogButton();
    return;
  }

  const float lat = static_cast<float>(tinyGps_.location.lat());
  const float lon = static_cast<float>(tinyGps_.location.lng());
  if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f)
  {
    hasValidGps_ = false;
    DTG_LOG("[GPS] invalid coordinate");
    handleLogButton();
    return;
  }

  hasValidGps_ = true;
  latestLat_ = lat;
  latestLon_ = lon;
  latestSpd_ = tinyGps_.speed.isValid() ? static_cast<float>(tinyGps_.speed.kmph()) : 0.0f;
  lastValidFixMs_ = now;

  if (now - lastPublishMs_ >= DTG_GPS_PUBLISH_INTERVAL_MS)
  {
    lastPublishMs_ = now;
    if (onData_)
      onData_(latestLat_, latestLon_, latestSpd_, now);
  }

  handleLogButton();
}

void DtgGps::handleLogButton()
{
  if (logButtonPin_ < 0)
    return;

  const int st = digitalRead(logButtonPin_);
  const unsigned long now = millis();

  if (st == LOW && lastButtonState_ == HIGH &&
      now - lastButtonMs_ >= DTG_GPS_BUTTON_DEBOUNCE_MS)
  {
    lastButtonMs_ = now;
    if (!hasValidGps_ || now - lastValidFixMs_ > DTG_GPS_FIX_STALE_MS)
      DTG_LOG("[GPS] button: no fresh fix");
    else if (onLog_)
      onLog_(latestLat_, latestLon_);
  }
  lastButtonState_ = st;
}

#endif // DTG_FEATURE_GPS
