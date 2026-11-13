#pragma once
// Decides whether the feet are allowed to move.
//
// Rules:
//   1. At power-up the drive-enable switch must be seen OFF before it can
//      arm. A switch left on cannot make the droid drive the moment it boots.
//   2. Arming also needs throttle and steering centred for armHoldMs.
//   3. Losing the radio link disarms at once.
//   4. When the link comes back with the switch still on, the drive re-arms
//      only after the sticks have been centred for armHoldMs. No switch
//      cycle is needed after a brief dropout.
//   5. Turning the switch off disarms.

#include <stdint.h>

namespace droid {

class DriveArming {
 public:
  explicit DriveArming(uint32_t armHoldMs = 300) : armHoldMs_(armHoldMs) {}

  // Call every loop. Returns true when the feet may move.
  bool update(uint32_t nowMs, bool linkOk, bool enableSwitchOn, bool sticksCentred);

  bool armed() const { return armed_; }
  bool waitingForSwitchOff() const { return needSwitchOff_; }

 private:
  uint32_t armHoldMs_;
  bool armed_ = false;
  bool needSwitchOff_ = true;
  bool centredTiming_ = false;
  uint32_t centredSinceMs_ = 0;
};

}  // namespace droid
