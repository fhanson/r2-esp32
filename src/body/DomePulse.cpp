#include "DomePulse.h"

// Arduino-ESP32 core 2.0.x LEDC API, as in FeetPwm.cpp.

void DomePulse::begin(uint16_t neutralUs) {
  ledcSetup(ch_, freqHz_, bits_);
  ledcWrite(ch_, dutyFor(neutralUs));  // duty first, so the first pulse is neutral
  ledcAttachPin(pin_, ch_);
  us_ = neutralUs;
}

void DomePulse::writeUs(uint16_t us) {
  if (us == us_) return;
  ledcWrite(ch_, dutyFor(us));
  us_ = us;
}

uint32_t DomePulse::dutyFor(uint16_t us) const {
  const uint32_t periodUs = 1000000UL / freqHz_;
  const uint32_t maxDuty = (1UL << bits_) - 1;
  return (static_cast<uint64_t>(us) * maxDuty) / periodUs;
}
