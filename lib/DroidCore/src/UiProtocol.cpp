#include "UiProtocol.h"

#include <string.h>

namespace droid {

void UiWriter::u8(uint8_t v) {
  if (!ok_ || n_ + 1 > cap_) {
    ok_ = false;
    return;
  }
  buf_[n_++] = v;
}

void UiWriter::u16(uint16_t v) {
  u8(static_cast<uint8_t>(v));
  u8(static_cast<uint8_t>(v >> 8));
}

void UiWriter::u32(uint32_t v) {
  u16(static_cast<uint16_t>(v));
  u16(static_cast<uint16_t>(v >> 16));
}

void UiWriter::text(const char* s) {
  const size_t n = s ? strlen(s) : 0;
  if (n > 255) {
    ok_ = false;
    return;
  }
  u8(static_cast<uint8_t>(n));
  bytes(reinterpret_cast<const uint8_t*>(s), n);
}

void UiWriter::bytes(const uint8_t* p, size_t n) {
  if (!ok_ || n > cap_ - n_) {
    ok_ = false;
    return;
  }
  if (n) memcpy(buf_ + n_, p, n);
  n_ += n;
}

uint8_t UiReader::u8() {
  if (!ok_ || n_ + 1 > len_) {
    ok_ = false;
    return 0;
  }
  return buf_[n_++];
}

uint16_t UiReader::u16() {
  const uint16_t lo = u8();
  return static_cast<uint16_t>(lo | (u8() << 8));
}

uint32_t UiReader::u32() {
  const uint32_t lo = u16();
  return lo | (static_cast<uint32_t>(u16()) << 16);
}

bool UiReader::text(char* out, size_t max) {
  const uint8_t n = u8();
  if (!ok_ || n > max || n > len_ - n_) {
    ok_ = false;
    out[0] = '\0';
    return false;
  }
  memcpy(out, buf_ + n_, n);
  out[n] = '\0';
  n_ += n;
  return true;
}

size_t uiStatus(uint8_t* out, size_t cap, const UiStatus& s) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::Status));
  w.u16(s.flags);
  w.u8(s.domeMode);
  w.i16(s.domeAngle);
  w.u16(s.animationId);
  w.u8(static_cast<uint8_t>(s.servoSequence));
  w.u32(s.uptimeS);
  return w.size();
}

bool uiParseStatus(const uint8_t* data, size_t len, UiStatus& s) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::Status)) return false;
  s.flags = r.u16();
  s.domeMode = r.u8();
  s.domeAngle = r.i16();
  s.animationId = r.u16();
  s.servoSequence = static_cast<int8_t>(r.u8());
  s.uptimeS = r.u32();
  return r.done();
}

size_t uiSettingInfo(uint8_t* out, size_t cap, uint8_t index, uint8_t count, int32_t value,
                     int32_t def, int32_t min, int32_t max, bool overridden, const char* key) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::SettingInfo));
  w.u8(index);
  w.u8(count);
  w.i32(value);
  w.i32(def);
  w.i32(min);
  w.i32(max);
  w.u8(overridden ? 1 : 0);
  w.text(key);
  return w.size();
}

size_t uiSettingValue(uint8_t* out, size_t cap, uint8_t index, int32_t value, bool overridden) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::SettingValue));
  w.u8(index);
  w.i32(value);
  w.u8(overridden ? 1 : 0);
  return w.size();
}

size_t uiListEnd(uint8_t* out, size_t cap, UiType list, uint8_t count) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::ListEnd));
  w.u8(static_cast<uint8_t>(list));
  w.u8(count);
  return w.size();
}

size_t uiResult(uint8_t* out, size_t cap, UiType request, UiResult result) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::Result));
  w.u8(static_cast<uint8_t>(request));
  w.u8(static_cast<uint8_t>(result));
  return w.size();
}

size_t uiServoSequenceInfo(uint8_t* out, size_t cap, uint8_t index, uint8_t frames, uint32_t lengthMs,
                           const char* name) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::ServoSequenceInfo));
  w.u8(index);
  w.u8(frames);
  w.u32(lengthMs);
  w.text(name);
  return w.size();
}

size_t uiWifiInfo(uint8_t* out, size_t cap, bool custom, const char* name, const char* password) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::WifiInfo));
  w.u8(custom ? 1 : 0);
  w.text(name);
  w.text(password);
  return w.ok() ? w.size() : 0;
}

bool uiParseSetWifi(const uint8_t* data, size_t len, char* name, char* password) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::SetWifi)) return false;
  r.text(name, kWifiNameMax);
  r.text(password, kWifiPasswordMax);
  return r.done();
}

size_t uiNodeVersion(uint8_t* out, size_t cap, uint8_t node, const char* version, const char* build) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::NodeVersion));
  w.u8(node);
  w.text(version);
  w.text(build);
  return w.ok() ? w.size() : 0;
}

size_t uiOtaStatus(uint8_t* out, size_t cap, uint8_t node, uint8_t state, uint8_t percent, uint8_t error,
                   const char* message) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::OtaStatus));
  w.u8(node);
  w.u8(state);
  w.u8(percent);
  w.u8(error);
  w.text(message);
  return w.ok() ? w.size() : 0;
}

size_t uiSeqHeader(uint8_t* out, size_t cap, const CustomSequence& seq) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::SeqHeader));
  w.u8(seq.id);
  w.u8(seq.frameCount);
  w.text(seq.name);
  return w.ok() ? w.size() : 0;
}

size_t uiSeqFrame(uint8_t* out, size_t cap, uint8_t id, uint8_t index, const CustomFrame& f) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::SeqFrame));
  w.u8(id);
  w.u8(index);
  w.u16(f.ms);
  w.bytes(reinterpret_cast<const uint8_t*>(f.pos), kSeqServos);
  return w.ok() ? w.size() : 0;
}

size_t uiAnimHeader(uint8_t* out, size_t cap, const Animation& anim) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::AnimHeader));
  w.u16(anim.id);
  w.u8(anim.stepCount);
  w.text(anim.name);
  w.text(anim.icon);
  return w.size();
}

size_t uiAnimStep(uint8_t* out, size_t cap, uint16_t id, uint8_t index, const AnimStep& step) {
  UiWriter w(out, cap);
  w.u8(static_cast<uint8_t>(UiType::AnimStep));
  w.u16(id);
  w.u8(index);
  if (!w.ok()) return 0;
  const size_t n = encodeStep(step, w.tail(), w.room());
  if (n == 0) return 0;
  w.advance(n);
  return w.size();
}

bool AnimAssembler::header(const uint8_t* data, size_t len) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::AnimHeader)) return false;
  Animation a;
  a.id = r.u16();
  a.stepCount = r.u8();
  r.text(a.name, kAnimNameMax);
  r.text(a.icon, kAnimIconMax);
  if (!r.done() || a.stepCount > kMaxAnimSteps) return false;
  anim_ = a;
  haveHeader_ = true;
  got_ = 0;
  return true;
}

bool AnimAssembler::step(const uint8_t* data, size_t len) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::AnimStep)) return false;
  const uint16_t id = r.u16();
  const uint8_t index = r.u8();
  if (!r.ok() || !haveHeader_ || id != anim_.id || index >= anim_.stepCount) return false;
  AnimStep s;
  if (decodeStep(r.rest(), r.left(), s) != r.left()) return false;
  anim_.steps[index] = s;
  got_ |= 1ul << index;
  return true;
}

bool AnimAssembler::complete(uint16_t id) const {
  if (!haveHeader_ || id != anim_.id) return false;
  const uint32_t all = (1ul << anim_.stepCount) - 1;
  return (got_ & all) == all;
}

bool SeqAssembler::header(const uint8_t* data, size_t len) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::SeqHeader)) return false;
  CustomSequence q;
  q.id = r.u8();
  q.frameCount = r.u8();
  r.text(q.name, kSeqNameMax);
  if (!r.done() || q.frameCount == 0 || q.frameCount > kMaxSeqFrames) return false;
  seq_ = q;
  haveHeader_ = true;
  got_ = 0;
  return true;
}

bool SeqAssembler::frame(const uint8_t* data, size_t len) {
  UiReader r(data, len);
  if (r.u8() != static_cast<uint8_t>(UiType::SeqFrame)) return false;
  const uint8_t id = r.u8();
  const uint8_t index = r.u8();
  CustomFrame f;
  f.ms = r.u16();
  if (r.left() != kSeqServos || !haveHeader_ || id != seq_.id || index >= seq_.frameCount) return false;
  memcpy(f.pos, r.rest(), kSeqServos);
  seq_.frames[index] = f;
  got_ |= 1ull << index;
  return true;
}

bool SeqAssembler::complete(uint8_t id) const {
  if (!haveHeader_ || id != seq_.id) return false;
  const uint64_t all = (1ull << seq_.frameCount) - 1;
  return (got_ & all) == all;
}

}  // namespace droid
