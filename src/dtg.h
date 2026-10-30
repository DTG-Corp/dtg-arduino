/**
 * @file dtg.h
 * @brief DTG IoT Hub official Arduino / ESP32 library (single include).
 *
 * Broker hostname, TLS trust anchor, ports, and topic root are defined in
 * dtg_config.h. Your sketch supplies only Wi-Fi or modem wiring, gateway token,
 * project id, gateway alias, and MQTT client id.
 *
 * Dependencies (Library Manager): PubSubClient, ArduinoJson (Hub no-code via enableNoCodeIo),
 * TinyGPSPlus (GPS examples only). For the startup banner, call setBootStream(&Serial)
 * after Serial.begin. For Serial logs from the library, call setLogStream(&Serial)
 * after Serial.begin (sketch #define DTG_DEBUG does not apply to library .cpp files).
 */
#pragma once

#include "dtg_config.h"
#include "dtg_banner.h"
#include "dtg_debug.h"
#include "dtg_types.h"
#include "dtg_dispatch.h"
#include "dtg_timer.h"
#include "dtg_sketch_log.h"
#include "dtg_client.h"
#include "dtg_hub_cfg.h"
#include "dtg_feature_runtime.h"
#include "dtg_hub_cmd.h"
#if DTG_FEATURE_NOCODE
#include "dtg_nocode.h"
#endif
#include "dtg_ota.h"
#include "dtg_wifi.h"
#include "dtg_modem_lifecycle.h"
#if DTG_FEATURE_GPS
#include "dtg_gps.h"
#endif
#if DTG_FEATURE_CELLULAR
#include "dtg_cellular.h"
#endif
