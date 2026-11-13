#pragma once
// Messages between a control UI (the web page over a WebSocket, the touch
// display over ESP-NOW) and the body S3.
//
// Every message is one binary frame: a type byte, then its fields,
// little-endian. None is longer than kUiMaxMessage, so each fits in one
// ESP-NOW packet with room for the link header. An animation is sent as a
// header plus one message per step for the same reason.

#include <stddef.h>
#include <stdint.h>

#include "Animations.h"
#include "CustomSequences.h"
#include "WifiConfig.h"

namespace droid {

constexpr size_t kUiMaxMessage = 240;

enum class UiType : uint8_t {
  // UI -> S3
  Hello = 0x01,          // client u8 (1 web page, 2 touch display)
  ListSettings = 0x10,
  SetSetting = 0x11,     // index u8, value i32
  ClearSetting = 0x12,   // index u8
  SetWifi = 0x13,        // name text, password text; both empty = back to the built-in pair
  PlaySound = 0x20,      // file u16
  PlayRandom = 0x21,     // first u16, last u16
  Emotion = 0x22,        // emotion u8 (0 happy, 1 sad, 2 mad, 3 scared, 4 random)
  DomeHome = 0x23,
  DomeGoTo = 0x24,       // degrees i16
  DomeStop = 0x25,
  RunServoSequence = 0x26,  // index u8, [first frame u8, custom sequences only]
  RunAnimation = 0x27,   // id u16
  StopAll = 0x28,        // stop the animation, close the panels
  ServoMoves = 0x29,     // count u8, then (servo id u8, us u16) each; disarmed only
  ListAnimations = 0x30,
  DeleteAnimation = 0x32,  // id u16
  ResetAnimations = 0x33,  // back to the default library
  AnimHeader = 0x34,     // both ways: id u16, step count u8, name, icon
  AnimStep = 0x35,       // both ways: id u16, index u8, step
  AnimCommit = 0x36,     // UI -> S3: id u16; save what was sent
  SeqHeader = 0x38,      // both ways: id u8 (0 new), frame count u8, name (CustomSequences.h)
  SeqFrame = 0x39,       // both ways: id u8, index u8, ms u16, kSeqServos positions i8
  SeqCommit = 0x3A,      // UI -> S3: id u8; save what was sent
  DeleteSequence = 0x3B, // UI -> S3: id u8
  CopySequence = 0x3C,   // UI -> S3: index u8 (any sequence); saved as a new one
  // S3 -> UI
  Status = 0x80,
  SettingInfo = 0x81,    // index u8, count u8, value, default, min, max (i32), overridden u8, key
  SettingValue = 0x82,   // index u8, value i32, overridden u8
  ListEnd = 0x83,        // list type u8 (the request), count u8
  Result = 0x84,         // request type u8, result u8
  ServoSequenceInfo = 0x85,  // index u8, frames u8, length ms u32, name
  NodeVersion = 0x86,        // node u8, version text, build text (Version.h)
  WifiInfo = 0x87,           // custom u8 (0 built-in, 1 set from Settings), name text, password text
  OtaStatus = 0x88,          // node u8, state u8, percent u8, error u8 (Ota.h), message text
};

enum class UiResult : uint8_t {
  Ok = 0,
  RefusedArmed = 1,  // settings and the animation library lock while armed
  Bad = 2,
  Full = 3,
  Unknown = 4,
  NoLink = 5,        // needs the radio link (dome moves)
};

enum UiStatusFlag : uint16_t {
  kStatusRadio = 1 << 0,
  kStatusArmed = 1 << 1,
  kStatusEnable = 1 << 2,
  kStatusDomeNode = 1 << 3,
  kStatusBodyC3 = 1 << 4,
  kStatusAnimating = 1 << 5,
};

struct UiStatus {
  uint16_t flags = 0;
  uint8_t domeMode = 0;        // DomeMode
  int16_t domeAngle = INT16_MIN;  // INT16_MIN when unknown
  uint16_t animationId = 0;    // 0 when none is playing
  int8_t servoSequence = -1;   // -1 when none
  uint32_t uptimeS = 0;
};

// A small writer and reader over a byte buffer. A writer that runs out of
// room, or a reader that runs past the end, goes bad and stays bad.
class UiWriter {
 public:
  UiWriter(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
  void u8(uint8_t v);
  void u16(uint16_t v);
  void i16(int16_t v) { u16(static_cast<uint16_t>(v)); }
  void u32(uint32_t v);
  void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
  void text(const char* s);  // length byte + characters
  void bytes(const uint8_t* p, size_t n);
  size_t size() const { return ok_ ? n_ : 0; }
  bool ok() const { return ok_; }
  uint8_t* tail() { return buf_ + n_; }
  size_t room() const { return cap_ - n_; }
  void advance(size_t n) { if (n > room()) ok_ = false; else n_ += n; }

 private:
  uint8_t* buf_;
  size_t cap_;
  size_t n_ = 0;
  bool ok_ = true;
};

class UiReader {
 public:
  UiReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}
  uint8_t u8();
  uint16_t u16();
  int16_t i16() { return static_cast<int16_t>(u16()); }
  uint32_t u32();
  int32_t i32() { return static_cast<int32_t>(u32()); }
  bool text(char* out, size_t max);  // max characters, plus a NUL
  const uint8_t* rest() const { return buf_ + n_; }
  size_t left() const { return ok_ ? len_ - n_ : 0; }
  void skip(size_t n) { if (n > left()) ok_ = false; else n_ += n; }
  bool ok() const { return ok_; }
  bool done() const { return ok_ && n_ == len_; }

 private:
  const uint8_t* buf_;
  size_t len_;
  size_t n_ = 0;
  bool ok_ = true;
};

// S3 -> UI builders. Each returns the message size, or 0 if it doesn't fit.
size_t uiStatus(uint8_t* out, size_t cap, const UiStatus& s);
size_t uiSettingInfo(uint8_t* out, size_t cap, uint8_t index, uint8_t count, int32_t value,
                     int32_t def, int32_t min, int32_t max, bool overridden, const char* key);
size_t uiSettingValue(uint8_t* out, size_t cap, uint8_t index, int32_t value, bool overridden);
size_t uiListEnd(uint8_t* out, size_t cap, UiType list, uint8_t count);
size_t uiResult(uint8_t* out, size_t cap, UiType request, UiResult result);
size_t uiWifiInfo(uint8_t* out, size_t cap, bool custom, const char* name, const char* password);
// SetWifi: name (kWifiNameMax+1) and password (kWifiPasswordMax+1) buffers.
bool uiParseSetWifi(const uint8_t* data, size_t len, char* name, char* password);
size_t uiNodeVersion(uint8_t* out, size_t cap, uint8_t node, const char* version, const char* build);
size_t uiOtaStatus(uint8_t* out, size_t cap, uint8_t node, uint8_t state, uint8_t percent, uint8_t error,
                   const char* message);
size_t uiServoSequenceInfo(uint8_t* out, size_t cap, uint8_t index, uint8_t frames, uint32_t lengthMs,
                           const char* name);
// Both ways.
size_t uiSeqHeader(uint8_t* out, size_t cap, const CustomSequence& seq);
size_t uiSeqFrame(uint8_t* out, size_t cap, uint8_t id, uint8_t index, const CustomFrame& f);
size_t uiAnimHeader(uint8_t* out, size_t cap, const Animation& anim);
size_t uiAnimStep(uint8_t* out, size_t cap, uint16_t id, uint8_t index, const AnimStep& step);

bool uiParseStatus(const uint8_t* data, size_t len, UiStatus& s);

// Collects an animation sent as a header plus steps.
class AnimAssembler {
 public:
  // Returns false for a malformed message.
  bool header(const uint8_t* data, size_t len);
  bool step(const uint8_t* data, size_t len);
  // True when the header and every step for this id have arrived.
  bool complete(uint16_t id) const;
  const Animation& animation() const { return anim_; }

 private:
  Animation anim_;
  bool haveHeader_ = false;
  uint32_t got_ = 0;  // bit per step received
};

// Puts a custom sequence back together from SeqHeader and SeqFrame messages.
class SeqAssembler {
 public:
  bool header(const uint8_t* data, size_t len);
  bool frame(const uint8_t* data, size_t len);
  bool complete(uint8_t id) const;
  const CustomSequence& sequence() const { return seq_; }

 private:
  CustomSequence seq_;
  bool haveHeader_ = false;
  uint64_t got_ = 0;  // bit per frame received
};

}  // namespace droid
