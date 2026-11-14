#include "Animations.h"

#include <string.h>

namespace droid {

namespace {

void copyText(char* dst, size_t cap, const char* src) {
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) n = cap - 1;
  if (n) memcpy(dst, src, n);
  dst[n] = '\0';
}

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

bool validKind(uint8_t k) { return k >= 1 && k <= 9; }

bool hasText(StepKind k) {
  return k == StepKind::Holo || k == StepKind::Logic || k == StepKind::SoundPick || k == StepKind::Pixels;
}

// A short text field: length byte then the characters.
size_t putText(const char* s, size_t max, uint8_t* out, size_t cap) {
  const size_t n = strlen(s);
  if (n > max || 1 + n > cap) return 0;
  out[0] = static_cast<uint8_t>(n);
  memcpy(out + 1, s, n);
  return 1 + n;
}

size_t getText(const uint8_t* data, size_t length, char* dst, size_t max) {
  if (length < 1 || data[0] > max || 1u + data[0] > length) return 0;
  memcpy(dst, data + 1, data[0]);
  dst[data[0]] = '\0';
  return 1 + data[0];
}

}  // namespace

const char* stepKindName(StepKind kind) {
  switch (kind) {
    case StepKind::Sound: return "sound";
    case StepKind::RandomSound: return "random sound";
    case StepKind::Emotion: return "emotion";
    case StepKind::ServoSequence: return "servo sequence";
    case StepKind::Holo: return "holo";
    case StepKind::Logic: return "logic";
    case StepKind::SoundPick: return "sound pick";
    case StepKind::Dome: return "dome";
    case StepKind::Pixels: return "lights";
    case StepKind::None: break;
  }
  return "none";
}

AnimStep soundStep(uint16_t atMs, uint16_t file) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = StepKind::Sound;
  s.a = file;
  return s;
}

AnimStep randomSoundStep(uint16_t atMs, uint16_t first, uint16_t last) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = StepKind::RandomSound;
  s.a = first;
  s.b = last;
  return s;
}

AnimStep emotionStep(uint16_t atMs, uint8_t emotion) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = StepKind::Emotion;
  s.a = emotion;
  return s;
}

AnimStep servoStep(uint16_t atMs, uint8_t sequence) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = StepKind::ServoSequence;
  s.a = sequence;
  return s;
}

AnimStep textStep(uint16_t atMs, StepKind kind, const char* text) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = kind;
  copyText(s.text, sizeof(s.text), text);
  return s;
}

AnimStep domeStep(uint16_t atMs, DomeAction action, int16_t value, uint8_t extra) {
  AnimStep s;
  s.atMs = atMs;
  s.kind = StepKind::Dome;
  s.a = static_cast<uint16_t>(static_cast<uint8_t>(action) | (extra << 8));
  s.b = static_cast<uint16_t>(value);
  return s;
}

bool soundPickText(const uint16_t* files, size_t count, char* out, size_t cap) {
  uint8_t nibbles[kStepTextMax] = {};
  size_t used = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint16_t f = files[i];
    if (f < 1 || f > kSoundPickMaxFile) return false;
    const size_t at = (f - 1) / 4;
    nibbles[at] |= static_cast<uint8_t>(1u << ((f - 1) % 4));
    if (at + 1 > used) used = at + 1;
  }
  if (used + 1 > cap) return false;
  for (size_t i = 0; i < used; ++i) out[i] = "0123456789abcdef"[nibbles[i]];
  out[used] = '\0';
  return true;
}

size_t soundPickFiles(const char* text, uint16_t* out, size_t max) {
  size_t n = 0;
  const size_t len = text ? strlen(text) : 0;
  if (len > kStepTextMax) return 0;
  for (size_t i = 0; i < len; ++i) {
    const char c = text[i];
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else return 0;
    for (int bit = 0; bit < 4; ++bit) {
      if (!(v & (1 << bit))) continue;
      if (n >= max) return n;
      out[n++] = static_cast<uint16_t>(i * 4 + bit + 1);
    }
  }
  return n;
}

bool addStep(Animation& anim, const AnimStep& step) {
  if (anim.stepCount >= kMaxAnimSteps || !validKind(static_cast<uint8_t>(step.kind))) return false;
  if (strlen(step.text) > kStepTextMax) return false;
  anim.steps[anim.stepCount++] = step;
  return true;
}

uint16_t animationLastStepMs(const Animation& anim) {
  uint16_t last = 0;
  for (size_t i = 0; i < anim.stepCount; ++i) {
    if (anim.steps[i].atMs > last) last = anim.steps[i].atMs;
  }
  return last;
}

// Step: atMs(2) kind(1) a(2) b(2) [text]
size_t encodeStep(const AnimStep& step, uint8_t* out, size_t cap) {
  if (cap < 7 || !validKind(static_cast<uint8_t>(step.kind))) return 0;
  put16(out, step.atMs);
  out[2] = static_cast<uint8_t>(step.kind);
  put16(out + 3, step.a);
  put16(out + 5, step.b);
  if (!hasText(step.kind)) return 7;
  const size_t t = putText(step.text, kStepTextMax, out + 7, cap - 7);
  return t ? 7 + t : 0;
}

size_t decodeStep(const uint8_t* data, size_t length, AnimStep& step) {
  if (length < 7 || !validKind(data[2])) return 0;
  step = AnimStep{};
  step.atMs = get16(data);
  step.kind = static_cast<StepKind>(data[2]);
  step.a = get16(data + 3);
  step.b = get16(data + 5);
  if (!hasText(step.kind)) return 7;
  const size_t t = getText(data + 7, length - 7, step.text, kStepTextMax);
  return t ? 7 + t : 0;
}

// Animation: id(2) name icon count(1) steps...
size_t encodeAnimation(const Animation& anim, uint8_t* out, size_t cap) {
  if (cap < 2 || anim.stepCount > kMaxAnimSteps) return 0;
  put16(out, anim.id);
  size_t n = 2;
  size_t t = putText(anim.name, kAnimNameMax, out + n, cap - n);
  if (!t) return 0;
  n += t;
  t = putText(anim.icon, kAnimIconMax, out + n, cap - n);
  if (!t || n + t >= cap) return 0;
  n += t;
  out[n++] = anim.stepCount;
  for (size_t i = 0; i < anim.stepCount; ++i) {
    t = encodeStep(anim.steps[i], out + n, cap - n);
    if (!t) return 0;
    n += t;
  }
  return n;
}

bool decodeAnimation(const uint8_t* data, size_t length, Animation& anim) {
  anim = Animation{};
  if (length < 2) return false;
  anim.id = get16(data);
  size_t n = 2;
  size_t t = getText(data + n, length - n, anim.name, kAnimNameMax);
  if (!t) return false;
  n += t;
  t = getText(data + n, length - n, anim.icon, kAnimIconMax);
  if (!t || n + t >= length) return false;
  n += t;
  const uint8_t count = data[n++];
  if (count > kMaxAnimSteps) return false;
  for (uint8_t i = 0; i < count; ++i) {
    t = decodeStep(data + n, length - n, anim.steps[i]);
    if (!t) return false;
    n += t;
  }
  anim.stepCount = count;
  return n == length;
}

size_t defaultAnimations(Animation* out, size_t cap) {
  // Logic Engine commands.
  const char* kLogicRandom5 = "~RTLE990505";  // random pattern, 5 s
  struct Def {
    const char* name;
    const char* icon;
    AnimStep sound;
    int servo;          // -1 for none
    const char* holo;   // nullptr for none
    const char* holo2;  // a second holo command, at holo2Ms
    const char* logic;
    uint16_t holo2Ms;
  };
  const Def defs[] = {
      {"Happy chatter", "sfx-happy", randomSoundStep(0, 13, 17), -1, "A0033|5", nullptr, kLogicRandom5, 0},
      {"Whistles and woos", "sfx-whistles", randomSoundStep(0, 17, 25), -1, "S5", nullptr, kLogicRandom5, 0},
      {"Random set 3", "sfx-dice3", randomSoundStep(0, 25, 32), -1, "S5", nullptr, kLogicRandom5, 0},
      {"Random set 4", "sfx-dice4", randomSoundStep(0, 32, 52), -1, "S5", nullptr, kLogicRandom5, 0},
      {"Alarm", "sfx-alarm", soundStep(0, 1), -1, nullptr, nullptr, "0T3", 0},
      {"Chuckle and fart", "sfx-chuckle", soundStep(0, 2), -1, "F0028|4", nullptr, kLogicRandom5, 0},
      {"Doot doot and whistle", "sfx-doot", soundStep(0, 3), 10, nullptr, nullptr, kLogicRandom5, 0},
      {"Wolf whistle", "sfx-wolf", soundStep(0, 4), 8, "A0034|5", nullptr, "~RTLE66505", 0},
      {"System failure", "sfx-failure", soundStep(0, 6), 1, "A007", nullptr, "0T4", 0},
      {"Beep beep boop boop", "sfx-beep", soundStep(0, 7), -1, nullptr, nullptr, kLogicRandom5, 0},
      {"Whistle beep wooo", "sfx-woo", soundStep(0, 8), -1, nullptr, nullptr, kLogicRandom5, 0},
      {"Star Wars theme", "music-theme", soundStep(0, 9), -1, nullptr, nullptr, kLogicRandom5, 0},
      {"Cantina", "music-cantina", soundStep(0, 10), 2, "A006|60", nullptr, "~RTLE100560", 0},
      {"Imperial March", "music-march", soundStep(0, 11), -1, "S4", "A0051|30", "0T11", 100},
      {"Duel of the Fates", "music-duel", soundStep(0, 12), -1, "A0027|15", nullptr, "~RTLE69530", 0},
      // Leia mode holds the front HP down until told otherwise, so hand the
      // HPs back to their normal show when the 34 s message is over.
      {"Leia message", "sfx-message", soundStep(0, 5), -1, "S1", "S5", "~RTLE30000", 34000},
  };
  size_t count = 0;
  for (const Def& d : defs) {
    if (count >= cap) break;
    Animation& a = out[count];
    a = Animation{};
    a.id = static_cast<uint16_t>(200 + count);
    copyText(a.name, sizeof(a.name), d.name);
    copyText(a.icon, sizeof(a.icon), d.icon);
    addStep(a, d.sound);
    if (d.servo >= 0) addStep(a, servoStep(0, static_cast<uint8_t>(d.servo)));
    if (d.holo) addStep(a, textStep(0, StepKind::Holo, d.holo));
    if (d.holo2) addStep(a, textStep(d.holo2Ms, StepKind::Holo, d.holo2));
    if (d.logic) addStep(a, textStep(0, StepKind::Logic, d.logic));
    ++count;
  }
  // Chortle No: a chortle and a shake of the head.
  if (count < cap) {
    Animation& a = out[count];
    a = Animation{};
    a.id = static_cast<uint16_t>(200 + count);
    copyText(a.name, sizeof(a.name), "Chortle No");
    copyText(a.icon, sizeof(a.icon), "sfx-chuckle");
    addStep(a, soundStep(0, 2));
    addStep(a, domeStep(0, DomeAction::GoTo, -30, 60));
    addStep(a, domeStep(1000, DomeAction::GoTo, 30, 60));
    addStep(a, domeStep(2000, DomeAction::Home));
    ++count;
  }
  return count;
}

void AnimationPlayer::start(const Animation& anim, uint32_t nowMs) {
  anim_ = anim;
  // Steps play in time order; keep the stored order for equal times.
  for (size_t i = 1; i < anim_.stepCount; ++i) {
    AnimStep s = anim_.steps[i];
    size_t j = i;
    while (j > 0 && anim_.steps[j - 1].atMs > s.atMs) {
      anim_.steps[j] = anim_.steps[j - 1];
      --j;
    }
    anim_.steps[j] = s;
  }
  nextStep_ = 0;
  startMs_ = nowMs;
  running_ = anim_.stepCount > 0;
}

const AnimStep* AnimationPlayer::next(uint32_t nowMs) {
  if (!running_) return nullptr;
  if (nextStep_ >= anim_.stepCount) {
    running_ = false;
    return nullptr;
  }
  const AnimStep& s = anim_.steps[nextStep_];
  if (nowMs - startMs_ < s.atMs) return nullptr;
  ++nextStep_;
  if (nextStep_ >= anim_.stepCount) running_ = false;
  return &s;
}

}  // namespace droid
