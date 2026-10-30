/**
 * @file dtg_banner.h
 * @brief Boot banner helpers for DTG IoT Hub firmware.
 */
#pragma once

#include <Arduino.h>
#include <Stream.h>

namespace detail
{
void printBootBanner(Stream &stream);
}
