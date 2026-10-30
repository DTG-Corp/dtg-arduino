/**
 * @file dtg_ota_hash.h
 * @brief Host-testable SHA-256 hex parse / compare for Hub start_ota checksum.
 *
 * Accepts "sha256:<64 hex>" or bare 64 hex. Arduino-free.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

inline int dtgOtaHexNibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

/** Parse Hub checksum into 32 raw bytes. Returns false if missing/invalid. */
inline bool dtgOtaParseSha256Hex(const char *spec, uint8_t out[32])
{
  if (!spec || !out)
    return false;
  const char *p = spec;
  if (strncmp(p, "sha256:", 7) == 0)
    p += 7;
  size_t n = 0;
  while (p[n])
    ++n;
  if (n != 64)
    return false;
  for (size_t i = 0; i < 32; ++i)
  {
    const int hi = dtgOtaHexNibble(p[i * 2]);
    const int lo = dtgOtaHexNibble(p[i * 2 + 1]);
    if (hi < 0 || lo < 0)
      return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

inline bool dtgOtaSha256Equal(const uint8_t a[32], const uint8_t b[32])
{
  if (!a || !b)
    return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < 32; ++i)
    diff |= static_cast<uint8_t>(a[i] ^ b[i]);
  return diff == 0;
}
