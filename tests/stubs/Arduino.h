#pragma once

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROGMEM
#define PSTR(x) x
#define F(x) x
#define HIGH 1
#define LOW 0
#ifndef strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#endif
#endif

class Stream
{
public:
  void print(const char *) {}
  void println(const char *) {}
};

class String
{
public:
  String() = default;
  String(const char *) {}
};
