#pragma once
// Holoprojectors, done on the dome C3 in place of the Flthy HP Nano.
// Decides what each LED and servo should do; the dome firmware drives them.
//
// Commands use the Flthy HP v1.8 syntax, so animations can use the whole
// Flthy command set:
//   <HP><T><##>[C][S][|seconds]
//     HP  F front, T top, R rear, A all, X front+rear, Y front+top, Z rear+top
//     T   0 LED function, 1 servo function
//     ##  sequence number, two digits
//     C   colour 0 random, 1 red, 2 yellow, 3 green, 4 cyan, 5 blue,
//         6 magenta, 7 orange, 8 purple, 9 white
//     S   dim pulse speed 0-9
//   LED:   01 Leia, 02 colour projector, 03 dim pulse, 04 cycle, 05 solid
//          colour, 06 rainbow, 07 short circuit, 96/98 clear (auto LED off),
//          97/99 clear (auto LED on)
//   Servo: 01P preset position (0 down, 1 centre, 2 up, 3 left, 4 upper left,
//          5 lower left, 6 right, 7 upper right, 8 lower right), 04 random
//          position, 05 wag left/right, 06 wag up/down, 98 twitch off,
//          99 twitch on
//   S1 Leia mode, S4 all off (no twitch, no auto LED), S5 or S8 or S9 back to
//   normal (twitch and auto LED on), S7 all off
// "Off colour" variants act like their plain versions here.

#include <stddef.h>
#include <stdint.h>

namespace droid {

constexpr size_t kHoloCount = 3;   // 0 front, 1 top, 2 rear
constexpr size_t kMaxHoloLeds = 12;

enum HoloMask : uint8_t { kHoloFront = 1, kHoloTop = 2, kHoloRear = 4, kHoloAll = 7 };

struct HoloCommand {
  enum class Type : uint8_t { Led, Servo, Sequence };
  Type type = Type::Led;
  uint8_t mask = 0;     // which HPs (Led and Servo)
  uint8_t number = 0;   // sequence number
  int8_t colour = -1;   // -1 not given
  int8_t speed = -1;    // -1 not given; for servo presets, the position
  uint16_t seconds = 0; // 0 = until changed
};

// False if the text isn't a command this engine understands.
bool parseHoloCommand(const char* text, HoloCommand& out);

// 0xRRGGBB for a Flthy colour number (1-9); 0 picks one at random each call.
uint32_t holoColour(uint8_t colour, uint32_t random);

struct HoloConfig {
  uint8_t ledsPerHp = 7;            // one NeoPixel jewel each
  uint32_t twitchMinMs = 3000;      // auto twitch picks a random HP and
  uint32_t twitchMaxMs = 8000;      // position this often
  uint32_t autoLedMinMs = 8000;     // auto LED shows a short random effect
  uint32_t autoLedMaxMs = 20000;    // this often
};

class HoloEngine {
 public:
  explicit HoloEngine(const HoloConfig& config = HoloConfig()) : config_(config) {}

  void seed(uint32_t value) { rng_ = value ? value : 1; }

  // Applies one command. Returns false for text it can't parse.
  bool command(const char* text, uint32_t nowMs);
  void apply(const HoloCommand& cmd, uint32_t nowMs);

  // Call every loop (about every 20 ms is plenty).
  void update(uint32_t nowMs);

  uint32_t pixel(size_t hp, size_t led) const { return pixels_[hp][led]; }
  // Servo positions, -1000..1000 (0 centre, -1000 left/down, 1000 right/up).
  int16_t servoX(size_t hp) const { return x_[hp]; }
  int16_t servoY(size_t hp) const { return y_[hp]; }

  uint8_t ledEffect(size_t hp) const { return hp_[hp].effect; }
  bool twitching() const { return twitch_; }
  bool autoLed() const { return autoLed_; }

 private:
  struct HpState {
    uint8_t effect = 0;   // LED sequence number, 0 off
    uint8_t colour = 0;
    uint8_t speed = 5;
    uint32_t startMs = 0;
    uint32_t endMs = 0;   // 0 = until changed
    uint32_t stepMs = 0;  // next animation step
    uint16_t frame = 0;
    bool fromAuto = false;
    // servo motion
    uint8_t wag = 0;      // 0 none, 1 left/right, 2 up/down
    uint8_t wagCount = 0;
    uint32_t wagMs = 0;
  };

  uint32_t random();
  uint32_t randomBetween(uint32_t lo, uint32_t hi);
  void startLed(size_t hp, uint8_t effect, int8_t colour, int8_t speed, uint16_t seconds, uint32_t nowMs, bool fromAuto);
  void clear(size_t hp);
  void renderLed(size_t hp, uint32_t nowMs);
  void preset(size_t hp, uint8_t position);

  HoloConfig config_;
  HpState hp_[kHoloCount];
  uint32_t pixels_[kHoloCount][kMaxHoloLeds] = {};
  int16_t x_[kHoloCount] = {};
  int16_t y_[kHoloCount] = {};
  bool twitch_ = true;
  bool autoLed_ = true;
  uint32_t nextTwitchMs_ = 0;
  uint32_t nextAutoLedMs_ = 0;
  uint32_t rng_ = 0x6C8E9CF5u;
};

}  // namespace droid
