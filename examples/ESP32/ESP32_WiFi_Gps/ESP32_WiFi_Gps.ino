/**
 * @file ESP32_WiFi_Gps.ino
 * @brief GPS over UART + Wi-Fi uplink to tele/_/gps_data.
 *
 * Default pins: RX=26, TX=27 (adjust to your wiring).
 * Optional log button on GPIO19 publishes tele/_/log_toado when pressed.
 */
#include <dtg.h>

// 1) Fill your Wi-Fi credentials.
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

// 2) Fill your Hub project + gateway credentials.
const char PROJECT_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char MQTT_PASS[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR-GATEWAY-UUID";

const int GPS_RX_PIN = 26;
const int GPS_TX_PIN = 27;
const int LOG_BUTTON_PIN = 19;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);
DtgGps gps;

void onGpsSample(float lat, float lon, float spdKmh, unsigned long tsMs)
{
  if (dtg.connected())
    dtg.publishGps(lat, lon, spdKmh, tsMs);
}

void onLogCoords(float lat, float lon)
{
  if (dtg.connected())
    dtg.publishLogCoord(lat, lon);
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  dtg.setLogStream(&Serial);

  // dtg.begin("SSID", "PASSWORD") - start Wi-Fi + MQTT (TLS on by default).
  dtg.begin(WIFI_SSID, WIFI_PASS);

  gps.setDataCallback(onGpsSample);
  gps.setLogCoordCallback(onLogCoords);
  gps.begin(GPS_RX_PIN, GPS_TX_PIN, LOG_BUTTON_PIN);
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  dtg.run();
  gps.loop();
}
