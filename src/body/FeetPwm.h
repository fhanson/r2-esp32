#pragma once
// RC servo pulses to the two VESCs, from the ESP32's LEDC hardware.
//
// The LEDC keeps sending the last pulse on its own, even if the main loop
// stops. That is why main.cpp runs the task watchdog: a stalled loop resets
// the chip, the pins go quiet during the reset, and the VESCs' own PPM
// timeout (set in VESC Tool) stops the motors.

#include <Arduino.h>

class FeetPwm {
 public:
  FeetPwm(int leftPin, int rightPin, uint8_t leftChannel, uint8_t rightChannel, uint32_t freqHz,
          uint8_t resolutionBits)
      : leftPin_(leftPin), rightPin_(rightPin), leftCh_(leftChannel), rightCh_(rightChannel),
        freqHz_(freqHz), bits_(resolutionBits) {}

  // Starts both outputs at neutralUs straight away.
  void begin(uint16_t neutralUs);

  void writeUs(uint16_t leftUs, uint16_t rightUs);

  uint16_t leftUs() const { return leftUs_; }
  uint16_t rightUs() const { return rightUs_; }

 private:
  uint32_t dutyFor(uint16_t us) const;

  int leftPin_, rightPin_;
  uint8_t leftCh_, rightCh_;
  uint32_t freqHz_;
  uint8_t bits_;
  uint16_t leftUs_ = 0, rightUs_ = 0;
};
