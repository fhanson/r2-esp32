#pragma once
// Decides when the droid should make a sound, from the drive state, the
// sound button and auto mode.

#include <stdint.h>

namespace droid {

struct HcrTriggers {
  bool connected = false;  // radio link came up
  bool armed = false;      // drive armed
  bool disarmed = false;   // drive disarmed, for any reason
  bool soundPress = false; // sound button pressed
  bool autoSound = false;  // auto mode's random chatter is due
};

class HcrEvents {
 public:
  // A sound button press is ignored for soundGapMs after any sound. The other
  // triggers always fire, so a disarm warning is never swallowed.
  explicit HcrEvents(uint32_t soundGapMs) : soundGapMs_(soundGapMs) {}

  void setSoundGapMs(uint32_t soundGapMs) { soundGapMs_ = soundGapMs; }

  // Auto mode (CH5 in auto, like the Mega's automation): a random sound after
  // a random pause in this range. Like the button, it waits for the sound gap.
  void setAutoDelayMs(uint32_t minMs, uint32_t maxMs);
  void seed(uint32_t value) { rng_ = value != 0 ? value : 1; }

  // Call once per loop. Each trigger is true for one call only. autoMode only
  // counts while the link is up.
  HcrTriggers update(uint32_t nowMs, bool link, bool armed, bool soundButton,
                     bool autoMode = false);

 private:
  uint32_t soundGapMs_;
  uint32_t lastSoundMs_ = 0;
  bool prevLink_ = false;
  bool prevArmed_ = false;
  // The button must be seen off on a live link before a press counts, so a
  // button held through boot or a dropout doesn't fire by itself.
  bool buttonSeenOff_ = false;

  uint32_t nextRandomDelay();

  uint32_t autoMinMs_ = 5000;
  uint32_t autoMaxMs_ = 12000;
  bool autoActive_ = false;
  uint32_t nextAutoMs_ = 0;
  uint32_t rng_ = 0x9E3779B9u;
};

}  // namespace droid
