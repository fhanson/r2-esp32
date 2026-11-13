#include "DomeLink.h"

#include <stdlib.h>
#include <string.h>

namespace droid {

namespace {
void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

size_t encodeServoEntries(const ServoEntry* entries, size_t count, uint8_t* out, size_t cap) {
  if (count > kMaxServoEntries || count * kServoEntryBytes > cap) return 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t* p = out + i * kServoEntryBytes;
    p[0] = entries[i].id;
    put16(p + 1, entries[i].ends.minUs);
    put16(p + 3, entries[i].ends.closedUs);
    put16(p + 5, entries[i].ends.openUs);
    put16(p + 7, entries[i].ends.maxUs);
  }
  return count * kServoEntryBytes;
}

size_t decodeServoEntries(const uint8_t* data, size_t length, ServoEntry* out, size_t cap) {
  if (length == 0 || length % kServoEntryBytes != 0) return 0;
  const size_t count = length / kServoEntryBytes;
  if (count > cap) return 0;
  for (size_t i = 0; i < count; ++i) {
    const uint8_t* p = data + i * kServoEntryBytes;
    out[i].id = p[0];
    out[i].ends.minUs = get16(p + 1);
    out[i].ends.closedUs = get16(p + 3);
    out[i].ends.openUs = get16(p + 5);
    out[i].ends.maxUs = get16(p + 7);
  }
  return count;
}

size_t encodeSpin(int16_t spin, uint8_t* out, size_t cap) {
  if (cap < 2) return 0;
  put16(out, static_cast<uint16_t>(spin));
  return 2;
}

bool decodeSpin(const uint8_t* data, size_t length, int16_t& spin) {
  if (length != 2) return false;
  const int16_t v = static_cast<int16_t>(get16(data));
  if (v < -1000 || v > 1000) return false;
  spin = v;
  return true;
}

DomeCommand parseDomeCommand(const char* text) {
  DomeCommand c;
  if (text == nullptr || text[0] == '\0') return c;
  const char* arg = text + 1;
  char* end = nullptr;
  switch (text[0]) {
    case 'Q': {
      if (strcmp(arg, "-") == 0) {
        c.kind = DomeCommand::Kind::StopSequence;
        return c;
      }
      const long n = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || n < 0 || n >= static_cast<long>(allSequenceCount())) return c;
      c.kind = DomeCommand::Kind::Sequence;
      c.sequence = static_cast<uint8_t>(n);
      return c;
    }
    case 'H':
    case 'L':
      if (*arg == '\0') return c;
      c.kind = text[0] == 'H' ? DomeCommand::Kind::Holo : DomeCommand::Kind::Logic;
      c.text = arg;
      return c;
    case 'X':
      if (*arg == '\0') return c;
      c.kind = DomeCommand::Kind::Pixels;
      c.text = arg;
      return c;
    case 'P': {
      const long id = strtol(arg, &end, 10);
      if (end == arg || *end != ',' || id < 0 || id > 255) return c;
      const char* usText = end + 1;
      const long us = strtol(usText, &end, 10);
      if (end == usText || *end != '\0' || us < 400 || us > 2600) return c;
      c.kind = DomeCommand::Kind::Move;
      c.servo = static_cast<uint8_t>(id);
      c.us = static_cast<uint16_t>(us);
      return c;
    }
    case 'A':
      if (arg[0] < '0' || arg[0] >= '0' + static_cast<int>(kServoArmCount) || (arg[1] != 'o' && arg[1] != 'c') ||
          arg[2] != '\0') {
        return c;
      }
      c.kind = DomeCommand::Kind::Arm;
      c.servo = static_cast<uint8_t>(kServoArmFirst + (arg[0] - '0'));
      c.open = arg[1] == 'o';
      return c;
    default:
      return c;
  }
}

}  // namespace droid
