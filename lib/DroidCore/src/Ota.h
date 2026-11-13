#pragma once
// Firmware updates over Wi-Fi, the parts the S3 and C3s share. Plain
// C++. The flow, and how rollback works, is in the README ("Updating over
// Wi-Fi"); the S3 side is src/body/OtaServer.cpp, the C3 side
// lib/DroidRadio/src/OtaNode.cpp.

#include <stddef.h>
#include <stdint.h>

#include "Version.h"

namespace droid {

// Finds the R2AU-FW tag in an image arriving in pieces, even across two.
class FirmwareTagScanner {
 public:
  void feed(const uint8_t* data, size_t length);
  bool found() const { return found_; }
  const FirmwareTag& tag() const { return tag_; }

 private:
  static constexpr size_t kKeep = 80;  // longer than any whole tag (72 at most)
  uint8_t carry_[kKeep * 2] = {};
  size_t carryLen_ = 0;
  bool found_ = false;
  FirmwareTag tag_;
};

enum class OtaState : uint8_t {
  Idle = 0,
  Receiving = 1,    // the page is uploading to the S3
  Joining = 2,      // the C3 is joining the access point
  Downloading = 3,  // the C3 is fetching its image from the S3
  Restarting = 4,   // written and checked; restarting into it
  Failed = 5,
};

// Sent with Failed.
enum class OtaError : uint8_t {
  None = 0,
  NoWifi = 1,
  NoServer = 2,
  WrongBoard = 3,   // tag names another board, or there's none
  WriteFailed = 4,  // flash write or image check
  Timeout = 5,
  Busy = 6,
};

const char* otaErrorText(OtaError e);

// OtaStart payload (S3 -> C3, MessageType::OtaStart):
//   u32 image size, 4 bytes access point IP, u8 len + name, u8 len + password
struct OtaStart {
  uint32_t size = 0;
  uint8_t ip[4] = {};
  char ssid[33] = {};
  char password[64] = {};
};
size_t encodeOtaStart(uint8_t* out, size_t cap, const OtaStart& s);
bool decodeOtaStart(const uint8_t* data, size_t length, OtaStart& out);

// NodeStatus field 2 (C3 -> S3): u8 2, u8 state, u8 percent, u8 error
struct OtaProgress {
  OtaState state = OtaState::Idle;
  uint8_t percent = 0;
  OtaError error = OtaError::None;
};
size_t encodeOtaProgress(uint8_t* out, size_t cap, const OtaProgress& p);
bool decodeOtaProgress(const uint8_t* data, size_t length, OtaProgress& out);

// Node id <-> the env name in a tag ("body_c3"); nullptr or 0 if unknown.
const char* otaNodeEnv(uint8_t node);
uint8_t otaNodeFromEnv(const char* env);

}  // namespace droid
