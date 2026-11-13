#pragma once
// Animations: a sound plus whatever plays with it, as a list of timed steps
//.
//
// A step fires at a time from the start of the animation:
//   Sound          play WAV a (on channel A)
//   RandomSound    play a random WAV from a to b
//   SoundPick      play one WAV at random from a chosen set, in text as hex:
//                  each character holds 4 files, lowest bit first, so
//                  "1" is file 1, "8" file 4, "01" file 5, "f1" files 1-5
//   Emotion        HCR stimulate: a = 0 happy, 1 sad, 2 mad, 3 scared, 4 random
//   ServoSequence  run servo sequence a (see ServoSequences.h)
//   Holo           holoprojector commands, Flthy HP syntax, in text; several
//                  are separated by spaces, so each HP can do its own thing
//                  (for example "A0033|5", or "F0035|5 T104 R0096")
//   Dome           turn the dome (DomeAction in the low byte of a):
//                    Home   look ahead
//                    GoTo   b = angle from home, -180..180; high byte of a =
//                           speed %, 0 for the usual UI speed
//                    Spin   b = speed %, -100 (left) .. 100 (right); high
//                           byte of a = how long, in tenths of a second.
//                           Only while CH5 is manual; the stick takes over.
//                    Stop   stop any turn
//   Pixels         body lights: a PixelEngine.h command for the body C3's
//                  NeoPixel strip, in text (for example "swoop #ff8800 > s7")
//   Logic          a Logic Engine command for the WReactor32, in text
//                  (for example "~RTLE990505" or "0T3")
//
// The S3 plays animations: sound steps go to HCR, the rest go to the dome C3.

#include <stddef.h>
#include <stdint.h>

namespace droid {

enum class StepKind : uint8_t {
  None = 0,
  Sound = 1,
  RandomSound = 2,
  Emotion = 3,
  ServoSequence = 4,
  Holo = 5,
  Logic = 6,
  SoundPick = 7,
  Dome = 8,
  Pixels = 9,
};

enum class DomeAction : uint8_t { Home = 0, GoTo = 1, Spin = 2, Stop = 3 };

constexpr size_t kStepTextMax = 63;   // Holo, Logic and SoundPick text
constexpr size_t kAnimNameMax = 23;
constexpr size_t kAnimIconMax = 19;   // icon file name without ".svg"
constexpr size_t kMaxAnimSteps = 20;  // the UI groups them into blocks by time
constexpr size_t kMaxAnimations = 48;

struct AnimStep {
  uint16_t atMs = 0;
  StepKind kind = StepKind::None;
  uint16_t a = 0;
  uint16_t b = 0;
  char text[kStepTextMax + 1] = {};
};

struct Animation {
  uint16_t id = 0;  // 200 and up
  char name[kAnimNameMax + 1] = {};
  char icon[kAnimIconMax + 1] = {};
  uint8_t stepCount = 0;
  AnimStep steps[kMaxAnimSteps];
};

const char* stepKindName(StepKind kind);

// Step helpers, for the defaults and the tests.
AnimStep soundStep(uint16_t atMs, uint16_t file);
AnimStep randomSoundStep(uint16_t atMs, uint16_t first, uint16_t last);
AnimStep emotionStep(uint16_t atMs, uint8_t emotion);
AnimStep servoStep(uint16_t atMs, uint8_t sequence);
AnimStep textStep(uint16_t atMs, StepKind kind, const char* text);
// value: angle (GoTo) or speed % (Spin); extra: speed % (GoTo) or tenths of
// a second (Spin).
AnimStep domeStep(uint16_t atMs, DomeAction action, int16_t value = 0, uint8_t extra = 0);
inline DomeAction domeStepAction(const AnimStep& s) { return static_cast<DomeAction>(s.a & 0xff); }
inline uint8_t domeStepExtra(const AnimStep& s) { return static_cast<uint8_t>(s.a >> 8); }
inline int16_t domeStepValue(const AnimStep& s) { return static_cast<int16_t>(s.b); }

// SoundPick sets. Files 1 to 4 * kStepTextMax.
constexpr uint16_t kSoundPickMaxFile = 4 * kStepTextMax;
// Writes the set as hex text; false if a file is out of range.
bool soundPickText(const uint16_t* files, size_t count, char* out, size_t cap);
// The files in a set, lowest first; returns how many (0 if the text is bad).
size_t soundPickFiles(const char* text, uint16_t* out, size_t max);

// Adds a step; false if the animation is full or the text is too long.
bool addStep(Animation& anim, const AnimStep& step);

// Length: the last step's time (steps have no length of their own).
uint16_t animationLastStepMs(const Animation& anim);

// Compact binary form, for LittleFS and the UI messages. Returns the size
// written, or 0 if it doesn't fit. Step encoding is shared by both.
size_t encodeStep(const AnimStep& step, uint8_t* out, size_t cap);
size_t decodeStep(const uint8_t* data, size_t length, AnimStep& step);  // bytes used, 0 if bad
size_t encodeAnimation(const Animation& anim, uint8_t* out, size_t cap);
bool decodeAnimation(const uint8_t* data, size_t length, Animation& anim);

// The default library: the 16 animations the Padawan's buttons fired, from
// the Mega's last sketch, then Chortle No (chortle and a head shake).
// Holo commands use the effect each button was meant to show (the Mega's
// FlthyHP library sent a few wrong codes: its "short circuit" was solid
// yellow and its "rainbow" was the short circuit).
size_t defaultAnimations(Animation* out, size_t cap);

// Plays one animation at a time.
class AnimationPlayer {
 public:
  void start(const Animation& anim, uint32_t nowMs);
  void stop() { running_ = false; }
  bool running() const { return running_; }
  uint16_t currentId() const { return running_ ? anim_.id : 0; }

  // Returns the next step that is due, or nullptr when none is due now.
  // Call in a loop until it returns nullptr.
  const AnimStep* next(uint32_t nowMs);

 private:
  Animation anim_;
  bool running_ = false;
  uint8_t nextStep_ = 0;
  uint32_t startMs_ = 0;
};

}  // namespace droid
