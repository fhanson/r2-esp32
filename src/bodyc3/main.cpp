// R2 body accessory node, ESP32-C3 Super Mini, behind the utility arm panel.
//
//   - Utility arms: two MG90S on a PCA9685, closed at boot. A0o/A0c, A1o/A1c,
//     P32,1500 (move one), Q<n> (arm sequence), Q- (stop).
//   - NeoPixel strip, PixelEngine.h effects (X...).
// Commands come from the S3 over ESP-NOW, or the USB console for bench tests.
// Not safety-critical: if the S3 goes quiet the arms hold where they are.

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "DomeLink.h"
#include "EspNowLink.h"
#include "LinkConsole.h"
#include "OtaNode.h"
#include "Pca9685.h"
#include "PixelEngine.h"
#include "ServoSequences.h"
#include "Version.h"

namespace {

constexpr int kPinSda = 4;
constexpr int kPinScl = 3;
// Strip data, through an Adafruit Pixel Shifter (use D5, not the inverted !D5).
constexpr int kPinPixels = 5;
constexpr uint32_t kPixelFrameMs = 20;
constexpr uint8_t kPcaAddress = 0x40;
constexpr uint8_t kArmChannel[droid::kServoArmCount] = {0, 1};  // top, bottom
constexpr droid::ServoEndpoints kDefaultArmEnds = {1000, 1150, 1850, 2000};

EspNowLink link_(droid::kNodeBodyC3);
OtaClient otaClient;

bool sendOtaStatus(const uint8_t* payload, size_t length) {
  return link_.send(droid::kNodeBodyS3, droid::MessageType::NodeStatus, payload, length);
}
LinkConsole linkConsole(link_);
Pca9685 pca(kPcaAddress);
// Sized for the longest strip; the engine only drives the first count().
Adafruit_NeoPixel strip(droid::kMaxPixels, kPinPixels, NEO_GRB + NEO_KHZ800);
droid::PixelEngine pixels;
droid::ServoSequencePlayer armSequences;
uint32_t lastFrameMs = 0;
droid::ServoEndpoints armEnds[droid::kServoArmCount];
Preferences prefs;
bool prefsOk = false;
char consoleLine[96];  // X commands can be long
size_t consoleLen = 0;

void loadEnds() {
  for (auto& e : armEnds) e = kDefaultArmEnds;
  prefsOk = prefs.begin("bodyc3", false);
  if (prefsOk && prefs.getBytesLength("arms") == sizeof(armEnds)) prefs.getBytes("arms", armEnds, sizeof(armEnds));
}

// Strip length and brightness come from the S3; kept here too, for boot.
void loadPixelSetup() {
  if (!prefsOk) return;
  pixels.setup(prefs.getUChar("pxn", 28), prefs.getUChar("pxb", 40));
}

void savePixelSetup() {
  if (!prefsOk) return;
  if (prefs.getUChar("pxn", 0) != pixels.count()) prefs.putUChar("pxn", static_cast<uint8_t>(pixels.count()));
  if (prefs.getUChar("pxb", 255) != pixels.master()) prefs.putUChar("pxb", pixels.master());
}

void updatePixels(uint32_t now) {
  if (now - lastFrameMs < kPixelFrameMs) return;
  lastFrameMs = now;
  pixels.update(now);
  for (size_t i = 0; i < droid::kMaxPixels; ++i) strip.setPixelColor(i, i < pixels.count() ? pixels.pixel(i) : 0);
  strip.show();
}

void setArm(size_t arm, bool open) {
  pca.setUs(kArmChannel[arm], droid::positionToUs(open ? 1000 : 0, armEnds[arm]));
}

void updateArmSequences(uint32_t now) {
  int16_t pos[droid::kDomePanels];
  if (!armSequences.update(now, pos)) return;
  for (size_t i = 0; i < droid::kServoArmCount; ++i) {
    if (pos[i] != droid::kHoldPosition) pca.setUs(kArmChannel[i], droid::positionToUs(pos[i], armEnds[i]));
  }
}

void runCommand(const char* text, Print* echo) {
  const droid::DomeCommand c = droid::parseDomeCommand(text);
  if (c.kind == droid::DomeCommand::Kind::Arm) {
    armSequences.stop();  // a direct command wins over a running sequence
    setArm(c.servo - droid::kServoArmFirst, c.open);
  } else if (c.kind == droid::DomeCommand::Kind::Sequence) {
    if (!droid::isArmSequence(c.sequence)) {  // dome sequences run on the dome C3
      if (echo) echo->printf("Q%u is a dome sequence\n", c.sequence);
      return;
    }
    armSequences.start(c.sequence, millis());
  } else if (c.kind == droid::DomeCommand::Kind::StopSequence) {
    armSequences.stop();
  } else if (c.kind == droid::DomeCommand::Kind::Pixels) {
    const size_t n = pixels.count();
    const uint8_t b = pixels.master();
    if (!pixels.command(c.text, millis())) {
      if (echo) echo->printf("lights: can't read %s\n", c.text);
      return;
    }
    if (pixels.count() != n || pixels.master() != b) savePixelSetup();
  } else if (c.kind == droid::DomeCommand::Kind::Move && c.servo >= droid::kServoArmFirst &&
             c.servo < droid::kServoArmFirst + droid::kServoArmCount) {
    armSequences.stop();
    pca.setUs(kArmChannel[c.servo - droid::kServoArmFirst], c.us);
  } else {
    if (echo) echo->printf("? %s  (A0o, A0c, A1o, A1c, P32,1500, Q11, Q-, Xswoop #ff8800 > s7, Xoff f1)\n", text);
    return;
  }
  if (echo) echo->printf("ok %s\n", text);
}

void onMessage(void*, const droid::Frame& f, uint32_t) {
  if (f.src != droid::kNodeBodyS3) return;
  if (f.type == droid::MessageType::DomeCommand) {
    char text[droid::kMaxPayload + 1];
    memcpy(text, f.payload, f.length);
    text[f.length] = '\0';
    runCommand(text, nullptr);
  } else if (f.type == droid::MessageType::OtaStart) {
    if (otaClient.start(droid::kNodeBodyC3, f.payload, f.length)) Serial.println("Firmware update requested by the S3");
  } else if (f.type == droid::MessageType::ServoEndpoints) {
    droid::ServoEntry e[droid::kMaxServoEntries];
    const size_t n = droid::decodeServoEntries(f.payload, f.length, e, droid::kMaxServoEntries);
    bool changed = false;
    for (size_t i = 0; i < n; ++i) {
      if (e[i].id >= droid::kServoArmFirst && e[i].id < droid::kServoArmFirst + droid::kServoArmCount) {
        armEnds[e[i].id - droid::kServoArmFirst] = e[i].ends;
        changed = true;
      }
    }
    if (changed && prefsOk) prefs.putBytes("arms", armEnds, sizeof(armEnds));
  }
}

// Tells the S3 this node's firmware version every few seconds.
uint32_t lastVersionMs = 0;
void reportVersion(uint32_t now) {
  if (lastVersionMs != 0 && now - lastVersionMs < 5000) return;
  uint8_t buf[2 * droid::kVersionMax + 3];
  const size_t n = droid::encodeVersionStatus(buf, sizeof(buf), droid::kFirmwareVersion, droid::kFirmwareBuild);
  if (link_.send(droid::kNodeBodyS3, droid::MessageType::NodeStatus, buf, n)) lastVersionMs = now ? now : 1;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  loadEnds();
  loadPixelSetup();
  strip.begin();
  strip.clear();
  strip.show();
  Wire.begin(kPinSda, kPinScl);
  const bool pcaOk = pca.begin();
  for (size_t i = 0; i < droid::kServoArmCount; ++i) setArm(i, false);
  const bool linkOk = link_.begin();
  link_.onMessage(onMessage, nullptr);
  const droid::FirmwareTag fw = droid::thisFirmware();
  Serial.printf("R2 body accessory node v%s (%s)\n", fw.version, fw.build);
  Serial.println("Type @ for link status; A0o / A0c to test the top arm; Xrainbow or Xswoop #ff8800 > s7 to test the lights, Xoff to stop.");
  if (!linkOk) Serial.println("ESP-NOW link off. Type @ for status.");
  if (!pcaOk) Serial.println("PCA9685 not found: arms off");
}

void loop() {
  const uint32_t now = millis();
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (consoleLen == 0 && linkConsole.feed(c, Serial, now)) continue;
    if (c == '\r' || c == '\n') {
      if (consoleLen) {
        consoleLine[consoleLen] = '\0';
        runCommand(consoleLine, &Serial);
      }
      consoleLen = 0;
    } else if (consoleLen < sizeof(consoleLine) - 1) {
      consoleLine[consoleLen++] = c;
    }
  }
  link_.poll(now);
  reportVersion(now);
  otaClient.loop(now, sendOtaStatus);
  // A newly updated image keeps itself once the S3 is heard again.
  otaConfirmIfHealthy(link_.peerUp(droid::kNodeBodyS3, now));
  updateArmSequences(now);
  updatePixels(now);
  delay(1);
}
