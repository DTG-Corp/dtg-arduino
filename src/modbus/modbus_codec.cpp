#include "modbus_codec.h"

#include <math.h>
#include <string.h>

namespace detail {
namespace {

bool transformerHas(const char *transformer, const char *needle) {
  return transformer && needle && strstr(transformer, needle) != nullptr;
}

bool transformerStartsWith(const char *transformer, const char *prefix) {
  if (!transformer || !prefix)
    return false;
  const size_t n = strlen(prefix);
  return strncmp(transformer, prefix, n) == 0;
}

uint64_t composeWords(const uint16_t *regs, size_t count, const char *transformer) {
  uint16_t words[4];
  const size_t n = count > 4 ? 4 : count;
  for (size_t i = 0; i < n; ++i)
    words[i] = regs[i];

  const bool le = transformerHas(transformer, "_LE") && !transformerHas(transformer, "MID_");
  const bool mid = transformerHas(transformer, "MID_");
  if (mid && n >= 2) {
    for (size_t i = 0; i + 1 < n; i += 2) {
      const uint16_t tmp = words[i];
      words[i] = words[i + 1];
      words[i + 1] = tmp;
    }
  }

  uint64_t u = 0;
  if (le) {
    for (size_t i = 0; i < n; ++i)
      u |= (uint64_t)words[i] << (16 * i);
  } else {
    for (size_t i = 0; i < n; ++i)
      u = (u << 16) | words[i];
  }
  return u;
}

} // namespace

bool decodeModbusRegisters(const uint16_t *regs, size_t count, const char *transformer,
                           float scale, float offset, float &valueOut) {
  if (!transformer || !regs || count == 0)
    return false;

  if (strcmp(transformer, "INT16") == 0) {
    valueOut = (float)((int16_t)regs[0]) * scale + offset;
    return true;
  }
  if (strcmp(transformer, "UINT16") == 0) {
    valueOut = (float)regs[0] * scale + offset;
    return true;
  }

  if (transformerStartsWith(transformer, "FLOAT_")) {
    if (count < 2)
      return false;
    const uint32_t bits = (uint32_t)composeWords(regs, 2, transformer);
    float v = 0.0f;
    memcpy(&v, &bits, sizeof(float));
    valueOut = v * scale + offset;
    return true;
  }

  if (transformerStartsWith(transformer, "UINT32_")) {
    if (count < 2)
      return false;
    const uint32_t v = (uint32_t)composeWords(regs, 2, transformer);
    valueOut = (float)v * scale + offset;
    return true;
  }

  if (transformerStartsWith(transformer, "INT64_") || transformerStartsWith(transformer, "UINT64_")) {
    if (count < 4)
      return false;
    const uint64_t u = composeWords(regs, 4, transformer);
    const double d = transformerStartsWith(transformer, "INT64_") ? (double)(int64_t)u : (double)u;
    valueOut = (float)(d * (double)scale + (double)offset);
    return true;
  }

  return false;
}

bool encodeModbusRegisterValue(float value, const char *transformer, float scale, float offset,
                               uint16_t &rawOut) {
  if (!transformer || scale == 0.0f)
    return false;

  const float unscaled = (value - offset) / scale;

  if (strcmp(transformer, "INT16") == 0) {
    const int32_t v = (int32_t)lroundf(unscaled);
    if (v < -32768 || v > 32767)
      return false;
    rawOut = (uint16_t)(int16_t)v;
    return true;
  }
  if (strcmp(transformer, "UINT16") == 0) {
    const int32_t v = (int32_t)lroundf(unscaled);
    if (v < 0 || v > 65535)
      return false;
    rawOut = (uint16_t)v;
    return true;
  }
  return false;
}

bool decodeModbusBits(const uint8_t *bytes, size_t byteCount, uint16_t bitCount, float scale,
                      float offset, float &valueOut) {
  if (!bytes || byteCount == 0 || bitCount == 0)
    return false;

  const uint16_t maxBits = bitCount > 32 ? 32 : bitCount;
  uint32_t composed = 0;
  for (uint16_t i = 0; i < maxBits; ++i) {
    const size_t byteIdx = i / 8;
    if (byteIdx >= byteCount)
      break;
    const uint8_t bit = (bytes[byteIdx] >> (i % 8)) & 0x01;
    composed |= ((uint32_t)bit) << i;
  }

  valueOut = (float)composed * scale + offset;
  return true;
}

} // namespace detail
