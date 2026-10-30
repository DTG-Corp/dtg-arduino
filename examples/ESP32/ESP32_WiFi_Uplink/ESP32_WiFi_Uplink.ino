/**
 * @file ESP32_WiFi_Uplink.ino
 * @brief Read a button and publish its state to DTG IoT Hub.
 *
 * Platform side:
 *   Create one datastream named "button" (type: integer, values: 0 or 1).
 *
 * Serial debug:
 *   dtg.setLogStream(&Serial) prints real MQTT topic + JSON on every publish.
 *   Example uplink line:
 *     [dtg_client.cpp:...] [Tele] topic=dtg/12345/gw01/tele/_/button payload={"v":1.000000,"ts":42}
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

// 3) Pin connected to the button (active LOW with internal pull-up).
// GPIO0 is BOOT - do not use it as a user button.
const int BUTTON_PIN = 4;

DtgWiFi dtg(PROJECT_ID, GW_ALIAS, MQTT_USER, MQTT_PASS, CLIENT_ID);

DTG_CONNECTED()
{
  dtgSerialLogConnected(dtg, CLIENT_ID);
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Library MQTT traces: connect, subscribe, uplink (topic + JSON payload).
  dtg.setLogStream(&Serial);

  // dtg.begin("SSID", "PASSWORD") - start Wi-Fi + MQTT (TLS on by default).
  dtg.begin(WIFI_SSID, WIFI_PASS);
}

void loop()
{
  // dtg.run() - call every loop(); library yields (DTG_LOOP_IDLE_MS) to keep the chip cool.
  dtg.run();

  static int lastState = -1;
  const int state = (digitalRead(BUTTON_PIN) == LOW) ? 1 : 0;

  // Publish only on change - saves bandwidth and keeps realtime response.
  if (state != lastState)
  {
    lastState = state;
    if (dtg.connected())
    {
      // dtg.publish("datastream_name", value)
      //   Sends JSON: {"v":<value>,"ts":<unix_seconds>}
      dtg.publish("button", state);
    }
  }
}
