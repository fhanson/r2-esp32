#include "WifiConfig.h"

#include <string.h>

namespace droid {

namespace {
bool printable(const char* s, size_t min, size_t max) {
  if (s == nullptr) return false;
  const size_t n = strlen(s);
  if (n < min || n > max) return false;
  for (size_t i = 0; i < n; ++i) {
    if (s[i] < 0x20 || s[i] > 0x7e) return false;
  }
  return true;
}
}  // namespace

bool wifiNameValid(const char* name) {
  // Not all spaces, or phones show a blank network.
  if (!printable(name, 1, kWifiNameMax)) return false;
  for (const char* p = name; *p; ++p) {
    if (*p != ' ') return true;
  }
  return false;
}

bool wifiPasswordValid(const char* password) { return printable(password, kWifiPasswordMin, kWifiPasswordMax); }

}  // namespace droid
