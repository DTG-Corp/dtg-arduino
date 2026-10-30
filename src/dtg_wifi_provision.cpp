/**
 * @file dtg_wifi_provision.cpp
 */
#include "dtg_wifi_provision.h"

#include "dtg_config.h"
#include "dtg_debug.h"
#include "dtg_wifi.h"

#include <esp_random.h>
#include <stdio.h>
#include <string.h>

DtgWifiProvisioner *DtgWifiProvisioner::instance_ = nullptr;

namespace
{

constexpr char kPortalCss[] PROGMEM = R"CSS(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,-apple-system,sans-serif;background:#f8f9ff;color:#0b1c30;padding:16px 16px 100px;line-height:1.45}
h1{font-size:1.4rem;margin-bottom:4px;font-weight:700;color:#0b1c30}
.sub{font-size:.85rem;color:#4b5563;margin-bottom:20px}
.card{background:#eff4ff;border-radius:16px;padding:16px;margin-bottom:16px;box-shadow:0 1px 3px rgba(0,0,0,.05)}
.card-title{font-size:1.05rem;font-weight:700;margin-bottom:12px;color:#006b5f;display:block}
label{display:block;font-size:.8rem;font-weight:600;margin:12px 0 6px;color:#374151}
input[type=text],input[type=password]{width:100%;padding:12px 14px;border:1px solid #c7d2fe;border-radius:12px;font-size:1rem;background:#fff;transition:border-color .2s;outline:none}
input[type=text]:focus,input[type=password]:focus{border-color:#14b8a6;box-shadow:0 0 0 3px rgba(20,184,166,.1)}
.checkbox-lbl{display:flex;align-items:center;font-size:.9rem;font-weight:500;margin-top:16px;cursor:pointer}
.checkbox-lbl input{margin-right:8px;width:18px;height:18px;accent-color:#006b5f}
.bar{position:fixed;left:0;right:0;bottom:0;padding:16px 16px calc(16px + env(safe-area-inset-bottom));background:rgba(248,249,255,.85);backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);border-top:1px solid #e0e7ff;z-index:100}
.btn{display:block;width:100%;padding:14px;border:0;border-radius:12px;font-size:1.05rem;font-weight:600;color:#fff;background:linear-gradient(135deg,#14b8a6,#0d9488);cursor:pointer;box-shadow:0 4px 6px rgba(13,148,136,.25);transition:opacity .2s}
.btn:active{opacity:.8}
.err{color:#b91c1c;font-size:.85rem;margin-top:8px}
.hint{font-size:.8rem;color:#6b7280;margin-top:6px}
)CSS";

void htmlEscape(const char *in, char *out, size_t cap)
{
  if (!in || !out || cap < 2)
  {
    if (out && cap)
      out[0] = '\0';
    return;
  }
  size_t j = 0;
  for (; *in && j + 6 < cap; ++in)
  {
    const char c = *in;
    if (c == '<')
    {
      memcpy(out + j, "&lt;", 4);
      j += 4;
    }
    else if (c == '>')
    {
      memcpy(out + j, "&gt;", 4);
      j += 4;
    }
    else if (c == '&')
    {
      memcpy(out + j, "&amp;", 5);
      j += 5;
    }
    else if (c == '"')
    {
      memcpy(out + j, "&quot;", 6);
      j += 6;
    }
    else if (c == '\'')
    {
      memcpy(out + j, "&#39;", 5);
      j += 5;
    }
    else
    {
      out[j++] = c;
    }
  }
  out[j] = '\0';
}

void genHexToken(char *out, size_t outLen, size_t nbytes)
{
  static const char hex[] = "0123456789abcdef";
  if (!out || outLen < nbytes * 2 + 1)
    return;
  for (size_t i = 0; i < nbytes; ++i)
  {
    const uint8_t b = static_cast<uint8_t>(esp_random() & 0xff);
    out[i * 2] = hex[b >> 4];
    out[i * 2 + 1] = hex[b & 0x0f];
  }
  out[nbytes * 2] = '\0';
}

} // namespace

void DtgWifiProvisioner::attach(DtgWiFi *owner)
{
  owner_ = owner;
  instance_ = this;
}

bool DtgWifiProvisioner::constantTimeEq(const char *a, const char *b)
{
  if (!a || !b)
    return false;
  constexpr size_t kCsrfHexLen = 32;
  if (strlen(a) != kCsrfHexLen || strlen(b) != kCsrfHexLen)
    return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < kCsrfHexLen; ++i)
    diff |= static_cast<uint8_t>(static_cast<uint8_t>(a[i]) ^ static_cast<uint8_t>(b[i]));
  return diff == 0;
}

void DtgWifiProvisioner::buildApSsid(char *out, size_t outLen) const
{
  if (!out || outLen < 12)
    return;
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(out, outLen, "dtg-gw-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

void DtgWifiProvisioner::requestApSetup()
{
  if (state_ == DtgWifiState::ApSetup)
    return;
  state_ = DtgWifiState::ApSetup;
}

void DtgWifiProvisioner::tick(unsigned long nowMs, bool staLinkUp,
                              bool mqttConnected)
{
  if (state_ == DtgWifiState::ApSetup)
    return;

  if (bootPin_ != 255)
  {
    const int bootRaw = digitalRead(bootPin_);
    const bool bootPressed = (bootRaw == LOW);
    if (bootPressed)
    {
      if (bootLowSinceMs_ == 0)
        bootLowSinceMs_ = nowMs;
      if (nowMs - bootLowSinceMs_ >= DTG_WIFI_BOOT_DEBOUNCE_MS)
      {
        bootDebounced_ = true;
        if (bootHoldStartMs_ == 0)
          bootHoldStartMs_ = nowMs;
        else if (nowMs - bootHoldStartMs_ >= DTG_WIFI_BOOT_CONFIG_HOLD_MS)
        {
          DTG_LOG("BOOT held - entering Wi-Fi setup AP");
          requestApSetup();
          return;
        }
      }
    }
    else
    {
      bootDebounced_ = false;
      bootLowSinceMs_ = 0;
      bootHoldStartMs_ = 0;
    }
  }

  const bool netOk = staLinkUp && mqttConnected;
  if (netOk)
  {
    offlineSinceMs_ = 0;
    return;
  }
  if (offlineSinceMs_ == 0)
    offlineSinceMs_ = nowMs;
  else if (nowMs - offlineSinceMs_ >= DTG_WIFI_OFFLINE_AP_MS)
  {
    DTG_LOG("Offline timeout - entering Wi-Fi setup AP");
    requestApSetup();
  }
}

void DtgWifiProvisioner::publishApInfo()
{
  if (!owner_)
    return;
  owner_->setApSetupReport(true, apSsid_, DTG_WIFI_AP_IP);
  if (owner_->connected())
    (void)owner_->publishDeviceInfo();
  owner_->disconnectMqtt();
}

void DtgWifiProvisioner::startAp()
{
  if (!dtgWifiStoreGetOrCreateApPsk(apPsk_, sizeof(apPsk_)))
  {
    DTG_LOG("AP PSK unavailable");
    return;
  }
  buildApSsid(apSsid_, sizeof(apSsid_));
  genHexToken(csrfToken_, sizeof(csrfToken_), 16);

  publishApInfo();

  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(apSsid_, apPsk_, 1, 0, 4);
  if (!ok)
  {
    DTG_LOG("softAP failed (retry in %lus)", static_cast<unsigned long>(DTG_WIFI_AP_RETRY_MS / 1000UL));
    return;
  }
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                    IPAddress(255, 255, 255, 0));
  apRunning_ = true;
  apEnteredMs_ = millis();
  postCountWindow_ = 0;
  apLastPostMs_ = 0;
  postLockoutUntilMs_ = 0;

#if DTG_WIFI_LOG_AP_PSK_ON_SERIAL
  DTG_LOG("Setup AP SSID=%s password=%s (join this Wi-Fi, then http://%s)", apSsid_,
          apPsk_, DTG_WIFI_AP_IP);
#else
  DTG_LOG("Setup AP SSID=%s (PSK on device label only)", apSsid_);
#endif

  if (!routesRegistered_)
  {
    setupRoutes();
    routesRegistered_ = true;
  }
  server_.begin();
}

void DtgWifiProvisioner::stopAp()
{
  server_.stop();
  WiFi.softAPdisconnect(true);
  apRunning_ = false;
  apEnteredMs_ = 0;
  if (owner_)
    owner_->setApSetupReport(false, nullptr, nullptr);
}

void DtgWifiProvisioner::setupRoutes()
{
  server_.on("/", HTTP_GET, handleRoot);
  server_.on("/wifi", HTTP_POST, handleSave);
  server_.onNotFound(handleNotFound);
}

bool DtgWifiProvisioner::handleApSetup(unsigned long nowMs)
{
  if (state_ != DtgWifiState::ApSetup)
    return false;

  if (!apRunning_)
  {
    if (apLastStartAttemptMs_ != 0 &&
        nowMs - apLastStartAttemptMs_ < DTG_WIFI_AP_RETRY_MS)
    {
      return true;
    }
    apLastStartAttemptMs_ = nowMs;
    startAp();
  }

  if (apRunning_)
    server_.handleClient();

  const unsigned long now = millis();
  if (apRunning_ && apEnteredMs_ > 0 &&
      now - apEnteredMs_ >= DTG_WIFI_AP_SETUP_TIMEOUT_MS)
  {
    DTG_LOG("AP setup timeout - reboot");
    stopAp();
    ESP.restart();
  }
  return true;
}

void DtgWifiProvisioner::handleRoot()
{
  if (!instance_ || !instance_->owner_)
  {
    if (instance_)
      instance_->server_.send(500, "text/plain", "err");
    return;
  }
  DtgWifiProvisioner *self = instance_;

  DtgWifiCredentials pri{};
  DtgWifiCredentials bak{};
  (void)dtgWifiStoreLoadPrimary(&pri);
  (void)dtgWifiStoreLoadBackup(&bak);

  char priSsidEsc[80] = {};
  char bakSsidEsc[80] = {};
  htmlEscape(pri.configured ? pri.ssid : "", priSsidEsc, sizeof(priSsidEsc));
  htmlEscape(bak.configured ? bak.ssid : "", bakSsidEsc, sizeof(bakSsidEsc));

  self->server_.sendHeader("Cache-Control", "no-store");
  self->server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  self->server_.send(200, "text/html", "");
  self->server_.sendContent(F("<!DOCTYPE html><html><head><meta charset=utf-8>"
                              "<meta name=viewport content=\"width=device-width,"
                              "initial-scale=1,viewport-fit=cover\"><title>DTG Wi-Fi</title><style>"));
  self->server_.sendContent_P(kPortalCss);
  self->server_.sendContent(F("</style></head><body><h1>DTG Gateway Wi-Fi</h1>"
                              "<p class=sub>AP "));
  self->server_.sendContent(self->apSsid_);
  self->server_.sendContent(F(" &middot; "));
  self->server_.sendContent(DTG_WIFI_AP_IP);
  self->server_.sendContent(F("</p><form method=POST action=/wifi>"
                              "<input type=hidden name=csrf value=\""));
  self->server_.sendContent(self->csrfToken_);
  self->server_.sendContent(F("\"><div class=card><span class=card-title>Primary Wi-Fi</span>"
                              "<label>SSID</label><input type=text name=pri_ssid maxlength=32 "
                              "value=\""));
  if (priSsidEsc[0] != '\0')
  {
    self->server_.sendContent(priSsidEsc);
  }
  self->server_.sendContent(F("\"><label>Password</label><input type=password "
                              "name=pri_pass maxlength=64 placeholder=\"leave blank "
                              "to keep\"><p class=hint>Empty password field keeps "
                              "stored secret.</p></div><div class=card><span class=card-title>Backup "
                              "Wi-Fi</span><label>SSID</label><input type=text name=bak_ssid "
                              "maxlength=32 value=\""));
  if (bakSsidEsc[0] != '\0')
  {
    self->server_.sendContent(bakSsidEsc);
  }
  self->server_.sendContent(F("\"><label>Password</label><input type=password "
                              "name=bak_pass maxlength=64 placeholder=\"leave blank "
                              "to keep\"><label class=checkbox-lbl><input type=checkbox name=bak_clear "
                              "value=1> Clear backup</label></div><div class=bar>"
                              "<button class=btn type=submit>Save &amp; reboot</button>"
                              "</div></form></body></html>"));
}

void DtgWifiProvisioner::handleSave()
{
  if (!instance_)
    return;
  DtgWifiProvisioner *self = instance_;
  WebServer &srv = self->server_;
  const unsigned long now = millis();

  if (self->postLockoutUntilMs_ > 0 &&
      static_cast<long>(self->postLockoutUntilMs_ - now) > 0)
  {
    srv.send(429, "text/html", "<p>Too many attempts. Try later.</p>");
    return;
  }

  if (self->apLastPostMs_ == 0 || now - self->apLastPostMs_ >= 60000UL)
  {
    self->apLastPostMs_ = now;
    self->postCountWindow_ = 0;
  }
  if (self->postCountWindow_ >= DTG_WIFI_AP_POST_MAX_PER_MIN)
  {
    self->postLockoutUntilMs_ = now + DTG_WIFI_AP_LOCKOUT_MS;
    srv.send(429, "text/html", "<p>Too many attempts. Try later.</p>");
    return;
  }
  self->postCountWindow_++;

  if (!srv.hasArg("csrf") ||
      !constantTimeEq(srv.arg("csrf").c_str(), self->csrfToken_))
  {
    srv.send(403, "text/html", "<p>Forbidden.</p>");
    return;
  }

  char priSsid[DTG_WIFI_SSID_MAX + 1] = {};
  char priPass[DTG_WIFI_PASS_MAX + 1] = {};
  char bakSsid[DTG_WIFI_SSID_MAX + 1] = {};
  char bakPass[DTG_WIFI_PASS_MAX + 1] = {};
  bool bakClear = srv.hasArg("bak_clear") && srv.arg("bak_clear") == "1";

  if (srv.hasArg("pri_ssid"))
  {
    if (srv.arg("pri_ssid").length() > DTG_WIFI_SSID_MAX)
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    strlcpy(priSsid, srv.arg("pri_ssid").c_str(), sizeof(priSsid));
  }
  if (srv.hasArg("pri_pass"))
  {
    if (srv.arg("pri_pass").length() > DTG_WIFI_PASS_MAX)
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    strlcpy(priPass, srv.arg("pri_pass").c_str(), sizeof(priPass));
  }
  if (srv.hasArg("bak_ssid"))
  {
    if (srv.arg("bak_ssid").length() > DTG_WIFI_SSID_MAX)
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    strlcpy(bakSsid, srv.arg("bak_ssid").c_str(), sizeof(bakSsid));
  }
  if (srv.hasArg("bak_pass"))
  {
    if (srv.arg("bak_pass").length() > DTG_WIFI_PASS_MAX)
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    strlcpy(bakPass, srv.arg("bak_pass").c_str(), sizeof(bakPass));
  }

  if (!dtgWifiValidateSsid(priSsid))
  {
    srv.send(400, "text/html", "<p>Invalid request.</p>");
    return;
  }

  DtgWifiCredentials oldPri{};
  (void)dtgWifiStoreLoadPrimary(&oldPri);
  const char *priPassFinal = priPass;
  if (priPass[0] == '\0' && oldPri.configured &&
      strcmp(priSsid, oldPri.ssid) == 0)
    priPassFinal = oldPri.pass;
  else if (!dtgWifiValidatePass(priPass))
  {
    srv.send(400, "text/html", "<p>Invalid request.</p>");
    return;
  }

  if (!dtgWifiStoreSavePrimary(priSsid, priPassFinal))
  {
    srv.send(400, "text/html", "<p>Invalid request.</p>");
    return;
  }

  if (bakClear)
  {
    dtgWifiStoreClearBackup();
  }
  else if (bakSsid[0] != '\0')
  {
    if (!dtgWifiValidateSsid(bakSsid))
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    DtgWifiCredentials oldBak{};
    (void)dtgWifiStoreLoadBackup(&oldBak);
    const char *bakPassFinal = bakPass;
    if (bakPass[0] == '\0' && oldBak.configured &&
        strcmp(bakSsid, oldBak.ssid) == 0)
      bakPassFinal = oldBak.pass;
    else if (!dtgWifiValidatePass(bakPass))
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
    if (!dtgWifiStoreSaveBackup(bakSsid, bakPassFinal))
    {
      srv.send(400, "text/html", "<p>Invalid request.</p>");
      return;
    }
  }

  srv.send(200, "text/html",
           "<p>Saved. Rebooting...</p><meta http-equiv=refresh content=\"3;url=/\">");
  delay(500);
  ESP.restart();
}

void DtgWifiProvisioner::handleNotFound()
{
  if (instance_)
    instance_->server_.send(404, "text/plain", "Not found");
}
