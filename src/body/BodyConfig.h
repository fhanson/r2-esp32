#pragma once
// Body (ESP32-S3) settings: everything likely to need tuning on the bench.

#include <stdint.h>
#include "DomeControl.h"
#include "RcChannel.h"
#include "DriveMixer.h"
#include "DriveRamp.h"
#include "Settings.h"

namespace body {

// ---------------------------------------------------------------------------
// Pins (ESP32-S3 N16R8). Avoid: GPIO 0, 3, 45, 46 (strapping), 19, 20 (USB),
// 26-32 (flash), 33-37 (octal PSRAM on R8 modules).
// ---------------------------------------------------------------------------
// Grouped by device in the Freenove breakout's header order, so each device's
// wires sit side by side.
constexpr int kPinSbusRx = 4;       // DS-650 #1 (right hand) SBUS-A receiver signal
// 5, 6: CAN Pal (below). 7, 15, 16: spare.
constexpr int kPinHcrTx = 17;       // to Teensy pin 28 (RX7), UART2
constexpr int kPinHcrRx = 18;       // from Teensy pin 29 (TX7), UART2
// 8: spare. 3, 46: strapping pins, leave empty.
constexpr int kPinLeftFoot = 9;     // left VESC PPM input
constexpr int kPinRightFoot = 10;   // right VESC PPM input
// 11-14: spare (dome lifter later).
//
// Roam-a-Dome, on the right header. 42, 41, 40 are the JTAG pads, free
// because JTAG runs over USB. Avoid 43 (boot log TX) and 48 (RGB LED).
constexpr int kPinDomePwm = 42;     // Roam-a-Dome Pulse in: spin speed (#DPPWMIN1)
constexpr int kPinRdhTx = 41;       // to Roam-a-Dome command serial Rx (a G V Rx row), UART1 TX
constexpr int kPinRdhRx = 40;       // from Roam-a-Dome command serial TX, UART1 RX

// CAN Pal, not used yet:
constexpr int kPinCanTx = 5;        // to CAN Pal TX (TWAI)
constexpr int kPinCanRx = 6;        // from CAN Pal RX (TWAI)

// SBUS on UART0: it's receive-only, and the boot log only transmits on UART0.
constexpr int kUartSbus = 0;

// ---------------------------------------------------------------------------
// Radio: HotRC DS-650 #1 (right hand). Channel numbers are 1-based CH1..CH6.
// Read real values with the radio monitor ('r' in the serial console) and
// replace these defaults. Standard SBUS is 172 / 992 / 1811.
// ---------------------------------------------------------------------------
constexpr int kChSteer = 1;     // stick left/right
constexpr int kChThrottle = 2;  // stick forward/back (pull back = reverse)
constexpr int kChSpeed = 3;     // button set to 3-position: speed 1 / 2 / 3
constexpr int kChEnable = 4;    // button set to latching: drive enable
constexpr int kChDomeMode = 5;  // button set to 3-position: dome manual / auto / home
constexpr int kChSound = 6;     // button set to momentary: random sound

// CH1 reads inverted on the DS-650 (protoR2 bench notes), hence invert = true.
// Check the sign on the bench: stick right should give a positive value.
inline droid::ChannelCal steerCal() {
  droid::ChannelCal c;
  c.invert = true;
  c.deadband = 0.15f;
  return c;
}

// CH2 is the stick's forward/back axis: push forward to drive, pull back to
// reverse, centred at rest. Forward should read positive; flip invert if not.
inline droid::ChannelCal throttleCal() {
  droid::ChannelCal c;
  c.deadband = 0.15f;
  return c;
}

inline droid::ChannelCal speedCal() { return droid::ChannelCal{}; }

// Low third = manual, middle = auto, high third = home. Set invert if the
// button's positions come out the other way round.
inline droid::ChannelCal domeModeCal() { return droid::ChannelCal{}; }

// Enable button: rawCenter = its "off" value. Defaults assume off = 172.
inline droid::ChannelCal enableCal() {
  droid::ChannelCal c;
  c.rawMin = 172;
  c.rawCenter = 172;
  c.rawMax = 1811;
  return c;
}

// Sound button: rawCenter = its "off" value. Defaults assume off = 172.
inline droid::ChannelCal soundCal() {
  droid::ChannelCal c;
  c.rawMin = 172;
  c.rawCenter = 172;
  c.rawMax = 1811;
  return c;
}

// ---------------------------------------------------------------------------
// Link and safety
// ---------------------------------------------------------------------------
constexpr uint32_t kLinkTimeoutMs = 200;  // no good frame for this long = stop
constexpr uint32_t kArmHoldMs = 300;      // sticks centred this long to arm
constexpr uint32_t kWatchdogSeconds = 1;  // loop stall -> chip reset -> PWM stops
constexpr uint32_t kAutoBaudSwitchMs = 500;

// ---------------------------------------------------------------------------
// Drive
// ---------------------------------------------------------------------------
// Speed levels 1 / 2 / 3 as a fraction of full.
constexpr float kSpeedLevels[3] = {50.0f / 127.0f, 100.0f / 127.0f, 1.0f};

inline droid::MixerConfig mixerConfig() {
  droid::MixerConfig m;
  m.steeringFactor = 100.0f;
  m.curveOnSteer = true;
  m.curveOnThrottle = false;
  m.spinInPlace = false;
  // One VESC already has "Invert motor direction" on, so both feet should run
  // forward together with these false. Only flip here if they don't; keep
  // direction fixes in one place.
  m.invertLeft = false;
  m.invertRight = false;
  m.swapFeet = false;
  return m;
}

// Smoothing, applied to the stick before mixing so both feet stay in step.
// Times are seconds for a full-scale change. Starting values for a 94 lb
// droid on a gentle sidewalk; tune on the bench.
// The VESCs run PID speed control with a short PPM ramp (about 0.2 s) and a
// linear throttle curve, so the firmware does the shaping; see README.
inline droid::RampConfig rampConfig() {
  droid::RampConfig r;
  r.throttleAccelS = 0.8f;  // rest -> full throttle
  r.throttleDecelS = 0.4f;  // full throttle -> rest, and through a reversal
  r.steerAccelS = 0.3f;
  r.steerDecelS = 0.15f;
  r.speedChangeS = 0.5f;    // switching speed level while moving
  r.throttleExpo = 0.3f;
  return r;
}

// Matches the VESCs' PPM calibration: 544 / 1472 / 2400 us.
inline droid::PulseRange footPulseRange() { return droid::PulseRange{544, 2400}; }

// ---------------------------------------------------------------------------
// Sound: HCR on the Teensy 4.1
// ---------------------------------------------------------------------------
constexpr uint32_t kHcrBaud = 9600;       // BaudRate in the Teensy's Config.txt
constexpr size_t kHcrTxBufferBytes = 512;
// Wait for the Teensy to start and read its SD card before the first command.
// Estimated, not measured; raise it if the startup settings don't take.
constexpr uint32_t kHcrStartDelayMs = 3000;

constexpr char kHcrChannel = 'A';

// ---------------------------------------------------------------------------
// Dome: Roam-a-Dome-Home. See DomeControl.h for the commands and pulses.
// ---------------------------------------------------------------------------
constexpr uint32_t kRdhBaud = 9600;  // its #DPSERIALBAUD default
constexpr size_t kRdhTxBufferBytes = 256;
// Pulse range, matching the Roam-a-Dome's #DPPWMMIN / #DPPWMMAX defaults.
constexpr uint16_t kDomePulseMinUs = 1000;
constexpr uint16_t kDomePulseMaxUs = 2000;
// A spin value from the dome node older than this counts as zero. Shorter than
// the 500 ms "node down" timeout, so a stale relay stops the dome sooner.
constexpr uint32_t kDomeSpinTimeoutMs = 250;
// Console bench test: each j / l keypress spins the dome this long.
constexpr uint32_t kDomeTestSpinMs = 1000;
constexpr float kDomeTestSpin = 0.3f;

// Control page. The access point shares the ESP-NOW channel; its password is
// in Secrets.h (gitignored, see Secrets.example.h).
constexpr char kApSsid[] = "R2-AU";
constexpr uint8_t kApMaxClients = 6;  // phones, plus room for both C3s during a firmware update
constexpr uint32_t kUiStatusMs = 200;      // status to every page, 5 times a second
constexpr size_t kUiEventsPerLoop = 4;     // incoming messages handled per loop
constexpr size_t kUiSyncPerLoop = 4;       // catch-up messages sent per client per loop
constexpr int kUiDomeSpeed = 40;           // % for a UI's dome moves

// ---------------------------------------------------------------------------
// Settings that can be changed without reflashing. These are the defaults; a
// saved override wins. Keys are at most 15 characters. Pins and safety values
// stay out of this table.
//
// To add one: add an entry to both lists, in the same order.
// ---------------------------------------------------------------------------
enum Setting : uint8_t {
  kSetHcrVolVocalizer,
  kSetHcrVolWavA,
  kSetHcrVolWavB,
  kSetHcrHappy,
  kSetHcrSad,
  kSetHcrMad,
  kSetHcrScared,
  kSetHcrWavConnected,
  kSetHcrWavArmed,
  kSetHcrWavDisarmed,
  kSetHcrRandomFirst,
  kSetHcrRandomLast,
  kSetHcrGapMs,
  kSetHcrAutoOn,
  kSetHcrAutoMinS,
  kSetHcrAutoMaxS,
  kSetDomeSpinMax,
  kSetDomeInvert,
  kSetDomeAutoDeg,
  kSetDomeAutoSpeed,
  kSetDomeAutoMinS,
  kSetDomeAutoMaxS,
  // Servo endpoints, in microseconds. The S3 keeps them and sends each node
  // its own (DomeLink.h). Blocks, in servo id order:
  kSetServoFirst,                         // 11 dome panels x min, close, open, max
  kSetHoloFirst = kSetServoFirst + 44,    // 6 holo axes x min, mid, max
  kSetArmFirst = kSetHoloFirst + 18,      // 2 arms x min, close, open, max
  // The body C3's NeoPixel strip (sent to it as "Xsetup n<count> b<bright>").
  kSetPixelCount = kSetArmFirst + 8,
  kSetPixelBright,
  kSettingCount,
};
constexpr size_t kServoSettingsPerPanel = 4;
constexpr size_t kServoSettingsPerHolo = 3;

constexpr droid::SettingDef kSettings[] = {
    // Volumes, each separate in HCR. The handset has no volume control.
    {"hcr_vol_voc", 100, 0, 100},  // vocalizer voice (emotions, muse)
    {"hcr_vol_a", 80, 0, 100},     // WAV channel A: every WAV we play
    {"hcr_vol_b", 80, 0, 100},     // WAV channel B: unused
    {"hcr_happy", 89, 0, 100},
    {"hcr_sad", 22, 0, 100},
    {"hcr_mad", 0, 0, 100},
    {"hcr_scared", 0, 0, 100},
    {"hcr_wav_conn", 21, 0, 9999},
    {"hcr_wav_arm", 52, 0, 9999},
    {"hcr_wav_disarm", 53, 0, 9999},
    {"hcr_rand_first", 9, 0, 9999},
    {"hcr_rand_last", 52, 0, 9999},
    {"hcr_gap_ms", 5000, 0, 60000},
    // Auto mode (CH5 in auto): random sounds from the same range as CH6.
    {"hcr_auto_on", 1, 0, 1},       // 0 = the dome wanders quietly
    {"hcr_auto_min_s", 5, 1, 600},  // pause between sounds, picked at
    {"hcr_auto_max_s", 12, 1, 600}, // random in this range
    // Dome. The Roam-a-Dome's own #DPMAXSPEED (default 50) applies on top of
    // dome_spin_max.
    {"dome_spin_max", 100, 0, 100},  // % of the stick passed on as spin
    {"dome_invert", 0, 0, 1},        // 1 if the stick turns the dome backwards
    {"dome_auto_deg", 80, 0, 180},   // auto picks angles within +/- this from home
    {"dome_auto_speed", 30, 0, 100}, // % for auto moves
    {"dome_auto_min_s", 6, 1, 600},  // pause between auto moves, picked at
    {"dome_auto_max_s", 8, 1, 600},  // random in this range
    // Dome panels, from the dome Maestro's settings file (tools/maestro/).
    {"pie1_min", 992, 500, 2500}, {"pie1_close", 992, 500, 2500}, {"pie1_open", 2000, 500, 2500}, {"pie1_max", 2000, 500, 2500},
    {"pie2_min", 992, 500, 2500}, {"pie2_close", 992, 500, 2500}, {"pie2_open", 2000, 500, 2500}, {"pie2_max", 2000, 500, 2500},
    {"pie3_min", 608, 500, 2500}, {"pie3_close", 608, 500, 2500}, {"pie3_open", 2000, 500, 2500}, {"pie3_max", 2000, 500, 2500},
    {"pie4_min", 608, 500, 2500}, {"pie4_close", 608, 500, 2500}, {"pie4_open", 2000, 500, 2500}, {"pie4_max", 2000, 500, 2500},
    {"dome1_min", 608, 500, 2500}, {"dome1_close", 608, 500, 2500}, {"dome1_open", 2000, 500, 2500}, {"dome1_max", 2000, 500, 2500},
    {"dome2_min", 608, 500, 2500}, {"dome2_close", 608, 500, 2500}, {"dome2_open", 2000, 500, 2500}, {"dome2_max", 2000, 500, 2500},
    {"dome3_min", 608, 500, 2500}, {"dome3_close", 608, 500, 2500}, {"dome3_open", 2000, 500, 2500}, {"dome3_max", 2000, 500, 2500},
    {"dome4_min", 608, 500, 2500}, {"dome4_close", 608, 500, 2500}, {"dome4_open", 2000, 500, 2500}, {"dome4_max", 2304, 500, 2500},
    {"dome5_min", 992, 500, 2500}, {"dome5_close", 992, 500, 2500}, {"dome5_open", 2000, 500, 2500}, {"dome5_max", 2000, 500, 2500},
    {"dome6_min", 608, 500, 2500}, {"dome6_close", 608, 500, 2500}, {"dome6_open", 1504, 500, 2500}, {"dome6_max", 1504, 500, 2500},
    {"dome7_min", 608, 500, 2500}, {"dome7_close", 608, 500, 2500}, {"dome7_open", 1504, 500, 2500}, {"dome7_max", 1504, 500, 2500},
    // Holo axes: front, top, rear; x side to side, y up and down.
    {"holo_fx_min", 1000, 500, 2500}, {"holo_fx_mid", 1500, 500, 2500}, {"holo_fx_max", 2000, 500, 2500},
    {"holo_fy_min", 1000, 500, 2500}, {"holo_fy_mid", 1500, 500, 2500}, {"holo_fy_max", 2000, 500, 2500},
    {"holo_tx_min", 1000, 500, 2500}, {"holo_tx_mid", 1500, 500, 2500}, {"holo_tx_max", 2000, 500, 2500},
    {"holo_ty_min", 1000, 500, 2500}, {"holo_ty_mid", 1500, 500, 2500}, {"holo_ty_max", 2000, 500, 2500},
    {"holo_rx_min", 1000, 500, 2500}, {"holo_rx_mid", 1500, 500, 2500}, {"holo_rx_max", 2000, 500, 2500},
    {"holo_ry_min", 1000, 500, 2500}, {"holo_ry_mid", 1500, 500, 2500}, {"holo_ry_max", 2000, 500, 2500},
    // Utility arms (body C3).
    {"arm_top_min", 1000, 500, 2500}, {"arm_top_close", 1150, 500, 2500}, {"arm_top_open", 1850, 500, 2500}, {"arm_top_max", 2000, 500, 2500},
    {"arm_bot_min", 1000, 500, 2500}, {"arm_bot_close", 1150, 500, 2500}, {"arm_bot_open", 1850, 500, 2500}, {"arm_bot_max", 2000, 500, 2500},
    // Body lights (body C3 NeoPixels): strip length, and overall brightness %
    // (40 % keeps 28 pixels at full white under about 0.7 A).
    {"px_count", 28, 1, 64}, {"px_bright", 40, 5, 100},
};
static_assert(sizeof(kSettings) / sizeof(kSettings[0]) == kSettingCount,
              "kSettings and Setting must list the same settings");

constexpr uint32_t kPwmFrequencyHz = 50;
constexpr uint8_t kPwmResolutionBits = 14;  // ESP32-S3 LEDC maximum
constexpr uint8_t kLedcLeftChannel = 0;
constexpr uint8_t kLedcRightChannel = 1;
// LEDC channels share timers in pairs (0-1, 2-3 ...), so the dome takes 2.
constexpr uint8_t kLedcDomeChannel = 2;

// Builds the dome settings from fixed values plus the settings table.
template <typename Settings>
droid::DomeConfig domeConfig(const Settings& settings) {
  droid::DomeConfig d;
  d.pulseMinUs = kDomePulseMinUs;
  d.pulseMaxUs = kDomePulseMaxUs;
  d.spinTimeoutMs = kDomeSpinTimeoutMs;
  d.spinMaxPercent = static_cast<uint8_t>(settings.get(kSetDomeSpinMax));
  d.invert = settings.get(kSetDomeInvert) != 0;
  d.autoDegrees = static_cast<uint16_t>(settings.get(kSetDomeAutoDeg));
  d.autoSpeed = static_cast<uint8_t>(settings.get(kSetDomeAutoSpeed));
  d.autoMinDelayMs = static_cast<uint32_t>(settings.get(kSetDomeAutoMinS)) * 1000u;
  d.autoMaxDelayMs = static_cast<uint32_t>(settings.get(kSetDomeAutoMaxS)) * 1000u;
  return d;
}

}  // namespace body
