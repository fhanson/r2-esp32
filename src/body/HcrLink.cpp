#include "HcrLink.h"

void HcrLink::begin() {
  // Must come before begin(). The default is no buffer, where a write blocks
  // once the 128-byte UART FIFO is full.
  port_.setTxBufferSize(txBufferBytes_);
  port_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
}

bool HcrLink::send(const char* command, size_t length) {
  if (length == 0 || static_cast<size_t>(port_.availableForWrite()) < length + 1) {
    ++dropped_;
    return false;
  }
  port_.write(reinterpret_cast<const uint8_t*>(command), length);
  port_.write('\n');
  ++sent_;
  return true;
}

void HcrLink::poll(Print* echo) {
  while (port_.available() > 0) {
    const int c = port_.read();
    if (echo != nullptr) echo->write(static_cast<uint8_t>(c));
  }
}
