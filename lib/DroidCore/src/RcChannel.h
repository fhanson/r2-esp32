#pragma once
// Turns raw SBUS channel values into something the droid can use.
//
// Every channel gets its own calibration (raw min / centre / max, invert),
// because the HotRC DS-650 does not follow SBUS conventions exactly (protoR2
// found CH1 reads inverted) and its buttons send whatever values they are
// programmed to. Read the real numbers off your own handset with the radio
// monitor (type 'r' in the serial console) and put them in the config.

#include <stdint.h>

namespace droid {

struct ChannelCal {
  uint16_t rawMin = 172;     // raw value at one end of travel
  uint16_t rawCenter = 992;  // raw value at rest (centre for a stick axis)
  uint16_t rawMax = 1811;    // raw value at the other end
  bool invert = false;       // flip the sign of the result
  float deadband = 0.0f;     // fraction of travel treated as zero (0..0.5)
};

// Proportional control (stick axis) -> -1.0 .. +1.0.
// Rest position maps to 0. Values past the calibrated ends are clamped.
// A control that rests at one end of its travel can set rawCenter = its rest
// value; the result then runs 0 .. +1 (or 0 .. -1).
float normalize(uint16_t raw, const ChannelCal& cal);

// Button configured as 2-position (momentary or latching) -> true/false.
// Set rawCenter to the button's "off" value and rawMin/rawMax to cover its
// "on" value; the result is true once the value is past halfway to "on".
bool decodeSwitch2(uint16_t raw, const ChannelCal& cal);

// Button configured as 3-position ("3 gears") -> 0, 1 or 2.
// Low third -> 0, middle -> 1, high third -> 2 (after invert).
uint8_t decodeSwitch3(uint16_t raw, const ChannelCal& cal);

}  // namespace droid
