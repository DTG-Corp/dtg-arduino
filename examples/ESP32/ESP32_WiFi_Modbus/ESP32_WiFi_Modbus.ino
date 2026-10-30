/**
 * @file ESP32_WiFi_Modbus.ino
 * @brief Wi-Fi gateway with no-code I/O + Modbus RTU runtime.
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

// Optional: override UART GPIO before #include <dtg.h>, or set Hub channel rxPin/txPin.
// #define DTG_MODBUS_UART2_RX 16
// #define DTG_MODBUS_UART2_TX 17
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

  // Enable no-code I/O + Modbus RTU runtime.
  dtg.enableHubCmd();
  dtg.enablePlatformFeatures();
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  dtg.run();
}
