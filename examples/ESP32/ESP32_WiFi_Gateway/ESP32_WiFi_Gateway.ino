/**
 * @file ESP32_WiFi_Gateway.ino
 * @brief Hub gateway sketch - no-code I/O + Hub FOTA inside dtg library.
 *
 * MQTT user/pass = gateway token; clientId = "gw-" + gateway UUID from Hub UI.
 * Wi-Fi: hold BOOT 20s or offline 10 min to enter setup AP (dtg-gw-xxxxxx).
 * Uplink cadence: library default (DTG_PERIOD_UPLINK_DEFAULT_MS); Hub overrides via
 * down/cfg - drives no-code I/O and optional DTG_EVERY(DTG_PERIOD_UPLINK).
 * DTG_EVERY(1000) is fixed forever. Downlink applies immediately.
 */
#define DTG_WIFI_GATEWAY_ONLY 1
#include <dtg.h>

// 1) Fill your Wi-Fi credentials.
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

// 2) MQTT broker defaults to mqtt.dtgsoft.vn:8883 (dtg_config.h).
//    Uncomment setMqttBroker() in setup() only for a private broker.

// 3) Fill your Hub project + gateway credentials.
const char PROJECT_ID[] = "12345";
const char GW_ALIAS[]   = "gw01";
const char MQTT_USER[]  = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char MQTT_PASS[]  = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char CLIENT_ID[]  = "gw-YOUR_GATEWAY_UUID";

const bool USE_TLS = true;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

void setup()
{
  Serial.begin(115200);
  delay(120);

  dtg.setLogStream(&Serial);
  // dtg.setMqttBroker("mqtt.example.com", 8883);

  // dtg.begin("SSID", "PASSWORD", useTLS) - start Wi-Fi + MQTT.
  dtg.begin(WIFI_SSID, WIFI_PASS, USE_TLS);

  // Enable no-code I/O: platform configures streams without recompiling.
  dtg.enableHubCmd();
  dtg.enablePlatformFeatures();
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  dtg.run();
}
