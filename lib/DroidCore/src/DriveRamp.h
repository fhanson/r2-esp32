#pragma once
// Drive smoothing: throttle curve plus ramping, applied to the stick values
// BEFORE they are mixed into left and right feet, so both feet stay in step.
//
// Ramp times are "seconds from rest to full stick" (accel) and "seconds from
// full stick to rest" (decel). Reversing direction decelerates to zero at the
// decel rate first, then accelerates the other way.
//
// Safety stops (failsafe, disarm) must not go through the ramp: call reset()
// and send neutral straight away.

#include <stdint.h>

namespace droid {

struct RampConfig {
  float throttleAccelS = 0.6f;  // rest -> full throttle
  float throttleDecelS = 0.3f;  // full throttle -> rest (also used through reversal)
  float steerAccelS = 0.3f;     // centre -> full steering
  float steerDecelS = 0.15f;    // full steering -> centre
  float speedChangeS = 0.5f;    // time to move between speed levels' max speed
  // Throttle curve, 0 = linear, 1 = full cubic. Softens the first part of the
  // stick for crawling speeds: out = (1 - e) * x + e * x^3.
  float throttleExpo = 0.3f;
  // Longest time step accepted in one update. After a stall the ramp resumes
  // smoothly instead of jumping.
  float maxStepS = 0.05f;
};

// The throttle curve on its own, -1..+1 in and out.
float throttleCurve(float x, float expo);

// Move `current` toward `target` with separate rates for moving away from
// zero (accelPerS) and toward zero (decelPerS). Crossing zero goes through
// zero at the decel rate first.
float rampToward(float current, float target, float accelPerS, float decelPerS, float dtS);

class DriveRamp {
 public:
  explicit DriveRamp(const RampConfig& cfg = RampConfig{}) : cfg_(cfg) {}

  void setConfig(const RampConfig& cfg) { cfg_ = cfg; }
  const RampConfig& config() const { return cfg_; }

  // Inputs: raw stick values (-1..+1, deadband already applied) and the
  // target speed scale for the current speed level. dtS = seconds since the
  // last update. Throttle curve is applied here.
  void update(float throttle, float steer, float speedScale, float dtS);

  float throttle() const { return throttle_; }
  float steer() const { return steer_; }
  float speedScale() const { return speedScale_; }

  // Immediate stop: everything back to zero, no ramp. speedScale snaps to
  // the given value so the next drive starts at the selected level.
  void reset(float speedScale);

 private:
  static float ratePerS(float seconds);

  RampConfig cfg_;
  float throttle_ = 0.0f;
  float steer_ = 0.0f;
  float speedScale_ = 0.0f;
  bool haveScale_ = false;
};

}  // namespace droid
