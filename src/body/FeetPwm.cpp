#include "FeetPwm.h"

// Arduino-ESP32 core 2.0.x LEDC API (ledcSetup / ledcAttachPin / ledcWrite).
// Core 3.x renamed these (ledcAttach); update here if the core moves.

void FeetPwm::begin(uint16_t neutralUs) {
  ledcSetup(leftCh_, freqHz_, bits_);
  ledcSetup(rightCh_, freqHz_, bits_);
  // Set the duty before attaching, so the first pulse out is neutral.
  ledcWrite(leftCh_, dutyFor(neutralUs));
  ledcWrite(rightCh_, dutyFor(neutralUs));
  ledcAttachPin(leftPin_, leftCh_);
  ledcAttachPin(rightPin_, rightCh_);
  leftUs_ = rightUs_ = neutralUs;
}

void FeetPwm::writeUs(uint16_t leftUs, uint16_t rightUs) {
  if (leftUs != leftUs_) {
    ledcWrite(leftCh_, dutyFor(leftUs));
    leftUs_ = leftUs;
  }
  if (rightUs != rightUs_) {
    ledcWrite(rightCh_, dutyFor(rightUs));
    rightUs_ = rightUs;
  }
}

uint32_t FeetPwm::dutyFor(uint16_t us) const {
  const uint32_t periodUs = 1000000UL / freqHz_;
  const uint32_t maxDuty = (1UL << bits_) - 1;
  return (static_cast<uint64_t>(us) * maxDuty) / periodUs;
}
