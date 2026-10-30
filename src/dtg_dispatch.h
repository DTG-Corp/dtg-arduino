/**
 * @file dtg_dispatch.h
 * @brief Declarative downlink listen helpers for DTG IoT Hub.
 *
 * End-user API (preferred):
 *   DTG_LISTEN("led") { int v = param.getInt(); ... dtg.publish("led", v); }
 *
 * Legacy alias: DTG_ON_VALUE("led") { ... value.asInt() ... }
 * Auto-wired via HubCmd command chain (enableHubCmd / enableNoCodeIo) - sketch does
 * not call onCommand(dtgValueRouter) when using Hub command channel.
 */
#pragma once

#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

class DtgParam
{
public:
  DtgParam(const uint8_t *payload, unsigned int length)
      : payload_(payload), length_(length)
  {
    parse_();
  }

  const uint8_t *raw() const { return payload_; }
  unsigned int length() const { return length_; }

  long getInt() const { return static_cast<long>(num_); }
  float getFloat() const { return num_; }
  bool getBool() const { return num_ >= 0.5f || boolTrue_; }

  long asInt() const { return getInt(); }
  float asFloat() const { return getFloat(); }
  bool asBool() const { return getBool(); }

  const char *asString(char *buf, size_t bufLen) const
  {
    if (!buf || bufLen == 0)
      return nullptr;
    size_t n = (length_ < (bufLen - 1U)) ? length_ : (bufLen - 1U);
    if (payload_ && n > 0)
      memcpy(buf, payload_, n);
    buf[n] = '\0';
    return buf;
  }

private:
  void parse_()
  {
    num_ = 0.0f;
    boolTrue_ = false;
    if (!payload_ || length_ == 0)
      return;

    char tmp[96];
    size_t n = length_;
    if (n >= sizeof(tmp))
      n = sizeof(tmp) - 1;
    memcpy(tmp, payload_, n);
    tmp[n] = '\0';

    char *a = tmp;
    while (*a && isspace(static_cast<unsigned char>(*a)))
      a++;
    char *b = a + strlen(a);
    while (b > a && isspace(static_cast<unsigned char>(b[-1])))
      *--b = 0;
    if (!a[0])
      return;

    if (a[0] == '{')
    {
      // Envelope {"id":"...","v":1} or {"v":1.0}
      const char *vp = strstr(a, "\"v\"");
      if (vp)
      {
        vp = strchr(vp + 3, ':');
        if (vp)
        {
          vp++;
          while (*vp && isspace(static_cast<unsigned char>(*vp)))
            vp++;
          if (!strncasecmp(vp, "true", 4))
          {
            boolTrue_ = true;
            num_ = 1.0f;
            return;
          }
          if (!strncasecmp(vp, "false", 5))
            return;
          num_ = static_cast<float>(atof(vp));
          boolTrue_ = num_ >= 0.5f;
          return;
        }
      }
      return;
    }

    if (!strcasecmp(a, "1") || !strcasecmp(a, "on") || !strcasecmp(a, "true") ||
        !strcasecmp(a, "high") || !strcasecmp(a, "yes"))
    {
      boolTrue_ = true;
      num_ = 1.0f;
      return;
    }
    if (!strcasecmp(a, "0") || !strcasecmp(a, "off") || !strcasecmp(a, "false") ||
        !strcasecmp(a, "low") || !strcasecmp(a, "no"))
      return;

    num_ = static_cast<float>(atof(a));
    boolTrue_ = num_ >= 0.5f;
  }

  const uint8_t *payload_ = nullptr;
  unsigned int length_ = 0;
  float num_ = 0.0f;
  bool boolTrue_ = false;
};

typedef void (*DtgListenHandler)(DtgParam &param);
/** Legacy: deviceAlias + const value ref. */
typedef void (*DtgValueHandler)(const char *deviceAlias, const DtgParam &value);

struct DtgListenRoute
{
  const char *streamId = nullptr;
  DtgListenHandler listenFn = nullptr;
  DtgValueHandler legacyFn = nullptr;
  DtgListenRoute *next = nullptr;
};

inline DtgListenRoute *&dtgListenHead()
{
  static DtgListenRoute *head = nullptr;
  return head;
}

inline DtgValueHandler &dtgValueDefaultHandler()
{
  static DtgValueHandler handler = nullptr;
  return handler;
}

struct DtgListenRegistrar
{
  DtgListenRegistrar(const char *stream, DtgListenHandler fn)
  {
    route_.streamId = stream;
    route_.listenFn = fn;
    route_.next = dtgListenHead();
    dtgListenHead() = &route_;
  }

private:
  DtgListenRoute route_{};
};

struct DtgValueRegistrar
{
  DtgValueRegistrar(const char *stream, DtgValueHandler fn)
  {
    route_.streamId = stream;
    route_.legacyFn = fn;
    route_.next = dtgListenHead();
    dtgListenHead() = &route_;
  }

private:
  DtgListenRoute route_{};
};

struct DtgValueDefaultRegistrar
{
  explicit DtgValueDefaultRegistrar(DtgValueHandler fn)
  {
    dtgValueDefaultHandler() = fn;
  }
};

#define DTG__JOIN_INNER(a, b) a##b
#define DTG__JOIN(a, b) DTG__JOIN_INNER(a, b)
#define DTG__UNIQUE(prefix) DTG__JOIN(prefix, __COUNTER__)

/** Preferred end-user API: DTG_LISTEN("streamId") { int v = param.getInt(); ... } */
#define DTG_LISTEN(streamLiteral)                                                      \
  DTG_LISTEN_IMPL(streamLiteral, DTG__UNIQUE(dtgListenFn_), DTG__UNIQUE(dtgListenReg_))

#define DTG_LISTEN_IMPL(streamLiteral, fnName, regName)                                \
  static void fnName(DtgParam &param);                                                 \
  static DtgListenRegistrar regName(streamLiteral, fnName);                            \
  static void fnName(DtgParam &param)

/** Legacy alias - uses `value` and optional `deviceAlias`. Prefer DTG_LISTEN. */
#define DTG_ON_VALUE(streamLiteral)                                                    \
  DTG_ON_VALUE_IMPL(streamLiteral, DTG__UNIQUE(dtgValueFn_), DTG__UNIQUE(dtgValueReg_))

#define DTG_ON_VALUE_IMPL(streamLiteral, fnName, regName)                              \
  static void fnName(const char *deviceAlias, const DtgParam &value);                  \
  static DtgValueRegistrar regName(streamLiteral, fnName);                             \
  static void fnName(const char *deviceAlias, const DtgParam &value)

#define DTG_ON_VALUE_DEFAULT()                                                         \
  DTG_ON_VALUE_DEFAULT_IMPL(DTG__UNIQUE(dtgDefaultFn_), DTG__UNIQUE(dtgDefaultReg_))

#define DTG_ON_VALUE_DEFAULT_IMPL(fnName, regName)                                     \
  static void fnName(const char *deviceAlias, const DtgParam &value);                  \
  static DtgValueDefaultRegistrar regName(fnName);                                     \
  static void fnName(const char *deviceAlias, const DtgParam &value)

/** @return true if a DTG_LISTEN / DTG_ON_VALUE handler consumed the stream. */
inline bool dtgListenDispatch(const char *deviceAlias, const char *streamId,
                              const uint8_t *payload, unsigned int length)
{
  if (!streamId)
    return false;
  DtgParam param(payload, length);
  for (DtgListenRoute *it = dtgListenHead(); it; it = it->next)
  {
    if (!it->streamId || strcasecmp(streamId, it->streamId) != 0)
      continue;
    if (it->listenFn)
    {
      it->listenFn(param);
      return true;
    }
    if (it->legacyFn)
    {
      it->legacyFn(deviceAlias ? deviceAlias : "_", param);
      return true;
    }
  }
  return false;
}

/**
 * Legacy: attach with dtg.onCommand(dtgValueRouter) when NOT using enableHubCmd().
 * With enableHubCmd / enableNoCodeIo, listen is auto-wired - do not call this.
 */
inline void dtgValueRouter(const char *deviceAlias, const char *streamId,
                           const uint8_t *payload, unsigned int length, void * /*user*/)
{
  if (dtgListenDispatch(deviceAlias, streamId, payload, length))
    return;
  DtgValueHandler fallback = dtgValueDefaultHandler();
  if (fallback)
  {
    DtgParam param(payload, length);
    fallback(deviceAlias ? deviceAlias : "_", param);
  }
}

/** HubCmd chain: listen handlers own the stream; otherwise fall through to no-code. */
void dtgListenCommandChain(const char *deviceAlias, const char *streamId,
                           const uint8_t *payload, unsigned int length, void *user);

inline bool dtgStreamIs(const char *streamId, const char *name)
{
  return streamId && name && strcasecmp(streamId, name) == 0;
}

#define DTG_CONNECTED() void dtgOnConnected()
void dtgOnConnected();

// Backward-compatible table dispatch API (kept for existing sketches).
typedef void (*DtgStreamHandler)(const char *deviceAlias, const uint8_t *payload,
                                 unsigned int length);

struct DtgStreamEntry
{
  const char *streamId;
  DtgStreamHandler handler;
};

#define DTG_STREAM_TABLE(name) static const DtgStreamEntry name[] = {
#define DTG_STREAM(id, fn) {id, fn},
#define DTG_STREAM_TABLE_END {nullptr, nullptr}};

inline void dtgDispatch(const DtgStreamEntry *table, const char *deviceAlias,
                        const char *streamId, const uint8_t *payload, unsigned int length)
{
  if (!table || !streamId)
    return;
  for (const DtgStreamEntry *entry = table; entry->streamId; ++entry)
  {
    if (strcasecmp(streamId, entry->streamId) == 0)
    {
      if (entry->handler)
        entry->handler(deviceAlias, payload, length);
      return;
    }
  }
}
