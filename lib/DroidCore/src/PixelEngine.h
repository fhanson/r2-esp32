#pragma once
// NeoPixel strip effects for the body C3 (the utility arm panel's strip).
// Decides each pixel's colour; the body C3 writes them to the strip.
//
// Commands are short text, the same from an animation's Lights item, from the
// S3 ("X<command>" over ESP-NOW) or typed on the body C3's console:
//
//   <effect> [#rrggbb ... up to 4] [> or <] [s0-9] [w1-20] [f<sec>] [t<sec>] [b0-100] [bounce]
//
//   off      dark. With f, fades out whatever is showing.
//   solid    the colour on every pixel; several colours sit in bands
//   rainbow  a rainbow along the strip, moving (s0 holds it still)
//   wipe     each colour in turn wipes across the strip
//   swoop    a bright head with a fading tail w pixels long sweeps across,
//            the next colour each pass; with bounce it goes back and forth
//   scroll   the colours as a repeating pattern, w pixels each, scrolling
//   pulse    the colours breathe in and out, one after another
//   sparkle  random pixels flash the colours
//
//   > left to right (the default), < right to left
//   s  speed 0-9 (default 5)
//   w  width 1-20: swoop's tail, scroll's bands (default 4)
//   f  fade in over this many seconds (tenths allowed), from what was showing
//   t  run this many seconds, then fade (over f) back to what showed before;
//      0 or none keeps it until the next command
//   b  brightness, % of the strip's own brightness setting (default 100)
//
//   setup n<count> b<percent>   strip length (1-64) and overall brightness,
//                               from the S3's settings
//
// With no colour, solid, wipe, swoop, scroll, pulse and sparkle use white.

#include <stddef.h>
#include <stdint.h>

namespace droid {

constexpr size_t kMaxPixels = 64;
constexpr size_t kMaxPixelColours = 4;

enum class PixelEffect : uint8_t { Off, Solid, Rainbow, Wipe, Swoop, Scroll, Pulse, Sparkle, Setup };

struct PixelCommand {
  PixelEffect effect = PixelEffect::Off;
  uint32_t colours[kMaxPixelColours] = {};  // 0xRRGGBB
  uint8_t colourCount = 0;
  int8_t direction = 1;       // 1 left to right, -1 right to left
  uint8_t speed = 5;          // 0-9
  uint8_t width = 4;          // 1-20
  uint16_t fadeTenths = 0;    // fade in, tenths of a second
  uint16_t runTenths = 0;     // 0 = until the next command
  uint8_t brightness = 100;   // % of the strip's brightness
  bool bounce = false;
  // setup only
  uint8_t count = 0;          // 0 = unchanged
  uint8_t master = 255;       // 255 = unchanged
};

// False if the text isn't a command this engine understands.
bool parsePixelCommand(const char* text, PixelCommand& out);

// One frame of an effect, ms after it started, before any fade or the
// strip's overall brightness. out holds count colours.
void renderPixels(const PixelCommand& cmd, uint32_t elapsedMs, size_t count, uint32_t* out);

class PixelEngine {
 public:
  PixelEngine() { clearFrame(); }

  // Applies text; false if it can't be read.
  bool command(const char* text, uint32_t nowMs);
  void apply(const PixelCommand& cmd, uint32_t nowMs);

  // Call every frame (about every 20 ms).
  void update(uint32_t nowMs);

  size_t count() const { return count_; }
  uint8_t master() const { return master_; }  // overall brightness %
  void setup(size_t count, uint8_t masterPercent);

  // The colour to write, with the overall brightness applied.
  uint32_t pixel(size_t i) const;
  // Before the overall brightness (for tests).
  uint32_t raw(size_t i) const { return i < count_ ? frame_[i] : 0; }
  bool fading() const { return fadeMs_ != 0; }

 private:
  void clearFrame();
  void startFade(uint32_t nowMs, uint32_t ms);

  size_t count_ = 28;
  uint8_t master_ = 40;
  PixelCommand base_;    // the last command without a run time
  PixelCommand active_;  // what's showing (base_ or a timed one)
  uint32_t activeStartMs_ = 0;
  bool timed_ = false;
  uint32_t fadeStartMs_ = 0;
  uint32_t fadeMs_ = 0;
  uint32_t from_[kMaxPixels];   // the frame a fade starts from
  uint32_t frame_[kMaxPixels];  // the current frame
};

}  // namespace droid
