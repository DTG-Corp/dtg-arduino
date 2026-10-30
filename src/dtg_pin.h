/**
 * @file dtg_pin.h
 * @brief GPIO / alias validation for Hub-pushed pins (no-code + Modbus).
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(ESP32)
#include "driver/gpio.h"
#endif

/** true if pin can be used as GPIO (and as output when asOutput). Flash pins denied. */
inline bool dtgPinUsable(int pin, bool asOutput)
{
  if (pin < 0)
    return false;
#if defined(ESP32)
#if CONFIG_IDF_TARGET_ESP32
  if (pin >= 6 && pin <= 11)
    return false;
#endif
  if (asOutput)
    return GPIO_IS_VALID_OUTPUT_GPIO(static_cast<gpio_num_t>(pin)) != 0;
  return GPIO_IS_VALID_GPIO(static_cast<gpio_num_t>(pin)) != 0;
#else
  (void)asOutput;
  return pin <= 48;
#endif
}

/** Hub aliases: 1..(maxLen-1) chars of [A-Za-z0-9_-]. Rejects / + # space. */
inline bool dtgAliasValid(const char *alias, size_t maxLen)
{
  if (!alias || !alias[0] || maxLen < 2)
    return false;
  size_t n = 0;
  for (const char *p = alias; *p; ++p, ++n)
  {
    if (n + 1 >= maxLen)
      return false;
    const char c = *p;
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok)
      return false;
  }
  return n > 0;
}
