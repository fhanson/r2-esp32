// R2 dome node, ESP32-C3.
//
//   - Dome panels: servo sequences on the Maestro (ServoSequences.h).
//   - Holoprojectors, in place of the Flthy HP Nano: LED jewels and PCA9685
//     servos, Flthy-style commands (HoloEngine.h).
//   - Logic displays: commands passed to the WReactor32 on the Maestro's line.
//   - Left-hand radio SBUS: stick X becomes dome spin, sent to the S3.
// Commands come from the S3 over ESP-NOW, or the USB console for bench tests
// (Q2, HA0033|5, L0T3, P16,1500). Not safety-critical: without the S3 the
// holos keep their normal show and any running sequence finishes.

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

#include "DomeConfig.h"
#include "DomeLink.h"
#include "EspNowLink.h"
#include "HoloEngine.h"
#include "LinkConsole.h"
#include "OtaNode.h"
#include "Maestro.h"
#include "Pca9685.h"
#include "SbusReceiver.h"
#include "ServoSequences.h"
#include "Version.h"

namespace {

EspNowLink link_(droid::kNodeDomeC3);
OtaClient otaClient;

bool sendOtaStatus(const uint8_t* payload, size_t length) {
  return link_.send(droid::kNodeBodyS3, droid::MessageType::NodeStatus, payload, length);
}
LinkConsole linkConsole(link_);

// Serial0 is UART0 (the console is on USB), Serial1 is UART1.
HardwareSerial& domeSerial = Serial0;
Maestro maestro(Serial0);
SbusReceiver sbus(Serial1, dome::kPinSbusRx, dome::kSbusLinkTimeoutMs, dome::kSbusAutoBaudMs);

Pca9685 pca(dome::kPcaAddress);
Adafruit_NeoPixel leds(dome::kLedsPerHolo * droid::kHoloCount, dome::kPinHoloLeds, NEO_GRB + NEO_KHZ800);

droid::ServoEndpoints panelEnds[droid::kDomePanels];
droid::ServoEndpoints holoEnds[droid::kServoHoloCount];
Preferences prefs;
bool prefsOk = false;

droid::ServoSequencePlayer sequences;
droid::HoloConfig holoConfig() {
  droid::HoloConfig c;
  c.ledsPerHp = dome::kLedsPerHolo;
  return c;
}
droid::HoloEngine holos(holoConfig());
uint32_t lastHoloMs = 0;
uint16_t lastHoloUs[droid::kServoHoloCount] = {};

int16_t lastSpinSent = 0;
uint32_t lastSpinMs = 0;

char consoleLine[96];  // P/Q/H/L commands; H can carry several
size_t consoleLen = 0;

// ---------------------------------------------------------------------------

void loadEnds() {
  for (size_t i = 0; i < droid::kDomePanels; ++i) panelEnds[i] = dome::kDefaultPanelEnds[i];
  for (size_t i = 0; i < droid::kServoHoloCount; ++i) holoEnds[i] = dome::kDefaultHoloEnds;
  prefsOk = prefs.begin("dome", false);
  if (!prefsOk) return;
  // The S3's last endpoints, so the dome is right even before the S3 is up.
  if (prefs.getBytesLength("panels") == sizeof(panelEnds)) prefs.getBytes("panels", panelEnds, sizeof(panelEnds));
  if (prefs.getBytesLength("holos") == sizeof(holoEnds)) prefs.getBytes("holos", holoEnds, sizeof(holoEnds));
}

void saveEnds() {
  if (!prefsOk) return;
  prefs.putBytes("panels", panelEnds, sizeof(panelEnds));
  prefs.putBytes("holos", holoEnds, sizeof(holoEnds));
}

void movePanel(size_t panel, uint16_t us) { maestro.setTargetUs(droid::kDomePanelMaestroChannel[panel], us); }

uint16_t holoAxisUs(size_t axis, int16_t pos) {
  const droid::ServoEndpoints& e = holoEnds[axis];
  const int32_t mid = e.closedUs;
  const int32_t span = pos < 0 ? mid - e.minUs : e.maxUs - mid;
  return static_cast<uint16_t>(mid + span * pos / 1000);
}

void runCommand(const char* text, Print* echo) {
  const droid::DomeCommand c = droid::parseDomeCommand(text);
  const uint32_t now = millis();
  switch (c.kind) {
    case droid::DomeCommand::Kind::Sequence:
      if (droid::isArmSequence(c.sequence)) {  // the arms are on the body C3
        if (echo) echo->println("arm sequences run on the body C3");
        return;
      }
      sequences.start(c.sequence, now);
      break;
    case droid::DomeCommand::Kind::StopSequence:
      sequences.stop();
      break;
    case droid::DomeCommand::Kind::Holo:
      if (!holos.command(c.text, now) && echo) echo->printf("holo: can't read %s\n", c.text);
      break;
    case droid::DomeCommand::Kind::Logic: {
      // One command, or several separated by ";" (one per display: logics
      // and PSIs each have their own address), each ended with CR.
      const char* p = c.text;
      while (*p) {
        const char* end = strchr(p, ';');
        const size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
        if (n) {
          domeSerial.write(reinterpret_cast<const uint8_t*>(p), n);
          domeSerial.print('\r');
        }
        if (!end) break;
        p = end + 1;
      }
      break;
    }
    case droid::DomeCommand::Kind::Move:
      if (c.servo < droid::kDomePanels) {
        movePanel(c.servo, c.us);
      } else if (c.servo >= droid::kServoHoloFirst && c.servo < droid::kServoHoloFirst + droid::kServoHoloCount) {
        pca.setUs(dome::kHoloServoChannel[c.servo - droid::kServoHoloFirst], c.us);
      }
      break;
    case droid::DomeCommand::Kind::Pixels:  // the body lights are on the body C3
    case droid::DomeCommand::Kind::Arm:     // so are the utility arms
      if (echo) echo->println("arms are on the body C3");
      return;
    case droid::DomeCommand::Kind::Bad:
      if (echo) echo->printf("? %s  (Q<n>, Q-, H<flthy>, L<logic>, P<id>,<us>)\n", text);
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
    if (otaClient.start(droid::kNodeDomeC3, f.payload, f.length)) Serial.println("Firmware update requested by the S3");
  } else if (f.type == droid::MessageType::ServoEndpoints) {
    droid::ServoEntry e[droid::kMaxServoEntries];
    const size_t n = droid::decodeServoEntries(f.payload, f.length, e, droid::kMaxServoEntries);
    for (size_t i = 0; i < n; ++i) {
      if (e[i].id < droid::kDomePanels) {
        panelEnds[e[i].id] = e[i].ends;
      } else if (e[i].id >= droid::kServoHoloFirst && e[i].id < droid::kServoHoloFirst + droid::kServoHoloCount) {
        holoEnds[e[i].id - droid::kServoHoloFirst] = e[i].ends;
      }
    }
    if (n) saveEnds();
  }
}

void updateSequences(uint32_t now) {
  int16_t pos[droid::kDomePanels];
  if (!sequences.update(now, pos)) return;
  for (size_t i = 0; i < droid::kDomePanels; ++i) {
    if (pos[i] != droid::kHoldPosition) movePanel(i, droid::positionToUs(pos[i], panelEnds[i]));
  }
}

void updateHolos(uint32_t now) {
  if (now - lastHoloMs < dome::kHoloUpdateMs) return;
  lastHoloMs = now;
  holos.update(now);
  for (size_t hp = 0; hp < droid::kHoloCount; ++hp) {
    for (size_t i = 0; i < dome::kLedsPerHolo; ++i) leds.setPixelColor(hp * dome::kLedsPerHolo + i, holos.pixel(hp, i));
    const uint16_t x = holoAxisUs(hp * 2, holos.servoX(hp));
    const uint16_t y = holoAxisUs(hp * 2 + 1, holos.servoY(hp));
    if (x != lastHoloUs[hp * 2]) pca.setUs(dome::kHoloServoChannel[hp * 2], x);
    if (y != lastHoloUs[hp * 2 + 1]) pca.setUs(dome::kHoloServoChannel[hp * 2 + 1], y);
    lastHoloUs[hp * 2] = x;
    lastHoloUs[hp * 2 + 1] = y;
  }
  leds.show();
}

// Left stick X -> dome spin for the S3. Sent while turning, and once more
// when it stops; the S3 drops a spin it hasn't heard for 250 ms.
void updateSpin(uint32_t now) {
  sbus.poll();
  int16_t spin = 0;
  if (sbus.linkOk(now)) {
    const float v = droid::normalize(sbus.frame().ch[dome::kChDomeSpin - 1], dome::domeSpinCal());
    spin = static_cast<int16_t>(v * 1000.0f);
  }
  const bool due = spin != 0 ? (now - lastSpinMs >= dome::kSpinSendMs) : (lastSpinSent != 0);
  if (!due) return;
  uint8_t buf[2];
  if (link_.send(droid::kNodeBodyS3, droid::MessageType::DomeSpin, buf, droid::encodeSpin(spin, buf, sizeof(buf)))) {
    lastSpinSent = spin;
    lastSpinMs = now;
  }
}

void handleConsole(uint32_t now) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (consoleLen == 0 && linkConsole.feed(c, Serial, now)) continue;
    if (c == '\r' || c == '\n') {
      if (consoleLen > 0) {
        consoleLine[consoleLen] = '\0';
        runCommand(consoleLine, &Serial);
      }
      consoleLen = 0;
    } else if (consoleLen < sizeof(consoleLine) - 1) {
      consoleLine[consoleLen++] = c;
    }
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
  domeSerial.begin(dome::kDomeSerialBaud, SERIAL_8N1, -1, dome::kPinDomeSerialTx);
  sbus.begin();
  Wire.begin(dome::kPinSda, dome::kPinScl);
  const bool pcaOk = pca.begin();
  leds.begin();
  leds.setBrightness(dome::kHoloBrightness);
  leds.clear();
  leds.show();
  holos.seed(esp_random());
  holos.command("S5", millis());  // normal show: twitch and auto LED

  const bool linkOk = link_.begin();
  link_.onMessage(onMessage, nullptr);
  const droid::FirmwareTag fw = droid::thisFirmware();
  Serial.printf("R2 dome node v%s (%s)\n", fw.version, fw.build);
  Serial.println("Type @ for link status; Q2, HA0033|5, L0T3 or P16,1500 to test.");
  if (!linkOk) Serial.println("ESP-NOW link off. Type @ for status.");
  if (!pcaOk) Serial.println("PCA9685 not found: holo servos off");
}

void loop() {
  const uint32_t now = millis();
  handleConsole(now);
  link_.poll(now);
  reportVersion(now);
  otaClient.loop(now, sendOtaStatus);
  // A newly updated image keeps itself once the S3 is heard again.
  otaConfirmIfHealthy(link_.peerUp(droid::kNodeBodyS3, now));
  updateSequences(now);
  updateHolos(now);
  updateSpin(now);
  delay(1);
}
