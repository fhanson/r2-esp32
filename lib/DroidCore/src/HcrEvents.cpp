#include "HcrEvents.h"

namespace droid {

void HcrEvents::setAutoDelayMs(uint32_t minMs, uint32_t maxMs) {
  autoMinMs_ = minMs < maxMs ? minMs : maxMs;
  autoMaxMs_ = minMs < maxMs ? maxMs : minMs;
}

uint32_t HcrEvents::nextRandomDelay() {
  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  const uint32_t span = autoMaxMs_ - autoMinMs_;
  return autoMinMs_ + (span > 0 ? rng_ % (span + 1) : 0);
}

HcrTriggers HcrEvents::update(uint32_t nowMs, bool link, bool armed, bool soundButton, bool autoMode) {
  HcrTriggers t;
  t.connected = link && !prevLink_;
  t.armed = armed && !prevArmed_;
  t.disarmed = !armed && prevArmed_;
  const bool gapOver = (nowMs - lastSoundMs_) >= soundGapMs_;
  t.soundPress = link && soundButton && buttonSeenOff_ && gapOver;

  // Auto chatter: the first sound comes one pause after auto is selected, not
  // straight away. When it's due but another sound played recently, it waits
  // for the gap instead of being skipped.
  const bool autoOn = link && autoMode;
  if (autoOn && !autoActive_) nextAutoMs_ = nowMs + nextRandomDelay();
  autoActive_ = autoOn;
  if (autoOn && !t.soundPress && gapOver && static_cast<int32_t>(nowMs - nextAutoMs_) >= 0) {
    t.autoSound = true;
    nextAutoMs_ = nowMs + nextRandomDelay();
  }

  if (t.connected || t.armed || t.disarmed || t.soundPress || t.autoSound) {
    lastSoundMs_ = nowMs;
  }

  buttonSeenOff_ = link && !soundButton;
  prevLink_ = link;
  prevArmed_ = armed;
  return t;
}

}  // namespace droid
