#pragma once
// Command serial link to the Roam-a-Dome-Home (its RXD3/TXD3 port).
//
// Like HcrLink, send() never waits: a command that doesn't fit in the TX
// buffer is dropped and counted, so the dome can't stall the drive loop.
// Commands end in CR, which the Roam-a-Dome accepts (CR or LF).
//
// The Roam-a-Dome only ever sends "#DP<mode><position>" lines here, when the
// dome moves. poll() keeps the latest one.

#include <Arduino.h>

#include "DomeControl.h"

class RdhLink {
 public:
  RdhLink(HardwareSerial& port, int rxPin, int txPin, uint32_t baud, size_t txBufferBytes)
      : port_(port), rxPin_(rxPin), txPin_(txPin), baud_(baud), txBufferBytes_(txBufferBytes) {}

  void begin();

  // Queues a command plus CR, or drops all of it. False if dropped.
  bool send(const char* command, size_t length);

  // Call every loop. Reads status lines. Pass echo only when a person is at
  // the USB console (see HcrLink.h).
  void poll(uint32_t nowMs, Print* echo);

  bool haveReport() const { return haveReport_; }
  const droid::DomeReport& report() const { return report_; }
  uint32_t reportAgeMs(uint32_t nowMs) const { return nowMs - reportAtMs_; }

  uint32_t sentCommands() const { return sent_; }
  uint32_t droppedCommands() const { return dropped_; }
  uint32_t badLines() const { return badLines_; }

 private:
  void finishLine(uint32_t nowMs);

  HardwareSerial& port_;
  int rxPin_;
  int txPin_;
  uint32_t baud_;
  size_t txBufferBytes_;

  char line_[24] = {};
  size_t lineLen_ = 0;
  bool lineTooLong_ = false;

  bool haveReport_ = false;
  droid::DomeReport report_;
  uint32_t reportAtMs_ = 0;

  uint32_t sent_ = 0;
  uint32_t dropped_ = 0;
  uint32_t badLines_ = 0;
};
