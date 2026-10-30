#include "dtg_dispatch.h"

#include "dtg_config.h"
#include "dtg_debug.h"

#if DTG_FEATURE_NOCODE
#include "dtg_nocode.h"
#endif

__attribute__((weak)) void dtgOnConnected() {}

void dtgListenCommandChain(const char *deviceAlias, const char *streamId,
                           const uint8_t *payload, unsigned int length, void *user)
{
  if (dtgListenDispatch(deviceAlias, streamId, payload, length))
  {
    DTG_LOG("[LISTEN] fire stream=%s", streamId ? streamId : "?");
    return;
  }
#if DTG_FEATURE_NOCODE
  detail::HubNoCodeIo::commandCb(deviceAlias, streamId, payload, length, user);
#else
  (void)user;
#endif
}
