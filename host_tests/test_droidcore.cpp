// Host-side tests for the Arduino-free DroidCore code.
// Build and run on a PC:
//   g++ -std=c++17 -Wall -Wextra -I lib/DroidCore/src -I src/body
//       host_tests/test_droidcore.cpp lib/DroidCore/src/*.cpp -o droidcore_tests
//       && ./droidcore_tests

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Animations.h"
#include "DomeControl.h"
#include "DomeLink.h"
#include "HoloEngine.h"
#include "ServoSequences.h"
#include "Version.h"
#include "Ota.h"
#include "CustomSequences.h"
#include "PixelEngine.h"
#include "UiProtocol.h"
#include "DriveArming.h"
#include "DriveMixer.h"
#include "DriveRamp.h"
#include "HcrCommands.h"
#include "HcrEvents.h"
#include "LinkProtocol.h"
#include "BodyConfig.h"
#include "RcChannel.h"
#include "SbusParser.h"
#include "Settings.h"

using namespace droid;

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
    }                                                                   \
  } while (0)

static bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// Build a 25-byte SBUS frame from 16 channel values.
static std::vector<uint8_t> encode(const uint16_t ch[16], uint8_t flags, uint8_t footer) {
  std::vector<uint8_t> b(25, 0);
  b[0] = 0x0F;
  uint32_t bits = 0;
  int nbits = 0;
  size_t idx = 1;
  for (int c = 0; c < 16; ++c) {
    bits |= static_cast<uint32_t>(ch[c] & 0x7FF) << nbits;
    nbits += 11;
    while (nbits >= 8) {
      b[idx++] = bits & 0xFF;
      bits >>= 8;
      nbits -= 8;
    }
  }
  b[23] = flags;
  b[24] = footer;
  return b;
}

static void testSbus() {
  uint16_t ch[16];
  for (int i = 0; i < 16; ++i) ch[i] = static_cast<uint16_t>(172 + i * 100);
  ch[0] = 1811;
  ch[15] = 0x7FF;

  // Standard footer.
  {
    SbusParser p;
    auto f = encode(ch, 0x00, 0x00);
    bool got = false;
    uint32_t t = 0;
    for (uint8_t b : f) got = p.feed(b, t += 120);
    CHECK(got);
    for (int i = 0; i < 16; ++i) CHECK(p.frame().ch[i] == ch[i]);
    CHECK(!p.frame().lostFrame && !p.frame().failsafe);
  }
  // HotRC footer 0x04 plus flags.
  {
    SbusParser p;
    auto f = encode(ch, 0x0C, 0x04);
    bool got = false;
    uint32_t t = 0;
    for (uint8_t b : f) got = p.feed(b, t += 120);
    CHECK(got);
    CHECK(p.frame().lostFrame && p.frame().failsafe);
  }
  // Bad footer rejected.
  {
    SbusParser p;
    auto f = encode(ch, 0x00, 0xAA);
    bool got = false;
    uint32_t t = 0;
    for (uint8_t b : f) got = p.feed(b, t += 120);
    CHECK(!got);
    CHECK(p.badFrames() == 1);
  }
  // Starts mid-frame: junk then a gap, then a full frame.
  {
    SbusParser p;
    auto f = encode(ch, 0x00, 0x04);
    uint32_t t = 0;
    for (size_t i = 10; i < f.size(); ++i) p.feed(f[i], t += 120);
    t += 7000;  // inter-frame gap
    bool got = false;
    for (uint8_t b : f) got = p.feed(b, t += 120);
    CHECK(got);
    CHECK(p.frame().ch[3] == ch[3]);
  }
  // Two frames back to back with no timing gap (bytes read in one burst).
  {
    SbusParser p;
    auto a = encode(ch, 0x00, 0x04);
    int good = 0;
    for (int rep = 0; rep < 3; ++rep)
      for (uint8_t b : a) good += p.feed(b, 0) ? 1 : 0;
    CHECK(good == 3);
  }
}

static void testChannels() {
  ChannelCal c;  // 172 / 992 / 1811
  CHECK(near(normalize(992, c), 0.0f));
  CHECK(near(normalize(1811, c), 1.0f));
  CHECK(near(normalize(172, c), -1.0f));
  CHECK(near(normalize(2047, c), 1.0f));  // clamped
  c.invert = true;
  CHECK(near(normalize(1811, c), -1.0f));

  ChannelCal d;
  d.deadband = 0.15f;
  CHECK(normalize(992 + 100, d) == 0.0f);            // inside deadband
  CHECK(near(normalize(1811, d), 1.0f));             // still reaches full
  CHECK(normalize(992 + 200, d) > 0.0f);             // just outside

  // Trigger resting at an endpoint: rest = rawCenter.
  ChannelCal trig;
  trig.rawMin = 172;
  trig.rawCenter = 172;
  trig.rawMax = 1811;
  CHECK(near(normalize(172, trig), 0.0f));
  CHECK(near(normalize(1811, trig), 1.0f));

  ChannelCal s;
  CHECK(decodeSwitch3(172, s) == 0);
  CHECK(decodeSwitch3(992, s) == 1);
  CHECK(decodeSwitch3(1811, s) == 2);

  ChannelCal en;
  en.rawMin = 172;
  en.rawCenter = 172;
  en.rawMax = 1811;
  CHECK(!decodeSwitch2(172, en));
  CHECK(decodeSwitch2(1811, en));
}

static void testMixer() {
  MixerConfig m;
  m.curveOnSteer = false;
  FeetSpeed s;

  s = mixDrive(0, 0, 1, m);
  CHECK(s.left == 0 && s.right == 0);

  s = mixDrive(1, 0, 1, m);
  CHECK(near(s.left, 1) && near(s.right, 1));

  s = mixDrive(-1, 0, 1, m);
  CHECK(near(s.left, -1) && near(s.right, -1));

  // Speed level scales output.
  s = mixDrive(1, 0, 50.0f / 127.0f, m);
  CHECK(near(s.left, 50.0f / 127.0f));

  // No spin in place by default (Padawan360 PWM behaviour).
  s = mixDrive(0, 1, 1, m);
  CHECK(s.left == 0 && s.right == 0);
  m.spinInPlace = true;
  s = mixDrive(0, 1, 1, m);
  CHECK(near(s.left, 1) && near(s.right, -1));
  m.spinInPlace = false;

  // Diamond clamp: full throttle + full right -> left full, right stopped.
  s = mixDrive(1, 1, 1, m);
  CHECK(near(s.left, 1) && near(s.right, 0));

  // Outputs never exceed +-1.
  for (float t = -1; t <= 1.001f; t += 0.1f)
    for (float st = -1; st <= 1.001f; st += 0.1f) {
      s = mixDrive(t, st, 1, m);
      CHECK(std::fabs(s.left) <= 1.0001f && std::fabs(s.right) <= 1.0001f);
    }

  // Inversion and swap.
  m.invertLeft = true;
  s = mixDrive(1, 0, 1, m);
  CHECK(near(s.left, -1) && near(s.right, 1));
  m.invertLeft = false;

  // Steering curve: ends fixed, middle softened, odd symmetric.
  CHECK(near(steeringCurve(0, 100), 0));
  CHECK(near(steeringCurve(1, 100), 1));
  CHECK(near(steeringCurve(-1, 100), -1));
  CHECK(steeringCurve(0.5f, 100) < 0.5f);
  CHECK(near(steeringCurve(0.5f, 100), 0.5f * std::sqrt(150.0f) / std::sqrt(200.0f)));
}

static void testPulses() {
  PulseRange r;  // 544..2400, like Arduino Servo
  CHECK(speedToPulseUs(0, r) == 1472);
  CHECK(speedToPulseUs(1, r) == 2400);
  CHECK(speedToPulseUs(-1, r) == 544);
  CHECK(servoAngleToPulseUs(90, r) == 1472);  // Mega's stopFeet() write(90)
  CHECK(servoAngleToPulseUs(0, r) == 544);
  CHECK(servoAngleToPulseUs(180, r) == 2400);
}

static void testArming() {
  DriveArming a(300);
  uint32_t t = 0;
  // Boot with switch already on: must not arm.
  CHECK(!a.update(t, true, true, true));
  CHECK(!a.update(t += 1000, true, true, true));
  CHECK(a.waitingForSwitchOff());
  // Switch off, then on with sticks centred: arms after hold time.
  CHECK(!a.update(t += 10, true, false, true));
  CHECK(!a.update(t += 10, true, true, true));
  CHECK(!a.update(t += 200, true, true, true));
  CHECK(a.update(t += 150, true, true, true));
  // Sticks moving while armed: stays armed.
  CHECK(a.update(t += 10, true, true, false));
  // Link lost: disarms at once.
  CHECK(!a.update(t += 10, false, true, false));
  // Link back, stick still pushed: stays disarmed.
  CHECK(!a.update(t += 10, true, true, false));
  CHECK(!a.update(t += 500, true, true, false));
  // Centre sticks: re-arms after hold, no switch cycle needed.
  CHECK(!a.update(t += 10, true, true, true));
  CHECK(a.update(t += 310, true, true, true));
  // Switch off: disarms.
  CHECK(!a.update(t += 10, true, false, true));
  // Arming with sticks off-centre is refused.
  CHECK(!a.update(t += 10, true, true, false));
  CHECK(!a.update(t += 1000, true, true, false));
}

// Run a ramp for `seconds` in 10 ms steps.
static void runRamp(DriveRamp& r, float thr, float steer, float scale, float seconds) {
  for (int i = 0; i < static_cast<int>(seconds / 0.01f + 0.5f); ++i) r.update(thr, steer, scale, 0.01f);
}

static void testRamp() {
  // Curve: ends fixed, softer near centre, symmetric.
  CHECK(near(throttleCurve(1, 0.3f), 1));
  CHECK(near(throttleCurve(-1, 0.3f), -1));
  CHECK(near(throttleCurve(0.5f, 0.3f), 0.7f * 0.5f + 0.3f * 0.125f));
  CHECK(near(throttleCurve(0.5f, 0), 0.5f));

  // rampToward basics.
  CHECK(near(rampToward(0, 1, 2, 4, 0.1f), 0.2f));      // accelerate at 2/s
  CHECK(near(rampToward(1, 0, 2, 4, 0.1f), 0.6f));      // decelerate at 4/s
  CHECK(near(rampToward(0.1f, -1, 2, 4, 0.1f), 0.0f));  // crossing stops at zero first
  CHECK(near(rampToward(0.5f, -1, 2, 4, 0.1f), 0.1f));  // ...at the decel rate
  CHECK(near(rampToward(0.95f, 1, 2, 4, 0.1f), 1.0f));  // no overshoot

  RampConfig cfg;
  cfg.throttleExpo = 0;
  cfg.throttleAccelS = 0.6f;
  cfg.throttleDecelS = 0.3f;
  cfg.speedChangeS = 0.5f;
  DriveRamp r(cfg);
  r.reset(1.0f);

  runRamp(r, 1, 0, 1, 0.3f);
  CHECK(near(r.throttle(), 0.5f, 0.02f));  // half way after half the accel time
  runRamp(r, 1, 0, 1, 0.3f);
  CHECK(near(r.throttle(), 1.0f, 0.02f));  // full after 0.6 s
  runRamp(r, 0, 0, 1, 0.15f);
  CHECK(near(r.throttle(), 0.5f, 0.02f));  // decel twice as fast
  runRamp(r, 0, 0, 1, 0.2f);
  CHECK(r.throttle() == 0.0f);             // lands exactly on zero

  // Reversal from full forward: through zero (0.3 s) then up in reverse.
  runRamp(r, 1, 0, 1, 1.0f);
  runRamp(r, -1, 0, 1, 0.29f);
  CHECK(r.throttle() >= 0.0f);
  runRamp(r, -1, 0, 1, 0.31f);
  CHECK(r.throttle() < 0.0f && r.throttle() > -0.6f);

  // Speed level change ramps the scale.
  r.reset(1.0f);
  runRamp(r, 0, 0, 50.0f / 127.0f, 0.25f);
  CHECK(r.speedScale() < 1.0f && r.speedScale() > 50.0f / 127.0f);
  runRamp(r, 0, 0, 50.0f / 127.0f, 0.5f);
  CHECK(near(r.speedScale(), 50.0f / 127.0f));

  // A long stall is clamped to maxStepS (0.05 s), not a jump to target.
  r.reset(1.0f);
  r.update(1, 0, 1, 5.0f);
  CHECK(near(r.throttle(), 0.05f / 0.6f, 0.005f));

  // reset() stops at once.
  runRamp(r, 1, 1, 1, 1.0f);
  r.reset(0.5f);
  CHECK(r.throttle() == 0 && r.steer() == 0 && near(r.speedScale(), 0.5f));
}

static void testHcrCommands() {
  char b[64];

  // Examples from the HCR command reference.
  CHECK(hcrPlayWav(b, sizeof(b), 'A', 25) == 8 && !std::strcmp(b, "<CA0025>"));
  CHECK(hcrPlayRandomWav(b, sizeof(b), 'B', 3, 185) == 13 && !std::strcmp(b, "<CB0003C0185>"));

  // The file range ends are valid, and past them is not.
  CHECK(hcrPlayWav(b, sizeof(b), 'B', 0) && !std::strcmp(b, "<CB0000>"));
  CHECK(hcrPlayWav(b, sizeof(b), 'A', 9999) && !std::strcmp(b, "<CA9999>"));
  CHECK(hcrPlayWav(b, sizeof(b), 'A', 10000) == 0);
  CHECK(hcrPlayRandomWav(b, sizeof(b), 'A', 0, 10000) == 0);
  CHECK(hcrPlayRandomWav(b, sizeof(b), 'A', 52, 9) == 0);
  CHECK(hcrPlayRandomWav(b, sizeof(b), 'A', 7, 7) && !std::strcmp(b, "<CA0007C0007>"));

  // Only channels A and B exist.
  CHECK(hcrPlayWav(b, sizeof(b), 'C', 1) == 0);
  CHECK(hcrPlayWav(b, sizeof(b), 'a', 1) == 0);
  CHECK(hcrPlayRandomWav(b, sizeof(b), 'V', 1, 2) == 0);

  // The buffer must hold the command and its NUL, or the result is 0.
  CHECK(hcrPlayWav(b, 8, 'A', 25) == 0);
  CHECK(hcrPlayWav(b, 9, 'A', 25) == 8);

  // Startup matches the Mega's values.
  HcrSetup s;
  s.happy = 89;
  s.sad = 22;
  CHECK(hcrStartup(b, sizeof(b), s) > 0);
  CHECK(!std::strcmp(b, "<O1,OH89,OS22,OM0,OC0,PVV100,PVA80,PVB80>"));

  // Levels clamp to 100.
  s.vocalizerVolume = 255;
  s.wavAVolume = 101;
  s.wavBVolume = 7;
  s.scared = 101;
  CHECK(hcrStartup(b, sizeof(b), s) > 0);
  CHECK(!std::strcmp(b, "<O1,OH89,OS22,OM0,OC100,PVV100,PVA100,PVB7>"));

  CHECK(hcrStartup(b, 10, s) == 0);
}

static void testHcrEvents() {
  // Link up fires once.
  {
    HcrEvents e(0);
    CHECK(!e.update(0, false, false, false).connected);
    CHECK(e.update(0, true, false, false).connected);
    CHECK(!e.update(0, true, false, false).connected);
    e.update(0, false, false, false);
    CHECK(e.update(0, true, false, false).connected);
  }

  // Arm and disarm each fire once.
  {
    HcrEvents e(0);
    e.update(0, true, false, false);
    CHECK(e.update(0, true, true, false).armed);
    HcrTriggers t = e.update(0, true, true, false);
    CHECK(!t.armed && !t.disarmed);
    CHECK(e.update(0, true, false, false).disarmed);
    t = e.update(0, true, false, false);
    CHECK(!t.armed && !t.disarmed);
  }

  // Losing the link while armed reports the disarm.
  {
    HcrEvents e(0);
    e.update(0, true, true, false);
    CHECK(e.update(0, false, false, false).disarmed);
  }

  // A press fires once however long it's held, and again after release.
  {
    HcrEvents e(0);
    e.update(0, true, false, false);
    CHECK(e.update(0, true, false, true).soundPress);
    CHECK(!e.update(0, true, false, true).soundPress);
    CHECK(!e.update(0, true, false, false).soundPress);
    CHECK(e.update(0, true, false, true).soundPress);
  }

  // Held from the first frame: nothing until released and pressed again.
  {
    HcrEvents e(0);
    CHECK(!e.update(0, true, false, true).soundPress);
    CHECK(!e.update(0, true, false, true).soundPress);
    e.update(0, true, false, false);
    CHECK(e.update(0, true, false, true).soundPress);
  }

  // Held through a dropout: the link coming back is not a press.
  {
    HcrEvents e(0);
    e.update(0, true, false, false);
    e.update(0, false, false, false);
    CHECK(!e.update(0, true, false, true).soundPress);
    CHECK(!e.update(0, true, false, true).soundPress);
  }

  // No press without a link.
  {
    HcrEvents e(0);
    e.update(0, true, false, false);
    CHECK(!e.update(0, false, false, true).soundPress);
  }

  // A press inside the gap is ignored, not saved for later.
  {
    HcrEvents e(5000);
    e.update(0, false, false, false);
    e.update(1, false, false, false);
    CHECK(!e.update(2, false, false, true).soundPress);  // no link yet
    e.update(3, false, false, false);
    e.update(10, true, false, false);                    // 0021 starts the gap
    CHECK(!e.update(20, true, false, true).soundPress);
    e.update(30, true, false, false);
    CHECK(!e.update(5009, true, false, true).soundPress);
    e.update(5010, true, false, false);
    CHECK(!e.update(9000, true, false, false).soundPress);
    CHECK(e.update(9001, true, false, true).soundPress);
    // That press starts a new gap.
    e.update(9002, true, false, false);
    CHECK(!e.update(14000, true, false, true).soundPress);
    e.update(14001, true, false, false);
    CHECK(e.update(14002, true, false, true).soundPress);
  }

  // Held through the gap: it doesn't fire when the gap ends.
  {
    HcrEvents e(5000);
    e.update(0, true, false, false);
    CHECK(!e.update(100, true, false, true).soundPress);
    CHECK(!e.update(6000, true, false, true).soundPress);
    e.update(6001, true, false, false);
    CHECK(e.update(6002, true, false, true).soundPress);
  }

  // Status sounds ignore the gap, so a disarm is always heard.
  {
    HcrEvents e(5000);
    e.update(0, true, false, false);
    CHECK(e.update(100, true, true, false).armed);
    CHECK(e.update(200, true, false, false).disarmed);
    CHECK(e.update(300, false, false, false).disarmed == false);
    CHECK(e.update(400, true, false, false).connected);
  }

  // The gap survives millis() wrapping round.
  {
    HcrEvents e(5000);
    e.update(0xFFFFFF00u, true, false, false);
    e.update(0xFFFFFF01u, true, false, false);
    CHECK(!e.update(100, true, false, true).soundPress);
    e.update(101, true, false, false);
    CHECK(e.update(5000, true, false, true).soundPress);
  }
}

class FakeBackend : public SettingsBackend {
 public:
  bool load(const char* key, int32_t& value) override {
    auto it = stored.find(key);
    if (it == stored.end()) return false;
    value = it->second;
    return true;
  }
  bool save(const char* key, int32_t value) override {
    ++saves;
    if (failWrites) return false;
    stored[key] = value;
    return true;
  }
  bool erase(const char* key) override {
    if (failWrites) return false;
    stored.erase(key);
    return true;
  }
  std::map<std::string, int32_t> stored;
  int saves = 0;
  bool failWrites = false;
};

static void testSettings() {
  const SettingDef defs[] = {{"vol", 80, 0, 100}, {"gap", 5000, 100, 60000}};

  // Defaults until something is saved.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    CHECK(s.get(0) == 80 && s.get(1) == 5000);
    s.load();
    CHECK(s.get(0) == 80 && !s.overridden(0));
  }

  // Find.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    CHECK(s.find("vol") == 0 && s.find("gap") == 1 && s.find("nope") == -1 && s.find("") == -1);
  }

  // An override takes effect, is saved, and survives a reboot.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    CHECK(s.set(0, 60) == SettingResult::Ok);
    CHECK(s.get(0) == 60 && s.overridden(0) && b.stored["vol"] == 60);
    Settings rebooted(defs, 2, b);
    rebooted.load();
    CHECK(rebooted.get(0) == 60 && rebooted.overridden(0));
    CHECK(rebooted.get(1) == 5000 && !rebooted.overridden(1));
  }

  // The range ends are allowed, and past them changes nothing.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    CHECK(s.set(0, 0) == SettingResult::Ok && s.get(0) == 0);
    CHECK(s.set(0, 100) == SettingResult::Ok && s.get(0) == 100);
    CHECK(s.set(0, 101) == SettingResult::OutOfRange && s.get(0) == 100);
    CHECK(s.set(0, -1) == SettingResult::OutOfRange && s.get(0) == 100);
    CHECK(b.stored["vol"] == 100);
  }

  // Clearing goes back to the default, and stays cleared after a reboot.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    s.set(0, 60);
    CHECK(s.clear(0) == SettingResult::Ok);
    CHECK(s.get(0) == 80 && !s.overridden(0) && b.stored.count("vol") == 0);
    Settings rebooted(defs, 2, b);
    rebooted.load();
    CHECK(rebooted.get(0) == 80 && !rebooted.overridden(0));
    CHECK(s.clear(1) == SettingResult::Ok);  // nothing to clear is fine
  }

  // Setting the default value still counts as an override.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    CHECK(s.set(0, 80) == SettingResult::Ok && s.overridden(0) && b.saves == 1);
  }

  // Repeating a value doesn't write the flash again.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    s.set(0, 60);
    s.set(0, 60);
    s.set(0, 60);
    CHECK(b.saves == 1);
    s.set(0, 61);
    CHECK(b.saves == 2);
  }

  // A saved value outside today's limits is ignored.
  {
    FakeBackend b;
    b.stored["vol"] = 500;
    Settings s(defs, 2, b);
    s.load();
    CHECK(s.get(0) == 80 && !s.overridden(0));
  }

  // load() replaces earlier values rather than keeping them.
  {
    FakeBackend b;
    Settings s(defs, 2, b);
    s.set(0, 60);
    b.stored.clear();
    s.load();
    CHECK(s.get(0) == 80 && !s.overridden(0));
  }

  // A failed save still applies the value, and says so.
  {
    FakeBackend b;
    b.failWrites = true;
    Settings s(defs, 2, b);
    CHECK(s.set(0, 60) == SettingResult::NotSaved && s.get(0) == 60);
    CHECK(s.clear(0) == SettingResult::NotSaved && s.get(0) == 80);
  }
}

static void testSettingCommands() {
  using K = SettingCommand::Kind;

  CHECK(parseSettingCommand("").kind == K::List);
  CHECK(parseSettingCommand("   ").kind == K::List);

  SettingCommand c = parseSettingCommand("hcr_vol_a");
  CHECK(c.kind == K::Show && !std::strcmp(c.key, "hcr_vol_a"));

  c = parseSettingCommand("hcr_vol_a 60");
  CHECK(c.kind == K::Set && !std::strcmp(c.key, "hcr_vol_a") && c.value == 60);

  c = parseSettingCommand("  hcr_vol_a   60  ");
  CHECK(c.kind == K::Set && c.value == 60);

  c = parseSettingCommand("x -5");
  CHECK(c.kind == K::Set && c.value == -5);

  c = parseSettingCommand("hcr_vol_a default");
  CHECK(c.kind == K::Clear && !std::strcmp(c.key, "hcr_vol_a"));

  CHECK(parseSettingCommand("x 60abc").kind == K::Bad);
  CHECK(parseSettingCommand("x abc").kind == K::Bad);
  CHECK(parseSettingCommand("x 6 0").kind == K::Bad);
  CHECK(parseSettingCommand("x defaults").kind == K::Bad);
  CHECK(parseSettingCommand("x 99999999999999999999").kind == K::Bad);

  // Keys can be 15 characters, the NVS limit, and no longer.
  CHECK(parseSettingCommand("abcdefghijklmno").kind == K::Show);
  CHECK(parseSettingCommand("abcdefghijklmnop").kind == K::Bad);
}

// The real table in BodyConfig.h.
static void testBodySettingsTable() {
  using namespace body;
  CHECK(kSettingCount <= kMaxSettings);
  for (size_t i = 0; i < kSettingCount; ++i) {
    const SettingDef& d = kSettings[i];
    CHECK(std::strlen(d.key) > 0 && std::strlen(d.key) <= kMaxSettingKey);
    CHECK(d.min <= d.defaultValue && d.defaultValue <= d.max);
    for (size_t j = i + 1; j < kSettingCount; ++j) CHECK(std::strcmp(d.key, kSettings[j].key) != 0);
  }

  // The enum and the table are in the same order.
  CHECK(!std::strcmp(kSettings[kSetHcrVolVocalizer].key, "hcr_vol_voc"));
  CHECK(!std::strcmp(kSettings[kSetHcrVolWavA].key, "hcr_vol_a"));
  CHECK(!std::strcmp(kSettings[kSetHcrVolWavB].key, "hcr_vol_b"));
  CHECK(!std::strcmp(kSettings[kSetHcrHappy].key, "hcr_happy"));
  CHECK(!std::strcmp(kSettings[kSetHcrSad].key, "hcr_sad"));
  CHECK(!std::strcmp(kSettings[kSetHcrMad].key, "hcr_mad"));
  CHECK(!std::strcmp(kSettings[kSetHcrScared].key, "hcr_scared"));
  CHECK(!std::strcmp(kSettings[kSetHcrWavConnected].key, "hcr_wav_conn"));
  CHECK(!std::strcmp(kSettings[kSetHcrWavArmed].key, "hcr_wav_arm"));
  CHECK(!std::strcmp(kSettings[kSetHcrWavDisarmed].key, "hcr_wav_disarm"));
  CHECK(!std::strcmp(kSettings[kSetHcrRandomFirst].key, "hcr_rand_first"));
  CHECK(!std::strcmp(kSettings[kSetHcrRandomLast].key, "hcr_rand_last"));
  CHECK(!std::strcmp(kSettings[kSetHcrGapMs].key, "hcr_gap_ms"));
  CHECK(!std::strcmp(kSettings[kSetHcrAutoOn].key, "hcr_auto_on"));
  CHECK(!std::strcmp(kSettings[kSetHcrAutoMaxS].key, "hcr_auto_max_s"));

  // Levels fit HcrSetup's uint8_t, and file numbers fit HCR's range.
  for (size_t i = kSetHcrVolVocalizer; i <= kSetHcrScared; ++i) CHECK(kSettings[i].max <= kHcrMaxLevel);
  for (size_t i = kSetHcrWavConnected; i <= kSetHcrRandomLast; ++i) CHECK(kSettings[i].max <= kHcrMaxWav);
}

static void testFrames() {
  // Round trip.
  Frame f;
  f.src = kNodeDomeC3;
  f.dst = kNodeBodyS3;
  f.type = MessageType::Heartbeat;
  f.seq = 0xBEEF;
  f.length = 3;
  f.payload[0] = 1;
  f.payload[1] = 2;
  f.payload[2] = 0xFF;
  uint8_t b[kMaxFrame + 1];
  CHECK(encodeFrame(f, b, kMaxFrame) == kFrameHeader + 3);
  CHECK(b[0] == 0xD2 && b[1] == 1 && b[2] == 2 && b[3] == 1 && b[4] == 1);
  CHECK(b[5] == 0xEF && b[6] == 0xBE && b[7] == 3);  // little-endian

  Frame d;
  CHECK(decodeFrame(b, kFrameHeader + 3, d));
  CHECK(d.src == kNodeDomeC3 && d.dst == kNodeBodyS3 && d.type == MessageType::Heartbeat);
  CHECK(d.seq == 0xBEEF && d.length == 3 && d.payload[2] == 0xFF);

  // An empty payload is fine.
  f.length = 0;
  CHECK(encodeFrame(f, b, kMaxFrame) == kFrameHeader);
  CHECK(decodeFrame(b, kFrameHeader, d) && d.length == 0);

  // The largest frame fits ESP-NOW's limit, and anything bigger is refused.
  CHECK(kFrameHeader + kMaxPayload == 250);
  f.length = kMaxPayload;
  CHECK(encodeFrame(f, b, kMaxFrame) == 250);
  CHECK(decodeFrame(b, 250, d) && d.length == kMaxPayload);
  f.length = kMaxPayload + 1;
  CHECK(encodeFrame(f, b, kMaxFrame) == 0);

  // A buffer that's too small.
  f.length = 3;
  CHECK(encodeFrame(f, b, kFrameHeader + 2) == 0);
  CHECK(encodeFrame(f, b, kFrameHeader + 3) == kFrameHeader + 3);

  // Bad input.
  encodeFrame(f, b, kMaxFrame);
  CHECK(!decodeFrame(b, 0, d));
  CHECK(!decodeFrame(b, kFrameHeader - 1, d));
  CHECK(!decodeFrame(b, kFrameHeader + 2, d));  // shorter than it says
  CHECK(!decodeFrame(b, kFrameHeader + 4, d));  // longer than it says
  CHECK(!decodeFrame(b, kMaxFrame + 1, d));
  b[0] = 0xD3;
  CHECK(!decodeFrame(b, kFrameHeader + 3, d));
  b[0] = 0xD2;
  b[1] = 2;
  CHECK(!decodeFrame(b, kFrameHeader + 3, d));
}

static void testNodes() {
  CHECK(!std::strcmp(nodeName(kNodeBodyS3), "body-s3"));
  CHECK(!std::strcmp(nodeName(kNodeDomeC3), "dome-c3"));
  CHECK(!std::strcmp(nodeName(kNodeBodyC3), "body-c3"));
  CHECK(nodeName(kNodeNone) == nullptr && nodeName(4) == nullptr && nodeName(kNodeAll) == nullptr);
  CHECK(nodeFromName("body-s3") == kNodeBodyS3 && nodeFromName("body-c3") == kNodeBodyC3);
  CHECK(nodeFromName("dome") == kNodeNone && nodeFromName("") == kNodeNone);
  CHECK(kNodeBodyC3 < kMaxNodes);
}

static void testPeerLink() {
  // Down until something arrives.
  {
    PeerLink p(500);
    CHECK(!p.alive(0) && !p.alive(1000) && !p.seen());
  }

  // Up for the timeout, then down.
  {
    PeerLink p(500);
    p.onFrame(1000, 0);
    CHECK(p.alive(1000) && p.alive(1499) && !p.alive(1500));
    CHECK(p.seen() && p.received() == 1 && p.ageMs(1200) == 200);
  }

  // Steady frames: nothing missed.
  {
    PeerLink p(500);
    for (uint16_t i = 0; i < 20; ++i) p.onFrame(i * 100, i);
    CHECK(p.received() == 20 && p.missed() == 0 && p.dropouts() == 0);
  }

  // A short gap counts the frames skipped.
  {
    PeerLink p(500);
    p.onFrame(0, 10);
    p.onFrame(400, 14);
    CHECK(p.missed() == 3 && p.dropouts() == 0);
  }

  // The sequence number wrapping isn't a gap.
  {
    PeerLink p(500);
    p.onFrame(0, 0xFFFF);
    p.onFrame(100, 0);
    CHECK(p.missed() == 0);
  }

  // A repeated or late frame isn't counted as thousands missed.
  {
    PeerLink p(500);
    p.onFrame(0, 100);
    p.onFrame(100, 100);
    p.onFrame(200, 99);
    CHECK(p.missed() == 0);
  }

  // Coming back after a dropout, even at a much later sequence number.
  {
    PeerLink p(500);
    p.onFrame(0, 10);
    p.onFrame(5000, 60);
    CHECK(p.dropouts() == 1 && p.missed() == 0 && p.alive(5000));
  }

  // A peer that rebooted restarts its numbering; that's a dropout, not missed.
  {
    PeerLink p(500);
    p.onFrame(0, 40000);
    p.onFrame(3000, 0);
    p.onFrame(3100, 1);
    CHECK(p.dropouts() == 1 && p.missed() == 0);
  }

  // Wrapping millis().
  {
    PeerLink p(500);
    p.onFrame(0xFFFFFF00u, 1);
    CHECK(p.alive(100) && !p.alive(300));
  }

  // The real timing: several heartbeats can be missed before the link drops.
  CHECK(kPeerTimeoutMs >= 3 * kHeartbeatMs);
}

static void testLinkParsing() {
  uint8_t mac[kMacBytes];
  CHECK(parseMac("AA:BB:CC:DD:EE:0F", mac));
  CHECK(mac[0] == 0xAA && mac[1] == 0xBB && mac[5] == 0x0F);
  CHECK(parseMac("aa:bb:cc:dd:ee:ff", mac) && mac[0] == 0xAA);
  CHECK(!parseMac("", mac));
  CHECK(!parseMac("AA:BB:CC:DD:EE", mac));
  CHECK(!parseMac("AA:BB:CC:DD:EE:FF:00", mac));
  CHECK(!parseMac("AA-BB-CC-DD-EE-FF", mac));
  CHECK(!parseMac("GG:BB:CC:DD:EE:FF", mac));
  CHECK(!parseMac("AABBCCDDEEFF00000", mac));

  uint8_t key[kLinkKeyBytes];
  CHECK(parseKey("000102030405060708090a0b0c0d0eFF", key));
  CHECK(key[0] == 0 && key[10] == 0x0A && key[15] == 0xFF);
  CHECK(!parseKey("", key));
  CHECK(!parseKey("000102030405060708090a0b0c0d0e", key));
  CHECK(!parseKey("000102030405060708090a0b0c0d0eFF00", key));
  CHECK(!parseKey("zz0102030405060708090a0b0c0d0eFF", key));

  using K = LinkCommand::Kind;
  CHECK(parseLinkCommand("").kind == K::Status);
  CHECK(parseLinkCommand("  ").kind == K::Status);
  CHECK(parseLinkCommand("forget").kind == K::Forget);
  CHECK(parseLinkCommand("forget now").kind == K::Bad);
  CHECK(parseLinkCommand("key new").kind == K::NewKey);
  CHECK(parseLinkCommand("key").kind == K::Bad);
  CHECK(parseLinkCommand("key short").kind == K::Bad);
  CHECK(parseLinkCommand("key new x").kind == K::Bad);
  CHECK(parseLinkCommand("nonsense").kind == K::Bad);
  CHECK(parseLinkCommand("waytoolongword x").kind == K::Bad);

  LinkCommand c = parseLinkCommand("key 000102030405060708090a0b0c0d0eff");
  CHECK(c.kind == K::Key && c.key[15] == 0xFF);

  c = parseLinkCommand(" peer  dome-c3   AA:BB:CC:DD:EE:FF ");
  CHECK(c.kind == K::Peer && c.node == kNodeDomeC3 && c.mac[5] == 0xFF);
  CHECK(parseLinkCommand("peer").kind == K::Bad);
  CHECK(parseLinkCommand("peer dome-c3").kind == K::Bad);
  CHECK(parseLinkCommand("peer dome AA:BB:CC:DD:EE:FF").kind == K::Bad);
  CHECK(parseLinkCommand("peer dome-c3 AA:BB").kind == K::Bad);
  CHECK(parseLinkCommand("peer dome-c3 AA:BB:CC:DD:EE:FF x").kind == K::Bad);
}

static std::string cmd(const DomeOutput& o) { return std::string(o.command, o.commandLength); }

static void testDomeCommands() {
  char b[24];
  CHECK(domeHomeCommand(b, sizeof(b)) == 4 && !std::strcmp(b, ":DPH"));
  CHECK(domeStopCommand(b, sizeof(b)) == 4 && !std::strcmp(b, ":DPZ"));
  CHECK(domeGoToCommand(b, sizeof(b), -40, 30) > 0 && !std::strcmp(b, ":DPA-40,30"));
  CHECK(domeGoToCommand(b, sizeof(b), 999, 150) > 0 && !std::strcmp(b, ":DPA360,100"));
  CHECK(domeGoToCommand(b, 4, 10, 10) == 0);

  DomeReport r;
  CHECK(parseDomeReport("#DP$-35", r) && r.mode == '$' && r.position == -35);
  CHECK(parseDomeReport("#DP@0", r) && r.mode == '@' && r.position == 0);
  CHECK(parseDomeReport("#DP%120", r) && r.mode == '%' && r.position == 120);
  CHECK(!parseDomeReport("#DPX10", r));
  CHECK(!parseDomeReport("#DP!", r));
  CHECK(!parseDomeReport("#DP!12a", r));
  CHECK(!parseDomeReport("READ: x", r));
  CHECK(!parseDomeReport("", r));
}

static void testDomeControl() {
  DomeConfig c;
  DomeControl d(c);
  d.seed(12345);
  CHECK(d.neutralUs() == 1500);

  // No link: neutral, nothing sent.
  DomeOutput o = d.update(0, false, DomeMode::Manual);
  CHECK(o.pulseUs == 1500 && o.commandLength == 0 && d.stopped());

  // Manual: spin maps onto the pulse, and times out.
  o = d.update(10, true, DomeMode::Manual);
  CHECK(o.pulseUs == 1500 && o.commandLength == 0 && !d.stopped());
  d.setSpin(1.0f, 20);
  CHECK(d.update(30, true, DomeMode::Manual).pulseUs == 2000);
  d.setSpin(-0.5f, 40);
  CHECK(d.update(50, true, DomeMode::Manual).pulseUs == 1250);
  CHECK(d.update(40 + c.spinTimeoutMs - 1, true, DomeMode::Manual).pulseUs == 1250);
  CHECK(d.update(40 + c.spinTimeoutMs, true, DomeMode::Manual).pulseUs == 1500);  // stale
  d.setSpin(5.0f, 1000);  // clamped
  CHECK(d.update(1001, true, DomeMode::Manual).pulseUs == 2000);
  d.setSpin(0.6f, 1100, 2000);  // longer hold, for the console test keys
  CHECK(d.update(2000, true, DomeMode::Manual).pulseUs == 1800);
  CHECK(d.update(3100, true, DomeMode::Manual).pulseUs == 1500);

  // Spin scaling and invert.
  DomeConfig c2 = c;
  c2.spinMaxPercent = 50;
  c2.invert = true;
  d.setConfig(c2);
  d.setSpin(1.0f, 4000);
  CHECK(d.update(4001, true, DomeMode::Manual).pulseUs == 1250);
  d.setConfig(c);

  // Home: one :DPH on entry, neutral pulse, stick ignored.
  d.setSpin(1.0f, 5000);
  o = d.update(5001, true, DomeMode::Home);
  CHECK(cmd(o) == ":DPH" && o.pulseUs == 1500);
  CHECK(d.update(5002, true, DomeMode::Home).commandLength == 0);

  // Back to manual from home: :DPZ cancels the home move.
  o = d.update(5003, true, DomeMode::Manual);
  CHECK(cmd(o) == ":DPZ");
  CHECK(d.update(5004, true, DomeMode::Manual).commandLength == 0);

  // Auto: a move straight away, then one per pause, angles within range.
  o = d.update(6000, true, DomeMode::Auto);
  CHECK(o.commandLength > 0 && cmd(o).rfind(":DPA", 0) == 0 && o.pulseUs == 1500);
  int moves = 1;
  uint32_t lastMove = 6000;
  bool gapsOk = true, anglesOk = true;
  for (uint32_t t = 6001; t < 6000 + 60000; ++t) {
    o = d.update(t, true, DomeMode::Auto);
    if (o.commandLength == 0) continue;
    ++moves;
    const uint32_t gap = t - lastMove;
    if (gap < c.autoMinDelayMs || gap > c.autoMaxDelayMs) gapsOk = false;
    lastMove = t;
    int deg = 0, spd = 0;
    if (std::sscanf(o.command, ":DPA%d,%d", &deg, &spd) != 2 || deg < -80 || deg > 80 || spd != 30) {
      anglesOk = false;
    }
  }
  CHECK(gapsOk && anglesOk);
  CHECK(moves >= 8 && moves <= 11);  // 60 s at 6-8 s per move

  // Losing the link in auto: :DPZ once, neutral, then quiet.
  o = d.update(70000, false, DomeMode::Auto);
  CHECK(cmd(o) == ":DPZ" && o.pulseUs == 1500 && d.stopped());
  CHECK(d.update(70001, false, DomeMode::Auto).commandLength == 0);

  // Link back with auto selected: starts moving again.
  o = d.update(70002, true, DomeMode::Auto);
  CHECK(cmd(o).rfind(":DPA", 0) == 0);

  // Losing the link in manual: nothing to cancel, spin cleared.
  d.update(71000, true, DomeMode::Manual);
  d.setSpin(1.0f, 71001, 5000);
  o = d.update(71002, false, DomeMode::Manual);
  CHECK(o.commandLength == 0 && o.pulseUs == 1500);
  CHECK(d.update(71003, true, DomeMode::Manual).pulseUs == 1500);  // old spin not reused

  // Home -> auto: no stop needed, the first auto move replaces it.
  d.update(72000, true, DomeMode::Home);
  o = d.update(72001, true, DomeMode::Auto);
  CHECK(cmd(o).rfind(":DPA", 0) == 0);

  // Body config wiring: settings reach the dome config.
  CHECK(!std::strcmp(body::kSettings[body::kSetDomeSpinMax].key, "dome_spin_max"));
  CHECK(!std::strcmp(body::kSettings[body::kSetDomeAutoMaxS].key, "dome_auto_max_s"));
  CHECK(body::kSettings[body::kSetDomeAutoDeg].max <= 360);

  struct FakeSettings {
    int32_t get(size_t i) const { return body::kSettings[i].defaultValue; }
  } fs;
  const DomeConfig bc = body::domeConfig(fs);
  CHECK(bc.pulseMinUs == 1000 && bc.pulseMaxUs == 2000 && bc.spinTimeoutMs == 250);
  CHECK(bc.spinMaxPercent == 100 && !bc.invert && bc.autoDegrees == 80 && bc.autoSpeed == 30);
  CHECK(bc.autoMinDelayMs == 6000 && bc.autoMaxDelayMs == 8000);
}

static void testHcrAutoSounds() {
  // Off unless auto is selected and the link is up.
  {
    HcrEvents e(0);
    e.setAutoDelayMs(5000, 12000);
    bool any = false;
    for (uint32_t t = 0; t < 60000; t += 10) any |= e.update(t, true, false, false, false).autoSound;
    for (uint32_t t = 60000; t < 120000; t += 10) any |= e.update(t, false, false, false, true).autoSound;
    CHECK(!any);
  }

  // In auto: first sound one pause in, then one per 5-12 s.
  {
    HcrEvents e(5000);
    e.seed(42);
    e.setAutoDelayMs(5000, 12000);
    e.update(0, true, false, false, false);
    uint32_t last = 1000, count = 0;
    bool gapsOk = true;
    for (uint32_t t = 1000; t <= 121000; t += 10) {
      if (!e.update(t, true, false, false, true).autoSound) continue;
      const uint32_t gap = t - last;
      if (gap < 5000 || gap > 12010) gapsOk = false;
      last = t;
      ++count;
    }
    CHECK(gapsOk);
    CHECK(count >= 10 && count <= 24);
  }

  // A button sound pushes auto back by the gap rather than talking over it.
  {
    HcrEvents e(5000);
    e.seed(7);
    e.setAutoDelayMs(1000, 1000);
    e.update(0, true, false, false, false);
    e.update(10000, true, false, false, true);    // auto on: due at 11000
    CHECK(e.update(10900, true, false, true, true).soundPress);
    CHECK(!e.update(11000, true, false, false, true).autoSound);  // inside the gap
    CHECK(!e.update(15899, true, false, false, true).autoSound);
    CHECK(e.update(15900, true, false, false, true).autoSound);   // gap over
  }

  // Leaving auto and coming back starts a fresh pause.
  {
    HcrEvents e(0);
    e.setAutoDelayMs(2000, 2000);
    e.update(0, true, false, false, true);
    CHECK(!e.update(1999, true, false, false, true).autoSound);
    e.update(2000 - 1, true, false, false, false);
    e.update(5000, true, false, false, true);
    CHECK(!e.update(6999, true, false, false, true).autoSound);
    CHECK(e.update(7000, true, false, false, true).autoSound);
  }
}


static void testOta() {
  // A fake image: noise, a decoy "R2AU-FW|" with no fields, then the tag.
  std::vector<uint8_t> img(5000);
  for (size_t i = 0; i < img.size(); ++i) img[i] = static_cast<uint8_t>((i * 37 + 11) & 0xff);
  const char decoy[] = "R2AU-FW|";
  memcpy(img.data() + 100, decoy, 8);
  img[108] = 0;
  const char tag[] = "R2AU-FW|body_c3|0.2.0|abc1234-dirty|";
  const size_t at = 3000;
  memcpy(img.data() + at, tag, strlen(tag));
  // Any piece size, so the tag lands inside one piece, across two, or across
  // several tiny ones.
  for (size_t piece : {1u, 3u, 7u, 16u, 40u, 79u, 80u, 81u, 512u, 3010u, 5000u}) {
    FirmwareTagScanner sc;
    for (size_t i = 0; i < img.size(); i += piece) sc.feed(img.data() + i, std::min(piece, img.size() - i));
    CHECK(sc.found());
    CHECK(strcmp(sc.tag().node, "body_c3") == 0);
    CHECK(strcmp(sc.tag().version, "0.2.0") == 0);
    CHECK(strcmp(sc.tag().build, "abc1234-dirty") == 0);
  }
  {
    FirmwareTagScanner sc;  // no tag at all
    sc.feed(img.data(), at);
    CHECK(!sc.found());
  }

  OtaStart s;
  s.size = 774384;
  s.ip[0] = 192; s.ip[1] = 168; s.ip[2] = 4; s.ip[3] = 1;
  strcpy(s.ssid, "R2-AU");
  strcpy(s.password, "abcd-efgh-ijkl");
  uint8_t buf[kMaxPayload];
  const size_t n = encodeOtaStart(buf, sizeof(buf), s);
  CHECK(n == 8 + 1 + 5 + 1 + 14);
  OtaStart d;
  CHECK(decodeOtaStart(buf, n, d));
  CHECK(d.size == 774384 && d.ip[0] == 192 && d.ip[3] == 1);
  CHECK(strcmp(d.ssid, "R2-AU") == 0 && strcmp(d.password, "abcd-efgh-ijkl") == 0);
  CHECK(!decodeOtaStart(buf, n - 1, d));
  {
    // The longest name and password still fit in one link frame.
    OtaStart big = s;
    memset(big.ssid, 'n', 32);
    memset(big.password, 'p', 63);
    CHECK(encodeOtaStart(buf, sizeof(buf), big) == 8 + 33 + 64);
  }

  OtaProgress p;
  p.state = OtaState::Downloading;
  p.percent = 42;
  CHECK(encodeOtaProgress(buf, sizeof(buf), p) == 4);
  OtaProgress q;
  CHECK(decodeOtaProgress(buf, 4, q) && q.state == OtaState::Downloading && q.percent == 42);
  // A version report isn't a progress report, and the other way round.
  char v[kVersionMax + 1], b[kVersionMax + 1];
  CHECK(!decodeVersionStatus(buf, 4, v, b));
  const size_t vn = encodeVersionStatus(buf, sizeof(buf), "0.1.0", "abc");
  CHECK(!decodeOtaProgress(buf, vn, q));

  CHECK(otaNodeFromEnv("body_c3") == kNodeBodyC3 && otaNodeFromEnv("dome_c3") == kNodeDomeC3);
  CHECK(otaNodeFromEnv("body_s3") == kNodeBodyS3 && otaNodeFromEnv("nope") == 0);
  CHECK(strcmp(otaNodeEnv(kNodeDomeC3), "dome_c3") == 0);

  uint8_t ui[kUiMaxMessage];
  const size_t un = uiOtaStatus(ui, sizeof(ui), kNodeBodyC3, 3, 50, 0, "downloading");
  CHECK(un == 1 + 4 + 1 + 11 && ui[0] == static_cast<uint8_t>(UiType::OtaStatus) && ui[3] == 50);
}


static void testCustomSequences() {
  CHECK(!isCustomSequenceId(63) && isCustomSequenceId(64) && isCustomSequenceId(87) && !isCustomSequenceId(88));
  // Custom numbers never overlap the built-ins.
  CHECK(allSequenceCount() < kCustomSeqFirst);

  CustomSequence s;
  s.id = 64;
  strcpy(s.name, "Peekaboo");
  s.frameCount = 2;
  s.frames[0].ms = 400;
  s.frames[0].pos[0] = 100;
  s.frames[0].pos[11] = 100;
  s.frames[1].ms = 600;
  s.frames[1].pos[0] = 0;
  s.frames[1].pos[12] = -10;
  CHECK(validCustomSequence(s));
  CHECK(customSequenceLengthMs(s) == 1000);

  uint8_t buf[kCustomSeqEncodedMax];
  const size_t n = encodeCustomSequence(s, buf, sizeof(buf));
  CHECK(n == 1 + 1 + 8 + 1 + 2 * 15);
  CustomSequence d;
  CHECK(decodeCustomSequence(buf, n, d));
  CHECK(d.id == 64 && strcmp(d.name, "Peekaboo") == 0 && d.frameCount == 2);
  CHECK(d.frames[1].ms == 600 && d.frames[1].pos[12] == -10 && d.frames[1].pos[5] == kSeqHold);
  CHECK(!decodeCustomSequence(buf, n - 1, d));

  // Out of range values are refused.
  CustomSequence bad = s;
  bad.frames[0].ms = 10;
  CHECK(!validCustomSequence(bad));
  bad = s;
  bad.frames[0].pos[3] = 126;
  CHECK(!validCustomSequence(bad));
  bad = s;
  bad.name[0] = '\0';
  CHECK(!validCustomSequence(bad));
  bad = s;
  bad.frameCount = 0;
  CHECK(!validCustomSequence(bad));
  // Largest one still encodes.
  CustomSequence big = s;
  big.frameCount = kMaxSeqFrames;
  memset(big.name, 'x', kSeqNameMax);
  CHECK(encodeCustomSequence(big, buf, sizeof(buf)) == kCustomSeqEncodedMax);

  // Copies of built-ins: dome panels stay in slots 0-10, arms go to 11-12.
  CustomSequence c;
  CHECK(customFromBuiltIn(servoSequenceIndex("Cantina"), c));
  CHECK(c.frameCount == 17 && customSequenceLengthMs(c) == 9700 && strcmp(c.name, "Cantina") == 0);
  CHECK(c.frames[0].pos[0] == 100 && c.frames[0].pos[11] == kSeqHold);
  CHECK(c.frames[4].pos[0] == 53);  // 534 thousandths
  CHECK(customFromBuiltIn(servoSequenceIndex("Top Arm Close"), c));
  CHECK(c.frames[0].pos[11] == 0 && c.frames[0].pos[12] == kSeqHold && c.frames[0].pos[0] == kSeqHold);
  CHECK(customFromBuiltIn(servoSequenceIndex("Ripple"), c));
  CHECK(c.frames[7].pos[7] > 100);  // past Open, as in the original
  CHECK(!customFromBuiltIn(99, c));

  CustomSequencePlayer p;
  int8_t out[kSeqServos];
  CHECK(p.start(s, 0, 1000));
  CHECK(p.update(1000, out) && out[0] == 100 && out[11] == 100);
  CHECK(!p.update(1399, out));
  CHECK(p.update(1400, out) && out[0] == 0 && out[12] == -10);
  CHECK(p.running() && p.id() == 64);
  CHECK(!p.update(2000, out) && !p.running());
  CHECK(p.start(s, 1, 0) && p.update(0, out) && out[12] == -10);  // from frame 2
  CHECK(!p.start(s, 2, 0));

  // Page messages, out and back through the assembler.
  uint8_t m[kUiMaxMessage];
  SeqAssembler a;
  size_t mn = uiSeqHeader(m, sizeof(m), s);
  CHECK(mn > 0 && a.header(m, mn));
  CHECK(!a.complete(64));
  for (uint8_t i = 0; i < s.frameCount; ++i) {
    mn = uiSeqFrame(m, sizeof(m), 64, i, s.frames[i]);
    CHECK(mn == 5 + kSeqServos && a.frame(m, mn));
  }
  CHECK(a.complete(64) && !a.complete(65));
  CHECK(a.sequence().frames[1].pos[12] == -10 && a.sequence().frames[0].ms == 400);
  mn = uiSeqFrame(m, sizeof(m), 64, 5, s.frames[0]);
  CHECK(!a.frame(m, mn));  // beyond the count
}

static void testServoSequences() {
  CHECK(kServoSequenceCount == 11);
  CHECK(servoSequenceIndex("All Closed") == 0);
  CHECK(servoSequenceIndex("Cantina") == 2);
  CHECK(servoSequenceIndex("Wave") == 10);
  CHECK(servoSequenceIndex("Nope") == -1);
  CHECK(servoSequenceLengthMs(kServoSequences[2]) == 9700);
  CHECK(servoSequenceLengthMs(kServoSequences[3]) == 1350);
  CHECK(servoSequenceLengthMs(kServoSequences[9]) == 800);

  // Arm sequences come after the dome's, so saved dome numbers don't move.
  CHECK(kArmSequenceCount == 6);
  CHECK(allSequenceCount() == 17);
  CHECK(!isArmSequence(10) && isArmSequence(11) && isArmSequence(16) && !isArmSequence(17));
  CHECK(sequenceAt(17) == nullptr);
  CHECK(servoSequenceIndex("Top Arm Open") == 11);
  CHECK(servoSequenceIndex("Arms Flap") == 16);
  CHECK(sequenceAt(12)->frames[0].position[0] == 0);              // Top Arm Close
  CHECK(sequenceAt(12)->frames[0].position[1] == kHoldPosition);  // bottom left alone
  CHECK(servoSequenceLengthMs(*sequenceAt(16)) == 5600);
  {
    // The flap ends with both arms closed, and its see-saws really alternate.
    const ServoSequence& f = *sequenceAt(16);
    const ServoFrame& last = f.frames[f.frameCount - 1];
    CHECK(last.position[0] == 0 && last.position[1] == 0);
    CHECK(f.frames[4].position[0] == 1000 && f.frames[4].position[1] == 0);
    CHECK(f.frames[5].position[0] == 0 && f.frames[5].position[1] == 1000);
    // Every arm frame is open, closed or held, for both arms.
    for (size_t q = kServoSequenceCount; q < allSequenceCount(); ++q) {
      const ServoSequence& s = *sequenceAt(q);
      for (size_t i = 0; i < s.frameCount; ++i) {
        for (size_t a = 0; a < 2; ++a) {
          const int16_t p = s.frames[i].position[a];
          CHECK(p == 0 || p == 1000 || p == kHoldPosition);
        }
      }
    }
  }
  {
    // The player runs an arm sequence like any other.
    ServoSequencePlayer p;
    int16_t out[kDomePanels];
    CHECK(p.start(16, 0));
    CHECK(p.update(0, out) && out[0] == 1000 && out[1] == 1000);
    CHECK(!p.update(499, out));
    CHECK(p.update(500, out) && out[0] == 0 && out[1] == 0);
    CHECK(!p.start(17, 0));
  }

  // With the Maestro's own endpoints, the positions come back as the
  // Maestro's microseconds (quarter-us / 4), give or take rounding.
  ServoEndpoints pie1{992, 992, 2000, 2000};
  ServoEndpoints dome4{608, 608, 2000, 2304};
  CHECK(positionToUs(0, pie1) == 992);
  CHECK(positionToUs(1000, pie1) == 2000);
  CHECK(positionToUs(-500, pie1) == 992);   // held at min
  CHECK(positionToUs(2000, pie1) == 2000);  // held at max
  // Ripple frame 7 sends Dome 4 (channel 10) to 9216 quarter-us = 2304 us.
  const ServoFrame& f7 = kServoSequences[9].frames[7];
  CHECK(std::abs(positionToUs(f7.position[7], dome4) - 2304) <= 1);
  // Cantina frame 4: Pie 1 to 6121 quarter-us = 1530 us.
  const ServoFrame& c4 = kServoSequences[2].frames[4];
  CHECK(std::abs(positionToUs(c4.position[0], pie1) - 1530) <= 1);
  CHECK(c4.position[4] == 0);  // Dome 1 held closed in Cantina
  // Retuned endpoints move the whole sequence with them.
  ServoEndpoints moved{1000, 1100, 1900, 2000};
  CHECK(positionToUs(1000, moved) == 1900);
  CHECK(positionToUs(500, moved) == 1500);
  // Reversed servo: open below closed.
  ServoEndpoints rev{900, 1900, 1100, 2000};
  CHECK(positionToUs(1000, rev) == 1100);
  CHECK(positionToUs(250, rev) == 1700);

  ServoSequencePlayer p;
  int16_t out[kDomePanels];
  CHECK(!p.update(0, out));
  CHECK(!p.start(99, 0));
  CHECK(p.start(10, 1000));  // Wave: 500, then 8 x 300 ms
  CHECK(p.update(1000, out) && out[1] == 0);
  CHECK(!p.update(1499, out));
  CHECK(p.update(1500, out) && out[1] == 1000);
  CHECK(!p.update(1799, out));
  CHECK(p.update(1800, out));
  int frames = 3;
  for (uint32_t t = 1801; t < 5000; ++t) if (p.update(t, out)) ++frames;
  CHECK(frames == 9);
  CHECK(!p.running());
}

static void testAnimations() {
  Animation all[kMaxAnimations];
  const size_t n = defaultAnimations(all, kMaxAnimations);
  CHECK(n == 17);
  CHECK(all[0].id == 200 && !std::strcmp(all[0].name, "Happy chatter"));
  // Chortle No: the chortle, then the dome 30 degrees left, right, ahead.
  {
    const Animation& no = all[16];
    CHECK(no.id == 216 && !std::strcmp(no.name, "Chortle No") && !std::strcmp(no.icon, "sfx-chuckle"));
    CHECK(no.stepCount == 4);
    CHECK(no.steps[0].kind == StepKind::Sound && no.steps[0].a == 2);
    CHECK(no.steps[1].kind == StepKind::Dome && domeStepAction(no.steps[1]) == DomeAction::GoTo &&
          domeStepValue(no.steps[1]) == -30 && domeStepExtra(no.steps[1]) == 60 && no.steps[1].atMs == 0);
    CHECK(domeStepValue(no.steps[2]) == 30 && no.steps[2].atMs == 1000);
    CHECK(domeStepAction(no.steps[3]) == DomeAction::Home && no.steps[3].atMs == 2000);
  }
  const Animation* cantina = nullptr;
  const Animation* leia = nullptr;
  for (size_t i = 0; i < n; ++i) {
    if (!std::strcmp(all[i].name, "Cantina")) cantina = &all[i];
    if (!std::strcmp(all[i].name, "Leia message")) leia = &all[i];
  }
  CHECK(cantina && leia);
  if (cantina) {
    CHECK(cantina->stepCount == 4);
    CHECK(cantina->steps[0].kind == StepKind::Sound && cantina->steps[0].a == 10);
    CHECK(cantina->steps[1].kind == StepKind::ServoSequence && cantina->steps[1].a == 2);
    CHECK(cantina->steps[2].kind == StepKind::Holo && !std::strcmp(cantina->steps[2].text, "A006|60"));
    CHECK(cantina->steps[3].kind == StepKind::Logic && !std::strcmp(cantina->steps[3].text, "~RTLE100560"));
  }
  if (leia) CHECK(animationLastStepMs(*leia) == 34000);
  // Every default round-trips through the binary form, and every holo
  // command parses.
  for (size_t i = 0; i < n; ++i) {
    uint8_t buf[600];
    const size_t len = encodeAnimation(all[i], buf, sizeof(buf));
    CHECK(len > 0);
    Animation back;
    CHECK(decodeAnimation(buf, len, back));
    CHECK(back.id == all[i].id && back.stepCount == all[i].stepCount && !std::strcmp(back.name, all[i].name));
    for (size_t k = 0; k < back.stepCount; ++k) {
      CHECK(back.steps[k].kind == all[i].steps[k].kind && back.steps[k].atMs == all[i].steps[k].atMs &&
            !std::strcmp(back.steps[k].text, all[i].steps[k].text));
      if (back.steps[k].kind == StepKind::Holo) {
        HoloCommand hc;
        CHECK(parseHoloCommand(back.steps[k].text, hc));
      }
    }
    CHECK(!decodeAnimation(buf, len - 1, back));
  }
  // Too long text, too many steps.
  Animation a;
  char longText[kStepTextMax + 20];
  std::memset(longText, 'x', sizeof(longText) - 1);
  longText[sizeof(longText) - 1] = '\0';
  AnimStep big = textStep(0, StepKind::Logic, longText);  // truncated to fit
  CHECK(std::strlen(big.text) == kStepTextMax);
  for (size_t i = 0; i < kMaxAnimSteps; ++i) CHECK(addStep(a, soundStep(0, 1)));
  CHECK(!addStep(a, soundStep(0, 1)));

  // A completely full animation (every step with the longest text) still
  // encodes in the size AnimationStore allows, and comes back the same.
  {
    Animation full;
    full.id = 247;
    std::memset(full.name, 'N', kAnimNameMax);
    std::memset(full.icon, 'i', kAnimIconMax);
    char text[kStepTextMax + 1];
    std::memset(text, 'x', kStepTextMax);
    text[kStepTextMax] = '\0';
    for (size_t i = 0; i < kMaxAnimSteps; ++i) CHECK(addStep(full, textStep(static_cast<uint16_t>(i * 100), StepKind::Logic, text)));
    const size_t most = 2 + (1 + kAnimNameMax) + (1 + kAnimIconMax) + 1 + kMaxAnimSteps * (7 + 1 + kStepTextMax);
    uint8_t big[2048];
    const size_t n = encodeAnimation(full, big, sizeof(big));
    CHECK(n == most);
    CHECK(encodeAnimation(full, big, most - 1) == 0);
    Animation back;
    CHECK(decodeAnimation(big, n, back) && back.stepCount == kMaxAnimSteps && !std::strcmp(back.steps[19].text, text) &&
          back.steps[19].atMs == 1900 && !std::strcmp(back.name, full.name));
  }

  // Sound picks: any set of files as hex, round trip through a step.
  {
    const uint16_t files[] = {1, 4, 5, 22, 59};
    char text[kStepTextMax + 1];
    CHECK(soundPickText(files, 5, text, sizeof(text)));
    CHECK(std::strcmp(text, "910002000000004") == 0);
    uint16_t back[kSoundPickMaxFile];
    CHECK(soundPickFiles(text, back, kSoundPickMaxFile) == 5);
    CHECK(back[0] == 1 && back[1] == 4 && back[2] == 5 && back[3] == 22 && back[4] == 59);
    CHECK(soundPickFiles("F1", back, kSoundPickMaxFile) == 5 && back[4] == 5);
    CHECK(soundPickFiles("1g", back, kSoundPickMaxFile) == 0);
    const uint16_t bad[] = {0};
    CHECK(!soundPickText(bad, 1, text, sizeof(text)));
    const uint16_t top[] = {kSoundPickMaxFile};
    CHECK(soundPickText(top, 1, text, sizeof(text)) && std::strlen(text) == kStepTextMax);
    AnimStep pick = textStep(250, StepKind::SoundPick, "910002000000004");
    uint8_t buf[80];
    const size_t n = encodeStep(pick, buf, sizeof(buf));
    CHECK(n == 7 + 1 + 15);
    AnimStep got;
    CHECK(decodeStep(buf, n, got) == n);
    CHECK(got.kind == StepKind::SoundPick && std::strcmp(got.text, pick.text) == 0 && got.atMs == 250);
  }

  // Dome steps: action, signed value and extra byte survive a round trip.
  {
    const AnimStep left = domeStep(0, DomeAction::GoTo, -30, 60);
    CHECK(domeStepAction(left) == DomeAction::GoTo && domeStepValue(left) == -30 && domeStepExtra(left) == 60);
    const AnimStep spin = domeStep(800, DomeAction::Spin, -55, 25);
    uint8_t buf[16];
    const size_t n = encodeStep(spin, buf, sizeof(buf));
    CHECK(n == 7);
    AnimStep got;
    CHECK(decodeStep(buf, n, got) == 7);
    CHECK(got.kind == StepKind::Dome && domeStepAction(got) == DomeAction::Spin && domeStepValue(got) == -55 &&
          domeStepExtra(got) == 25 && got.atMs == 800);
    CHECK(domeStepAction(domeStep(0, DomeAction::Home)) == DomeAction::Home);
    CHECK(std::strcmp(stepKindName(StepKind::Dome), "dome") == 0);
    buf[2] = 9;  // a kind this firmware doesn't know
    CHECK(decodeStep(buf, n, got) == 0);
  }

  // The player sorts by time and releases steps when due.
  Animation t;
  t.id = 300;
  addStep(t, soundStep(500, 2));
  addStep(t, soundStep(0, 1));
  addStep(t, soundStep(500, 3));
  AnimationPlayer p;
  p.start(t, 1000);
  const AnimStep* s = p.next(1000);
  CHECK(s && s->a == 1);
  CHECK(p.next(1000) == nullptr);
  CHECK(p.next(1499) == nullptr);
  s = p.next(1500);
  CHECK(s && s->a == 2);
  s = p.next(1500);
  CHECK(s && s->a == 3);
  CHECK(!p.running());
}

static void testHolo() {
  HoloCommand c;
  CHECK(parseHoloCommand("A0033|5", c) && c.type == HoloCommand::Type::Led && c.mask == kHoloAll &&
        c.number == 3 && c.colour == 3 && c.seconds == 5);
  CHECK(parseHoloCommand("F0028|4", c) && c.mask == kHoloFront && c.number == 2 && c.colour == 8);
  CHECK(parseHoloCommand("A007", c) && c.number == 7 && c.colour == -1 && c.seconds == 0);
  CHECK(parseHoloCommand("X1011", c) && c.type == HoloCommand::Type::Servo && c.mask == (kHoloFront | kHoloRear) &&
        c.number == 1 && c.colour == 1);
  CHECK(parseHoloCommand("S1", c) && c.type == HoloCommand::Type::Sequence && c.number == 1);
  CHECK(!parseHoloCommand("Q0011", c));
  CHECK(!parseHoloCommand("A2011", c));
  CHECK(!parseHoloCommand("A0", c));
  CHECK(!parseHoloCommand("A001|x", c));
  CHECK(!parseHoloCommand("", c));

  // Several commands in one step, one per HP; all must read or none apply.
  {
    HoloEngine m;
    m.seed(1);
    CHECK(m.command("S4 F0055 R0052", 0));
    m.update(20);
    CHECK(m.pixel(0, 0) != 0 && m.pixel(2, 0) != 0 && m.pixel(1, 0) == 0);
    CHECK(!m.command("T0055 bogus", 100));
    m.update(120);
    CHECK(m.pixel(1, 0) == 0);
    CHECK(!m.command("   ", 100));
  }

  HoloEngine e;
  e.seed(42);
  e.command("S4", 0);  // quiet, no twitch
  CHECK(!e.twitching() && !e.autoLed());
  CHECK(e.command("T0055|2", 100));  // top HP solid blue for 2 s
  e.update(120);
  CHECK(e.pixel(1, 0) == 0x0000FF && e.pixel(0, 0) == 0);
  e.update(2099);
  CHECK(e.ledEffect(1) == 5);
  e.update(2100);
  CHECK(e.ledEffect(1) == 0 && e.pixel(1, 0) == 0);
  // Short circuit ends by itself.
  e.command("F007", 3000);
  for (uint32_t t = 3000; t < 9000; t += 10) e.update(t);  // about 4.5 s of flashes
  CHECK(e.ledEffect(0) == 0);
  // Leia mode: front down, front shows Leia, the others dark.
  e.command("S1", 10000);
  e.update(10000);
  CHECK(e.ledEffect(0) == 1 && e.ledEffect(1) == 0 && e.servoY(0) < 0 && e.servoX(0) == 0);
  // Preset positions and wag.
  e.command("R1016", 11000);  // rear to the right
  CHECK(e.servoX(2) == 800 && e.servoY(2) == 0);
  e.command("R105", 12000);
  e.update(12000);
  CHECK(e.servoX(2) == 700);
  for (uint32_t t = 12000; t < 16000; t += 10) e.update(t);
  CHECK(e.servoX(2) == 0);
  // Normal again: twitch and auto LED on.
  e.command("S5", 20000);
  CHECK(e.twitching() && e.autoLed());
  CHECK(holoColour(1, 0) == 0xFF0000 && holoColour(9, 0) == 0xFFFFFF && holoColour(0, 3) != 0);
}

static void testUiProtocol() {
  uint8_t buf[kUiMaxMessage];
  UiStatus s;
  s.flags = kStatusRadio | kStatusArmed;
  s.domeMode = 1;
  s.domeAngle = -45;
  s.animationId = 212;
  s.servoSequence = 2;
  s.uptimeS = 123456;
  size_t n = uiStatus(buf, sizeof(buf), s);
  CHECK(n == 13);
  UiStatus back;
  CHECK(uiParseStatus(buf, n, back));
  CHECK(back.flags == s.flags && back.domeAngle == -45 && back.animationId == 212 && back.servoSequence == 2 &&
        back.uptimeS == 123456);
  CHECK(!uiParseStatus(buf, n - 1, back));
  CHECK(uiStatus(buf, 5, s) == 0);

  n = uiSettingInfo(buf, sizeof(buf), 3, 40, 80, 80, 0, 100, false, "hcr_vol_a");
  UiReader r(buf, n);
  CHECK(r.u8() == static_cast<uint8_t>(UiType::SettingInfo) && r.u8() == 3 && r.u8() == 40 && r.i32() == 80);
  CHECK(r.i32() == 80 && r.i32() == 0 && r.i32() == 100 && r.u8() == 0);
  char key[16];
  CHECK(r.text(key, 15) && !std::strcmp(key, "hcr_vol_a") && r.done());

  // An animation goes over as a header plus steps, each in one message.
  Animation all[kMaxAnimations];
  defaultAnimations(all, kMaxAnimations);
  const Animation& a = all[12];  // Cantina
  AnimAssembler asmb;
  n = uiAnimHeader(buf, sizeof(buf), a);
  CHECK(n > 0 && n <= kUiMaxMessage && asmb.header(buf, n));
  CHECK(!asmb.complete(a.id));
  for (uint8_t i = 0; i < a.stepCount; ++i) {
    n = uiAnimStep(buf, sizeof(buf), a.id, i, a.steps[i]);
    CHECK(n > 0 && asmb.step(buf, n));
  }
  CHECK(asmb.complete(a.id) && !asmb.complete(999));
  CHECK(!std::strcmp(asmb.animation().steps[3].text, "~RTLE100560"));
  n = uiAnimStep(buf, sizeof(buf), 999, 0, a.steps[0]);
  CHECK(!asmb.step(buf, n));  // wrong id
  n = uiResult(buf, sizeof(buf), UiType::SetSetting, UiResult::RefusedArmed);
  CHECK(n == 3 && buf[1] == 0x11 && buf[2] == 1);
}

static void testUiDomeAndEmotions() {
  char b[16];
  CHECK(hcrStimulate(b, sizeof(b), 0, false) == 5 && !std::strcmp(b, "<SH0>"));
  CHECK(hcrStimulate(b, sizeof(b), 3, true) && !std::strcmp(b, "<SC1>"));
  CHECK(hcrStimulate(b, sizeof(b), 4, false) == 0);

  DomeControl d{DomeConfig{}};
  d.update(0, true, DomeMode::Manual);
  d.requestGoTo(-90, 40);
  DomeOutput o = d.update(10, true, DomeMode::Manual);
  CHECK(!std::strcmp(o.command, ":DPA-90,40"));
  CHECK(d.update(20, true, DomeMode::Manual).commandLength == 0);
  d.requestHome();
  CHECK(!std::strcmp(d.update(30, true, DomeMode::Manual).command, ":DPH"));
  d.requestStop();
  o = d.update(40, false, DomeMode::Manual);  // no link: dropped
  CHECK(o.commandLength == 0);
  CHECK(d.update(50, true, DomeMode::Manual).commandLength == 0);
}

static void testDomeLink() {
  ServoEntry e[3];
  e[0].id = 0;
  e[0].ends = {992, 992, 2000, 2000};
  e[1].id = 16;
  e[1].ends = {1000, 1500, 1500, 2000};
  e[2].id = 33;
  e[2].ends = {1000, 1150, 1850, 2000};
  uint8_t buf[kMaxPayload];
  const size_t n = encodeServoEntries(e, 3, buf, sizeof(buf));
  CHECK(n == 27);
  ServoEntry back[4];
  CHECK(decodeServoEntries(buf, n, back, 4) == 3);
  CHECK(back[2].id == 33 && back[2].ends.openUs == 1850 && back[0].ends.minUs == 992);
  CHECK(decodeServoEntries(buf, n - 1, back, 4) == 0);
  CHECK(decodeServoEntries(buf, n, back, 2) == 0);
  CHECK(kMaxServoEntries * kServoEntryBytes <= kMaxPayload);

  int16_t spin = 0;
  CHECK(encodeSpin(-500, buf, sizeof(buf)) == 2 && decodeSpin(buf, 2, spin) && spin == -500);
  encodeSpin(2000, buf, sizeof(buf));
  CHECK(!decodeSpin(buf, 2, spin));

  DomeCommand c = parseDomeCommand("Q2");
  CHECK(c.kind == DomeCommand::Kind::Sequence && c.sequence == 2);
  CHECK(parseDomeCommand("Q-").kind == DomeCommand::Kind::StopSequence);
  CHECK(parseDomeCommand("Q11").kind == DomeCommand::Kind::Sequence);  // Top Arm Open
  CHECK(parseDomeCommand("Q16").kind == DomeCommand::Kind::Sequence);
  CHECK(parseDomeCommand("Q17").kind == DomeCommand::Kind::Bad);
  c = parseDomeCommand("HA0033|5");
  CHECK(c.kind == DomeCommand::Kind::Holo && !std::strcmp(c.text, "A0033|5"));
  c = parseDomeCommand("L~RTLE990505");
  CHECK(c.kind == DomeCommand::Kind::Logic && !std::strcmp(c.text, "~RTLE990505"));
  c = parseDomeCommand("P16,1500");
  CHECK(c.kind == DomeCommand::Kind::Move && c.servo == 16 && c.us == 1500);
  CHECK(parseDomeCommand("P16,3000").kind == DomeCommand::Kind::Bad);
  CHECK(parseDomeCommand("H").kind == DomeCommand::Kind::Bad);
  CHECK(parseDomeCommand("Z").kind == DomeCommand::Kind::Bad);
  c = parseDomeCommand("A1o");
  CHECK(c.kind == DomeCommand::Kind::Arm && c.servo == 33 && c.open);
  c = parseDomeCommand("A0c");
  CHECK(c.kind == DomeCommand::Kind::Arm && c.servo == 32 && !c.open);
  CHECK(parseDomeCommand("A2o").kind == DomeCommand::Kind::Bad);
  CHECK(parseDomeCommand("A0x").kind == DomeCommand::Kind::Bad);
}

static void testVersion() {
  // Versions sort by number, and a pre-release before its release.
  CHECK(compareVersions("0.1.0", "0.1.0") == 0);
  CHECK(compareVersions("0.1.0", "0.2.0") < 0);
  CHECK(compareVersions("0.10.0", "0.9.9") > 0);
  CHECK(compareVersions("1.0.0", "0.99.99") > 0);
  CHECK(compareVersions("0.2.0-dev", "0.2.0") < 0);
  CHECK(compareVersions("0.2.0", "0.2.0-dev") > 0);
  CHECK(compareVersions("0.2.0-a", "0.2.0-b") < 0);
  CHECK(compareVersions("0.2.0-dev", "0.1.9") > 0);

  // The tag this build carries, found inside an image-like blob.
  CHECK(std::strncmp(kFirmwareTag, "R2AU-FW|host|0.0.0-dev|local|", 64) == 0);
  const FirmwareTag me = thisFirmware();
  CHECK(!std::strcmp(me.node, "host") && !std::strcmp(me.version, "0.0.0-dev") && !std::strcmp(me.build, "local"));
  uint8_t image[200];
  for (size_t i = 0; i < sizeof(image); ++i) image[i] = static_cast<uint8_t>(i * 37 + 11);
  const char tag[] = "R2AU-FW|dome_c3|0.3.1|a1b2c3d-dirty|";
  std::memcpy(image + 120, tag, sizeof(tag) - 1);
  FirmwareTag t;
  CHECK(findFirmwareTag(image, sizeof(image), t));
  CHECK(!std::strcmp(t.node, "dome_c3") && !std::strcmp(t.version, "0.3.1") && !std::strcmp(t.build, "a1b2c3d-dirty"));
  CHECK(!findFirmwareTag(image, 130, t));  // cut off before the end
  const char bad[] = "R2AU-FW|body_s3||x|";
  CHECK(!findFirmwareTag(reinterpret_cast<const uint8_t*>(bad), sizeof(bad) - 1, t));
  // A broken tag earlier doesn't hide a good one later.
  uint8_t two[80] = {};
  std::memcpy(two, bad, sizeof(bad) - 1);
  std::memcpy(two + 30, tag, sizeof(tag) - 1);
  CHECK(findFirmwareTag(two, sizeof(two), t) && !std::strcmp(t.node, "dome_c3"));

  // NodeStatus round trip, and the UI message carrying it.
  uint8_t buf[64];
  const size_t n = encodeVersionStatus(buf, sizeof(buf), "0.1.0", "a1b2c3d");
  CHECK(n == 3 + 5 + 7);
  char v[kVersionMax + 1], b[kVersionMax + 1];
  CHECK(decodeVersionStatus(buf, n, v, b) && !std::strcmp(v, "0.1.0") && !std::strcmp(b, "a1b2c3d"));
  CHECK(!decodeVersionStatus(buf, n - 1, v, b));
  buf[0] = 2;
  CHECK(!decodeVersionStatus(buf, n, v, b));
  CHECK(encodeVersionStatus(buf, 10, "0.1.0", "a1b2c3d") == 0);
  const size_t u = uiNodeVersion(buf, sizeof(buf), kNodeDomeC3, "0.1.0", "nogit");
  CHECK(u == 1 + 1 + 6 + 6 && buf[0] == static_cast<uint8_t>(UiType::NodeVersion) && buf[1] == kNodeDomeC3);
}

static void testWifiConfig() {
  CHECK(wifiNameValid("R2-AU"));
  CHECK(wifiNameValid("R2 at the Con"));
  CHECK(!wifiNameValid(""));
  CHECK(!wifiNameValid("   "));
  CHECK(wifiNameValid("12345678901234567890123456789012"));    // 32
  CHECK(!wifiNameValid("123456789012345678901234567890123"));  // 33
  CHECK(!wifiNameValid("tab\there"));
  CHECK(wifiPasswordValid("abcd-efgh-ijkl"));
  CHECK(!wifiPasswordValid("short"));
  CHECK(wifiPasswordValid("12345678"));
  CHECK(!wifiPasswordValid(""));
  char longPass[70];
  std::memset(longPass, 'a', 63);
  longPass[63] = '\0';
  CHECK(wifiPasswordValid(longPass));
  longPass[63] = 'a';
  longPass[64] = '\0';
  CHECK(!wifiPasswordValid(longPass));
  CHECK(!wifiPasswordValid("caf\xc3\xa9-droid"));  // not plain ASCII

  // SetWifi from the page, and WifiInfo back.
  uint8_t buf[160];
  UiWriter w(buf, sizeof(buf));
  w.u8(static_cast<uint8_t>(UiType::SetWifi));
  w.text("R2 Con");
  w.text("beep-boop-42");
  char name[kWifiNameMax + 1], pass[kWifiPasswordMax + 1];
  CHECK(uiParseSetWifi(buf, w.size(), name, pass) && !std::strcmp(name, "R2 Con") && !std::strcmp(pass, "beep-boop-42"));
  CHECK(!uiParseSetWifi(buf, w.size() - 1, name, pass));
  UiWriter e(buf, sizeof(buf));
  e.u8(static_cast<uint8_t>(UiType::SetWifi));
  e.text("");
  e.text("");
  CHECK(uiParseSetWifi(buf, e.size(), name, pass) && !name[0] && !pass[0]);  // back to built-in
  const size_t n = uiWifiInfo(buf, sizeof(buf), true, "R2-AU", "abcd-efgh-ijkl");
  CHECK(n == 1 + 1 + 1 + 5 + 1 + 14 && buf[0] == static_cast<uint8_t>(UiType::WifiInfo) && buf[1] == 1);
}

static void testPixels() {
  // Reading commands.
  PixelCommand c;
  CHECK(parsePixelCommand("off", c) && c.effect == PixelEffect::Off);
  CHECK(parsePixelCommand("solid #00c8ff f1.5", c) && c.effect == PixelEffect::Solid && c.colourCount == 1 &&
        c.colours[0] == 0x00c8ff && c.fadeTenths == 15);
  CHECK(parsePixelCommand("scroll #ff0000 #FFFFFF #0000ff < s4 w3 t10 b80", c) && c.colourCount == 3 &&
        c.colours[1] == 0xffffff && c.direction == -1 && c.speed == 4 && c.width == 3 && c.runTenths == 100 &&
        c.brightness == 80);
  CHECK(parsePixelCommand("swoop #ff8800 bounce", c) && c.bounce && c.direction == 1 && c.speed == 5);
  CHECK(parsePixelCommand("setup n28 b40", c) && c.effect == PixelEffect::Setup && c.count == 28 && c.master == 40);
  CHECK(!parsePixelCommand("", c));
  CHECK(!parsePixelCommand("glitter", c));
  CHECK(!parsePixelCommand("solid #12345", c));
  CHECK(!parsePixelCommand("solid s10", c));
  CHECK(!parsePixelCommand("solid #111111 #222222 #333333 #444444 #555555", c));  // 4 colours at most
  CHECK(!parsePixelCommand("setup n65", c));
  CHECK(!parsePixelCommand("pulse w0", c));

  uint32_t out[kMaxPixels];
  // Solid in bands: two colours, half each.
  parsePixelCommand("solid #ff0000 #0000ff", c);
  renderPixels(c, 0, 10, out);
  CHECK(out[0] == 0xff0000 && out[4] == 0xff0000 && out[5] == 0x0000ff && out[9] == 0x0000ff);
  // Brightness halves it.
  parsePixelCommand("solid #ff0000 b50", c);
  renderPixels(c, 0, 4, out);
  CHECK(out[0] == 0x800000);
  // Wipe left to right: after a quarter of a pass, the first quarter is lit.
  parsePixelCommand("wipe #00ff00 s5", c);  // 27 pixels a second
  renderPixels(c, 1000 * 5 / 27, 20, out);  // 5 pixels along
  CHECK(out[0] == 0x00ff00 && out[4] == 0x00ff00 && out[6] == 0 && out[19] == 0);
  // ...and right to left lights the other end.
  parsePixelCommand("wipe #00ff00 s5 <", c);
  renderPixels(c, 1000 * 5 / 27, 20, out);
  CHECK(out[19] == 0x00ff00 && out[15] == 0x00ff00 && out[13] == 0 && out[0] == 0);
  // One colour holds once wiped; two colours keep wiping over each other.
  parsePixelCommand("wipe #00ff00 s5", c);
  renderPixels(c, 5000, 20, out);
  CHECK(out[0] == 0x00ff00 && out[19] == 0x00ff00);
  // Swoop: the head is brightest, the tail fades, ahead of the head is dark.
  parsePixelCommand("swoop #ffffff s5 w4", c);
  renderPixels(c, 1000 * 10 / 27 + 2, 20, out);  // head just past pixel 10
  CHECK((out[10] & 0xff) > 0xf0 && (out[9] & 0xff) < (out[10] & 0xff) && out[9] != 0 && out[11] == 0 && out[5] == 0);
  // Bounce comes back: on the second pass the tail is on the other side.
  parsePixelCommand("swoop #ffffff s5 w4 bounce", c);
  renderPixels(c, 1000 * (19 + 5) / 27 + 2, 20, out);  // back just past pixel 14, moving left
  CHECK((out[14] & 0xff) > 0xf0 && out[15] != 0 && (out[15] & 0xff) < (out[14] & 0xff) && out[13] == 0);
  // Scroll: bands of w pixels each, repeating.
  parsePixelCommand("scroll #ff0000 #0000ff w2 s0", c);
  renderPixels(c, 0, 8, out);
  CHECK(out[0] == 0xff0000 && out[1] == 0xff0000 && out[2] == 0x0000ff && out[3] == 0x0000ff && out[4] == 0xff0000);
  // Rainbow starts red at the first pixel.
  parsePixelCommand("rainbow s0", c);
  renderPixels(c, 0, 12, out);
  CHECK(out[0] == 0xff0000 && out[4] != out[0]);
  // Pulse is dark at the start of a breath and full in the middle.
  parsePixelCommand("pulse #ffffff s5", c);  // 1 s breaths
  renderPixels(c, 0, 4, out);
  CHECK(out[0] == 0);
  renderPixels(c, 500, 4, out);
  CHECK(out[0] == 0xffffff);
  // Sparkle lights some pixels, not all.
  parsePixelCommand("sparkle #ffffff s9", c);
  size_t lit = 0;
  renderPixels(c, 0, 60, out);
  for (size_t i = 0; i < 60; ++i) lit += out[i] != 0;
  CHECK(lit > 0 && lit < 30);

  // The engine: setup, overall brightness, fades, and timed effects.
  PixelEngine e;
  CHECK(e.count() == 28 && e.master() == 40);
  CHECK(e.command("setup n10 b50", 0));
  CHECK(e.count() == 10 && e.master() == 50);
  CHECK(e.command("solid #ff0000", 0));
  e.update(0);
  CHECK(e.raw(0) == 0xff0000 && e.pixel(0) == 0x800000 && e.raw(10) == 0);
  // Fade from red to blue over 1 s: halfway is a mix.
  CHECK(e.command("solid #0000ff f1", 1000));
  e.update(1500);
  CHECK(e.fading() && e.raw(0) == 0x800080);
  e.update(2100);
  CHECK(!e.fading() && e.raw(0) == 0x0000ff);
  // A timed effect returns to blue afterwards, fading over its own fade time.
  CHECK(e.command("solid #00ff00 t2 f0.5", 3000));
  e.update(3600);
  CHECK(e.raw(0) == 0x00ff00);
  e.update(5000);  // run time over: fading back
  e.update(5250);
  CHECK(e.fading() && e.raw(0) == 0x008080);
  e.update(5600);
  CHECK(!e.fading() && e.raw(0) == 0x0000ff);
  // off with a fade fades out.
  CHECK(e.command("off f1", 6000));
  e.update(6500);
  CHECK(e.raw(0) == 0x000080);
  e.update(7100);
  CHECK(e.raw(0) == 0 && e.pixel(0) == 0);
  CHECK(!e.command("solid nope", 8000));
}

int main() {
  testRamp();
  testSbus();
  testChannels();
  testMixer();
  testPulses();
  testArming();
  testHcrCommands();
  testHcrEvents();
  testHcrAutoSounds();
  testSettings();
  testSettingCommands();
  testBodySettingsTable();
  testFrames();
  testNodes();
  testPeerLink();
  testLinkParsing();
  testDomeCommands();
  testDomeControl();
  testServoSequences();
  testAnimations();
  testVersion();
  testWifiConfig();
  testPixels();
  testHolo();
  testUiProtocol();
  testDomeLink();
  testUiDomeAndEmotions();
  testOta();
  testCustomSequences();
  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
