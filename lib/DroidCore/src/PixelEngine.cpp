#include "PixelEngine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace droid {

namespace {

constexpr uint32_t kWhite = 0xFFFFFF;

uint8_t r8(uint32_t c) { return static_cast<uint8_t>(c >> 16); }
uint8_t g8(uint32_t c) { return static_cast<uint8_t>(c >> 8); }
uint8_t b8(uint32_t c) { return static_cast<uint8_t>(c); }
uint32_t rgb(int r, int g, int b) {
  auto clamp = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
  return (static_cast<uint32_t>(clamp(r)) << 16) | (static_cast<uint32_t>(clamp(g)) << 8) | static_cast<uint32_t>(clamp(b));
}
uint32_t scale(uint32_t c, float k) {
  if (k <= 0.0f) return 0;
  if (k >= 1.0f) return c;
  return rgb(static_cast<int>(r8(c) * k + 0.5f), static_cast<int>(g8(c) * k + 0.5f), static_cast<int>(b8(c) * k + 0.5f));
}
uint32_t blend(uint32_t a, uint32_t b, float t) {
  return rgb(static_cast<int>(r8(a) + (r8(b) - r8(a)) * t + 0.5f), static_cast<int>(g8(a) + (g8(b) - g8(a)) * t + 0.5f),
             static_cast<int>(b8(a) + (b8(b) - b8(a)) * t + 0.5f));
}
// Hue 0-1 to a fully saturated colour.
uint32_t hue(float h) {
  h -= floorf(h);
  const float x = h * 6.0f;
  const int sector = static_cast<int>(x);
  const float f = x - sector;
  const int up = static_cast<int>(255 * f + 0.5f), down = 255 - up;
  switch (sector) {
    case 0: return rgb(255, up, 0);
    case 1: return rgb(down, 255, 0);
    case 2: return rgb(0, 255, up);
    case 3: return rgb(0, down, 255);
    case 4: return rgb(up, 0, 255);
    default: return rgb(255, 0, down);
  }
}
// A repeatable "random" number per pixel and time slot, for sparkle.
uint32_t mix(uint32_t a, uint32_t b) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}

// Pixels per second for a speed: s1 crawls, s9 races.
float pixelsPerSecond(uint8_t s) { return s == 0 ? 0.0f : 2.0f + s * s; }

bool word(const char* t, size_t n, const char* w) { return strlen(w) == n && strncmp(t, w, n) == 0; }

// "1.5" -> 15 tenths; false if it isn't a number.
bool tenths(const char* t, size_t n, uint32_t max, uint16_t& out) {
  uint32_t whole = 0, frac = 0;
  bool dot = false, any = false;
  int fracDigits = 0;
  for (size_t i = 0; i < n; ++i) {
    if (t[i] == '.' && !dot) { dot = true; continue; }
    if (t[i] < '0' || t[i] > '9') return false;
    any = true;
    if (!dot) {
      whole = whole * 10 + (t[i] - '0');
      if (whole > max) return false;
    } else if (fracDigits++ == 0) {
      frac = t[i] - '0';
    }
  }
  if (!any) return false;
  out = static_cast<uint16_t>(whole * 10 + frac);
  return true;
}

bool number(const char* t, size_t n, uint32_t lo, uint32_t hi, uint32_t& out) {
  if (n == 0 || n > 5) return false;
  uint32_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (t[i] < '0' || t[i] > '9') return false;
    v = v * 10 + (t[i] - '0');
  }
  if (v < lo || v > hi) return false;
  out = v;
  return true;
}

bool hexColour(const char* t, size_t n, uint32_t& out) {
  if (n != 7 || t[0] != '#') return false;
  uint32_t v = 0;
  for (size_t i = 1; i < 7; ++i) {
    const char c = t[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = v * 16 + d;
  }
  out = v;
  return true;
}

}  // namespace

bool parsePixelCommand(const char* text, PixelCommand& out) {
  PixelCommand c;
  const char* p = text ? text : "";
  bool first = true;
  while (*p) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const char* t = p;
    while (*p && *p != ' ') ++p;
    const size_t n = static_cast<size_t>(p - t);
    if (first) {
      first = false;
      static const struct { const char* name; PixelEffect e; } kNames[] = {
          {"off", PixelEffect::Off}, {"solid", PixelEffect::Solid}, {"rainbow", PixelEffect::Rainbow},
          {"wipe", PixelEffect::Wipe}, {"swoop", PixelEffect::Swoop}, {"scroll", PixelEffect::Scroll},
          {"pulse", PixelEffect::Pulse}, {"sparkle", PixelEffect::Sparkle}, {"setup", PixelEffect::Setup}};
      bool known = false;
      for (const auto& k : kNames) {
        if (word(t, n, k.name)) {
          c.effect = k.e;
          known = true;
        }
      }
      if (!known) return false;
      continue;
    }
    uint32_t v = 0;
    uint32_t colour = 0;
    if (c.effect == PixelEffect::Setup) {
      if (t[0] == 'n' && number(t + 1, n - 1, 1, kMaxPixels, v)) c.count = static_cast<uint8_t>(v);
      else if (t[0] == 'b' && number(t + 1, n - 1, 0, 100, v)) c.master = static_cast<uint8_t>(v);
      else return false;
    } else if (hexColour(t, n, colour)) {
      if (c.colourCount >= kMaxPixelColours) return false;
      c.colours[c.colourCount++] = colour;
    } else if (word(t, n, ">")) {
      c.direction = 1;
    } else if (word(t, n, "<")) {
      c.direction = -1;
    } else if (word(t, n, "bounce")) {
      c.bounce = true;
    } else if (t[0] == 's' && number(t + 1, n - 1, 0, 9, v)) {
      c.speed = static_cast<uint8_t>(v);
    } else if (t[0] == 'w' && number(t + 1, n - 1, 1, 20, v)) {
      c.width = static_cast<uint8_t>(v);
    } else if (t[0] == 'b' && number(t + 1, n - 1, 0, 100, v)) {
      c.brightness = static_cast<uint8_t>(v);
    } else if (t[0] == 'f' && n > 1 && tenths(t + 1, n - 1, 600, c.fadeTenths)) {
    } else if (t[0] == 't' && n > 1 && tenths(t + 1, n - 1, 3600, c.runTenths)) {
    } else {
      return false;
    }
  }
  if (first) return false;
  out = c;
  return true;
}

void renderPixels(const PixelCommand& cmd, uint32_t elapsedMs, size_t count, uint32_t* out) {
  if (count == 0) return;
  const size_t n = count > kMaxPixels ? kMaxPixels : count;
  const uint8_t k = cmd.colourCount ? cmd.colourCount : 1;
  auto colour = [&](size_t i) { return cmd.colourCount ? cmd.colours[i % k] : kWhite; };
  const float dist = pixelsPerSecond(cmd.speed) * elapsedMs / 1000.0f;  // pixels travelled
  const float level = cmd.brightness / 100.0f;
  // j is the position along the direction of travel.
  auto at = [&](size_t j) { return cmd.direction >= 0 ? j : n - 1 - j; };

  for (size_t i = 0; i < n; ++i) out[i] = 0;
  switch (cmd.effect) {
    case PixelEffect::Off:
    case PixelEffect::Setup:
      break;
    case PixelEffect::Solid:
      for (size_t j = 0; j < n; ++j) out[at(j)] = colour(j * k / n);
      break;
    case PixelEffect::Rainbow:
      for (size_t j = 0; j < n; ++j) out[at(j)] = hue((static_cast<float>(j) - dist) / n);
      break;
    case PixelEffect::Wipe: {
      const size_t pass = static_cast<size_t>(dist / n);
      const float head = dist - static_cast<float>(pass) * n;
      if (k == 1 && pass >= 1) {
        for (size_t j = 0; j < n; ++j) out[j] = colour(0);  // one colour: wipe once and hold
        break;
      }
      for (size_t j = 0; j < n; ++j) {
        out[at(j)] = j < head ? colour(pass) : (pass == 0 ? 0 : colour(pass + k - 1));
      }
      break;
    }
    case PixelEffect::Swoop: {
      const float w = cmd.width;
      float h;          // head position along the strip, 0..n-1
      int dir = 1;      // which way the head is moving, along j
      size_t pass;
      if (cmd.bounce) {
        const float span = n > 1 ? static_cast<float>(n - 1) : 1.0f;
        pass = static_cast<size_t>(dist / span);
        const float f = dist - pass * span;
        if (pass % 2 == 0) {
          h = f;
        } else {
          h = span - f;
          dir = -1;
        }
      } else {
        const float travel = n + w;  // the tail leaves before the next pass
        pass = static_cast<size_t>(dist / travel);
        h = dist - pass * travel;
      }
      const uint32_t c = colour(pass);
      for (size_t j = 0; j < n; ++j) {
        const float behind = (h - static_cast<float>(j)) * dir;  // pixels behind the head
        if (behind < 0.0f || behind >= w) continue;
        const float fade = 1.0f - behind / w;
        out[at(j)] = scale(c, fade * fade);
      }
      break;
    }
    case PixelEffect::Scroll: {
      const float unit = static_cast<float>(k) * cmd.width;
      for (size_t j = 0; j < n; ++j) {
        float x = fmodf(static_cast<float>(j) - dist, unit);
        if (x < 0) x += unit;
        out[at(j)] = colour(static_cast<size_t>(x / cmd.width));
      }
      break;
    }
    case PixelEffect::Pulse: {
      const float periodMs = 6000.0f / (cmd.speed + 1);
      const float phase = elapsedMs / periodMs;
      const size_t breath = static_cast<size_t>(phase);
      const float v = (1.0f - cosf(6.2831853f * (phase - breath))) / 2.0f;
      for (size_t j = 0; j < n; ++j) out[j] = scale(colour(breath), v);
      break;
    }
    case PixelEffect::Sparkle: {
      const uint32_t slotMs = 1000u / (cmd.speed + 2u);
      const uint32_t slot = elapsedMs / slotMs;
      const float within = 1.0f - static_cast<float>(elapsedMs % slotMs) / slotMs;
      for (size_t j = 0; j < n; ++j) {
        const uint32_t h = mix(static_cast<uint32_t>(j), slot);
        if (h % 6 == 0) out[j] = scale(colour(h >> 8), within);
      }
      break;
    }
  }
  if (level < 1.0f) {
    for (size_t i = 0; i < n; ++i) out[i] = scale(out[i], level);
  }
}

void PixelEngine::clearFrame() {
  for (size_t i = 0; i < kMaxPixels; ++i) from_[i] = frame_[i] = 0;
}

void PixelEngine::setup(size_t count, uint8_t masterPercent) {
  if (count >= 1 && count <= kMaxPixels) {
    for (size_t i = count; i < kMaxPixels; ++i) from_[i] = frame_[i] = 0;
    count_ = count;
  }
  if (masterPercent <= 100) master_ = masterPercent;
}

bool PixelEngine::command(const char* text, uint32_t nowMs) {
  PixelCommand c;
  if (!parsePixelCommand(text, c)) return false;
  apply(c, nowMs);
  return true;
}

void PixelEngine::startFade(uint32_t nowMs, uint32_t ms) {
  memcpy(from_, frame_, sizeof(frame_));
  fadeStartMs_ = nowMs;
  fadeMs_ = ms;
}

void PixelEngine::apply(const PixelCommand& cmd, uint32_t nowMs) {
  if (cmd.effect == PixelEffect::Setup) {
    setup(cmd.count ? cmd.count : count_, cmd.master <= 100 ? cmd.master : master_);
    return;
  }
  startFade(nowMs, cmd.fadeTenths * 100u);
  active_ = cmd;
  activeStartMs_ = nowMs;
  timed_ = cmd.runTenths != 0;
  if (!timed_) base_ = cmd;
}

void PixelEngine::update(uint32_t nowMs) {
  // A timed effect ends by fading (over its own fade time) back to the base.
  if (timed_ && nowMs - activeStartMs_ >= active_.runTenths * 100u) {
    const uint32_t back = active_.fadeTenths * 100u;
    timed_ = false;
    startFade(nowMs, back);
    active_ = base_;
    active_.fadeTenths = 0;
    activeStartMs_ = nowMs;
  }
  uint32_t now[kMaxPixels];
  renderPixels(active_, nowMs - activeStartMs_, count_, now);
  if (fadeMs_) {
    const uint32_t gone = nowMs - fadeStartMs_;
    if (gone >= fadeMs_) {
      fadeMs_ = 0;
    } else {
      const float t = static_cast<float>(gone) / fadeMs_;
      for (size_t i = 0; i < count_; ++i) now[i] = blend(from_[i], now[i], t);
    }
  }
  memcpy(frame_, now, count_ * sizeof(uint32_t));
}

uint32_t PixelEngine::pixel(size_t i) const {
  if (i >= count_) return 0;
  return scale(frame_[i], master_ / 100.0f);
}

}  // namespace droid
