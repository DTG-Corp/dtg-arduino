/**
 * @file ESP32_WiFi_Listen.ino
 * @brief Listen to a Hub datastream and drive a GPIO (ERa-style, DTG stream names).
 *
 * Hub: create downlink datastream "led", send 0/1.
 * Sketch: DTG_LISTEN("led") { ... } then dtg.publish back.
 *
 * Optional periodic uplink: uncomment DTG_EVERY(DTG_PERIOD_UPLINK) (follows Hub).
 */
#define DTG_WIFI_GATEWAY_ONLY 1
#include <dtg.h>

#define LED_PIN 2

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";
const char PROJECT_ID[] = "12345";
const char GW_ALIAS[] = "gw01";
const char MQTT_USER[] = "YOUR_GATEWAY_MQTT_TOKEN";
const char MQTT_PASS[] = "YOUR_GATEWAY_MQTT_TOKEN";
const char CLIENT_ID[] = "gw-YOUR-GATEWAY-UUID";

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

DTG_LISTEN("led")
{
  uint8_t value = static_cast<uint8_t>(param.getInt() ? 1 : 0);
  digitalWrite(LED_PIN, value ? HIGH : LOW);
  dtg.publish("led", digitalRead(LED_PIN), true);
}

// File scope - follows Hub periodUplink. Keep callbacks short:
// DTG_EVERY(DTG_PERIOD_UPLINK) {
//   if (dtg.connected()) {
//     dtg.publish("uptime", millis() / 1000UL);
//   }
// }

void setup()
{
  Serial.begin(115200);
  delay(80);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dtg.setBootStream(&Serial);
  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS);
  dtg.enableHubCmd();
}

void loop()
{
  dtg.run();
}
