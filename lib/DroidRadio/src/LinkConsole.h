#pragma once
// The '@' console commands that set up and check the link, the same on every
// node.

#include <Arduino.h>

#include "EspNowLink.h"

class LinkConsole {
 public:
  explicit LinkConsole(EspNowLink& link) : link_(link) {}

  // Offer each typed character here first. True means it was part of an '@'
  // line and the caller should ignore it.
  bool feed(char c, Print& out, uint32_t nowMs);

 private:
  void run(const char* line, Print& out, uint32_t nowMs);
  void reportSaved(bool saved, Print& out);

  EspNowLink& link_;
  char line_[80];
  size_t length_ = 0;  // 0 = not inside an '@' line
};
