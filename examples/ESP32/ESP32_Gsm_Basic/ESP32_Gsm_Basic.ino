/**
 * @file ESP32_Gsm_Basic.ino
 * @brief Cellular (SIMcom A7680C-class) PPP + MQTT telemetry.
 *
 * Requires ESP32 Arduino core with PPP.
 * Typical Viettel APN: v-internet (confirm with your carrier).
 * Modem wiring: ESP32 TX->modem RX, RX<-modem TX, RST per your board.
 *
 * Periodic publish: uncomment DTG_EVERY(DTG_PERIOD_UPLINK) (follows Hub period;
 * library default, no sketch PERIOD_UPLINK_MS).
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

DtgCellular dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

// File scope - follows Hub periodUplink. Keep callbacks short:
// DTG_EVERY(DTG_PERIOD_UPLINK) {
//   if (dtg.connected()) {
//     // dtg.publish("datastream_name", value)
//     dtg.publish("cell_uptime", millis() / 1000UL);
//   }
// }

void setup()
{
  Serial.begin(115200);
  delay(300);

  dtg.setUseGoogleDns(false);
  dtg.setLogStream(&Serial);

  // dtg.beginCellular(APN, TX_PIN, RX_PIN, RST_PIN) - start modem + MQTT.
  dtg.beginCellular(CELLULAR_APN, MODEM_TX_PIN, MODEM_RX_PIN, MODEM_RST_PIN);
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  dtg.run();
}
