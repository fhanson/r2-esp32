#pragma once
// Minimal PCA9685 servo driver over I2C, 50 Hz.

#include <Arduino.h>
#include <Wire.h>

class Pca9685 {
 public:
  explicit Pca9685(uint8_t address) : address_(address) {}

  // False if the chip doesn't answer.
  bool begin() {
    Wire.beginTransmission(address_);
    if (Wire.endTransmission() != 0) return false;
    write(0x00, 0x10);  // MODE1: sleep, to set the prescaler
    write(0xFE, 121);   // 25 MHz / (4096 * 50 Hz) - 1
    write(0x00, 0x20);  // wake, auto-increment
    delay(1);
    write(0x00, 0xA0);  // restart
    ok_ = true;
    return true;
  }

  bool ok() const { return ok_; }

  void setUs(uint8_t channel, uint16_t us) {
    if (!ok_ || channel > 15) return;
    const uint16_t off = static_cast<uint16_t>(static_cast<uint32_t>(us) * 4096u / 20000u);
    Wire.beginTransmission(address_);
    Wire.write(0x06 + 4 * channel);
    Wire.write(0);
    Wire.write(0);
    Wire.write(off & 0xFF);
    Wire.write(off >> 8);
    Wire.endTransmission();
  }

  // Output off: the servo goes limp.
  void release(uint8_t channel) {
    if (!ok_ || channel > 15) return;
    Wire.beginTransmission(address_);
    Wire.write(0x06 + 4 * channel);
    Wire.write(0);
    Wire.write(0);
    Wire.write(0);
    Wire.write(0x10);
    Wire.endTransmission();
  }

 private:
  void write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address_);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  uint8_t address_;
  bool ok_ = false;
};
