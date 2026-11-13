#pragma once
// Dome rotation through the Roam-a-Dome-Home (reeltwo DomeControlFirmware).
//
// The S3 talks to the Roam-a-Dome two ways:
//   - Pulse in: an RC pulse for manual spin speed. 1500 us is stop, 1000 and
//     2000 us are full speed each way (its #DPPWMMIN/#DPPWMMAX defaults), with
//     a 5% deadband. Turn it on there with #DPPWMIN1.
//   - Command serial: text commands ending in CR, for home and go-to-angle.
//     It replies only with "#DP<mode><position>" when the position changes.
//
// Commands (from DomeControlFirmware.ino):
//   :DPH            home at its home speed
//   :DPA<deg>,<spd> go to an angle from home, speed 0-100 %
//   :DPZ            stop automatic movement and restore its settings
// AUTO SAFETY is on by default there: home and go-to are refused until the
// dome has been moved by hand once since power-up (a pulse outside its
// deadband counts). A manual pulse also cancels a home or go-to in progress.

#include <stddef.h>
#include <stdint.h>

namespace droid {

// Right-hand radio CH5, a 3-position button: 0 / 1 / 2.
enum class DomeMode : uint8_t { Manual = 0, Auto = 1, Home = 2 };

const char* domeModeName(DomeMode mode);

// Command strings, NUL-terminated, without the line ending (RdhLink adds CR).
// Each returns the length, or 0 if it doesn't fit in cap.
size_t domeHomeCommand(char* out, size_t cap);                         // :DPH
size_t domeStopCommand(char* out, size_t cap);                         // :DPZ
size_t domeGoToCommand(char* out, size_t cap, int degrees, int speed);  // :DPA-40,30

// One status line from the Roam-a-Dome's command serial TX, without the line
// ending: "#DP" + mode character + home-relative position, e.g. "#DP$-35".
struct DomeReport {
  char mode = '?';  // '@' off, '!' home, '$' random, '%' target
  int position = 0;
};
bool parseDomeReport(const char* line, DomeReport& out);

struct DomeConfig {
  uint16_t pulseMinUs = 1000;  // must match #DPPWMMIN on the Roam-a-Dome
  uint16_t pulseMaxUs = 2000;  // must match #DPPWMMAX
  uint8_t spinMaxPercent = 100;  // scales the stick; the Roam-a-Dome's
                                 // #DPMAXSPEED (default 50) applies on top
  bool invert = false;           // flip if the stick turns the dome backwards
  uint16_t autoDegrees = 80;     // auto mode picks angles within +/- this
  uint8_t autoSpeed = 30;        // % for auto moves
  uint32_t autoMinDelayMs = 6000;  // pause between auto moves, picked at
  uint32_t autoMaxDelayMs = 8000;  // random in this range
  uint32_t spinTimeoutMs = 250;    // a spin value older than this is ignored
};

// What the dome should do now. command is empty when nothing needs sending.
struct DomeOutput {
  uint16_t pulseUs = 1500;
  char command[24] = {};
  size_t commandLength = 0;
};

class DomeControl {
 public:
  explicit DomeControl(const DomeConfig& config) : config_(config) {}

  void setConfig(const DomeConfig& config) { config_ = config; }
  const DomeConfig& config() const { return config_; }

  // Manual spin, -1 .. +1. Normally the left-hand stick, relayed by the dome
  // node; it counts for holdMs (the spin timeout unless given), then drops to
  // zero, so a lost relay can't leave the dome turning.
  void setSpin(float spin, uint32_t nowMs);
  void setSpin(float spin, uint32_t nowMs, uint32_t holdMs);

  // Call every loop. link = the right-hand radio is up; without it the dome
  // stops and auto/home are cancelled.
  DomeOutput update(uint32_t nowMs, bool link, DomeMode mode);

  // One-off moves from a UI. Sent at the next update while the radio
  // link is up; dropped without it. CH5 keeps its mode: in auto the next
  // random move takes over again, and in manual any stick spin cancels it.
  void requestHome() { requestMove(Move::Home, 0, 0); }
  void requestGoTo(int degrees, int speed) { requestMove(Move::GoTo, degrees, speed); }
  void requestStop() { requestMove(Move::Stop, 0, 0); }

  // Seed for the auto mode's angles and pauses (tests pass a fixed one).
  void seed(uint32_t value) { rng_ = value != 0 ? value : 1; }

  DomeMode activeMode() const { return active_; }
  bool stopped() const { return stopped_; }
  float spin(uint32_t nowMs) const;
  uint16_t neutralUs() const;

 private:
  enum class Move : uint8_t { None, Home, GoTo, Stop };
  void requestMove(Move m, int degrees, int speed) {
    move_ = m;
    moveDegrees_ = degrees;
    moveSpeed_ = speed;
  }
  uint32_t nextRandom();
  uint16_t pulseFor(float spin) const;

  DomeConfig config_;
  float spin_ = 0.0f;
  uint32_t spinAtMs_ = 0;
  uint32_t spinHoldMs_ = 0;
  bool spinSet_ = false;

  bool started_ = false;
  bool stopped_ = true;  // no link: neutral pulse, auto/home cancelled
  DomeMode active_ = DomeMode::Manual;
  uint32_t nextAutoMs_ = 0;
  uint32_t rng_ = 0x2545F491u;
  Move move_ = Move::None;
  int moveDegrees_ = 0;
  int moveSpeed_ = 0;
};

}  // namespace droid
