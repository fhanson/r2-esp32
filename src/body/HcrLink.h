#pragma once
// Serial link to the Teensy 4.1 running HCR.
//
// send() never waits, so sound can't stall the drive loop: 9600 baud moves
// about one byte per millisecond, and a blocking write would hold up the
// feet. If a command doesn't fit in the TX buffer it is dropped and counted.

#include <Arduino.h>

class HcrLink {
 public:
  HcrLink(HardwareSerial& port, int rxPin, int txPin, uint32_t baud, size_t txBufferBytes)
      : port_(port), rxPin_(rxPin), txPin_(txPin), baud_(baud), txBufferBytes_(txBufferBytes) {}

  void begin();

  // Queues a whole command plus its line ending, or drops all of it. False if
  // dropped. The HCR library ends every command with a newline, so this does.
  bool send(const char* command, size_t length);

  // Call often (every loop). Reads replies and throws them away unless echo
  // is set. Only pass echo when a person is at the USB console: USB writes
  // can block for 100 ms with no host attached.
  void poll(Print* echo);

  uint32_t sentCommands() const { return sent_; }
  uint32_t droppedCommands() const { return dropped_; }

 private:
  HardwareSerial& port_;
  int rxPin_;
  int txPin_;
  uint32_t baud_;
  size_t txBufferBytes_;
  uint32_t sent_ = 0;
  uint32_t dropped_ = 0;
};
