/**
 * @file dtg_debug.h
 * @brief Serial tracing for dtg (ESP32).
 *
 * Library .cpp files are compiled separately from your sketch, so `#define DTG_DEBUG 1`
 * in the .ino does NOT enable logs inside the library. Use:
 *   Serial.begin(115200);
 *   dtg.setLogStream(&Serial);   // before begin() if you want Wi-Fi/MQTT traces too
 *
 * Optional: `#define DTG_DEBUG 1` before #include in the **sketch only** adds Serial
 * fallback in this TU when dtgLogStream is null (sketch-side macros only).
 *
 * Line format: [basename:line] message
 * Prefer a tag in the message: [Tele], [MQTT], [Down], [CFG], [OTA], [WiFi], ...
 */
#pragma once

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#ifndef DTG_DEBUG
#define DTG_DEBUG 0
#endif

/** Set via DtgClient::setLogStream (defined in dtg_debug.cpp). */
extern Stream *dtgLogStream;

static inline const char *dtgLogBasename(const char *path)
{
  if (!path || !path[0])
    return "?";
  const char *slash = strrchr(path, '/');
  const char *bslash = strrchr(path, '\\');
  const char *base = slash;
  if (bslash && (!base || bslash > base))
    base = bslash;
  return base ? base + 1 : path;
}

/** MQTT/event name → log tag (Tele / Down / CFG / MQTT). */
static inline const char *dtgLogMqttTag(const char *event)
{
  if (!event)
    return "[MQTT]";
  if (strcmp(event, "uplink") == 0)
    return "[Tele]";
  if (strcmp(event, "downlink") == 0 || strcmp(event, "modbus_write") == 0)
    return "[Down]";
  if (strcmp(event, "config") == 0 || strcmp(event, "modbus_config") == 0 ||
      strcmp(event, "gw_caps") == 0)
    return "[CFG]";
  return "[MQTT]";
}

#define DTG_LOG(fmt, ...)                                                                      \
  do                                                                                           \
  {                                                                                            \
    char _dtgLogBuf[192];                                                                      \
    snprintf_P(_dtgLogBuf, sizeof(_dtgLogBuf), PSTR("[%s:%d] " fmt "\n"),                     \
               dtgLogBasename(__FILE__), __LINE__, ##__VA_ARGS__);                             \
    if (dtgLogStream)                                                                          \
      dtgLogStream->print(_dtgLogBuf);                                                         \
    else if (DTG_DEBUG)                                                                        \
      Serial.print(_dtgLogBuf);                                                                \
  } while (0)

#define DTG_LOG_RAW(msg)                                                                       \
  do                                                                                           \
  {                                                                                            \
    if (dtgLogStream)                                                                          \
      dtgLogStream->print(msg);                                                                \
    else if (DTG_DEBUG)                                                                        \
      Serial.print(msg);                                                                       \
  } while (0)
