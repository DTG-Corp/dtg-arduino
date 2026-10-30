/**
 * @file ESP32_WiFi_Basic.ino
 * @brief Publish telemetry over Wi-Fi to DTG IoT Hub.
 *
 * Serial debug:
 *   dtg.setLogStream(&Serial) prints real MQTT topic + JSON on every publish.
 *   Example uplink line:
 *     [dtg_client.cpp:...] [Tele] topic=dtg/12345/gw01/tele/_/demo_value payload={"v":25.300000,"ts":10}
 *
 * Periodic publish: uncomment DTG_EVERY(DTG_PERIOD_UPLINK) - follows Hub periodUplink
 * (library default DTG_PERIOD_UPLINK_DEFAULT_MS; Hub may change via down/cfg).
 * DTG_EVERY(1000) is fixed forever. Timers tick from dtg.run().
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

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

DTG_CONNECTED()
{
  dtgSerialLogConnected(dtg, CLIENT_ID);
}

// File scope - follows Hub periodUplink. Keep callbacks short, no delay():
// DTG_EVERY(DTG_PERIOD_UPLINK) {
//   if (dtg.connected()) {
//     const float demo = 25.3f;
//     // dtg.publish("datastream_name", value) -> {"v":...,"ts":...}
//     dtg.publish("demo_value", demo);
//   }
// }

void setup()
{
  /* Setup debug console */
  Serial.begin(115200);
  delay(300);

  dtg.setLogStream(&Serial);

  // dtg.begin("SSID", "PASSWORD") - start Wi-Fi + MQTT (TLS on by default).
  dtg.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  // Also ticks DTG_EVERY / addInterval timers.
  dtg.run();
}
