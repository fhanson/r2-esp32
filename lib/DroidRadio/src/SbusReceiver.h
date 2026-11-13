#pragma once
// SBUS on an ESP32 UART: inverted 8E2, with auto-baud between the standard
// 100000 and the ~115200 the HotRC SBUS-A has been measured at.

#include <Arduino.h>
#include "SbusParser.h"

class SbusReceiver {
 public:
  SbusReceiver(HardwareSerial& port, int rxPin, uint32_t linkTimeoutMs, uint32_t autoBaudSwitchMs)
      : port_(port), rxPin_(rxPin), linkTimeoutMs_(linkTimeoutMs), autoBaudSwitchMs_(autoBaudSwitchMs) {}

  void begin();

  // Call often (every loop). Reads all waiting bytes.
  void poll();

  // True while good frames keep arriving and the receiver is not in failsafe.
  // A good frame is valid and has neither the lost-frame nor failsafe flag.
  bool linkOk(uint32_t nowMs) const;

  // Last good frame. Only meaningful while linkOk().
  const droid::SbusFrame& frame() const { return lastGood_; }

  uint32_t baud() const { return kBauds[baudIdx_]; }
  uint32_t goodFrames() const { return parser_.goodFrames(); }
  uint32_t badFrames() const { return parser_.badFrames(); }
  uint32_t flaggedFrames() const { return flagged_; }
  bool lastFailsafe() const { return lastFailsafe_; }

 private:
  static constexpr uint32_t kBauds[2] = {100000, 115200};

  HardwareSerial& port_;
  int rxPin_;
  uint32_t linkTimeoutMs_;
  uint32_t autoBaudSwitchMs_;

  droid::SbusParser parser_;
  droid::SbusFrame lastGood_;
  bool haveGood_ = false;
  uint32_t lastGoodMs_ = 0;
  uint32_t lastValidMs_ = 0;  // any decodable frame, used for auto-baud
  bool lastFailsafe_ = false;
  uint32_t flagged_ = 0;
  uint8_t baudIdx_ = 0;
  uint32_t baudSinceMs_ = 0;
};
