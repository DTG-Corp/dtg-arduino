/**
 * Host self-check for dtg_ota_hash.h (NIST empty-string SHA-256).
 * g++ -std=c++17 -fsanitize=address,undefined -I../src -o ota_sha256_test ota_sha256_test.cpp
 */
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/dtg_ota_hash.h"

int main()
{
  uint8_t out[32] = {};
  assert(!dtgOtaParseSha256Hex(nullptr, out));
  assert(!dtgOtaParseSha256Hex("", out));
  assert(!dtgOtaParseSha256Hex("sha256:zz", out));
  assert(!dtgOtaParseSha256Hex("deadbeef", out));

  const char *emptySha =
      "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
  assert(dtgOtaParseSha256Hex(emptySha, out));
  uint8_t bare[32] = {};
  assert(dtgOtaParseSha256Hex(emptySha + 7, bare));
  assert(dtgOtaSha256Equal(out, bare));

  uint8_t other[32] = {};
  other[0] = 1;
  assert(!dtgOtaSha256Equal(out, other));

  std::puts("ota_sha256_test OK");
  return 0;
}
