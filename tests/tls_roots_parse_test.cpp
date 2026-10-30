/**
 * Host self-check: concatenated Let's Encrypt PEM bundle has four roots.
 * g++ -std=c++17 -fsanitize=address,undefined -I../src -o tls_roots_parse_test tls_roots_parse_test.cpp
 */
#include <cassert>
#include <cstdio>
#include <cstring>

#include "../src/dtg_tls_roots.h"

static int countNeedle(const char *hay, const char *needle)
{
  int n = 0;
  const char *p = hay;
  const size_t k = strlen(needle);
  while ((p = strstr(p, needle)) != nullptr)
  {
    ++n;
    p += k;
  }
  return n;
}

int main()
{
  assert(countNeedle(DTG_TLS_CA_BUNDLE, "-----BEGIN CERTIFICATE-----") == 4);
  assert(countNeedle(DTG_TLS_CA_BUNDLE, "-----END CERTIFICATE-----") == 4);
  assert(DTG_TLS_CA_ISRG_ROOT_X1 == DTG_TLS_CA_BUNDLE);
  assert(strlen(DTG_TLS_CA_BUNDLE) > 4000);
  std::puts("tls_roots_parse_test OK");
  return 0;
}
