// Utility arm sequences (ServoSequences.h), played by the body C3. Hand-
// written. position[0] is the top arm, position[1] the bottom (0 Closed, 1000
// Open). An MG90S swings in about 0.3 s, so frames are 0.5 s or longer.
#include "ServoSequences.h"

namespace droid {

namespace {

constexpr int16_t H = kHoldPosition;  // leave that arm where it is

const ServoFrame kTopOpen[] = {{600, {1000, H}}};
const ServoFrame kTopClose[] = {{600, {0, H}}};
const ServoFrame kBottomOpen[] = {{600, {H, 1000}}};
const ServoFrame kBottomClose[] = {{600, {H, 0}}};

// Both open, a moment's pause, both close.
const ServoFrame kBothOpenClose[] = {
    {1500, {1000, 1000}},
    {600, {0, 0}},
};

// Both together twice, three see-saws, then both closed.
const ServoFrame kFlap[] = {
    {500, {1000, 1000}}, {500, {0, 0}},
    {500, {1000, 1000}}, {500, {0, 0}},
    {500, {1000, 0}},    {500, {0, 1000}},
    {500, {1000, 0}},    {500, {0, 1000}},
    {500, {1000, 0}},    {500, {0, 1000}},
    {600, {0, 0}},
};

}  // namespace

const ServoSequence kArmSequences[] = {
    {"Top Arm Open", kTopOpen, sizeof(kTopOpen) / sizeof(kTopOpen[0])},
    {"Top Arm Close", kTopClose, sizeof(kTopClose) / sizeof(kTopClose[0])},
    {"Bottom Arm Open", kBottomOpen, sizeof(kBottomOpen) / sizeof(kBottomOpen[0])},
    {"Bottom Arm Close", kBottomClose, sizeof(kBottomClose) / sizeof(kBottomClose[0])},
    {"Arms Open and Close", kBothOpenClose, sizeof(kBothOpenClose) / sizeof(kBothOpenClose[0])},
    {"Arms Flap", kFlap, sizeof(kFlap) / sizeof(kFlap[0])},
};
const size_t kArmSequenceCount = sizeof(kArmSequences) / sizeof(kArmSequences[0]);

}  // namespace droid
