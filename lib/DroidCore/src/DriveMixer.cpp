#include "DriveMixer.h"

#include <math.h>

namespace droid {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float absf(float v) { return v < 0 ? -v : v; }
}  // namespace

float steeringCurve(float x, float factor) {
  const float m = clampf(absf(x), 0.0f, 1.0f);
  if (factor < 0.0f) factor = 0.0f;
  // Padawan360 works on a 0..100 scale: |X| * sqrt(|X| + F) / (100 * sqrt(100 + F)).
  const float out = m * sqrtf(100.0f * m + factor) / sqrtf(100.0f + factor);
  return x < 0 ? -out : out;
}

FeetSpeed mixDrive(float throttle, float steer, float speedScale, const MixerConfig& cfg) {
  FeetSpeed out;
  throttle = clampf(throttle, -1.0f, 1.0f);
  steer = clampf(steer, -1.0f, 1.0f);
  speedScale = clampf(speedScale, 0.0f, 1.0f);

  if (throttle == 0.0f && (steer == 0.0f || !cfg.spinInPlace)) return out;  // stopped

  if (cfg.curveOnThrottle) throttle = steeringCurve(throttle, cfg.steeringFactor);
  if (cfg.curveOnSteer) steer = steeringCurve(steer, cfg.steeringFactor);

  // Pull the point back inside the diamond |t| + |s| <= 1 along its ray from
  // the centre. This is what BigHappyDude's line intersections compute.
  const float k = absf(throttle) + absf(steer);
  if (k > 1.0f) {
    throttle /= k;
    steer /= k;
  }

  float left = throttle + steer;
  float right = throttle - steer;

  left = clampf(left, -1.0f, 1.0f) * speedScale;
  right = clampf(right, -1.0f, 1.0f) * speedScale;

  if (cfg.invertLeft) left = -left;
  if (cfg.invertRight) right = -right;
  if (cfg.swapFeet) {
    const float t = left;
    left = right;
    right = t;
  }
  out.left = left;
  out.right = right;
  return out;
}

uint16_t speedToPulseUs(float speed, const PulseRange& r) {
  speed = clampf(speed, -1.0f, 1.0f);
  const float mid = (r.minUs + r.maxUs) * 0.5f;
  const float half = (r.maxUs - r.minUs) * 0.5f;
  return static_cast<uint16_t>(lroundf(mid + speed * half));
}

uint16_t servoAngleToPulseUs(int angle, const PulseRange& r) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  // Arduino Servo: map(angle, 0, 180, min, max) with integer maths.
  return static_cast<uint16_t>(r.minUs + (static_cast<long>(angle) * (r.maxUs - r.minUs)) / 180);
}

}  // namespace droid
