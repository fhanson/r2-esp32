#include "HoloEngine.h"

#include <stdlib.h>
#include <string.h>

namespace droid {

namespace {

constexpr uint32_t kOff = 0x000000, kRed = 0xFF0000, kOrange = 0xFF8000, kYellow = 0xFFFF00,
                   kGreen = 0x00FF00, kCyan = 0x00FFFF, kBlue = 0x0000FF, kMagenta = 0xFF00FF,
                   kPurple = 0x800080, kWhite = 0xFFFFFF;

// Flthy's colour tables: each row repeats shades (and off) so the projector
// effects flicker like a hologram. Row 0 is "random".
const uint32_t kShades[10][10] = {
    {kRed, kYellow, kGreen, kCyan, kBlue, kMagenta, kPurple, kWhite, kOff, kOff},
    {kRed, kRed, kRed, kWhite, 0xFFA0A0, 0xFD5555, 0xFFD3D3, kOff, kOff, kOff},
    {kYellow, kYellow, kYellow, kWhite, 0xFDFD43, 0xFFFF82, 0xFFFFBA, kOff, kOff, kOff},
    {kGreen, kGreen, kGreen, kWhite, 0x57FC57, 0x80FC80, 0xBDFFB1, kOff, kOff, kOff},
    {kCyan, kCyan, kCyan, kWhite, 0x38FFFF, 0x71FDFD, 0xA4FDFD, kOff, kOff, kOff},
    {kBlue, kBlue, kBlue, kWhite, 0xACACFF, 0x7676FF, 0x5A5AFF, kOff, kOff, kOff},
    {kMagenta, kMagenta, kMagenta, kWhite, 0xFB3BFB, 0xFD75FD, 0xFD9EFD, kOff, kOff, kOff},
    {kOrange, kOrange, kOrange, kWhite, 0xFB9B3A, 0xFFBE7D, 0xFCD2A7, kOff, kOff, kOff},
    {kPurple, kPurple, kPurple, kWhite, 0xA131A1, 0x9B449B, 0xBD5FBD, kOff, kOff, kOff},
    {kWhite, kWhite, kWhite, kWhite, 0xB7B6B6, 0x858484, 0xA09F9F, kOff, kOff, kOff},
};

uint8_t maskFor(char c) {
  switch (c) {
    case 'F': return kHoloFront;
    case 'T': return kHoloTop;
    case 'R': return kHoloRear;
    case 'A': return kHoloAll;
    case 'X': return kHoloFront | kHoloRear;
    case 'Y': return kHoloFront | kHoloTop;
    case 'Z': return kHoloRear | kHoloTop;
    default: return 0;
  }
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

uint32_t scale(uint32_t rgb, uint32_t level) {  // level 0..255
  const uint32_t r = ((rgb >> 16) & 0xFF) * level / 255;
  const uint32_t g = ((rgb >> 8) & 0xFF) * level / 255;
  const uint32_t b = (rgb & 0xFF) * level / 255;
  return (r << 16) | (g << 8) | b;
}

uint32_t wheel(uint32_t hue) {  // 0..767
  hue %= 768;
  if (hue < 256) return ((255 - hue) << 16) | (hue << 8);
  hue -= 256;
  if (hue < 256) return ((255 - hue) << 8) | hue;
  hue -= 256;
  return (hue << 16) | (255 - hue);
}

}  // namespace

bool parseHoloCommand(const char* text, HoloCommand& out) {
  out = HoloCommand{};
  if (text == nullptr || text[0] == '\0') return false;
  const char* bar = strchr(text, '|');
  size_t len = bar ? static_cast<size_t>(bar - text) : strlen(text);
  if (bar) {
    if (!isDigit(bar[1])) return false;
    const long s = strtol(bar + 1, nullptr, 10);
    if (s < 0 || s > 3600) return false;
    out.seconds = static_cast<uint16_t>(s);
  }
  if (text[0] == 'S') {
    if (len < 2 || len > 3) return false;
    for (size_t i = 1; i < len; ++i) if (!isDigit(text[i])) return false;
    out.type = HoloCommand::Type::Sequence;
    out.number = static_cast<uint8_t>(strtol(text + 1, nullptr, 10));
    out.mask = kHoloAll;
    return true;
  }
  out.mask = maskFor(text[0]);
  if (out.mask == 0 || len < 4 || len > 6) return false;
  for (size_t i = 1; i < len; ++i) if (!isDigit(text[i])) return false;
  if (text[1] != '0' && text[1] != '1') return false;
  out.type = text[1] == '0' ? HoloCommand::Type::Led : HoloCommand::Type::Servo;
  out.number = static_cast<uint8_t>((text[2] - '0') * 10 + (text[3] - '0'));
  if (len >= 5) out.colour = static_cast<int8_t>(text[4] - '0');
  if (len >= 6) out.speed = static_cast<int8_t>(text[5] - '0');
  return true;
}

uint32_t holoColour(uint8_t colour, uint32_t random) {
  if (colour > 9) colour = 9;
  if (colour == 0) return kShades[0][random % 8];  // never "off"
  return kShades[colour][0];
}

uint32_t HoloEngine::random() {
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

uint32_t HoloEngine::randomBetween(uint32_t lo, uint32_t hi) {
  if (hi <= lo) return lo;
  return lo + random() % (hi - lo + 1);
}

void HoloEngine::clear(size_t hp) {
  hp_[hp].effect = 0;
  hp_[hp].endMs = 0;
  hp_[hp].fromAuto = false;
  for (size_t i = 0; i < kMaxHoloLeds; ++i) pixels_[hp][i] = 0;
}

void HoloEngine::startLed(size_t hp, uint8_t effect, int8_t colour, int8_t speed, uint16_t seconds,
                          uint32_t nowMs, bool fromAuto) {
  HpState& s = hp_[hp];
  s.effect = effect;
  // Defaults when no colour is given: Leia blue, the rest Flthy's own picks.
  s.colour = colour >= 0 ? static_cast<uint8_t>(colour) : (effect == 7 ? 9 : 5);
  s.speed = speed >= 0 ? static_cast<uint8_t>(speed) : 5;
  s.startMs = nowMs;
  s.stepMs = nowMs;
  s.frame = 0;
  s.fromAuto = fromAuto;
  // Short circuit ends on its own; the rest run until changed or timed out.
  s.endMs = seconds ? nowMs + seconds * 1000u : 0;
}

void HoloEngine::preset(size_t hp, uint8_t position) {
  static const int16_t kX[9] = {0, 0, 0, -800, -800, -800, 800, 800, 800};
  static const int16_t kY[9] = {-800, 0, 800, 0, 800, -800, 0, 800, -800};
  if (position > 8) return;
  x_[hp] = kX[position];
  y_[hp] = kY[position];
}

// One command, or several separated by spaces (one per HP, say). All of
// them must read, or none is applied.
bool HoloEngine::command(const char* text, uint32_t nowMs) {
  constexpr size_t kMaxCommands = 8;
  HoloCommand cmds[kMaxCommands];
  size_t count = 0;
  const char* p = text ? text : "";
  while (*p) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* end = p;
    while (*end && *end != ' ') ++end;
    char one[16];
    const size_t len = static_cast<size_t>(end - p);
    if (len >= sizeof(one) || count >= kMaxCommands) return false;
    memcpy(one, p, len);
    one[len] = '\0';
    if (!parseHoloCommand(one, cmds[count])) return false;
    ++count;
    p = end;
  }
  if (count == 0) return false;
  for (size_t i = 0; i < count; ++i) apply(cmds[i], nowMs);
  return true;
}

void HoloEngine::apply(const HoloCommand& cmd, uint32_t nowMs) {
  if (cmd.type == HoloCommand::Type::Sequence) {
    switch (cmd.number) {
      case 1:  // Leia mode: front HP down, Leia on it, the others dark and still
        twitch_ = false;
        autoLed_ = false;
        for (size_t hp = 0; hp < kHoloCount; ++hp) clear(hp);
        preset(0, 0);
        startLed(0, 1, -1, -1, cmd.seconds, nowMs, false);
        break;
      case 4:
      case 7:
        twitch_ = false;
        autoLed_ = false;
        for (size_t hp = 0; hp < kHoloCount; ++hp) clear(hp);
        break;
      case 5:
      case 8:
      case 9:
        twitch_ = true;
        autoLed_ = true;
        for (size_t hp = 0; hp < kHoloCount; ++hp) {
          clear(hp);
          preset(hp, 1);
        }
        nextTwitchMs_ = nowMs + randomBetween(config_.twitchMinMs, config_.twitchMaxMs);
        nextAutoLedMs_ = nowMs + randomBetween(config_.autoLedMinMs, config_.autoLedMaxMs);
        break;
      default:
        break;
    }
    return;
  }
  for (size_t hp = 0; hp < kHoloCount; ++hp) {
    if (!(cmd.mask & (1u << hp))) continue;
    if (cmd.type == HoloCommand::Type::Led) {
      switch (cmd.number) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 7:
          startLed(hp, cmd.number, cmd.colour, cmd.speed, cmd.seconds, nowMs, false);
          break;
        case 96: case 98:
          clear(hp);
          autoLed_ = false;
          break;
        case 97: case 99:
          clear(hp);
          autoLed_ = true;
          nextAutoLedMs_ = nowMs + randomBetween(config_.autoLedMinMs, config_.autoLedMaxMs);
          break;
        default:
          break;
      }
    } else {
      HpState& s = hp_[hp];
      switch (cmd.number) {
        case 1:
          s.wag = 0;
          preset(hp, cmd.colour >= 0 ? static_cast<uint8_t>(cmd.colour) : 1);
          break;
        case 4:
          s.wag = 0;
          x_[hp] = static_cast<int16_t>(static_cast<int32_t>(random() % 1601) - 800);
          y_[hp] = static_cast<int16_t>(static_cast<int32_t>(random() % 1601) - 800);
          break;
        case 5:
        case 6:
          s.wag = cmd.number == 5 ? 1 : 2;
          s.wagCount = 0;
          s.wagMs = nowMs;
          break;
        case 98: twitch_ = false; break;
        case 99:
          twitch_ = true;
          nextTwitchMs_ = nowMs + randomBetween(config_.twitchMinMs, config_.twitchMaxMs);
          break;
        default: break;
      }
    }
  }
}

void HoloEngine::renderLed(size_t hp, uint32_t nowMs) {
  HpState& s = hp_[hp];
  const size_t n = config_.ledsPerHp > kMaxHoloLeds ? kMaxHoloLeds : config_.ledsPerHp;
  uint32_t* px = pixels_[hp];
  const uint32_t t = nowMs - s.startMs;
  switch (s.effect) {
    case 1:  // Leia: flickering shades of blue
    case 2:  // colour projector
      if (static_cast<int32_t>(nowMs - s.stepMs) >= 0) {
        const uint8_t row = s.effect == 1 ? 5 : (s.colour > 9 ? 9 : s.colour);
        for (size_t i = 0; i < n; ++i) px[i] = kShades[row][random() % 10];
        s.stepMs = nowMs + 60;
      }
      break;
    case 3: {  // dim pulse: speed 0 slow .. 9 fast
      const uint32_t period = 2400 - s.speed * 200u;
      const uint32_t phase = t % period;
      const uint32_t half = period / 2;
      const uint32_t level = (phase < half ? phase : period - phase) * 255 / half;
      const uint32_t c = holoColour(s.colour ? s.colour : 5, 0);
      for (size_t i = 0; i < n; ++i) px[i] = scale(c, level);
      break;
    }
    case 4:  // cycle: one lit LED walking round the ring
      if (static_cast<int32_t>(nowMs - s.stepMs) >= 0) {
        const uint32_t c = holoColour(s.colour, random());
        const size_t ring = n > 1 ? n - 1 : 1;
        for (size_t i = 0; i < n; ++i) px[i] = 0;
        px[n > 1 ? 1 + s.frame % ring : 0] = c;
        ++s.frame;
        s.stepMs = nowMs + 90;
      }
      break;
    case 5: {  // solid colour
      const uint32_t c = holoColour(s.colour, s.frame == 0 ? random() : 0);
      if (s.frame == 0) {
        for (size_t i = 0; i < n; ++i) px[i] = c;
        s.frame = 1;
      }
      break;
    }
    case 6:  // rainbow, turning
      for (size_t i = 0; i < n; ++i) px[i] = wheel(t / 4 + i * 768 / n);
      break;
    case 7:  // short circuit: flashes that slow down, then off
      if (static_cast<int32_t>(nowMs - s.stepMs) >= 0) {
        const bool on = (s.frame % 2) == 0;
        const uint32_t c = on ? holoColour(s.colour, random()) : 0;
        for (size_t i = 0; i < n; ++i) px[i] = c;
        ++s.frame;
        const uint32_t gap = 20 + s.frame * 8u;  // about 4.5 s in all
        if (gap > 260) {
          clear(hp);
          return;
        }
        s.stepMs = nowMs + gap;
      }
      break;
    default:
      for (size_t i = 0; i < n; ++i) px[i] = 0;
      break;
  }
}

void HoloEngine::update(uint32_t nowMs) {
  for (size_t hp = 0; hp < kHoloCount; ++hp) {
    HpState& s = hp_[hp];
    if (s.effect && s.endMs && static_cast<int32_t>(nowMs - s.endMs) >= 0) {
      clear(hp);
      if (autoLed_) nextAutoLedMs_ = nowMs + randomBetween(config_.autoLedMinMs, config_.autoLedMaxMs);
    }
    if (s.effect) renderLed(hp, nowMs);

    if (s.wag && static_cast<int32_t>(nowMs - s.wagMs) >= 0) {
      const int16_t v = (s.wagCount % 2) ? -700 : 700;
      if (s.wagCount >= 10) {
        s.wag = 0;
        x_[hp] = 0;
        y_[hp] = 0;
      } else if (s.wag == 1) {
        x_[hp] = v;
      } else {
        y_[hp] = v;
      }
      ++s.wagCount;
      s.wagMs = nowMs + 250;
    }
  }

  if (twitch_ && static_cast<int32_t>(nowMs - nextTwitchMs_) >= 0) {
    const size_t hp = random() % kHoloCount;
    if (!hp_[hp].wag) {
      x_[hp] = static_cast<int16_t>(static_cast<int32_t>(random() % 1401) - 700);
      y_[hp] = static_cast<int16_t>(static_cast<int32_t>(random() % 1401) - 700);
    }
    nextTwitchMs_ = nowMs + randomBetween(config_.twitchMinMs, config_.twitchMaxMs);
  }

  if (autoLed_ && static_cast<int32_t>(nowMs - nextAutoLedMs_) >= 0) {
    const size_t hp = random() % kHoloCount;
    if (hp_[hp].effect == 0) {
      const uint8_t effect = (random() % 2) ? 2 : 3;
      startLed(hp, effect, static_cast<int8_t>(random() % 10), -1,
               static_cast<uint16_t>(randomBetween(2, 4)), nowMs, true);
    }
    nextAutoLedMs_ = nowMs + randomBetween(config_.autoLedMinMs, config_.autoLedMaxMs);
  }
}

}  // namespace droid
