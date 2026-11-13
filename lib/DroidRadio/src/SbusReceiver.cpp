#include "SbusReceiver.h"

constexpr uint32_t SbusReceiver::kBauds[2];

void SbusReceiver::begin() {
  // Inverted 8E2, receive only. The ESP32 inverts in hardware, so no
  // inverter board is needed.
  port_.begin(kBauds[baudIdx_], SERIAL_8E2, rxPin_, -1, true);
  baudSinceMs_ = millis();
}

void SbusReceiver::poll() {
  while (port_.available() > 0) {
    const uint8_t b = static_cast<uint8_t>(port_.read());
    if (!parser_.feed(b, micros())) continue;

    const droid::SbusFrame& f = parser_.frame();
    const uint32_t now = millis();
    lastValidMs_ = now;
    lastFailsafe_ = f.failsafe;
    if (f.lostFrame || f.failsafe) {
      // Values in a flagged frame can look plausible but be stale. Ignore
      // them; the link timeout decides when to stop.
      ++flagged_;
      continue;
    }
    lastGood_ = f;
    haveGood_ = true;
    lastGoodMs_ = now;
  }

  // Auto-baud: if nothing decodes for a while, try the other rate.
  const uint32_t now = millis();
  if ((now - lastValidMs_) > autoBaudSwitchMs_ && (now - baudSinceMs_) > autoBaudSwitchMs_) {
    baudIdx_ ^= 1;
    port_.updateBaudRate(kBauds[baudIdx_]);
    parser_.reset();
    baudSinceMs_ = now;
  }
}

bool SbusReceiver::linkOk(uint32_t nowMs) const {
  if (!haveGood_ || lastFailsafe_) return false;
  return (nowMs - lastGoodMs_) < linkTimeoutMs_;
}
