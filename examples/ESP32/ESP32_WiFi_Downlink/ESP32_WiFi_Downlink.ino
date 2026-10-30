/**
 * @file ESP32_WiFi_Downlink.ino
 * @brief Receive a value from DTG IoT Hub (prefer DTG_LISTEN - see ESP32_WiFi_Listen).
 *
 * Legacy path used DTG_ON_VALUE + onCommand(dtgValueRouter).
 * New sketches: DTG_LISTEN + enableHubCmd().
 */
#include <dtg.h>

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
  const bool on = param.getBool();
  (void)on;
  // digitalWrite(LED_PIN, on ? HIGH : LOW);
  // dtg.publish("led", on ? 1 : 0, true);
}

DTG_CONNECTED()
{
  dtgSerialLogConnected(dtg, CLIENT_ID);
}

void setup()
{
  Serial.begin(115200);
  delay(300);
  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS);
  dtg.enableHubCmd();
}

void loop()
{
  dtg.run();
}
