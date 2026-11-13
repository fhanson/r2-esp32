#pragma once
// The S3 access point's name and password, as the Settings page can change
// them.
//
// The built-in pair is kApSsid (BodyConfig.h) and kApPassword (Secrets.h,
// kept out of git). A pair set from Settings is saved on the S3 and used
// instead, until "Use the built-in name and password" clears it.

#include <stddef.h>

namespace droid {

constexpr size_t kWifiNameMax = 32;      // SSID limit
constexpr size_t kWifiPasswordMin = 8;   // WPA2 limits
constexpr size_t kWifiPasswordMax = 63;

// Printable ASCII only, so it types the same on any phone. No open network:
// the droid goes to public events.
bool wifiNameValid(const char* name);
bool wifiPasswordValid(const char* password);

}  // namespace droid
