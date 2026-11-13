#include "CustomSequences.h"

#include <string.h>

namespace droid {

bool isCustomSequenceId(size_t index) {
  return index >= kCustomSeqFirst && index < kCustomSeqFirst + kMaxCustomSeqs;
}

uint32_t customSequenceLengthMs(const CustomSequence& s) {
  uint32_t total = 0;
  for (size_t i = 0; i < s.frameCount && i < kMaxSeqFrames; ++i) total += s.frames[i].ms;
  return total;
}

bool validCustomSequence(const CustomSequence& s) {
  if (s.name[0] == '\0' || strlen(s.name) > kSeqNameMax) return false;
  if (s.frameCount == 0 || s.frameCount > kMaxSeqFrames) return false;
  for (size_t i = 0; i < s.frameCount; ++i) {
    const CustomFrame& f = s.frames[i];
    if (f.ms < kSeqFrameMinMs || f.ms > kSeqFrameMaxMs) return false;
    for (int8_t p : f.pos) {
      if (p != kSeqHold && (p < kSeqPosMin || p > kSeqPosMax)) return false;
    }
  }
  return true;
}

namespace {
int8_t toHundredths(int16_t thousandths) {
  if (thousandths == kHoldPosition) return kSeqHold;
  int32_t v = thousandths >= 0 ? (thousandths + 5) / 10 : (thousandths - 5) / 10;
  if (v < kSeqPosMin) v = kSeqPosMin;
  if (v > kSeqPosMax) v = kSeqPosMax;
  return static_cast<int8_t>(v);
}
}  // namespace

bool customFromBuiltIn(size_t index, CustomSequence& out) {
  const ServoSequence* q = sequenceAt(index);
  if (!q || q->frameCount == 0) return false;
  CustomSequence s;
  strncpy(s.name, q->name, kSeqNameMax);
  const bool arms = isArmSequence(index);
  const size_t n = q->frameCount < kMaxSeqFrames ? q->frameCount : kMaxSeqFrames;
  for (size_t i = 0; i < n; ++i) {
    CustomFrame& f = s.frames[i];
    const ServoFrame& src = q->frames[i];
    f.ms = src.durationMs < kSeqFrameMinMs ? kSeqFrameMinMs : src.durationMs;
    if (arms) {
      f.pos[11] = toHundredths(src.position[0]);
      f.pos[12] = toHundredths(src.position[1]);
    } else {
      for (size_t k = 0; k < kDomePanels; ++k) f.pos[k] = toHundredths(src.position[k]);
    }
  }
  s.frameCount = static_cast<uint8_t>(n);
  out = s;
  return true;
}

size_t encodeCustomSequence(const CustomSequence& s, uint8_t* out, size_t cap) {
  const size_t nameLen = strlen(s.name);
  const size_t need = 1 + 1 + nameLen + 1 + s.frameCount * (2 + kSeqServos);
  if (!validCustomSequence(s) || need > cap) return 0;
  size_t n = 0;
  out[n++] = s.id;
  out[n++] = static_cast<uint8_t>(nameLen);
  memcpy(out + n, s.name, nameLen);
  n += nameLen;
  out[n++] = s.frameCount;
  for (size_t i = 0; i < s.frameCount; ++i) {
    out[n++] = static_cast<uint8_t>(s.frames[i].ms);
    out[n++] = static_cast<uint8_t>(s.frames[i].ms >> 8);
    memcpy(out + n, s.frames[i].pos, kSeqServos);
    n += kSeqServos;
  }
  return n;
}

bool decodeCustomSequence(const uint8_t* data, size_t length, CustomSequence& out) {
  if (length < 3) return false;
  CustomSequence s;
  size_t n = 0;
  s.id = data[n++];
  const size_t nameLen = data[n++];
  if (nameLen > kSeqNameMax || n + nameLen + 1 > length) return false;
  memcpy(s.name, data + n, nameLen);
  n += nameLen;
  s.frameCount = data[n++];
  if (s.frameCount > kMaxSeqFrames || n + s.frameCount * (2 + kSeqServos) != length) return false;
  for (size_t i = 0; i < s.frameCount; ++i) {
    s.frames[i].ms = static_cast<uint16_t>(data[n] | (data[n + 1] << 8));
    n += 2;
    memcpy(s.frames[i].pos, data + n, kSeqServos);
    n += kSeqServos;
  }
  if (!isCustomSequenceId(s.id) || !validCustomSequence(s)) return false;
  out = s;
  return true;
}

bool CustomSequencePlayer::start(const CustomSequence& s, uint8_t fromFrame, uint32_t nowMs) {
  if (fromFrame >= s.frameCount) return false;
  seq_ = s;
  frame_ = fromFrame;
  frameAtMs_ = nowMs;
  running_ = true;
  return true;
}

bool CustomSequencePlayer::update(uint32_t nowMs, int8_t out[kSeqServos]) {
  if (!running_ || static_cast<int32_t>(nowMs - frameAtMs_) < 0) return false;
  if (frame_ >= seq_.frameCount) {
    running_ = false;  // the last frame's wait is over
    return false;
  }
  const CustomFrame& f = seq_.frames[frame_++];
  memcpy(out, f.pos, kSeqServos);
  frameAtMs_ += f.ms;
  return true;
}

}  // namespace droid
