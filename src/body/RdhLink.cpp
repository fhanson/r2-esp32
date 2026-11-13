#include "RdhLink.h"

void RdhLink::begin() {
  port_.setTxBufferSize(txBufferBytes_);  // before begin(); see HcrLink.cpp
  port_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
  // End any noise the Roam-a-Dome picked up while the S3 booted, so the first
  // real command starts on a clean line. It just reports that line invalid.
  port_.write('\r');
}

bool RdhLink::send(const char* command, size_t length) {
  if (length == 0 || static_cast<size_t>(port_.availableForWrite()) < length + 1) {
    ++dropped_;
    return false;
  }
  port_.write(reinterpret_cast<const uint8_t*>(command), length);
  port_.write('\r');
  ++sent_;
  return true;
}

void RdhLink::finishLine(uint32_t nowMs) {
  if (lineLen_ == 0 && !lineTooLong_) return;
  line_[lineLen_] = '\0';
  droid::DomeReport r;
  if (!lineTooLong_ && droid::parseDomeReport(line_, r)) {
    report_ = r;
    haveReport_ = true;
    reportAtMs_ = nowMs;
  } else {
    ++badLines_;
  }
  lineLen_ = 0;
  lineTooLong_ = false;
}

void RdhLink::poll(uint32_t nowMs, Print* echo) {
  while (port_.available() > 0) {
    const char c = static_cast<char>(port_.read());
    if (echo != nullptr) echo->write(static_cast<uint8_t>(c));
    if (c == '\r' || c == '\n') {
      finishLine(nowMs);
    } else if (lineLen_ < sizeof(line_) - 1) {
      line_[lineLen_++] = c;
    } else {
      lineTooLong_ = true;
    }
  }
}
