/**
 * @file dtg_config.h
 * @brief Production broker endpoints, DNS defaults, and TLS trust anchor for DTG IoT Hub.
 *
 * These values match the deployed EMQX stack (see project deploy docs). Sketches only
 * supply per-gateway credentials (MQTT user/pass, project id, gateway alias, client id).
 *
 * Optional: set DTG_MQTT_BROKER_IPV4 to a dotted IPv4 string before including
 * dtg.h to bypass DNS (plain MQTT only; not recommended with TLS hostname verify).
 */
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Optional modules (set 0 before #include <dtg.h> to shrink flash on Wi-Fi-only gateways)
// ---------------------------------------------------------------------------
#ifndef DTG_FEATURE_CELLULAR
#define DTG_FEATURE_CELLULAR 1
#endif
#ifndef DTG_FEATURE_GPS
#define DTG_FEATURE_GPS 1
#endif
#ifndef DTG_WIFI_GATEWAY_ONLY
#define DTG_WIFI_GATEWAY_ONLY 0
#endif
#if DTG_WIFI_GATEWAY_ONLY
#undef DTG_FEATURE_CELLULAR
#define DTG_FEATURE_CELLULAR 0
#undef DTG_FEATURE_GPS
#define DTG_FEATURE_GPS 0
#endif

// ---------------------------------------------------------------------------
// Firmware profiles (set one to 1 before #include <dtg.h> - Hub export sets these)
// ---------------------------------------------------------------------------
#ifndef DTG_PROFILE_MQTT_ONLY
#define DTG_PROFILE_MQTT_ONLY 0
#endif
#ifndef DTG_PROFILE_MODBUS_ONLY
#define DTG_PROFILE_MODBUS_ONLY 0
#endif
#ifndef DTG_PROFILE_MODBUS_CAPABLE
#define DTG_PROFILE_MODBUS_CAPABLE 0
#endif
#ifndef DTG_PROFILE_NOCODE_CAPABLE
#define DTG_PROFILE_NOCODE_CAPABLE 0
#endif
#ifndef DTG_PROFILE_HYBRID_CAPABLE
#define DTG_PROFILE_HYBRID_CAPABLE 0
#endif
#ifndef DTG_PROFILE_HARDWARE_GATEWAY
#define DTG_PROFILE_HARDWARE_GATEWAY 0
#endif

#ifndef DTG_FEATURE_NOCODE
#define DTG_FEATURE_NOCODE 1
#endif
#ifndef DTG_FEATURE_MODBUS
#define DTG_FEATURE_MODBUS 1
#endif
#ifndef DTG_FEATURE_HUB_CMD
#define DTG_FEATURE_HUB_CMD 1
#endif
#ifndef DTG_FEATURE_OTA
#define DTG_FEATURE_OTA 1
#endif

#if DTG_PROFILE_MQTT_ONLY
#undef DTG_FEATURE_NOCODE
#define DTG_FEATURE_NOCODE 0
#undef DTG_FEATURE_MODBUS
#define DTG_FEATURE_MODBUS 0
#undef DTG_FEATURE_OTA
#define DTG_FEATURE_OTA 0
#undef DTG_FEATURE_HUB_CMD
#define DTG_FEATURE_HUB_CMD 0
#undef DTG_FEATURE_GPS
#define DTG_FEATURE_GPS 0
#elif DTG_PROFILE_MODBUS_ONLY || DTG_PROFILE_MODBUS_CAPABLE
#undef DTG_FEATURE_MODBUS
#define DTG_FEATURE_MODBUS 1
#undef DTG_FEATURE_NOCODE
#define DTG_FEATURE_NOCODE 0
#elif DTG_PROFILE_NOCODE_CAPABLE
#undef DTG_FEATURE_NOCODE
#define DTG_FEATURE_NOCODE 1
#undef DTG_FEATURE_MODBUS
#define DTG_FEATURE_MODBUS 0
#elif DTG_PROFILE_HYBRID_CAPABLE || DTG_PROFILE_HARDWARE_GATEWAY
#undef DTG_FEATURE_MODBUS
#define DTG_FEATURE_MODBUS 1
#undef DTG_FEATURE_NOCODE
#define DTG_FEATURE_NOCODE 1
#endif

#ifndef DTG_PROFILE_NAME
#if DTG_FEATURE_MODBUS && DTG_FEATURE_NOCODE
#define DTG_PROFILE_NAME "HYBRID_CAPABLE"
#elif DTG_FEATURE_MODBUS
#define DTG_PROFILE_NAME "MODBUS_CAPABLE"
#elif DTG_FEATURE_NOCODE
#define DTG_PROFILE_NAME "NOCODE_CAPABLE"
#else
#define DTG_PROFILE_NAME "MQTT_ONLY"
#endif
#endif

// MQTT PubSubClient buffer - grown lazily when modbus/nocode activate.
#ifndef DTG_MQTT_BUFFER_CORE
#define DTG_MQTT_BUFFER_CORE 2048
#endif
#ifndef DTG_MQTT_BUFFER_MODBUS
#define DTG_MQTT_BUFFER_MODBUS 4096
#endif
#ifndef DTG_MQTT_BUFFER_NOCODE
#define DTG_MQTT_BUFFER_NOCODE 6144
#endif
#ifndef DTG_MODBUS_CFG_BUF_MAX
#define DTG_MODBUS_CFG_BUF_MAX 8192
#endif
#ifndef DTG_NOCODE_CFG_BUF_MAX
#define DTG_NOCODE_CFG_BUF_MAX 8192
#endif
#ifndef DTG_MQTT_CRED_MAX
#define DTG_MQTT_CRED_MAX 128
#endif
#ifndef DTG_TELE_BATCH_MS
#define DTG_TELE_BATCH_MS 5000
#endif
#ifndef DTG_TELE_BATCH_MAX
#define DTG_TELE_BATCH_MAX 16
#endif
/** Library default gateway uplink period (ms). Hub overrides via down/cfg. */
#ifndef DTG_PERIOD_UPLINK_DEFAULT_MS
#define DTG_PERIOD_UPLINK_DEFAULT_MS 5000U
#endif

// ---------------------------------------------------------------------------
// Broker hostnames (DNS records - do not hardcode secrets here)
// ---------------------------------------------------------------------------
#ifndef DTG_MQTT_HOST
#define DTG_MQTT_HOST "mqtt.dtgsoft.vn"
#endif

#ifndef DTG_MQTT_PORT_TLS
#define DTG_MQTT_PORT_TLS 8883
#endif

#ifndef DTG_MQTT_PORT_PLAIN
#define DTG_MQTT_PORT_PLAIN 1883
#endif

// ---------------------------------------------------------------------------
// Preferred DNS when overriding Wi-Fi stack DNS (see DtgWiFi)
// Cellular: optional Google DNS via PPP - many carriers work better with carrier DNS;
// use DtgCellular::setUseGoogleDns(true) only after you verified it on your SIM.
// ---------------------------------------------------------------------------
#ifndef DTG_DNS_PRIMARY_A
#define DTG_DNS_PRIMARY_A 8
#endif
#ifndef DTG_DNS_PRIMARY_B
#define DTG_DNS_PRIMARY_B 8
#endif
#ifndef DTG_DNS_PRIMARY_C
#define DTG_DNS_PRIMARY_C 8
#endif
#ifndef DTG_DNS_PRIMARY_D
#define DTG_DNS_PRIMARY_D 8
#endif

#ifndef DTG_DNS_SECONDARY_A
#define DTG_DNS_SECONDARY_A 8
#endif
#ifndef DTG_DNS_SECONDARY_B
#define DTG_DNS_SECONDARY_B 8
#endif
#ifndef DTG_DNS_SECONDARY_C
#define DTG_DNS_SECONDARY_C 4
#endif
#ifndef DTG_DNS_SECONDARY_D
#define DTG_DNS_SECONDARY_D 4
#endif

// ---------------------------------------------------------------------------
// Topic contract v2 root segment
// ---------------------------------------------------------------------------
#ifndef DTG_TOPIC_ROOT
#define DTG_TOPIC_ROOT "dtg"
#endif

// ---------------------------------------------------------------------------
// MQTT keep-alive (seconds) - balances cellular power vs broker disconnects
// ---------------------------------------------------------------------------
#ifndef DTG_MQTT_KEEPALIVE_SEC
#define DTG_MQTT_KEEPALIVE_SEC 60
#endif

// Firmware semver reported on up/info (number1.number2.number3). Override in sketch if needed.
#ifndef DTG_FIRMWARE_VERSION
#define DTG_FIRMWARE_VERSION "1.1.1"
#endif

// tele/snap heartbeat interval (ms) before jitter.
#ifndef DTG_SNAPSHOT_INTERVAL_MS
#define DTG_SNAPSHOT_INTERVAL_MS 60000
#endif
#ifndef DTG_HEAP_GUARD_MIN_FREE_BYTES
#define DTG_HEAP_GUARD_MIN_FREE_BYTES 20000
#endif
#ifndef DTG_HEAP_GUARD_MIN_BLOCK_BYTES
#define DTG_HEAP_GUARD_MIN_BLOCK_BYTES 8000
#endif
#ifndef DTG_HEAP_GUARD_CONSECUTIVE_CHECKS
#define DTG_HEAP_GUARD_CONSECUTIVE_CHECKS 3
#endif

// Heap guard - controlled restart before OOM (override in sketch before #include <dtg.h>).
#ifndef DTG_HEAP_GUARD_BOOT_GRACE_MS
#define DTG_HEAP_GUARD_BOOT_GRACE_MS (5UL * 60UL * 1000UL)
#endif
#ifndef DTG_HEAP_GUARD_COOLDOWN_MS
#define DTG_HEAP_GUARD_COOLDOWN_MS (6UL * 60UL * 60UL * 1000UL)
#endif

// Board profiles - set one to 1 in build_opt.h before #include <dtg.h>
#ifndef DTG_BOARD_ESP32_CLASSIC
#define DTG_BOARD_ESP32_CLASSIC 0
#endif
#ifndef DTG_BOARD_ESP32C3
#define DTG_BOARD_ESP32C3 0
#endif
#ifndef DTG_BOARD_ESP32S3
#define DTG_BOARD_ESP32S3 0
#endif
#ifndef DTG_BOARD_CELLULAR_PPP
#define DTG_BOARD_CELLULAR_PPP 0
#endif

#if DTG_BOARD_ESP32C3
#undef DTG_HEAP_GUARD_MIN_FREE_BYTES
#define DTG_HEAP_GUARD_MIN_FREE_BYTES 12000
#undef DTG_HEAP_GUARD_MIN_BLOCK_BYTES
#define DTG_HEAP_GUARD_MIN_BLOCK_BYTES 4096
#elif DTG_BOARD_ESP32S3
#undef DTG_HEAP_GUARD_MIN_FREE_BYTES
#define DTG_HEAP_GUARD_MIN_FREE_BYTES 25000
#undef DTG_HEAP_GUARD_MIN_BLOCK_BYTES
#define DTG_HEAP_GUARD_MIN_BLOCK_BYTES 10000
#elif DTG_BOARD_CELLULAR_PPP
#undef DTG_HEAP_GUARD_CONSECUTIVE_CHECKS
#define DTG_HEAP_GUARD_CONSECUTIVE_CHECKS 5
#endif

// ---------------------------------------------------------------------------
// Power / thermal - idle yield + optional CPU clock (busy-loop = hot chip)
// ---------------------------------------------------------------------------
/** Idle yield per run() iteration (ms). 0 = disable (old busy-loop behavior). */
#ifndef DTG_LOOP_IDLE_MS
#define DTG_LOOP_IDLE_MS 2
#endif
/** Target CPU MHz after begin(). 0 = leave Arduino-core default (often 240). */
#ifndef DTG_CPU_FREQ_MHZ
#define DTG_CPU_FREQ_MHZ 80
#endif

// ---------------------------------------------------------------------------
// Resilience - task watchdog and MQTT reconnect backoff
// ---------------------------------------------------------------------------
#ifndef DTG_WATCHDOG_TIMEOUT_S
#define DTG_WATCHDOG_TIMEOUT_S 120
#endif
#ifndef DTG_MQTT_RECONNECT_INTERVAL_MS
#define DTG_MQTT_RECONNECT_INTERVAL_MS 3000
#endif
#ifndef DTG_MQTT_BACKOFF_BASE_MS
#define DTG_MQTT_BACKOFF_BASE_MS 1000
#endif
#ifndef DTG_MQTT_BACKOFF_MAX_MS
#define DTG_MQTT_BACKOFF_MAX_MS 60000
#endif
#ifndef DTG_MQTT_BACKOFF_JITTER_PCT
#define DTG_MQTT_BACKOFF_JITTER_PCT 20
#endif

// Wi-Fi NVS provisioning (DtgWiFi)
#ifndef DTG_WIFI_FAILOVER_MS
#define DTG_WIFI_FAILOVER_MS (45 * 1000)
#endif
#ifndef DTG_WIFI_OFFLINE_AP_MS
#define DTG_WIFI_OFFLINE_AP_MS (3 * 60 * 1000)
#endif
#ifndef DTG_WIFI_BOOT_CONFIG_HOLD_MS
#define DTG_WIFI_BOOT_CONFIG_HOLD_MS (20 * 1000)
#endif
#ifndef DTG_WIFI_AP_SETUP_TIMEOUT_MS
#define DTG_WIFI_AP_SETUP_TIMEOUT_MS (20* 60 * 1000)
#endif
#ifndef DTG_WIFI_AP_IP
#define DTG_WIFI_AP_IP "192.168.4.1"
#endif
#ifndef DTG_WIFI_AP_PSK_LEN
#define DTG_WIFI_AP_PSK_LEN 12
#endif
/** Setup SoftAP WPA2 password (dtg-gw-*). Override in sketch before #include <dtg.h>. Empty "" = random per device. */
#ifndef DTG_WIFI_AP_PSK
#define DTG_WIFI_AP_PSK ""
#endif
#ifndef DTG_WIFI_AP_POST_MAX_PER_MIN
#define DTG_WIFI_AP_POST_MAX_PER_MIN 5
#endif
#ifndef DTG_WIFI_AP_LOCKOUT_MS
#define DTG_WIFI_AP_LOCKOUT_MS (5 * 60 * 1000)
#endif
#ifndef DTG_WIFI_BOOT_DEBOUNCE_MS
#define DTG_WIFI_BOOT_DEBOUNCE_MS 50
#endif
/** Optional BOOT-hold Wi-Fi portal pin; 255 = disabled (set via sketch build_opt.h). Default GPIO0. */
#ifndef DTG_WIFI_BOOT_PIN
#define DTG_WIFI_BOOT_PIN 0
#endif
/** 1 = overwrite NVS primary SSID/pass from sketch begin() every boot. */
#ifndef DTG_WIFI_FORCE_SKETCH_CREDS
#define DTG_WIFI_FORCE_SKETCH_CREDS 0
#endif
/** Minimum interval between softAP start attempts when the previous attempt failed. */
#ifndef DTG_WIFI_AP_RETRY_MS
#define DTG_WIFI_AP_RETRY_MS 5000
#endif
#ifndef DTG_WIFI_BEGIN_CONNECT_MS
#define DTG_WIFI_BEGIN_CONNECT_MS 15000
#endif
/** 1 = print setup AP PSK on Serial when entering config AP. Set 0 if using fixed DTG_WIFI_AP_PSK. */
#ifndef DTG_WIFI_LOG_AP_PSK_ON_SERIAL
#define DTG_WIFI_LOG_AP_PSK_ON_SERIAL 0
#endif

// ---------------------------------------------------------------------------
// Cellular (PPP) - generic defaults; override DTG_MODEM_* in sketch Config.h
// when the board wires modem RESET (e.g. A7680C custom board GPIO15).
// ---------------------------------------------------------------------------
#ifndef DTG_PPP_CONNECTED_WAIT_MS
#define DTG_PPP_CONNECTED_WAIT_MS 30000
#endif
#ifndef DTG_PPP_RECOVERY_INTERVAL_MS
#define DTG_PPP_RECOVERY_INTERVAL_MS 30000
#endif
#ifndef DTG_PPP_HARD_RESET_AFTER
#define DTG_PPP_HARD_RESET_AFTER 3
#endif
#ifndef DTG_MODEM_RST_DELAY_MS
#define DTG_MODEM_RST_DELAY_MS 200
#endif
#ifndef DTG_MODEM_RST_HOLD_MS
#define DTG_MODEM_RST_HOLD_MS DTG_MODEM_RST_DELAY_MS
#endif
#ifndef DTG_MODEM_BOOT_WAIT_MS
#define DTG_MODEM_BOOT_WAIT_MS 3000
#endif
#ifndef DTG_MODEM_BOOT_AT_RETRIES
#define DTG_MODEM_BOOT_AT_RETRIES 3
#endif
#ifndef DTG_MODEM_SOFT_RESYNC_DELAY_MS
#define DTG_MODEM_SOFT_RESYNC_DELAY_MS 2500
#endif
#ifndef DTG_MODEM_CRESET_SETTLE_MS
#define DTG_MODEM_CRESET_SETTLE_MS 300
#endif
/** After graceful ESP restart + modem HW reset, wait before first AT (modem boots slower than ESP). */
#ifndef DTG_MODEM_SOFT_BOOT_INITIAL_WAIT_MS
#define DTG_MODEM_SOFT_BOOT_INITIAL_WAIT_MS 12000
#endif

// ---------------------------------------------------------------------------
// Optional dotted IPv4 broker override (nullptr = use DTG_MQTT_HOST)
// Define in sketch before #include <dtg.h> if needed:
//   const char *DTG_MQTT_BROKER_IPV4 = "10.0.0.1";
// ---------------------------------------------------------------------------
#ifndef DTG_MQTT_BROKER_IPV4
#define DTG_MQTT_BROKER_IPV4 nullptr
#endif

// ---------------------------------------------------------------------------
// Modbus RTU default UART GPIO (override in sketch before #include <dtg.h>).
// Defaults follow common ESP32 UART1/UART2 pinouts; Hub channel rxPin/txPin
// or board-specific #defines win when the board wiring differs.
// ---------------------------------------------------------------------------
#ifndef DTG_MODBUS_UART1_RX
#define DTG_MODBUS_UART1_RX 32
#endif
#ifndef DTG_MODBUS_UART1_TX
#define DTG_MODBUS_UART1_TX 33
#endif
#ifndef DTG_MODBUS_UART2_RX
#define DTG_MODBUS_UART2_RX 16
#endif
#ifndef DTG_MODBUS_UART2_TX
#define DTG_MODBUS_UART2_TX 17
#endif

// Auto-direction RS485-TTL (no DE/RE): drain stale RX and gap before each request.
#ifndef DTG_MODBUS_AUTO_DIR_DRAIN_RX
#define DTG_MODBUS_AUTO_DIR_DRAIN_RX 1
#endif
#ifndef DTG_MODBUS_INTER_FRAME_US
#define DTG_MODBUS_INTER_FRAME_US 2000
#endif
// Hex TX/RX Modbus frame trace. Must be set here or via global build flag (-D DTG_MODBUS_TRACE=1).
// A #define in the sketch alone does NOT affect precompiled library .cpp files.
#ifndef DTG_MODBUS_TRACE
#define DTG_MODBUS_TRACE 0
#endif
#ifndef DTG_MODBUS_FAIL_THRESHOLD
#define DTG_MODBUS_FAIL_THRESHOLD 3
#endif
#ifndef DTG_MODBUS_FAIL_BACKOFF_MS
#define DTG_MODBUS_FAIL_BACKOFF_MS 30000
#endif
#ifndef DTG_MODBUS_COMPLETED_MAX
#define DTG_MODBUS_COMPLETED_MAX 32
#endif
#ifndef DTG_MODBUS_WRITE_QUEUE_MAX
#define DTG_MODBUS_WRITE_QUEUE_MAX 8
#endif
#ifndef DTG_MODBUS_PENDING_MAX
#define DTG_MODBUS_PENDING_MAX 16
#endif
#ifndef DTG_MODBUS_INFLIGHT_MS
#define DTG_MODBUS_INFLIGHT_MS 3000
#endif
#ifndef DTG_MODBUS_TOKEN_SLOTS
#define DTG_MODBUS_TOKEN_SLOTS (DTG_MODBUS_PENDING_MAX * 2)
#endif
#ifndef DTG_MODBUS_ALIAS_MAX
#define DTG_MODBUS_ALIAS_MAX 24
#endif
#ifndef DTG_MODBUS_TRANSFORMER_MAX
#define DTG_MODBUS_TRANSFORMER_MAX 16
#endif
#ifndef DTG_MODBUS_ERROR_MAX
#define DTG_MODBUS_ERROR_MAX 32
#endif

// ---------------------------------------------------------------------------
// Let's Encrypt trust store (X1, X2, Root YR, Root YE). See dtg_tls_roots.h.
// ---------------------------------------------------------------------------
#include "dtg_tls_roots.h"

/** 1 = skip HTTPS certificate verify during OTA (lab only). */
#ifndef DTG_OTA_TLS_INSECURE
#define DTG_OTA_TLS_INSECURE 0
#endif
