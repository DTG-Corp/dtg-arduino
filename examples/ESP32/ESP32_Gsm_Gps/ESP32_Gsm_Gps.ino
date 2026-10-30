/**
 * @file ESP32_Gsm_Gps.ino
 * @brief Cellular data + GPS uplink (production-style stack).
 *
 * Combines PPP attach, TLS MQTT to the Hub, and periodic gps_data publishes.
 * Adjust pins for your PCB; keep GPS UART off the modem UART bus.
 */
#include <dtg.h>

// 1) Fill your Hub project + gateway credentials.
const char PROJECT_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char MQTT_PASS[]  = "YOUR_GATEWAY_MQTT_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR-GATEWAY-UUID";

// 2) Cellular modem settings.
const char CELLULAR_APN[] = "v-internet";
const int MODEM_TX_PIN = 17;
const int MODEM_RX_PIN = 16;
const int MODEM_RST_PIN = 15;

// 3) GPS module pins.
const int GPS_RX_PIN = 26;
const int GPS_TX_PIN = 27;
const int LOG_BUTTON_PIN = 19;

DtgCellular dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);
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

  dtg.setUseGoogleDns(false);
  dtg.setLogStream(&Serial);

  // dtg.beginCellular(APN, TX_PIN, RX_PIN, RST_PIN) - start modem + MQTT.
  dtg.beginCellular(CELLULAR_APN, MODEM_TX_PIN, MODEM_RX_PIN, MODEM_RST_PIN);

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
