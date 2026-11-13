#pragma once
// RC pulse to the Roam-a-Dome's Pulse in, from one LEDC channel.
//
// As with the feet, the LEDC keeps repeating the last pulse on its own. If
// the loop stalls, the watchdog resets the chip, the pin goes quiet, and the
// Roam-a-Dome stops the dome when it sees no pulse ("No PWM Input").

#include <Arduino.h>

class DomePulse {
 public:
  DomePulse(int pin, uint8_t channel, uint32_t freqHz, uint8_t resolutionBits)
      : pin_(pin), ch_(channel), freqHz_(freqHz), bits_(resolutionBits) {}

  // Starts the output at neutralUs straight away.
  void begin(uint16_t neutralUs);
  void writeUs(uint16_t us);
  uint16_t us() const { return us_; }

 private:
  uint32_t dutyFor(uint16_t us) const;

  int pin_;
  uint8_t ch_;
  uint32_t freqHz_;
  uint8_t bits_;
  uint16_t us_ = 0;
};
