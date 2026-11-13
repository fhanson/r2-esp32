#pragma once
// What the body S3 and the two C3 nodes say to each other over ESP-NOW,
// beyond the heartbeat.
//
// DomeCommand payload: one text command, no line ending.
//   Q<n>        run servo sequence n (ServoSequences.h); Q- stops
//   H<text>     holoprojector command, Flthy syntax (HoloEngine.h)
//   L<text>     Logic Engine command, sent to the WReactor32 as it is
//   X<text>     body lights (body C3 NeoPixels), PixelEngine.h syntax
//   P<id>,<us>  move one servo now (live moves and custom sequences)
//   A<n><o|c>   utility arm n (0 top, 1 bottom) open or closed (body C3)
//
// ServoEndpoints payload: a list of servo entries. The S3 owns every servo
// setting and sends a node its servos whenever the node comes up or a
// setting changes.

#include <stddef.h>
#include <stdint.h>

#include "ServoSequences.h"

namespace droid {

// Servo ids, the same on every node.
//   0-10   dome panels, in kDomePanelMaestroChannel order (dome C3, Maestro)
//   16-21  holo axes: front X, front Y, top X, top Y, rear X, rear Y
//          (dome C3, PCA9685; endpoints are min, centre, centre, max)
//   32-33  utility arms: top, bottom (body C3, PCA9685)
constexpr uint8_t kServoHoloFirst = 16;
constexpr uint8_t kServoHoloCount = 6;
constexpr uint8_t kServoArmFirst = 32;
constexpr uint8_t kServoArmCount = 2;

struct ServoEntry {
  uint8_t id = 0;
  ServoEndpoints ends;
};

constexpr size_t kServoEntryBytes = 9;
constexpr size_t kMaxServoEntries = 24;  // fits one frame

size_t encodeServoEntries(const ServoEntry* entries, size_t count, uint8_t* out, size_t cap);
// Returns the number of entries read, or 0 for a bad payload.
size_t decodeServoEntries(const uint8_t* data, size_t length, ServoEntry* out, size_t cap);

// Dome spin from the left stick, -1000..1000.
size_t encodeSpin(int16_t spin, uint8_t* out, size_t cap);
bool decodeSpin(const uint8_t* data, size_t length, int16_t& spin);

// A parsed DomeCommand.
struct DomeCommand {
  enum class Kind : uint8_t { Bad, Sequence, StopSequence, Holo, Logic, Move, Arm, Pixels };
  Kind kind = Kind::Bad;
  uint8_t sequence = 0;
  uint8_t servo = 0;
  uint16_t us = 0;
  bool open = false;           // Arm
  const char* text = nullptr;  // points into the parsed string (Holo, Logic)
};

DomeCommand parseDomeCommand(const char* text);

}  // namespace droid
