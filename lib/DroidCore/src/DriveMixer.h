#pragma once
// Differential (tank) drive mixing for two foot motors, after BigHappyDude's
// diamond mix in Padawan360.
//
// Inputs are normalised stick values (-1..+1, deadband already applied).
// Output is a speed per foot (-1..+1), then a servo pulse in microseconds.
//
// Differences from the Padawan360 PWM path (padawan360_body_mega_i2c.ino):
//   - The Mega called mixBHD(throttle, turn) into parameters named
//     (stickX, stickY), so its steering curve was applied to throttle and
//     the feet stopped whenever throttle was centred (no spin in place).
//     Here throttle and steering are explicit. Both old behaviours are
//     available as options so the droid can feel the same as before.
//   - Foot direction is set per foot with invert flags, tuned on the bench
//     with the wheels off the ground.

#include <stdint.h>

namespace droid {

struct MixerConfig {
  // Padawan360's KnightShade steering curve: x * sqrt(100x + F) / sqrt(100 + F).
  // Larger F = closer to linear. Padawan360 used F = 100. Set curveOnSteer
  // false and curveOnThrottle true to reproduce the Mega exactly.
  float steeringFactor = 100.0f;
  bool curveOnSteer = true;
  bool curveOnThrottle = false;

  // Padawan360's PWM path stopped the feet whenever throttle was centred.
  // true = allow turning on the spot with the stick alone.
  bool spinInPlace = false;

  bool invertLeft = false;
  bool invertRight = false;
  bool swapFeet = false;
};

struct FeetSpeed {
  float left = 0.0f;   // -1 (full reverse) .. +1 (full forward)
  float right = 0.0f;
};

// The curve used by Padawan360, normalised to -1..+1.
float steeringCurve(float x, float factor);

// throttle, steer: -1..+1 with deadband already applied (0 = centred).
// speedScale: 0..1 maximum speed for the current speed level.
FeetSpeed mixDrive(float throttle, float steer, float speedScale, const MixerConfig& cfg);

// Servo pulse for a foot speed, matching the Arduino Servo library the Mega
// used: write(0) = 544 us, write(90) = 1472 us, write(180) = 2400 us. The
// VESCs' PPM input was calibrated against those pulses, so keeping them
// identical means no VESC Tool changes at cutover.
struct PulseRange {
  uint16_t minUs = 544;
  uint16_t maxUs = 2400;
};
uint16_t speedToPulseUs(float speed, const PulseRange& range);

// Arduino Servo write(angle) equivalent, for checking against the Mega.
uint16_t servoAngleToPulseUs(int angle, const PulseRange& range);

}  // namespace droid
