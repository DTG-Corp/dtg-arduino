/**
 * Host self-check for DtgParam parse (plain + JSON envelope).
 * g++ -std=c++17 -fsanitize=address,undefined -Istubs -I../src -o dtg_param_test dtg_param_test.cpp
 */
#include <cassert>
#include <cstdio>
#include <cstring>

#include "dtg_dispatch.h"

static void check(const char *raw, int expect)
{
  DtgParam p(reinterpret_cast<const uint8_t *>(raw), static_cast<unsigned>(strlen(raw)));
  assert(p.getInt() == expect);
}

int main()
{
  check("1", 1);
  check("0", 0);
  check("on", 1);
  check("{\"v\":1}", 1);
  check("{\"id\":\"x\",\"v\":0}", 0);
  check("{\"v\":1.0}", 1);
  std::puts("dtg_param_test OK");
  return 0;
}
