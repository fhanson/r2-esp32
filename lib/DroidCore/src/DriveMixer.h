#pragma once
// Differential (tank) drive mixing for two foot motors, after BigHappyDude's
// diamond mix.
//
// Inputs are normalised stick values (-1..+1, deadband already applied).
// Output is a speed per foot (-1..+1), then a servo pulse in microseconds.
// Foot direction is set per foot with invert flags, tuned on the bench with
// the wheels off the ground.

#include <stdint.h>

namespace droid {

struct MixerConfig {
  // KnightShade steering curve: x * sqrt(100x + F) / sqrt(100 + F).
  // Larger F = closer to linear.
  float steeringFactor = 100.0f;
  bool curveOnSteer = true;
  bool curveOnThrottle = false;

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

// The steering curve, normalised to -1..+1.
float steeringCurve(float x, float factor);

// throttle, steer: -1..+1 with deadband already applied (0 = centred).
// speedScale: 0..1 maximum speed for the current speed level.
FeetSpeed mixDrive(float throttle, float steer, float speedScale, const MixerConfig& cfg);

// Servo pulse for a foot speed, on the Arduino Servo scale: write(0) = 544 us,
// write(90) = 1472 us, write(180) = 2400 us, matching the VESCs' PPM calibration.
struct PulseRange {
  uint16_t minUs = 544;
  uint16_t maxUs = 2400;
};
uint16_t speedToPulseUs(float speed, const PulseRange& range);

// Arduino Servo write(angle) equivalent.
uint16_t servoAngleToPulseUs(int angle, const PulseRange& range);

}  // namespace droid
