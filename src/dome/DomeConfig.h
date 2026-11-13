#pragma once
// Dome C3 pins and fixed values. Settings that can change from the UI live
// on the S3 (BodyConfig.h); the S3 sends this node its servo endpoints.
//
// Pins are for an ESP32-C3 Super Mini. Check them against the board before
// wiring: GPIO 2, 8 and 9 are strapping pins, 18/19 are USB, and GPIO21 is
// UART0's boot-message TX, so nothing listens there.

#include <stdint.h>

#include "RcChannel.h"
#include "ServoSequences.h"

namespace dome {

// Shared 9600 baud line to the Maestro and the WReactor32. TX only.
constexpr int kPinDomeSerialTx = 7;
constexpr uint32_t kDomeSerialBaud = 9600;

// DS-650 #2 (left hand) SBUS, inverted 8E2 on UART1 RX.
constexpr int kPinSbusRx = 6;
constexpr uint32_t kSbusLinkTimeoutMs = 100;
constexpr uint32_t kSbusAutoBaudMs = 1000;

// PCA9685 for the holo servos.
constexpr int kPinSda = 4;
constexpr int kPinScl = 5;
constexpr uint8_t kPcaAddress = 0x40;
// Channels: front X/Y, top X/Y, rear X/Y.
constexpr uint8_t kHoloServoChannel[6] = {0, 1, 2, 3, 4, 5};

// Holo LED jewels through the Pixel Shifter: front, top, rear chained on one
// data line, 7 LEDs each.
constexpr int kPinHoloLeds = 10;
constexpr uint8_t kLedsPerHolo = 7;
constexpr uint8_t kHoloBrightness = 120;  // 0-255; full white on 21 LEDs is about 1.3 A

// Left stick X on the left-hand radio spins the dome (1-based channel).
constexpr int kChDomeSpin = 1;
inline droid::ChannelCal domeSpinCal() {
  droid::ChannelCal c;
  c.deadband = 0.12f;  // Padawan360 DOMEDEADZONERANGE 15/128
  return c;
}
constexpr uint32_t kSpinSendMs = 50;   // to the S3 while turning (its hold is 250 ms)

// Endpoints used until the S3 sends its own: the Maestro's, from its
// settings file (tools/maestro/), and the holos centred.
constexpr droid::ServoEndpoints kDefaultPanelEnds[droid::kDomePanels] = {
    {992, 992, 2000, 2000}, {992, 992, 2000, 2000}, {608, 608, 2000, 2000}, {608, 608, 2000, 2000},
    {608, 608, 2000, 2000}, {608, 608, 2000, 2000}, {608, 608, 2000, 2000}, {608, 608, 2000, 2304},
    {992, 992, 2000, 2000}, {608, 608, 1504, 1504}, {608, 608, 1504, 1504},
};
constexpr droid::ServoEndpoints kDefaultHoloEnds = {1000, 1500, 1500, 2000};

constexpr uint32_t kHoloUpdateMs = 20;

}  // namespace dome
