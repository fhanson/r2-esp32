#include "DriveArming.h"

namespace droid {

bool DriveArming::update(uint32_t nowMs, bool linkOk, bool enableSwitchOn, bool sticksCentred) {
  if (!linkOk) {
    armed_ = false;
    centredTiming_ = false;
    return false;
  }

  if (!enableSwitchOn) {
    armed_ = false;
    needSwitchOff_ = false;
    centredTiming_ = false;
    return false;
  }

  if (armed_) return true;
  if (needSwitchOff_) return false;

  if (!sticksCentred) {
    centredTiming_ = false;
    return false;
  }
  if (!centredTiming_) {
    centredTiming_ = true;
    centredSinceMs_ = nowMs;
  }
  if (nowMs - centredSinceMs_ >= armHoldMs_) {
    armed_ = true;
    centredTiming_ = false;
  }
  return armed_;
}

}  // namespace droid
