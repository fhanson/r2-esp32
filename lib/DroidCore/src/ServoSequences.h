#pragma once
// Servo sequences: timed moves of the dome panels, ported from the dome
// Maestro's own sequences (tools/maestro/), plus the utility arms' own
// (ArmSequenceData.cpp).
//
// Positions are stored relative to each panel's endpoints, in thousandths:
// 0 is Closed, 1000 is Open, and values outside that range go past them (the
// Ripple pushes Dome panel 4 to about 1220). The player turns them into
// microseconds with whatever endpoints are set now, so tuning a panel in the
// UI also fixes every sequence that uses it. The result is always held inside
// the panel's min and max limits.
//
// Like the Maestro's, a frame jumps straight to its positions, then waits for
// its duration. There's no easing between frames.

#include <stddef.h>
#include <stdint.h>

namespace droid {

// Dome panel servos, in this order: Pie 1-4 (Maestro channels 0-3), then
// Dome panels 1-7 (channels 7-13).
constexpr size_t kDomePanels = 11;
constexpr uint8_t kDomePanelMaestroChannel[kDomePanels] = {0, 1, 2, 3, 7, 8, 9, 10, 11, 12, 13};

// A frame position that leaves the servo where it is.
constexpr int16_t kHoldPosition = INT16_MIN;

struct ServoFrame {
  uint16_t durationMs;
  int16_t position[kDomePanels];  // thousandths of Closed->Open, or kHoldPosition
};

struct ServoSequence {
  const char* name;
  const ServoFrame* frames;
  uint8_t frameCount;
};

// The built-in sequences, in the Maestro's script order, so the index is the
// old restartScript() number: 0 All Closed, 1 All Open, 2 Cantina, 3 Clap,
// 4-7 Pie 1-4 Open, 8 Point, 9 Ripple, 10 Wave.
extern const ServoSequence kServoSequences[];
extern const size_t kServoSequenceCount;
constexpr uint8_t kSequenceAllClosed = 0;
constexpr uint8_t kSequenceAllOpen = 1;

// Utility arm sequences (ArmSequenceData.cpp): position[0] top arm,
// position[1] bottom arm, other slots unused.
extern const ServoSequence kArmSequences[];
extern const size_t kArmSequenceCount;

// One numbering for all: the dome's first (so saved animations keep theirs),
// then the arms'. The S3 sends each Q<n> to whichever C3 owns it.
size_t allSequenceCount();
const ServoSequence* sequenceAt(size_t index);  // nullptr if out of range
bool isArmSequence(size_t index);

// Index for a name (exact match), or -1. Searches dome and arm sequences.
int servoSequenceIndex(const char* name);

// Total length in milliseconds.
uint32_t servoSequenceLengthMs(const ServoSequence& seq);

struct ServoEndpoints {
  uint16_t minUs = 1000;
  uint16_t closedUs = 1000;
  uint16_t openUs = 2000;
  uint16_t maxUs = 2000;
};

// Microseconds for a relative position, held inside min..max.
uint16_t positionToUs(int16_t thousandths, const ServoEndpoints& ends);

// Plays one sequence at a time.
class ServoSequencePlayer {
 public:
  // Starts a sequence (an allSequenceCount() number) from its first frame,
  // replacing any that is running. False for an unknown index.
  bool start(uint8_t index, uint32_t nowMs);
  void stop() { running_ = false; }
  bool running() const { return running_; }
  int current() const { return running_ ? index_ : -1; }

  // Call every loop. When a new frame starts, writes its positions to out
  // (kHoldPosition where the servo isn't touched) and returns true.
  bool update(uint32_t nowMs, int16_t out[kDomePanels]);

 private:
  bool running_ = false;
  uint8_t index_ = 0;
  uint8_t frame_ = 0;        // next frame to start
  uint32_t frameAtMs_ = 0;   // when it starts
};

}  // namespace droid
