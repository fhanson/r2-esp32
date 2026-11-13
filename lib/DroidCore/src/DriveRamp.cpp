#include "DriveRamp.h"

namespace droid {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float absf(float v) { return v < 0 ? -v : v; }
}  // namespace

float throttleCurve(float x, float expo) {
  x = clampf(x, -1.0f, 1.0f);
  expo = clampf(expo, 0.0f, 1.0f);
  return (1.0f - expo) * x + expo * x * x * x;
}

float rampToward(float current, float target, float accelPerS, float decelPerS, float dtS) {
  if (dtS <= 0.0f) return current;
  const float accelStep = accelPerS * dtS;
  const float decelStep = decelPerS * dtS;

  const bool crossing = (current > 0.0f && target < 0.0f) || (current < 0.0f && target > 0.0f);
  if (crossing) {
    // Slow down to zero first; never jump through it in one step.
    if (absf(current) <= decelStep) return 0.0f;
    return current > 0.0f ? current - decelStep : current + decelStep;
  }

  const float diff = target - current;
  // Moving away from zero = accelerating; toward zero = decelerating.
  const bool accelerating = absf(target) > absf(current);
  const float step = accelerating ? accelStep : decelStep;
  if (absf(diff) <= step) return target;
  return diff > 0.0f ? current + step : current - step;
}

float DriveRamp::ratePerS(float seconds) {
  // A full-scale move (0 -> 1) takes `seconds`. Zero or negative = instant.
  return seconds > 0.0001f ? 1.0f / seconds : 1.0e6f;
}

void DriveRamp::update(float throttle, float steer, float speedScale, float dtS) {
  dtS = clampf(dtS, 0.0f, cfg_.maxStepS);
  throttle = throttleCurve(throttle, cfg_.throttleExpo);
  steer = clampf(steer, -1.0f, 1.0f);
  speedScale = clampf(speedScale, 0.0f, 1.0f);

  throttle_ = rampToward(throttle_, throttle, ratePerS(cfg_.throttleAccelS), ratePerS(cfg_.throttleDecelS), dtS);
  steer_ = rampToward(steer_, steer, ratePerS(cfg_.steerAccelS), ratePerS(cfg_.steerDecelS), dtS);

  if (!haveScale_) {
    speedScale_ = speedScale;
    haveScale_ = true;
  } else {
    const float r = ratePerS(cfg_.speedChangeS);
    speedScale_ = rampToward(speedScale_, speedScale, r, r, dtS);
  }
}

void DriveRamp::reset(float speedScale) {
  throttle_ = 0.0f;
  steer_ = 0.0f;
  speedScale_ = clampf(speedScale, 0.0f, 1.0f);
  haveScale_ = true;
}

}  // namespace droid
