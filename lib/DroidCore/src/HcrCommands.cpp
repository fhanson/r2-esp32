#include "HcrCommands.h"

#include <stdio.h>

namespace droid {

namespace {

bool validChannel(char channel) { return channel == 'A' || channel == 'B'; }

unsigned level(uint8_t value) { return value > kHcrMaxLevel ? kHcrMaxLevel : value; }

size_t finish(int written, size_t cap) {
  if (written <= 0 || static_cast<size_t>(written) >= cap) return 0;
  return static_cast<size_t>(written);
}

}  // namespace

size_t hcrPlayWav(char* out, size_t cap, char channel, uint16_t file) {
  if (!validChannel(channel) || file > kHcrMaxWav) return 0;
  return finish(snprintf(out, cap, "<C%c%04u>", channel, static_cast<unsigned>(file)), cap);
}

size_t hcrPlayRandomWav(char* out, size_t cap, char channel, uint16_t first, uint16_t last) {
  if (!validChannel(channel) || first > last || last > kHcrMaxWav) return 0;
  return finish(snprintf(out, cap, "<C%c%04uC%04u>", channel, static_cast<unsigned>(first),
                         static_cast<unsigned>(last)),
                cap);
}

size_t hcrStimulate(char* out, size_t cap, uint8_t emotion, bool strong) {
  static const char kPrefix[] = "HSMC";
  if (emotion > 3) return 0;
  return finish(snprintf(out, cap, "<S%c%u>", kPrefix[emotion], strong ? 1u : 0u), cap);
}

size_t hcrStartup(char* out, size_t cap, const HcrSetup& s) {
  return finish(snprintf(out, cap, "<O1,OH%u,OS%u,OM%u,OC%u,PVV%u,PVA%u,PVB%u>", level(s.happy),
                         level(s.sad), level(s.mad), level(s.scared), level(s.vocalizerVolume),
                         level(s.wavAVolume), level(s.wavBVolume)),
                cap);
}

}  // namespace droid
