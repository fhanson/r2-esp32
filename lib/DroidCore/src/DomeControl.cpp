#include "DomeControl.h"

#include <stdio.h>
#include <stdlib.h>

namespace droid {

namespace {

size_t finish(int written, size_t cap) {
  if (written <= 0 || static_cast<size_t>(written) >= cap) return 0;
  return static_cast<size_t>(written);
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

const char* domeModeName(DomeMode mode) {
  switch (mode) {
    case DomeMode::Manual: return "manual";
    case DomeMode::Auto: return "auto";
    case DomeMode::Home: return "home";
  }
  return "?";
}

size_t domeHomeCommand(char* out, size_t cap) { return finish(snprintf(out, cap, ":DPH"), cap); }

size_t domeStopCommand(char* out, size_t cap) { return finish(snprintf(out, cap, ":DPZ"), cap); }

size_t domeGoToCommand(char* out, size_t cap, int degrees, int speed) {
  return finish(snprintf(out, cap, ":DPA%d,%d", clampInt(degrees, -360, 360), clampInt(speed, 0, 100)),
                cap);
}

bool parseDomeReport(const char* line, DomeReport& out) {
  if (line[0] != '#' || line[1] != 'D' || line[2] != 'P') return false;
  const char mode = line[3];
  if (mode != '@' && mode != '!' && mode != '$' && mode != '%') return false;
  const char* digits = line + 4;
  if (*digits == '\0') return false;
  char* end = nullptr;
  const long value = strtol(digits, &end, 10);
  if (end == digits || *end != '\0') return false;
  out.mode = mode;
  out.position = static_cast<int>(value);
  return true;
}

void DomeControl::setSpin(float spin, uint32_t nowMs) { setSpin(spin, nowMs, config_.spinTimeoutMs); }

void DomeControl::setSpin(float spin, uint32_t nowMs, uint32_t holdMs) {
  spin_ = spin > 1.0f ? 1.0f : (spin < -1.0f ? -1.0f : spin);
  spinAtMs_ = nowMs;
  spinHoldMs_ = holdMs;
  spinSet_ = true;
}

float DomeControl::spin(uint32_t nowMs) const {
  if (!spinSet_ || (nowMs - spinAtMs_) >= spinHoldMs_) return 0.0f;
  return spin_;
}

uint16_t DomeControl::neutralUs() const {
  return static_cast<uint16_t>((config_.pulseMinUs + config_.pulseMaxUs) / 2);
}

uint16_t DomeControl::pulseFor(float spin) const {
  float s = spin * (config_.spinMaxPercent > 100 ? 100 : config_.spinMaxPercent) / 100.0f;
  if (config_.invert) s = -s;
  const float centre = (config_.pulseMinUs + config_.pulseMaxUs) / 2.0f;
  const float half = (static_cast<float>(config_.pulseMaxUs) - config_.pulseMinUs) / 2.0f;
  const float us = centre + s * half;
  return static_cast<uint16_t>(us + 0.5f);
}

uint32_t DomeControl::nextRandom() {
  // xorshift32: plenty for picking angles and pauses.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

DomeOutput DomeControl::update(uint32_t nowMs, bool link, DomeMode mode) {
  DomeOutput out;
  out.pulseUs = neutralUs();

  if (!link) {
    // Stop the motor (neutral pulse) and cancel anything the Roam-a-Dome is
    // doing on its own. Manual spin had nothing queued, so it needs no stop.
    if (!stopped_ && active_ != DomeMode::Manual) {
      out.commandLength = domeStopCommand(out.command, sizeof(out.command));
    }
    stopped_ = true;
    active_ = DomeMode::Manual;
    spinSet_ = false;
    move_ = Move::None;
    return out;
  }

  if (stopped_ || mode != active_) {
    const bool wasRunningAuto = !stopped_ && active_ != DomeMode::Manual;
    switch (mode) {
      case DomeMode::Home:
        out.commandLength = domeHomeCommand(out.command, sizeof(out.command));
        break;
      case DomeMode::Manual:
        if (wasRunningAuto) out.commandLength = domeStopCommand(out.command, sizeof(out.command));
        break;
      case DomeMode::Auto:
        nextAutoMs_ = nowMs;  // first move straight away
        break;
    }
    active_ = mode;
    stopped_ = false;
  }

  switch (active_) {
    case DomeMode::Manual:
      out.pulseUs = pulseFor(spin(nowMs));
      break;
    case DomeMode::Auto:
      if (out.commandLength == 0 && static_cast<int32_t>(nowMs - nextAutoMs_) >= 0) {
        const int range = config_.autoDegrees > 360 ? 360 : config_.autoDegrees;
        const int degrees = static_cast<int>(nextRandom() % (2u * range + 1u)) - range;
        out.commandLength = domeGoToCommand(out.command, sizeof(out.command), degrees, config_.autoSpeed);
        uint32_t lo = config_.autoMinDelayMs, hi = config_.autoMaxDelayMs;
        if (hi < lo) {
          const uint32_t t = lo;
          lo = hi;
          hi = t;
        }
        nextAutoMs_ = nowMs + lo + (hi > lo ? nextRandom() % (hi - lo + 1) : 0);
      }
      break;
    case DomeMode::Home:
      break;
  }

  // A UI's one-off move goes out when nothing else needs the line this time.
  if (move_ != Move::None && out.commandLength == 0) {
    switch (move_) {
      case Move::Home: out.commandLength = domeHomeCommand(out.command, sizeof(out.command)); break;
      case Move::GoTo:
        out.commandLength = domeGoToCommand(out.command, sizeof(out.command), moveDegrees_, moveSpeed_);
        break;
      case Move::Stop: out.commandLength = domeStopCommand(out.command, sizeof(out.command)); break;
      case Move::None: break;
    }
    move_ = Move::None;
  }
  return out;
}

}  // namespace droid
