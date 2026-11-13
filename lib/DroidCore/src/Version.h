#pragma once
// Firmware version, the same for all three nodes, from the VERSION file at
// the top of the project (tools/version.py passes it in at build time; see
// CHANGELOG.md).
//
//   FW_VERSION  "0.1.0"                  semantic version, from VERSION
//   FW_BUILD    "a1b2c3d" or "a1b2c3d-dirty" or "nogit"   the source it came from
//   FW_NODE     "body_s3", "dome_c3", "body_c3"           the PlatformIO env
//
// Every image carries kFirmwareTag, so an update can check a .bin is for the
// right board before flashing it:
//   "R2AU-FW|<node>|<version>|<build>|"

#include <stddef.h>
#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif
#ifndef FW_BUILD
#define FW_BUILD "local"
#endif
#ifndef FW_NODE
#define FW_NODE "host"
#endif

namespace droid {

constexpr const char* kFirmwareVersion = FW_VERSION;
constexpr const char* kFirmwareBuild = FW_BUILD;
constexpr const char* kFirmwareNode = FW_NODE;
constexpr size_t kVersionMax = 23;  // longest version or build text kept

// In the image's read-only data, for OTA checks. Boards print their version
// through thisFirmware(), which reads it, so the linker keeps it.
extern const char kFirmwareTag[];

struct FirmwareTag {
  char node[16] = {};
  char version[kVersionMax + 1] = {};
  char build[kVersionMax + 1] = {};
};

// Finds the tag in a buffer; FirmwareTagScanner (Ota.h) handles an image
// arriving in pieces. False if there's none or it's malformed.
bool findFirmwareTag(const uint8_t* data, size_t length, FirmwareTag& out);

// This image's own tag, read back from kFirmwareTag.
FirmwareTag thisFirmware();

// Compares "major.minor.patch[-pre]" versions: <0, 0 or >0, like strcmp.
// A pre-release ("0.2.0-dev") sorts before its release ("0.2.0").
int compareVersions(const char* a, const char* b);

// NodeStatus payload (C3 -> S3, LinkProtocol.h): field 1, firmware version.
//   u8 1, u8 len, version, u8 len, build
size_t encodeVersionStatus(uint8_t* out, size_t cap, const char* version, const char* build);
bool decodeVersionStatus(const uint8_t* data, size_t length, char* version, char* build);  // each kVersionMax+1

}  // namespace droid
