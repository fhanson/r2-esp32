#include "Version.h"

#include <stdlib.h>
#include <string.h>

namespace droid {

const char kFirmwareTag[] = "R2AU-FW|" FW_NODE "|" FW_VERSION "|" FW_BUILD "|";

namespace {
constexpr char kTagStart[] = "R2AU-FW|";

// Copies one '|'-ended field; false if it's empty, too long or unended.
bool field(const uint8_t*& p, const uint8_t* end, char* out, size_t cap) {
  size_t n = 0;
  while (p < end && *p != '|') {
    const uint8_t c = *p++;
    if (c < 0x20 || c > 0x7e || n + 1 >= cap) return false;
    out[n++] = static_cast<char>(c);
  }
  if (p >= end || n == 0) return false;
  ++p;  // the '|'
  out[n] = '\0';
  return true;
}

// Reads one number; *p moves past it.
long number(const char*& p) {
  long v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  return v;
}
}  // namespace

bool findFirmwareTag(const uint8_t* data, size_t length, FirmwareTag& out) {
  const size_t k = sizeof(kTagStart) - 1;
  for (size_t i = 0; i + k <= length; ++i) {
    if (memcmp(data + i, kTagStart, k) != 0) continue;
    const uint8_t* p = data + i + k;
    const uint8_t* end = data + length;
    FirmwareTag t;
    if (field(p, end, t.node, sizeof(t.node)) && field(p, end, t.version, sizeof(t.version)) &&
        field(p, end, t.build, sizeof(t.build))) {
      out = t;
      return true;
    }
  }
  return false;
}

FirmwareTag thisFirmware() {
  FirmwareTag t;
  findFirmwareTag(reinterpret_cast<const uint8_t*>(kFirmwareTag), strlen(kFirmwareTag), t);
  return t;
}

int compareVersions(const char* a, const char* b) {
  for (int part = 0; part < 3; ++part) {
    const long x = number(a), y = number(b);
    if (x != y) return x < y ? -1 : 1;
    if (*a == '.') ++a;
    if (*b == '.') ++b;
  }
  // Same numbers: a release beats any pre-release of it.
  const bool preA = *a == '-', preB = *b == '-';
  if (preA != preB) return preA ? -1 : 1;
  if (!preA) return 0;
  const int c = strcmp(a, b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

size_t encodeVersionStatus(uint8_t* out, size_t cap, const char* version, const char* build) {
  const size_t v = strlen(version), b = strlen(build);
  if (v > kVersionMax || b > kVersionMax || cap < 3 + v + b) return 0;
  size_t n = 0;
  out[n++] = 1;
  out[n++] = static_cast<uint8_t>(v);
  memcpy(out + n, version, v);
  n += v;
  out[n++] = static_cast<uint8_t>(b);
  memcpy(out + n, build, b);
  return n + b;
}

bool decodeVersionStatus(const uint8_t* data, size_t length, char* version, char* build) {
  if (length < 3 || data[0] != 1) return false;
  const size_t v = data[1];
  if (v > kVersionMax || 2 + v >= length) return false;
  const size_t b = data[2 + v];
  if (b > kVersionMax || 3 + v + b != length) return false;
  memcpy(version, data + 2, v);
  version[v] = '\0';
  memcpy(build, data + 3 + v, b);
  build[b] = '\0';
  return true;
}

}  // namespace droid
