#pragma once
// Servo sequences made on the page.
//
// The S3 keeps and plays them itself, sending each frame's moves to the C3s
// as P<id>,<us> commands (DomeLink.h), so the C3s need nothing new. They're
// numbered from kCustomSeqFirst, after the built-in ones (ServoSequences.h),
// and animations refer to them by that number.
//
// A frame holds a position for each of kSeqServos servos, in hundredths of
// Closed->Open (0 Closed, 100 Open, beyond allowed), or kSeqHold.

#include <stddef.h>
#include <stdint.h>

#include "ServoSequences.h"

namespace droid {

constexpr size_t kSeqServos = 13;  // dome panels 0-10, then the two arms
constexpr uint8_t kSeqServoId[kSeqServos] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 32, 33};  // DomeLink.h ids
constexpr int8_t kSeqHold = INT8_MIN;
constexpr int8_t kSeqPosMin = -125;
constexpr int8_t kSeqPosMax = 125;
constexpr uint8_t kCustomSeqFirst = 64;
constexpr size_t kMaxCustomSeqs = 24;
constexpr size_t kMaxSeqFrames = 40;
constexpr size_t kSeqNameMax = 23;
constexpr uint16_t kSeqFrameMinMs = 50;
constexpr uint16_t kSeqFrameMaxMs = 10000;

struct CustomFrame {
  uint16_t ms = 500;
  int8_t pos[kSeqServos];
  CustomFrame() { for (auto& p : pos) p = kSeqHold; }
};

struct CustomSequence {
  uint8_t id = 0;  // kCustomSeqFirst and up; 0 = new
  char name[kSeqNameMax + 1] = {};
  uint8_t frameCount = 0;
  CustomFrame frames[kMaxSeqFrames];
};

bool isCustomSequenceId(size_t index);
uint32_t customSequenceLengthMs(const CustomSequence& s);
// Name present, 1..kMaxSeqFrames frames, durations and positions in range.
bool validCustomSequence(const CustomSequence& s);

// A built-in sequence as an editable one (positions rounded to hundredths).
bool customFromBuiltIn(size_t index, CustomSequence& out);

// Whole sequence, for the S3's file: id, name, count, then each frame as
// u16 ms and kSeqServos bytes.
size_t encodeCustomSequence(const CustomSequence& s, uint8_t* out, size_t cap);
bool decodeCustomSequence(const uint8_t* data, size_t length, CustomSequence& out);
constexpr size_t kCustomSeqEncodedMax = 1 + 1 + kSeqNameMax + 1 + kMaxSeqFrames * (2 + kSeqServos);

// Plays one at a time, from any frame.
class CustomSequencePlayer {
 public:
  bool start(const CustomSequence& s, uint8_t fromFrame, uint32_t nowMs);
  void stop() { running_ = false; }
  bool running() const { return running_; }
  uint8_t id() const { return running_ ? seq_.id : 0; }
  // When a frame starts, copies its positions to out and returns true.
  bool update(uint32_t nowMs, int8_t out[kSeqServos]);

 private:
  CustomSequence seq_;  // a copy, so edits mid-play can't change it
  bool running_ = false;
  uint8_t frame_ = 0;
  uint32_t frameAtMs_ = 0;
};

}  // namespace droid
