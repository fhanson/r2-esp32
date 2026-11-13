#include "SbusParser.h"

namespace droid {

bool SbusParser::decode(const uint8_t* b, SbusFrame& f) {
  if (b[0] != kHeader || !footerOk(b[24])) return false;

  // 16 channels x 11 bits, packed little-endian starting at byte 1.
  uint32_t bits = 0;
  int nbits = 0;
  size_t byteIdx = 1;
  for (size_t c = 0; c < SbusFrame::kChannels; ++c) {
    while (nbits < 11) {
      bits |= static_cast<uint32_t>(b[byteIdx++]) << nbits;
      nbits += 8;
    }
    f.ch[c] = static_cast<uint16_t>(bits & 0x07FF);
    bits >>= 11;
    nbits -= 11;
  }

  const uint8_t flags = b[23];
  f.ch17 = flags & 0x01;
  f.ch18 = flags & 0x02;
  f.lostFrame = flags & 0x04;
  f.failsafe = flags & 0x08;
  return true;
}

bool SbusParser::feed(uint8_t b, uint32_t nowUs) {
  if (haveLast_ && (nowUs - lastByteUs_) > kResyncGapUs) pos_ = 0;
  lastByteUs_ = nowUs;
  haveLast_ = true;

  if (pos_ == 0 && b != kHeader) return false;  // wait for a header

  buf_[pos_++] = b;
  if (pos_ < kFrameLen) return false;

  pos_ = 0;
  SbusFrame f;
  if (!decode(buf_, f)) {
    ++badFrames_;
    return false;
  }
  frame_ = f;
  ++goodFrames_;
  return true;
}

}  // namespace droid
