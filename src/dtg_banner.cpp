/**
 * @file dtg_banner.cpp
 */
#include "dtg_banner.h"

namespace detail
{
void printBootBanner(Stream &stream)
{
#ifndef DTG_NO_DEFAULT_BANNER
  stream.println();
  stream.println(F(".DTG-IoT-Hub-----------------------------."));
  stream.println(F("|========================================|"));
  stream.println(F("|                                        |"));
  stream.println(F("|         ____  ____________             |"));
  stream.println(F("|        / __ \\/_  __/ ____/             |"));
  stream.println(F("|       / / / / / / / / __               |"));
  stream.println(F("|      / /_/ / / / / /_/ /               |"));
  stream.println(F("|     /_____/ /_/  \\____/                |"));
  stream.println(F("|                                        |"));
  stream.println(F("| .System-Information------------------. |"));
  stream.println(F("| |====================================| |"));
  stream.println(F("| | * DTG IoT Hub Firmware             | |"));
  stream.println(F("| | * Boot sequence initiated...       | |"));
  stream.println(F("| `------------------------------------' |"));
  stream.println(F("`----------------------------------------'"));
  stream.println();
#else
  (void)stream;
#endif
}
}