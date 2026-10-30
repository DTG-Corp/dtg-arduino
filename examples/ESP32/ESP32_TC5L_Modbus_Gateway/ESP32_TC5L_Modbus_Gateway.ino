/**
 * @file ESP32_TC5L_Modbus_Gateway.ino
 * @brief TC5-L on UART2 + DTG IoT Hub (Modbus RTU no-code runtime).
 *
 * Workflow:
 *   1. Flash ESP32_TC5L_Modbus_Bare - confirm TX/RX hex and CRC OK on Serial.
 *   2. Hub: MODBUS_RTU channel UART2 @ 9600 8N1, apply device template TC5-L (tpl_tc5l_v1).
 *   3. Flash this sketch - verify tele/modbus/ch0/tc5l/pv and up/modbus/ch0/health.
 *
 * Wiring (UART2): RX=GPIO14, TX=GPIO25, auto-direction RS485-TTL module.
 */
#define DTG_WIFI_GATEWAY_ONLY 1
#define DTG_PROFILE_MODBUS_CAPABLE 1
#define DTG_MODBUS_UART2_RX 14
#define DTG_MODBUS_UART2_TX 25
// #define DTG_MODBUS_TRACE 1

#include <dtg.h>

const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASS[] = "YOUR_WIFI_PASSWORD";

const char PROJECT_ID[] = "12345";
const char GW_ALIAS[] = "gw01";
const char MQTT_USER[] = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char MQTT_PASS[] = "YOUR_20_CHAR_GATEWAY_TOKEN";
const char CLIENT_ID[] = "gw-YOUR-GATEWAY-UUID";

const bool USE_TLS = true;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

void setup() {
  Serial.begin(115200);
  delay(120);

  dtg.setLogStream(&Serial);
  dtg.begin(WIFI_SSID, WIFI_PASS, USE_TLS);

  dtg.enableHubCmd();
  dtg.enablePlatformFeatures();
}

void loop() {
  dtg.run();
}
