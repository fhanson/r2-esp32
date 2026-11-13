#pragma once
// Pololu Maestro over serial, compact protocol (what the Mega's library sent
// to the same board). Only Set Target is used: the Maestro is a plain servo
// driver now, and the sequences run on this node.

#include <Arduino.h>

class Maestro {
 public:
  explicit Maestro(Stream& port) : port_(port) {}

  // Target in microseconds; the Maestro takes quarter-microseconds.
  void setTargetUs(uint8_t channel, uint16_t us) {
    const uint16_t q = static_cast<uint16_t>(us * 4u);
    const uint8_t cmd[4] = {0x84, channel, static_cast<uint8_t>(q & 0x7F), static_cast<uint8_t>((q >> 7) & 0x7F)};
    port_.write(cmd, sizeof(cmd));
  }

 private:
  Stream& port_;
};
