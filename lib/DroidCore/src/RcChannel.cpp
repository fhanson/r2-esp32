#include "RcChannel.h"

namespace droid {

namespace {
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
}  // namespace

float normalize(uint16_t raw, const ChannelCal& cal) {
  const float r = static_cast<float>(raw);
  const float c = static_cast<float>(cal.rawCenter);
  const float lo = static_cast<float>(cal.rawMin);
  const float hi = static_cast<float>(cal.rawMax);

  float v = 0.0f;
  if (r >= c) {
    const float span = hi - c;
    v = span > 0.5f ? (r - c) / span : 0.0f;
  } else {
    const float span = c - lo;
    v = span > 0.5f ? (r - c) / span : 0.0f;
  }
  v = clampf(v, -1.0f, 1.0f);

  if (cal.deadband > 0.0f) {
    const float db = clampf(cal.deadband, 0.0f, 0.5f);
    const float mag = v < 0 ? -v : v;
    if (mag <= db) {
      v = 0.0f;
    } else {
      // Rescale so output still reaches 1.0 at full travel, with no jump.
      const float scaled = (mag - db) / (1.0f - db);
      v = v < 0 ? -scaled : scaled;
    }
  }
  return cal.invert ? -v : v;
}

bool decodeSwitch2(uint16_t raw, const ChannelCal& cal) {
  ChannelCal c = cal;
  c.deadband = 0.0f;
  const float v = normalize(raw, c);
  return (v < 0 ? -v : v) > 0.5f;
}

uint8_t decodeSwitch3(uint16_t raw, const ChannelCal& cal) {
  ChannelCal c = cal;
  c.deadband = 0.0f;
  const float v = normalize(raw, c);  // -1 .. +1, centre at rawCenter
  if (v < -0.33f) return 0;
  if (v > 0.33f) return 2;
  return 1;
}

}  // namespace droid
