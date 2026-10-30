#include <cassert>
#include <cstring>

#include "../src/dtg_heap_telemetry.h"

int main()
{
  char buf[96];
  const int n = dtgFormatHeapTelemetryFields(buf, sizeof(buf), 123456, 98765, 54321);
  assert(n > 0);
  assert(strcmp(buf, ",\"heap\":123456,\"heapMin\":98765,\"heapBlk\":54321") == 0);

  assert(dtgFormatHeapTelemetryFields(buf, 10, 1, 2, 3) < 0);
  assert(dtgFormatHeapTelemetryFields(nullptr, 96, 1, 2, 3) < 0);

  return 0;
}
