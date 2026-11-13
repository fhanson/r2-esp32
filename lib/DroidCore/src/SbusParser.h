#pragma once
// SBUS frame parser. Feed it bytes from an inverted 8E2 UART.
//
// Frame: 25 bytes = 0x0F header, 22 bytes of 16 x 11-bit channels,
// 1 flags byte, 1 footer byte.
//
// HotRC DS-650 + SBUS-A quirks (from protoR2's bench testing):
//   - footer is 0x04 instead of the standard 0x00, so any footer whose low
//     nibble is 0x00 or 0x04 is accepted (this also covers SBUS2 footers);
//   - the line runs near 115 kbaud rather than 100 kbaud. That is handled by
//     the UART wrapper (auto-baud), not here.

#include <stdint.h>
#include <stddef.h>

namespace droid {

struct SbusFrame {
  static constexpr size_t kChannels = 16;
  uint16_t ch[kChannels] = {};  // raw 11-bit values, typically 172..1811
  bool ch17 = false;            // digital channel 17
  bool ch18 = false;            // digital channel 18
  bool lostFrame = false;       // receiver missed a frame from the handset
  bool failsafe = false;        // receiver has given up on the handset
};

class SbusParser {
 public:
  static constexpr size_t kFrameLen = 25;
  static constexpr uint8_t kHeader = 0x0F;
  // Bytes inside one frame arrive back to back (about 120 us apart at
  // 100 kbaud 8E2). A gap longer than this means a new frame is starting.
  static constexpr uint32_t kResyncGapUs = 2000;

  // Feed one byte with its arrival time in microseconds.
  // Returns true when a complete, valid frame has just been decoded;
  // read it with frame().
  bool feed(uint8_t b, uint32_t nowUs);

  const SbusFrame& frame() const { return frame_; }

  uint32_t goodFrames() const { return goodFrames_; }
  uint32_t badFrames() const { return badFrames_; }

  void reset() { pos_ = 0; }

  // Footer check, exposed for tests.
  static bool footerOk(uint8_t f) { return (f & 0x0F) == 0x00 || (f & 0x0F) == 0x04; }

  // Decode a full 25-byte buffer. Returns false if header or footer is wrong.
  static bool decode(const uint8_t* buf, SbusFrame& out);

 private:
  uint8_t buf_[kFrameLen] = {};
  size_t pos_ = 0;
  uint32_t lastByteUs_ = 0;
  bool haveLast_ = false;
  SbusFrame frame_;
  uint32_t goodFrames_ = 0;
  uint32_t badFrames_ = 0;
};

}  // namespace droid
