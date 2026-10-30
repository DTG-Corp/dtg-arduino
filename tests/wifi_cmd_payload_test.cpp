/**
 * Host-side contract check for set_wifi short-key validation rules.
 * Mirrors dtgWifiValidateSsid/Pass + payload bounds used by HubCmd.
 */
#include <cassert>
#include <cstring>

static bool hasControlChar(const char *s)
{
  for (; s && *s; ++s)
  {
    if (static_cast<unsigned char>(*s) < 0x20)
      return true;
  }
  return false;
}

static bool validateSsid(const char *ssid)
{
  if (!ssid)
    return false;
  const size_t len = std::strlen(ssid);
  if (len == 0 || len > 32)
    return false;
  return !hasControlChar(ssid);
}

static bool validatePass(const char *pass)
{
  if (!pass)
    return true;
  const size_t len = std::strlen(pass);
  if (len > 64)
    return false;
  return !hasControlChar(pass);
}

static unsigned long clampVf(unsigned long vf)
{
  if (vf < 10000UL)
    return 10000UL;
  if (vf > 120000UL)
    return 120000UL;
  return vf;
}

int main()
{
  assert(validateSsid("HomeNet"));
  assert(!validateSsid(""));
  assert(!validateSsid("this-ssid-is-way-too-long-for-wifi!!")); // 33+
  assert(!validateSsid("bad\nssid"));

  assert(validatePass(""));
  assert(validatePass("password1"));
  assert(validatePass("short")); // device NVS allows 1..64; Hub API enforces 0 or 8..64
  assert(!validatePass("pass\tword"));
  assert(!validatePass(
      "this-password-is-far-too-long-for-wpa2-and-exceeds-sixty-four-characters!!"));

  assert(clampVf(1000) == 10000UL);
  assert(clampVf(45000) == 45000UL);
  assert(clampVf(999999) == 120000UL);
  return 0;
}
