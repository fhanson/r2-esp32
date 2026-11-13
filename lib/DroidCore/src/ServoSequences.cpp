#include "ServoSequences.h"

#include <string.h>

namespace droid {

size_t allSequenceCount() { return kServoSequenceCount + kArmSequenceCount; }

const ServoSequence* sequenceAt(size_t index) {
  if (index < kServoSequenceCount) return &kServoSequences[index];
  if (index < allSequenceCount()) return &kArmSequences[index - kServoSequenceCount];
  return nullptr;
}

bool isArmSequence(size_t index) { return index >= kServoSequenceCount && index < allSequenceCount(); }

int servoSequenceIndex(const char* name) {
  for (size_t i = 0; i < allSequenceCount(); ++i) {
    if (strcmp(sequenceAt(i)->name, name) == 0) return static_cast<int>(i);
  }
  return -1;
}

uint32_t servoSequenceLengthMs(const ServoSequence& seq) {
  uint32_t total = 0;
  for (size_t i = 0; i < seq.frameCount; ++i) total += seq.frames[i].durationMs;
  return total;
}

uint16_t positionToUs(int16_t thousandths, const ServoEndpoints& ends) {
  const int32_t span = static_cast<int32_t>(ends.openUs) - ends.closedUs;
  // Round to the nearest microsecond, either side of zero.
  const int32_t scaled = span * thousandths;
  const int32_t offset = scaled >= 0 ? (scaled + 500) / 1000 : (scaled - 500) / 1000;
  int32_t us = ends.closedUs + offset;
  const int32_t lo = ends.minUs < ends.maxUs ? ends.minUs : ends.maxUs;
  const int32_t hi = ends.minUs < ends.maxUs ? ends.maxUs : ends.minUs;
  if (us < lo) us = lo;
  if (us > hi) us = hi;
  return static_cast<uint16_t>(us);
}

bool ServoSequencePlayer::start(uint8_t index, uint32_t nowMs) {
  const ServoSequence* seq = sequenceAt(index);
  if (!seq || seq->frameCount == 0) return false;
  index_ = index;
  frame_ = 0;
  frameAtMs_ = nowMs;
  running_ = true;
  return true;
}

bool ServoSequencePlayer::update(uint32_t nowMs, int16_t out[kDomePanels]) {
  if (!running_) return false;
  const ServoSequence& seq = *sequenceAt(index_);
  if (static_cast<int32_t>(nowMs - frameAtMs_) < 0) return false;
  if (frame_ >= seq.frameCount) {
    running_ = false;  // the last frame's wait is over
    return false;
  }
  const ServoFrame& f = seq.frames[frame_];
  memcpy(out, f.position, sizeof(f.position));
  frameAtMs_ += f.durationMs;
  ++frame_;
  return true;
}

}  // namespace droid
