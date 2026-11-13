// R2 body controller, ESP32-S3.
//
// Drive (safety-critical): SBUS -> failsafe -> arming -> curve and ramp ->
// mix -> VESC pulses. Nothing in the drive path waits on sound, the dome,
// ESP-NOW or the web page.
// Also: HCR sound on the Teensy, the Roam-a-Dome, the two C3 nodes over
// ESP-NOW, and the control page on its own access point.
//
// Bench-test with the wheels off the ground. See README.md.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>

#include "AnimationStore.h"
#include "CustomSequences.h"
#include "Animations.h"
#include "BodyConfig.h"
#include "DomeLink.h"
#include "DriveArming.h"
#include "DriveMixer.h"
#include "DomeControl.h"
#include "DomePulse.h"
#include "DriveRamp.h"
#include "EspNowLink.h"
#include "FeetPwm.h"
#include "HcrCommands.h"
#include "HcrEvents.h"
#include "HcrLink.h"
#include "LinkConsole.h"
#include "NvsSettings.h"
#include "Ota.h"
#include "OtaNode.h"
#include "OtaServer.h"
#include "RcChannel.h"
#include "RdhLink.h"
#include "SbusReceiver.h"
#include "SequenceStore.h"
#include "Secrets.h"  // kApPassword; gitignored, made by tools/make_secrets.py
#include "Settings.h"
#include "UiProtocol.h"
#include "Version.h"
#include "WifiConfig.h"
#include "WebUi.h"

namespace {

// With ARDUINO_USB_CDC_ON_BOOT=1, Serial is the native USB console and
// Serial0 is UART0, free for SBUS.
SbusReceiver sbus(Serial0, body::kPinSbusRx, body::kLinkTimeoutMs, body::kAutoBaudSwitchMs);

FeetPwm feet(body::kPinLeftFoot, body::kPinRightFoot, body::kLedcLeftChannel,
             body::kLedcRightChannel, body::kPwmFrequencyHz, body::kPwmResolutionBits);

NvsSettings nvs;
droid::Settings settings(body::kSettings, body::kSettingCount, nvs);

HcrLink hcr(Serial2, body::kPinHcrRx, body::kPinHcrTx, body::kHcrBaud, body::kHcrTxBufferBytes);
droid::HcrEvents hcrEvents(body::kSettings[body::kSetHcrGapMs].defaultValue);
bool hcrStarted = false;

RdhLink rdh(Serial1, body::kPinRdhRx, body::kPinRdhTx, body::kRdhBaud, body::kRdhTxBufferBytes);
DomePulse domePulse(body::kPinDomePwm, body::kLedcDomeChannel, body::kPwmFrequencyHz,
                    body::kPwmResolutionBits);
droid::DomeControl dome(droid::DomeConfig{});
uint32_t rdhEchoUntilMs = 0;

EspNowLink radio(droid::kNodeBodyS3);
LinkConsole linkConsole(radio);
bool domeNodeWasUp = false;
bool bodyC3WasUp = false;

WebUi webUi;
AnimationStore animations;
SequenceStore customSeqs;  // made on the page; played here
droid::CustomSequencePlayer customPlayer;
droid::AnimationPlayer animPlayer;
int8_t servoSequenceRunning = -1;   // what the dome was last told to run
uint32_t servoSequenceEndsMs = 0;
uint32_t lastStatusMs = 0;

// What each web page still needs to be sent after it says hello.
struct UiClient {
  int fd = -1;
  uint8_t stage = 0;     // 0 done, 1 servo sequences, 2 settings, 3 animations, 4 versions
  bool onlyOne = false;  // stop after this stage (a List request)
  uint16_t cursor = 0;
  droid::AnimAssembler assembler;
  droid::SeqAssembler seqAssembler;
};
UiClient uiClients[WebUi::kMaxClients];

droid::DriveArming arming(body::kArmHoldMs);
droid::DriveRamp ramp(body::rampConfig());
uint32_t lastLoopUs = 0;
uint32_t worstLoopUs = 0;  // since the last monitor line

const droid::ChannelCal kSteerCal = body::steerCal();
const droid::ChannelCal kThrottleCal = body::throttleCal();
const droid::ChannelCal kSpeedCal = body::speedCal();
const droid::ChannelCal kEnableCal = body::enableCal();
const droid::ChannelCal kSoundCal = body::soundCal();
const droid::ChannelCal kDomeModeCal = body::domeModeCal();
const droid::MixerConfig kMixer = body::mixerConfig();
const droid::PulseRange kPulses = body::footPulseRange();
const uint16_t kNeutralUs = droid::speedToPulseUs(0.0f, kPulses);

bool monitorOn = false;
uint32_t lastPrintMs = 0;

// Typed lines. A <...> command goes straight to HCR, and its reply is echoed
// for a moment afterwards. A line starting with ':' works on settings, and one
// starting with '>' goes to the Roam-a-Dome's command serial.
constexpr uint32_t kHcrEchoMs = 1000;
constexpr size_t kConsoleLineBytes = 64;
char consoleLine[kConsoleLineBytes];
size_t consoleLineLen = 0;  // 0 = not inside a typed line
uint32_t hcrEchoUntilMs = 0;

// Latest decoded inputs, kept for the monitor.
struct Inputs {
  float steer = 0, throttle = 0;
  uint8_t speedLevel = 0;
  bool enable = false;
  bool sound = false;
  uint8_t domeMode = 0;  // DomeMode: 0 manual, 1 auto, 2 home
} in;

uint16_t raw(const droid::SbusFrame& f, int ch1Based) { return f.ch[ch1Based - 1]; }

void readInputs(const droid::SbusFrame& f) {
  in.steer = droid::normalize(raw(f, body::kChSteer), kSteerCal);
  in.throttle = droid::normalize(raw(f, body::kChThrottle), kThrottleCal);
  in.speedLevel = droid::decodeSwitch3(raw(f, body::kChSpeed), kSpeedCal);
  in.enable = droid::decodeSwitch2(raw(f, body::kChEnable), kEnableCal);
  in.sound = droid::decodeSwitch2(raw(f, body::kChSound), kSoundCal);
  in.domeMode = droid::decodeSwitch3(raw(f, body::kChDomeMode), kDomeModeCal);
}

void stopFeet() { feet.writeUs(kNeutralUs, kNeutralUs); }

void playWavNumber(uint16_t number) {
  char command[16];
  hcr.send(command, droid::hcrPlayWav(command, sizeof(command), body::kHcrChannel, number));
}

void playWav(body::Setting file) { playWavNumber(static_cast<uint16_t>(settings.get(file))); }

void playRandomRange(uint16_t a, uint16_t b) {
  char command[16];
  hcr.send(command, droid::hcrPlayRandomWav(command, sizeof(command), body::kHcrChannel, a < b ? a : b,
                                            a < b ? b : a));
}

// emotion 0-3, or 4 for a random emotion at a random strength.
void stimulate(uint8_t emotion) {
  bool strong = false;
  if (emotion > 3) {
    const uint32_t r = esp_random();
    emotion = static_cast<uint8_t>(r % 4);
    strong = (r >> 8) & 1;
  }
  char command[16];
  hcr.send(command, droid::hcrStimulate(command, sizeof(command), emotion, strong));
}

// ---------------------------------------------------------------------------
// Dome C3 and body C3 over ESP-NOW
// ---------------------------------------------------------------------------
bool sendDome(char kind, const char* text) {
  char buf[droid::kMaxPayload];
  const int n = snprintf(buf, sizeof(buf), "%c%s", kind, text);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return false;
  return radio.send(droid::kNodeDomeC3, droid::MessageType::DomeCommand, reinterpret_cast<uint8_t*>(buf), n);
}

bool sendBodyC3(char kind, const char* text);

// Built-in sequences play on the C3 that owns them; custom ones play here.
void runServoSequence(uint8_t index, uint32_t now, uint8_t fromFrame = 0) {
  char arg[8];
  if (droid::isCustomSequenceId(index)) {
    const droid::CustomSequence* q = customSeqs.find(index);
    if (!q || !customPlayer.start(*q, fromFrame, now)) return;
    sendDome('Q', "-");  // so a built-in one doesn't fight it
    sendBodyC3('Q', "-");
    uint32_t left = 0;
    for (size_t i = fromFrame; i < q->frameCount; ++i) left += q->frames[i].ms;
    servoSequenceRunning = static_cast<int8_t>(index);
    servoSequenceEndsMs = now + left;
    return;
  }
  const droid::ServoSequence* seq = droid::sequenceAt(index);
  if (!seq) return;
  customPlayer.stop();
  snprintf(arg, sizeof(arg), "%u", index);
  if (droid::isArmSequence(index)) {
    sendBodyC3('Q', arg);
  } else {
    sendDome('Q', arg);
  }
  servoSequenceRunning = static_cast<int8_t>(index);
  servoSequenceEndsMs = now + droid::servoSequenceLengthMs(*seq);
}

droid::ServoEntry servoEntry(uint8_t id) {
  droid::ServoEntry e;
  e.id = id;
  size_t base;
  if (id < droid::kDomePanels) {
    base = body::kSetServoFirst + id * body::kServoSettingsPerPanel;
  } else if (id >= droid::kServoArmFirst) {
    base = body::kSetArmFirst + (id - droid::kServoArmFirst) * body::kServoSettingsPerPanel;
  } else {
    // Holo axes: min, centre, max. Centre goes in both middle slots.
    base = body::kSetHoloFirst + (id - droid::kServoHoloFirst) * body::kServoSettingsPerHolo;
    e.ends.minUs = static_cast<uint16_t>(settings.get(base));
    e.ends.closedUs = e.ends.openUs = static_cast<uint16_t>(settings.get(base + 1));
    e.ends.maxUs = static_cast<uint16_t>(settings.get(base + 2));
    return e;
  }
  e.ends.minUs = static_cast<uint16_t>(settings.get(base));
  e.ends.closedUs = static_cast<uint16_t>(settings.get(base + 1));
  e.ends.openUs = static_cast<uint16_t>(settings.get(base + 2));
  e.ends.maxUs = static_cast<uint16_t>(settings.get(base + 3));
  return e;
}

void syncDomeServos() {
  droid::ServoEntry e[droid::kDomePanels + droid::kServoHoloCount];
  size_t n = 0;
  for (uint8_t id = 0; id < droid::kDomePanels; ++id) e[n++] = servoEntry(id);
  for (uint8_t i = 0; i < droid::kServoHoloCount; ++i) e[n++] = servoEntry(droid::kServoHoloFirst + i);
  uint8_t buf[droid::kMaxPayload];
  const size_t len = droid::encodeServoEntries(e, n, buf, sizeof(buf));
  radio.send(droid::kNodeDomeC3, droid::MessageType::ServoEndpoints, buf, len);
}

// A command for the body C3 (utility arms "A...", lights "X...").
bool sendBodyC3(char kind, const char* text) {
  char buf[droid::kMaxPayload];
  const int n = snprintf(buf, sizeof(buf), "%c%s", kind, text);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf)) return false;
  return radio.send(droid::kNodeBodyC3, droid::MessageType::DomeCommand, reinterpret_cast<uint8_t*>(buf), n);
}

// Strip length and brightness, when the body C3 comes up or either changes.
void syncPixels() {
  char text[24];
  snprintf(text, sizeof(text), "setup n%ld b%ld", (long)settings.get(body::kSetPixelCount),
           (long)settings.get(body::kSetPixelBright));
  sendBodyC3('X', text);
}

void syncArmServos() {
  droid::ServoEntry e[droid::kServoArmCount];
  for (uint8_t i = 0; i < droid::kServoArmCount; ++i) e[i] = servoEntry(droid::kServoArmFirst + i);
  uint8_t buf[droid::kMaxPayload];
  const size_t len = droid::encodeServoEntries(e, droid::kServoArmCount, buf, sizeof(buf));
  radio.send(droid::kNodeBodyC3, droid::MessageType::ServoEndpoints, buf, len);
}

// The access point's name and password: the built-in pair (kApSsid, and
// kApPassword from Secrets.h), or one set from Settings and saved in NVS.
bool apOk = false;  // the access point started
char apSsid[droid::kWifiNameMax + 1] = {};
char apPass[droid::kWifiPasswordMax + 1] = {};
bool apCustom = false;
Preferences wifiPrefs;
bool wifiPrefsOk = false;
uint32_t wifiRestartAtMs = 0;  // a change waits a moment, so the page hears "saved" first

void loadWifi() {
  strcpy(apSsid, body::kApSsid);
  strcpy(apPass, body::kApPassword);
  apCustom = false;
  wifiPrefsOk = wifiPrefs.begin("wifi", false);
  if (!wifiPrefsOk || !wifiPrefs.isKey("ssid")) return;
  char name[sizeof(apSsid)] = {}, pass[sizeof(apPass)] = {};
  wifiPrefs.getString("ssid", name, sizeof(name));
  wifiPrefs.getString("pass", pass, sizeof(pass));
  if (!droid::wifiNameValid(name) || !droid::wifiPasswordValid(pass)) return;  // damaged: built-in
  strcpy(apSsid, name);
  strcpy(apPass, pass);
  apCustom = true;
}

// Saves a new pair (or, with both empty, goes back to the built-in one).
// The access point restarts with it shortly after.
bool setWifi(const char* name, const char* pass, uint32_t now) {
  if (!name[0] && !pass[0]) {
    if (wifiPrefsOk) wifiPrefs.clear();
    strcpy(apSsid, body::kApSsid);
    strcpy(apPass, body::kApPassword);
    apCustom = false;
  } else {
    if (!droid::wifiNameValid(name) || !droid::wifiPasswordValid(pass) || !wifiPrefsOk) return false;
    if (!wifiPrefs.putString("ssid", name) || !wifiPrefs.putString("pass", pass)) return false;
    strcpy(apSsid, name);
    strcpy(apPass, pass);
    apCustom = true;
  }
  wifiRestartAtMs = (now + 1500) | 1;
  return true;
}

// At startup and on 'v' (the console often connects after boot).
void printBanner() {
  const droid::FirmwareTag fw = droid::thisFirmware();
  Serial.printf("R2 body controller v%s (%s)\n", fw.version, fw.build);
  static const char* const kModes[] = {"off", "STA", "AP", "AP+STA"};
  const int mode = static_cast<int>(WiFi.getMode());
  const char* modeName = mode >= 0 && mode <= 3 ? kModes[mode] : "?";
  if (apOk) {
    Serial.printf("Wi-Fi \"%s\", password \"%s\" (%s), page http://%s/\n", apSsid, apPass,
                  apCustom ? "set in Settings" : "built in", WiFi.softAPIP().toString().c_str());
    Serial.printf("  access point on channel %d, MAC %s, Wi-Fi mode %s, %d joined\n", WiFi.channel(),
                  WiFi.softAPmacAddress().c_str(), modeName, WiFi.softAPgetStationNum());
  } else {
    Serial.printf("Wi-Fi \"%s\" didn't start (Wi-Fi mode %s, password %u characters): no control page\n", apSsid,
                  modeName, static_cast<unsigned>(strlen(apPass)));
  }
}

// Configured before it's switched on, so it's never briefly an open "ESP_..."
// network, as with WiFi.softAP().
void startAccessPoint() {
  WiFi.persistent(false);  // set every boot; nothing to save to flash
  wifi_config_t conf = {};
  const size_t n = strlen(apSsid);
  memcpy(conf.ap.ssid, apSsid, n);
  conf.ap.ssid_len = static_cast<uint8_t>(n);
  memcpy(conf.ap.password, apPass, strlen(apPass));
  conf.ap.channel = droid::kLinkChannel;
  conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
  conf.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
  conf.ap.max_connection = body::kApMaxClients;
  conf.ap.beacon_interval = 100;
  if (WiFi.getMode() & WIFI_MODE_AP) {
    apOk = esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK;
    return;
  }
  if (WiFi.getMode() == WIFI_OFF) WiFi.mode(WIFI_STA);  // starts the Wi-Fi driver
  esp_wifi_stop();
  apOk = esp_wifi_set_mode(WIFI_MODE_APSTA) == ESP_OK && esp_wifi_set_config(WIFI_IF_AP, &conf) == ESP_OK &&
         esp_wifi_start() == ESP_OK;
}

// Logs joins, addresses and leaves. "joined" means the password was accepted,
// so a try that logs nothing was refused. Runs in the Wi-Fi task.
void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  char mac[18];
  auto fmt = [&](const uint8_t* m) {
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return mac;
  };
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START: {
      // What the radio is really broadcasting, read back from it.
      wifi_config_t cfg = {};
      esp_wifi_get_config(WIFI_IF_AP, &cfg);
      static const char* const kAuth[] = {"open", "WEP", "WPA", "WPA2", "WPA/WPA2", "WPA2-Enterprise", "WPA3", "WPA2/WPA3"};
      const unsigned auth = cfg.ap.authmode;
      Serial.printf("Wi-Fi: access point up at %lu ms: \"%.32s\", %s, channel %u, password %u characters\n",
                    (unsigned long)millis(), reinterpret_cast<const char*>(cfg.ap.ssid), auth < 8 ? kAuth[auth] : "?",
                    cfg.ap.channel, static_cast<unsigned>(strnlen(reinterpret_cast<const char*>(cfg.ap.password), 64)));
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.printf("Wi-Fi: %s joined, password accepted\n", fmt(info.wifi_ap_staconnected.mac));
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED: {
      const uint32_t ip = info.wifi_ap_staipassigned.ip.addr;
      Serial.printf("Wi-Fi: gave it %u.%u.%u.%u; the page is http://%s/\n", ip & 0xff, (ip >> 8) & 0xff,
                    (ip >> 16) & 0xff, ip >> 24, WiFi.softAPIP().toString().c_str());
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.printf("Wi-Fi: %s left\n", fmt(info.wifi_ap_stadisconnected.mac));
      break;
    default:
      break;
  }
}

// Firmware versions by node id, as each node reports them ("" until then).
char nodeVersion[droid::kMaxNodes][droid::kVersionMax + 1] = {};
char nodeBuild[droid::kMaxNodes][droid::kVersionMax + 1] = {};
void uiBroadcastWifi();
void uiBroadcastVersion(uint8_t node);
void onOtaProgress(uint8_t node, const droid::OtaProgress& p);

// Messages the C3 nodes send. Runs inside radio.poll().
void onRadioMessage(void*, const droid::Frame& f, uint32_t now) {
  if (f.type == droid::MessageType::DomeSpin && f.src == droid::kNodeDomeC3) {
    int16_t spin = 0;
    if (droid::decodeSpin(f.payload, f.length, spin)) dome.setSpin(spin / 1000.0f, now);
  } else if (f.type == droid::MessageType::NodeStatus && f.src < droid::kMaxNodes) {
    droid::OtaProgress p;
    if (droid::decodeOtaProgress(f.payload, f.length, p)) {
      onOtaProgress(f.src, p);
      return;
    }
    char v[droid::kVersionMax + 1], b[droid::kVersionMax + 1];
    if (!droid::decodeVersionStatus(f.payload, f.length, v, b)) return;
    if (strcmp(v, nodeVersion[f.src]) == 0 && strcmp(b, nodeBuild[f.src]) == 0) return;
    strcpy(nodeVersion[f.src], v);
    strcpy(nodeBuild[f.src], b);
    if (droid::compareVersions(v, droid::kFirmwareVersion) != 0) {
      Serial.printf("%s runs v%s, this S3 v%s: update them to match\n", droid::nodeName(f.src), v,
                    droid::kFirmwareVersion);
    }
    uiBroadcastVersion(f.src);
  }
}

void playStep(const droid::AnimStep& s, uint32_t now) {
  switch (s.kind) {
    case droid::StepKind::Sound: playWavNumber(s.a); break;
    case droid::StepKind::RandomSound: playRandomRange(s.a, s.b); break;
    case droid::StepKind::Dome: {
      // As the Dome tiles: CH5 keeps its mode.
      const int16_t v = droid::domeStepValue(s);
      const uint8_t x = droid::domeStepExtra(s);
      switch (droid::domeStepAction(s)) {
        case droid::DomeAction::Home: dome.requestHome(); break;
        case droid::DomeAction::GoTo:
          dome.requestGoTo(v < -180 ? -180 : (v > 180 ? 180 : v), x ? (x > 100 ? 100 : x) : body::kUiDomeSpeed);
          break;
        case droid::DomeAction::Spin:
          dome.setSpin((v < -100 ? -100 : (v > 100 ? 100 : v)) / 100.0f, now, x * 100u);
          break;
        case droid::DomeAction::Stop:
          dome.setSpin(0.0f, now, 0);
          dome.requestStop();
          break;
      }
      break;
    }
    case droid::StepKind::SoundPick: {
      uint16_t files[droid::kSoundPickMaxFile];
      const size_t n = droid::soundPickFiles(s.text, files, droid::kSoundPickMaxFile);
      if (n) playWavNumber(files[esp_random() % n]);
      break;
    }
    case droid::StepKind::Emotion: stimulate(static_cast<uint8_t>(s.a)); break;
    case droid::StepKind::ServoSequence:
      if (s.a < droid::allSequenceCount() || droid::isCustomSequenceId(s.a)) runServoSequence(static_cast<uint8_t>(s.a), now);
      break;
    case droid::StepKind::Holo: sendDome('H', s.text); break;
    case droid::StepKind::Logic: sendDome('L', s.text); break;
    case droid::StepKind::Pixels: sendBodyC3('X', s.text); break;
    case droid::StepKind::None: break;
  }
}

void playRandomWav() {
  const int32_t a = settings.get(body::kSetHcrRandomFirst);
  const int32_t b = settings.get(body::kSetHcrRandomLast);
  char command[16];
  hcr.send(command, droid::hcrPlayRandomWav(command, sizeof(command), body::kHcrChannel,
                                            static_cast<uint16_t>(a < b ? a : b),
                                            static_cast<uint16_t>(a < b ? b : a)));
}

void sendHcrStartup() {
  droid::HcrSetup h;
  h.vocalizerVolume = static_cast<uint8_t>(settings.get(body::kSetHcrVolVocalizer));
  h.wavAVolume = static_cast<uint8_t>(settings.get(body::kSetHcrVolWavA));
  h.wavBVolume = static_cast<uint8_t>(settings.get(body::kSetHcrVolWavB));
  h.happy = static_cast<uint8_t>(settings.get(body::kSetHcrHappy));
  h.sad = static_cast<uint8_t>(settings.get(body::kSetHcrSad));
  h.mad = static_cast<uint8_t>(settings.get(body::kSetHcrMad));
  h.scared = static_cast<uint8_t>(settings.get(body::kSetHcrScared));
  char command[64];
  hcr.send(command, droid::hcrStartup(command, sizeof(command), h));
}

void applyHcrAutoDelay() {
  hcrEvents.setAutoDelayMs(static_cast<uint32_t>(settings.get(body::kSetHcrAutoMinS)) * 1000u,
                           static_cast<uint32_t>(settings.get(body::kSetHcrAutoMaxS)) * 1000u);
}

// Puts a changed setting into effect. Settings not listed here are read each
// time they're used.
void applySetting(size_t index) {
  switch (index) {
    case body::kSetHcrVolVocalizer:
    case body::kSetHcrVolWavA:
    case body::kSetHcrVolWavB:
    case body::kSetHcrHappy:
    case body::kSetHcrSad:
    case body::kSetHcrMad:
    case body::kSetHcrScared:
      if (hcrStarted) sendHcrStartup();
      break;
    case body::kSetHcrGapMs:
      hcrEvents.setSoundGapMs(static_cast<uint32_t>(settings.get(index)));
      break;
    case body::kSetHcrAutoMinS:
    case body::kSetHcrAutoMaxS:
      applyHcrAutoDelay();
      break;
    case body::kSetDomeSpinMax:
    case body::kSetDomeInvert:
    case body::kSetDomeAutoDeg:
    case body::kSetDomeAutoSpeed:
    case body::kSetDomeAutoMinS:
    case body::kSetDomeAutoMaxS:
      dome.setConfig(body::domeConfig(settings));
      break;
    case body::kSetPixelCount:
    case body::kSetPixelBright:
      syncPixels();
      break;
    default:
      if (index >= body::kSetArmFirst) {
        syncArmServos();
      } else if (index >= body::kSetServoFirst) {
        syncDomeServos();
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// The web page (UiProtocol over WebUi)
// ---------------------------------------------------------------------------
UiClient* uiClient(int fd, bool create) {
  for (UiClient& c : uiClients) if (c.fd == fd) return &c;
  if (!create) return nullptr;
  for (UiClient& c : uiClients) {
    if (c.fd < 0) {
      c = UiClient{};
      c.fd = fd;
      return &c;
    }
  }
  return nullptr;
}

void uiReply(int fd, droid::UiType request, droid::UiResult result) {
  uint8_t buf[8];
  webUi.send(fd, buf, droid::uiResult(buf, sizeof(buf), request, result));
}

void uiBroadcastSetting(size_t index) {
  uint8_t buf[16];
  webUi.broadcast(buf, droid::uiSettingValue(buf, sizeof(buf), static_cast<uint8_t>(index), settings.get(index),
                                             settings.overridden(index)));
}

// What a page still needs after hello, a few messages per loop.
void uiSyncStep(UiClient& c) {
  uint8_t buf[droid::kUiMaxMessage];
  for (size_t sent = 0; sent < body::kUiSyncPerLoop && c.stage != 0; ++sent) {
    size_t n = 0;
    if (c.stage == 1) {
      // Built-ins: one info each. Custom ones: info, header, then each frame.
      const size_t builtIn = droid::allSequenceCount();
      size_t left = c.cursor;
      size_t i = 0;
      if (left >= builtIn) {
        left -= builtIn;
        while (i < customSeqs.count() && left >= 2u + customSeqs.at(i).frameCount) {
          left -= 2u + customSeqs.at(i).frameCount;
          ++i;
        }
      }
      if (c.cursor < builtIn) {
        const droid::ServoSequence& q = *droid::sequenceAt(c.cursor);
        n = droid::uiServoSequenceInfo(buf, sizeof(buf), static_cast<uint8_t>(c.cursor), q.frameCount,
                                       droid::servoSequenceLengthMs(q), q.name);
        ++c.cursor;
      } else if (i < customSeqs.count()) {
        const droid::CustomSequence& q = customSeqs.at(i);
        if (left == 0) {
          n = droid::uiServoSequenceInfo(buf, sizeof(buf), q.id, q.frameCount, droid::customSequenceLengthMs(q), q.name);
        } else if (left == 1) {
          n = droid::uiSeqHeader(buf, sizeof(buf), q);
        } else {
          n = droid::uiSeqFrame(buf, sizeof(buf), q.id, static_cast<uint8_t>(left - 2), q.frames[left - 2]);
        }
        ++c.cursor;
      } else {
        n = droid::uiListEnd(buf, sizeof(buf), droid::UiType::RunServoSequence,
                             static_cast<uint8_t>(builtIn + customSeqs.count()));
        c.stage = c.onlyOne ? 0 : 2;
        c.cursor = 0;
      }
    } else if (c.stage == 2) {
      if (c.cursor < settings.count()) {
        const droid::SettingDef& d = settings.def(c.cursor);
        n = droid::uiSettingInfo(buf, sizeof(buf), static_cast<uint8_t>(c.cursor),
                                 static_cast<uint8_t>(settings.count()), settings.get(c.cursor), d.defaultValue,
                                 d.min, d.max, settings.overridden(c.cursor), d.key);
        ++c.cursor;
      } else {
        n = droid::uiListEnd(buf, sizeof(buf), droid::UiType::ListSettings, static_cast<uint8_t>(settings.count()));
        c.stage = c.onlyOne ? 0 : 3;
        c.cursor = 0;
      }
    } else if (c.stage == 3) {
      // cursor counts messages: for each animation, its header then its steps.
      size_t left = c.cursor;
      size_t i = 0;
      while (i < animations.count() && left >= 1u + animations.at(i).stepCount) {
        left -= 1u + animations.at(i).stepCount;
        ++i;
      }
      if (i < animations.count()) {
        const droid::Animation& a = animations.at(i);
        n = left == 0 ? droid::uiAnimHeader(buf, sizeof(buf), a)
                      : droid::uiAnimStep(buf, sizeof(buf), a.id, static_cast<uint8_t>(left - 1), a.steps[left - 1]);
        ++c.cursor;
      } else {
        n = droid::uiListEnd(buf, sizeof(buf), droid::UiType::ListAnimations,
                             static_cast<uint8_t>(animations.count()));
        c.stage = c.onlyOne ? 0 : 4;
        c.cursor = 0;
      }
    } else if (c.stage == 4) {
      // Firmware versions: this S3's and each node's that has reported.
      const uint8_t node = static_cast<uint8_t>(droid::kNodeBodyS3 + c.cursor);
      if (node <= droid::kNamedNodes) {
        if (nodeVersion[node][0]) n = droid::uiNodeVersion(buf, sizeof(buf), node, nodeVersion[node], nodeBuild[node]);
        ++c.cursor;
      } else {
        n = droid::uiWifiInfo(buf, sizeof(buf), apCustom, apSsid, apPass);
        c.stage = 0;
      }
    }
    if (n) webUi.send(c.fd, buf, n);
  }
}

void uiBroadcastWifi() {
  uint8_t buf[2 + 2 + droid::kWifiNameMax + droid::kWifiPasswordMax];
  const size_t n = droid::uiWifiInfo(buf, sizeof(buf), apCustom, apSsid, apPass);
  if (n) webUi.broadcast(buf, n);
}

void uiBroadcastVersion(uint8_t node) {
  uint8_t buf[64];
  const size_t n = droid::uiNodeVersion(buf, sizeof(buf), node, nodeVersion[node], nodeBuild[node]);
  if (n) webUi.broadcast(buf, n);
}

// After the sequence library changes, every page gets the list again.
void uiResendSequences() {
  for (UiClient& c : uiClients) {
    if (c.fd < 0) continue;
    if (c.stage == 0 || c.stage == 1) {
      c.stage = 1;
      c.cursor = 0;
      c.onlyOne = true;
    }
  }
}

// After the animation library changes, every page gets it again.
void uiResendAnimations() {
  for (UiClient& c : uiClients) {
    if (c.fd < 0) continue;
    if (c.stage == 0 || c.stage == 3) {
      c.stage = 3;
      c.cursor = 0;
      c.onlyOne = true;
    }
  }
}

// One servo to a pulse width now, on whichever C3 drives it.
void moveServo(uint8_t id, uint16_t us) {
  if (us < 500) us = 500;
  if (us > 2500) us = 2500;
  char arg[16];
  snprintf(arg, sizeof(arg), "%u,%u", id, us);
  const bool dome = id < droid::kDomePanels || (id >= droid::kServoHoloFirst && id < droid::kServoHoloFirst + droid::kServoHoloCount);
  const bool arm = id >= droid::kServoArmFirst && id < droid::kServoArmFirst + droid::kServoArmCount;
  if (dome) sendDome('P', arg);
  else if (arm) sendBodyC3('P', arg);
}

// The custom sequence playing, if any: each frame's moves, held inside each
// servo's limits by its endpoints.
void updateCustomSequence(uint32_t now) {
  int8_t pos[droid::kSeqServos];
  if (!customPlayer.update(now, pos)) return;
  for (size_t k = 0; k < droid::kSeqServos; ++k) {
    if (pos[k] == droid::kSeqHold) continue;
    const uint8_t id = droid::kSeqServoId[k];
    moveServo(id, droid::positionToUs(static_cast<int16_t>(pos[k] * 10), servoEntry(id).ends));
  }
}

// Firmware updates (OtaServer.h, Ota.h). A C3 gets OtaStart until it answers,
// three tries at most.
struct NodeOta {
  bool waiting = false;  // OtaStart sent, no answer yet
  uint32_t size = 0;
  uint32_t sentMs = 0;
  uint8_t tries = 0;
  droid::OtaState last = droid::OtaState::Idle;
};
NodeOta nodeOta[droid::kMaxNodes];
uint32_t s3RestartAtMs = 0;
constexpr uint32_t kOtaRetryMs = 4000;
constexpr uint8_t kOtaTries = 3;
constexpr uint32_t kOtaConfirmMs = 20000;  // a new S3 image keeps itself after this long

void uiBroadcastOta(uint8_t node, droid::OtaState state, uint8_t percent, droid::OtaError error, const char* text) {
  uint8_t buf[96];
  const size_t n = droid::uiOtaStatus(buf, sizeof(buf), node, static_cast<uint8_t>(state), percent,
                                      static_cast<uint8_t>(error), text);
  if (n) webUi.broadcast(buf, n);
}

void onOtaProgress(uint8_t node, const droid::OtaProgress& p) {
  NodeOta& o = nodeOta[node];
  o.waiting = false;
  if (p.state != o.last) {
    o.last = p.state;
    if (p.state == droid::OtaState::Failed) {
      Serial.printf("%s update failed: %s\n", droid::nodeName(node), droid::otaErrorText(p.error));
    } else if (p.state == droid::OtaState::Restarting) {
      Serial.printf("%s update written, restarting\n", droid::nodeName(node));
      // So the page hears its next report even if the tag hasn't changed.
      nodeVersion[node][0] = '\0';
      nodeBuild[node][0] = '\0';
    }
  }
  uiBroadcastOta(node, p.state, p.percent, p.error, droid::otaErrorText(p.error));
}

void sendOtaStart(uint8_t node, uint32_t now) {
  droid::OtaStart s;
  s.size = nodeOta[node].size;
  const IPAddress ip = WiFi.softAPIP();
  for (int i = 0; i < 4; ++i) s.ip[i] = ip[i];
  strncpy(s.ssid, apSsid, sizeof(s.ssid) - 1);
  strncpy(s.password, apPass, sizeof(s.password) - 1);
  uint8_t buf[droid::kMaxPayload];
  const size_t n = droid::encodeOtaStart(buf, sizeof(buf), s);
  if (n) radio.send(static_cast<droid::NodeId>(node), droid::MessageType::OtaStart, buf, n);
  nodeOta[node].sentMs = now ? now : 1;
  ++nodeOta[node].tries;
}

void updateOta(uint32_t now, bool armed) {
  const char* reason = armed ? "Disarm the drive first" : (in.enable ? "Turn the enable switch off first" : "");
  const bool allowed = reason[0] == '\0';
  ota::setAllowed(allowed, reason);
  ota::setNodeUp(droid::kNodeDomeC3, radio.peerUp(droid::kNodeDomeC3, now));
  ota::setNodeUp(droid::kNodeBodyC3, radio.peerUp(droid::kNodeBodyC3, now));

  if (ota::takeRestart()) {
    Serial.println("New S3 firmware written: restarting into it");
    uiBroadcastOta(droid::kNodeBodyS3, droid::OtaState::Restarting, 100, droid::OtaError::None, "");
    s3RestartAtMs = (now + 1500) | 1;  // time for the page to hear it
  }
  // Never while the droid can drive: it waits for disarm and the switch off.
  if (s3RestartAtMs && allowed && static_cast<int32_t>(now - s3RestartAtMs) >= 0) ESP.restart();

  uint8_t node;
  uint32_t size;
  if (ota::takeNodeImage(node, size) && node < droid::kMaxNodes) {
    nodeOta[node] = NodeOta{};
    nodeOta[node].waiting = true;
    nodeOta[node].size = size;
    Serial.printf("Sending %s its new firmware\n", droid::nodeName(node));
  }
  for (uint8_t n = 0; n < droid::kMaxNodes; ++n) {
    NodeOta& o = nodeOta[n];
    if (!o.waiting || (o.sentMs && now - o.sentMs < kOtaRetryMs)) continue;
    if (o.tries >= kOtaTries) {
      o.waiting = false;
      uiBroadcastOta(n, droid::OtaState::Failed, 0, droid::OtaError::Timeout, "the board didn't answer");
      Serial.printf("%s didn't answer the update\n", droid::nodeName(n));
      continue;
    }
    sendOtaStart(n, now);
  }

  otaConfirmIfHealthy(now > kOtaConfirmMs && webUi.running());
}

void stopAll(uint32_t now) {
  animPlayer.stop();
  customPlayer.stop();
  runServoSequence(droid::kSequenceAllClosed, now);
  // A direct close also stops any arm sequence.
  sendBodyC3('A', "0c");
  sendBodyC3('A', "1c");
}

void handleUiMessage(int fd, const uint8_t* data, size_t len, uint32_t now, bool link, bool armed) {
  droid::UiReader r(data, len);
  const droid::UiType type = static_cast<droid::UiType>(r.u8());
  UiClient* c = uiClient(fd, type == droid::UiType::Hello);
  using T = droid::UiType;
  using R = droid::UiResult;
  // Library and settings changes, and live moves, lock while armed.
  const bool locked = type == T::SetSetting || type == T::ClearSetting || type == T::DeleteAnimation ||
                      type == T::ResetAnimations || type == T::AnimCommit || type == T::SetWifi ||
                      type == T::ServoMoves || type == T::SeqCommit || type == T::DeleteSequence ||
                      type == T::CopySequence;
  if (locked && armed) {
    uiReply(fd, type, R::RefusedArmed);
    return;
  }
  switch (type) {
    case T::Hello:
      if (c) {
        c->stage = 1;
        c->cursor = 0;
        c->onlyOne = false;
      }
      break;
    case T::ListSettings:
    case T::ListAnimations:
      if (c) {
        c->stage = type == T::ListSettings ? 2 : 3;
        c->cursor = 0;
        c->onlyOne = true;
      }
      break;
    case T::SetSetting:
    case T::ClearSetting: {
      const uint8_t index = r.u8();
      const int32_t value = type == T::SetSetting ? r.i32() : 0;
      if (!r.done() || index >= settings.count()) {
        uiReply(fd, type, R::Bad);
        break;
      }
      const droid::SettingResult res = type == T::SetSetting ? settings.set(index, value) : settings.clear(index);
      if (res == droid::SettingResult::OutOfRange) {
        uiReply(fd, type, R::Bad);
      } else {
        applySetting(index);
        uiBroadcastSetting(index);
      }
      break;
    }
    case T::PlaySound: {
      const uint16_t file = r.u16();
      if (r.done()) playWavNumber(file);
      break;
    }
    case T::PlayRandom: {
      const uint16_t a = r.u16();
      const uint16_t b = r.u16();
      if (r.done()) playRandomRange(a, b);
      break;
    }
    case T::Emotion: {
      const uint8_t e = r.u8();
      if (r.done() && e <= 4) stimulate(e);
      break;
    }
    case T::DomeHome:
    case T::DomeGoTo:
    case T::DomeStop: {
      const int16_t deg = type == T::DomeGoTo ? r.i16() : 0;
      if (!link) {
        uiReply(fd, type, R::NoLink);
        break;
      }
      if (type == T::DomeHome) dome.requestHome();
      if (type == T::DomeGoTo) dome.requestGoTo(deg, body::kUiDomeSpeed);
      if (type == T::DomeStop) dome.requestStop();
      break;
    }
    case T::RunServoSequence: {
      const uint8_t index = r.u8();
      const uint8_t from = r.left() ? r.u8() : 0;
      if (r.done() && (index < droid::allSequenceCount() || customSeqs.find(index))) {
        runServoSequence(index, now, from);
      } else {
        uiReply(fd, type, R::Unknown);
      }
      break;
    }
    case T::RunAnimation: {
      const uint16_t id = r.u16();
      const droid::Animation* a = animations.find(id);
      if (a) {
        animPlayer.start(*a, now);
      } else {
        uiReply(fd, type, R::Unknown);
      }
      break;
    }
    case T::SetWifi: {
      char name[droid::kWifiNameMax + 1], pass[droid::kWifiPasswordMax + 1];
      if (!droid::uiParseSetWifi(data, len, name, pass) || !setWifi(name, pass, now)) {
        uiReply(fd, type, R::Bad);
        break;
      }
      uiReply(fd, type, R::Ok);
      uiBroadcastWifi();
      Serial.printf("Wi-Fi changed from the page: \"%s\", restarting the access point\n", apSsid);
      break;
    }
    case T::StopAll:
      stopAll(now);
      break;
    case T::DeleteAnimation: {
      const uint16_t id = r.u16();
      if (animations.remove(id)) {
        animations.save();
        uiResendAnimations();
      } else {
        uiReply(fd, type, R::Unknown);
      }
      break;
    }
    case T::ResetAnimations:
      animations.reset();
      animations.save();
      uiResendAnimations();
      break;
    case T::ServoMoves: {
      const uint8_t count = r.u8();
      if (count == 0 || count > droid::kSeqServos + droid::kServoHoloCount || r.left() != count * 3u) {
        uiReply(fd, type, R::Bad);
        break;
      }
      customPlayer.stop();
      for (uint8_t i = 0; i < count; ++i) {
        const uint8_t id = r.u8();
        const uint16_t us = r.u16();
        moveServo(id, us);
      }
      break;
    }
    case T::SeqHeader:
      if (!c || !c->seqAssembler.header(data, len)) uiReply(fd, type, R::Bad);
      break;
    case T::SeqFrame:
      if (!c || !c->seqAssembler.frame(data, len)) uiReply(fd, type, R::Bad);
      break;
    case T::SeqCommit: {
      const uint8_t id = r.u8();
      if (!c || !c->seqAssembler.complete(id)) {
        uiReply(fd, type, R::Bad);
        break;
      }
      const droid::CustomSequence& q = c->seqAssembler.sequence();
      const droid::CustomSequence* same = customSeqs.findName(q.name);
      if ((same && same->id != q.id) || droid::servoSequenceIndex(q.name) >= 0) {  // names must be unique
        uiReply(fd, type, R::Bad);
        break;
      }
      if (customSeqs.put(q) == 0) {
        uiReply(fd, type, R::Full);
        break;
      }
      customSeqs.save();
      uiReply(fd, type, R::Ok);
      uiResendSequences();
      break;
    }
    case T::DeleteSequence: {
      const uint8_t id = r.u8();
      if (!customSeqs.remove(id)) {
        uiReply(fd, type, R::Unknown);
        break;
      }
      if (customPlayer.id() == id) customPlayer.stop();
      customSeqs.save();
      uiReply(fd, type, R::Ok);
      uiResendSequences();
      break;
    }
    case T::CopySequence: {
      const uint8_t index = r.u8();
      droid::CustomSequence q;
      const droid::CustomSequence* src = customSeqs.find(index);
      if (src) {
        q = *src;
      } else if (!droid::customFromBuiltIn(index, q)) {
        uiReply(fd, type, R::Unknown);
        break;
      }
      // "<name> copy", then "<name> copy 2" and on, until it's unused.
      char base[droid::kSeqNameMax + 1];
      snprintf(base, sizeof(base), "%.18s copy", q.name);
      strcpy(q.name, base);
      for (int k = 2; (customSeqs.findName(q.name) || droid::servoSequenceIndex(q.name) >= 0) && k < 100; ++k) {
        snprintf(q.name, sizeof(q.name), "%.18s %d", base, k);
      }
      q.id = 0;
      if (customSeqs.put(q) == 0) {
        uiReply(fd, type, R::Full);
        break;
      }
      customSeqs.save();
      uiReply(fd, type, R::Ok);
      uiResendSequences();
      break;
    }
    case T::AnimHeader:
      if (!c || !c->assembler.header(data, len)) uiReply(fd, type, R::Bad);
      break;
    case T::AnimStep:
      if (!c || !c->assembler.step(data, len)) uiReply(fd, type, R::Bad);
      break;
    case T::AnimCommit: {
      const uint16_t id = r.u16();
      if (!c || !c->assembler.complete(id)) {
        uiReply(fd, type, R::Bad);
        break;
      }
      if (animations.put(c->assembler.animation()) == 0) {
        uiReply(fd, type, R::Full);
        break;
      }
      animations.save();
      uiReply(fd, type, R::Ok);
      uiResendAnimations();
      break;
    }
    default:
      uiReply(fd, type, R::Unknown);
      break;
  }
}

void uiBroadcastStatus(uint32_t now, bool link, bool armed) {
  droid::UiStatus s;
  if (link) s.flags |= droid::kStatusRadio;
  if (armed) s.flags |= droid::kStatusArmed;
  if (in.enable) s.flags |= droid::kStatusEnable;
  if (radio.peerUp(droid::kNodeDomeC3, now)) s.flags |= droid::kStatusDomeNode;
  if (radio.peerUp(droid::kNodeBodyC3, now)) s.flags |= droid::kStatusBodyC3;
  if (animPlayer.running()) s.flags |= droid::kStatusAnimating;
  s.domeMode = in.domeMode;
  s.domeAngle = rdh.haveReport() ? static_cast<int16_t>(rdh.report().position) : INT16_MIN;
  s.animationId = animPlayer.currentId();
  if (servoSequenceRunning >= 0 && static_cast<int32_t>(now - servoSequenceEndsMs) >= 0) servoSequenceRunning = -1;
  s.servoSequence = servoSequenceRunning;
  s.uptimeS = now / 1000;
  uint8_t buf[32];
  webUi.broadcast(buf, droid::uiStatus(buf, sizeof(buf), s));
}

void updateUi(uint32_t now, bool link, bool armed) {
  WebUi::Event e;
  for (size_t i = 0; i < body::kUiEventsPerLoop && webUi.poll(e); ++i) {
    if (e.kind == WebUi::Event::Kind::Message) {
      handleUiMessage(e.fd, e.data, e.length, now, link, armed);
    } else if (e.kind == WebUi::Event::Kind::Disconnected) {
      if (UiClient* c = uiClient(e.fd, false)) c->fd = -1;
    }
  }
  for (UiClient& c : uiClients) {
    if (c.fd >= 0 && c.stage != 0) uiSyncStep(c);
  }
  if (now - lastStatusMs >= body::kUiStatusMs) {
    lastStatusMs = now;
    if (webUi.clientCount() > 0) uiBroadcastStatus(now, link, armed);
  }

  while (const droid::AnimStep* s = animPlayer.next(now)) playStep(*s, now);
  updateCustomSequence(now);

  // Nodes that have just come up get their servo endpoints.
  const bool domeUp = radio.peerUp(droid::kNodeDomeC3, now);
  const bool bodyUp = radio.peerUp(droid::kNodeBodyC3, now);
  if (domeUp && !domeNodeWasUp) syncDomeServos();
  if (bodyUp && !bodyC3WasUp) {
    syncArmServos();
    syncPixels();
  }
  domeNodeWasUp = domeUp;
  bodyC3WasUp = bodyUp;
}

// Waits for the Teensy, sends the startup settings, then turns drive state
// into sounds.
void updateSound(uint32_t now, bool link, bool armed) {
  if (!hcrStarted) {
    if (now < body::kHcrStartDelayMs) return;
    sendHcrStartup();
    hcrStarted = true;
  }

  const bool autoChatter = in.domeMode == static_cast<uint8_t>(droid::DomeMode::Auto) &&
                           settings.get(body::kSetHcrAutoOn) != 0;
  const droid::HcrTriggers t = hcrEvents.update(now, link, armed, in.sound, autoChatter);
  if (t.connected) playWav(body::kSetHcrWavConnected);
  if (t.armed) playWav(body::kSetHcrWavArmed);
  if (t.disarmed) playWav(body::kSetHcrWavDisarmed);
  if (t.soundPress || t.autoSound) playRandomWav();
}

void printSetting(size_t index) {
  const droid::SettingDef& d = settings.def(index);
  Serial.printf("%-15s %6ld  %s (default %ld, range %ld to %ld)\n", d.key, (long)settings.get(index),
                settings.overridden(index) ? "saved  " : "default", (long)d.defaultValue, (long)d.min,
                (long)d.max);
}

void reportSettingResult(droid::SettingResult result, size_t index) {
  if (result == droid::SettingResult::OutOfRange) {
    Serial.println("out of range, not changed");
    printSetting(index);
    return;
  }
  applySetting(index);
  if (result == droid::SettingResult::NotSaved) {
    Serial.println("changed for now, but not saved: it will be lost at the next boot");
  }
  printSetting(index);
}

void runSettingLine(const char* line) {
  if (strncmp(line, "wifi", 4) == 0) {  // :wifi shows it, :wifi default goes back to the built-in pair
    if (strstr(line, "default")) {
      setWifi("", "", millis());
      Serial.println("Wi-Fi back to the built-in name and password; restarting the access point");
    } else {
      printBanner();
    }
    return;
  }
  const droid::SettingCommand c = droid::parseSettingCommand(line);
  if (c.kind == droid::SettingCommand::Kind::List) {
    for (size_t i = 0; i < settings.count(); ++i) printSetting(i);
    return;
  }
  const int index = c.kind == droid::SettingCommand::Kind::Bad ? -1 : settings.find(c.key);
  if (index < 0) {
    Serial.println("usage: ':' lists, ':key' shows, ':key 60' sets, ':key default' clears");
    return;
  }
  if (c.kind == droid::SettingCommand::Kind::Set) {
    reportSettingResult(settings.set(index, c.value), index);
  } else if (c.kind == droid::SettingCommand::Kind::Clear) {
    reportSettingResult(settings.clear(index), index);
  } else {
    printSetting(index);
  }
}

void runDomeLine(const char* line, size_t length) {
  if (length == 0) {
    Serial.println("usage: >:DPH sends :DPH to the Roam-a-Dome");
    return;
  }
  const bool sent = rdh.send(line, length);
  Serial.printf("dome %s: %.*s\n", sent ? "sent" : "dropped", (int)length, line);
  rdhEchoUntilMs = millis() + kHcrEchoMs;
}

// Collects one character of a typed line. A <...> command ends at '>', and a
// ':' or '>' line at the line ending. An overlong line is abandoned.
void collectConsoleLine(char c) {
  const char first = consoleLineLen > 0 ? consoleLine[0] : c;
  const bool settingLine = first == ':';
  const bool domeLine = first == '>';
  if (c == '\r' || c == '\n') {
    consoleLine[consoleLineLen] = '\0';
    if (settingLine && consoleLineLen > 0) runSettingLine(consoleLine + 1);
    if (domeLine && consoleLineLen > 0) runDomeLine(consoleLine + 1, consoleLineLen - 1);
    consoleLineLen = 0;
    return;
  }
  if (consoleLineLen >= kConsoleLineBytes - 1) {
    consoleLineLen = 0;
    return;
  }
  consoleLine[consoleLineLen++] = c;
  if (settingLine || domeLine || c != '>') return;
  const bool sent = hcr.send(consoleLine, consoleLineLen);
  Serial.printf("hcr %s: %.*s\n", sent ? "sent" : "dropped", (int)consoleLineLen, consoleLine);
  hcrEchoUntilMs = millis() + kHcrEchoMs;
  consoleLineLen = 0;
}

void handleConsole() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (consoleLineLen == 0 && linkConsole.feed(c, Serial, millis())) continue;
    if (consoleLineLen > 0 || c == '<' || c == ':' || c == '>') {
      collectConsoleLine(c);
    } else if (c == 's' || c == 'S') {
      playRandomWav();
      Serial.println("hcr: random sound");
    } else if (c == 'j' || c == 'J' || c == 'l' || c == 'L') {
      const bool left = (c == 'j' || c == 'J');
      dome.setSpin(left ? -body::kDomeTestSpin : body::kDomeTestSpin, millis(), body::kDomeTestSpinMs);
      Serial.printf("dome: spin %s for %lu ms (manual mode, radio on)\n", left ? "-" : "+",
                    (unsigned long)body::kDomeTestSpinMs);
    } else if (c == 'r' || c == 'R') {
      monitorOn = !monitorOn;
      Serial.printf("radio monitor %s\n", monitorOn ? "on" : "off");
    } else if (c == 'v' || c == 'V') {
      printBanner();
      for (uint8_t node = droid::kNodeBodyS3; node <= droid::kNamedNodes; ++node) {
        if (nodeVersion[node][0]) Serial.printf("%-8s v%s (%s)\n", droid::nodeName(node), nodeVersion[node], nodeBuild[node]);
        else Serial.printf("%-8s not heard from\n", droid::nodeName(node));
      }
    } else if (c == 'h' || c == '?') {
      Serial.println("v = version, Wi-Fi name and password, and every board's version");
      Serial.println("r = toggle radio/drive monitor (10 Hz)");
      Serial.println("s = play a random sound");
      Serial.println("<...> = send a command to HCR and show the reply, e.g. <QVA>");
      Serial.println(": = list settings. :key 60 sets one, :key default clears it");
      Serial.println(":wifi default = back to the built-in Wi-Fi name and password");
      Serial.println("j / l = spin the dome briefly one way or the other (manual mode, radio on)");
      Serial.println(">... = send a command to the Roam-a-Dome, e.g. >:DPH");
      Serial.println("@ = link status and setup; see README");
    }
  }
}

void printMonitor(uint32_t now, bool link, bool armed) {
  if (!monitorOn || (now - lastPrintMs) < 100) return;
  lastPrintMs = now;
  const droid::SbusFrame& f = sbus.frame();
  Serial.printf(
      "baud %lu good %lu bad %lu flagged %lu | link %d fs %d | raw CH1-6 %4u %4u %4u %4u %4u %4u | "
      "steer %+.2f thr %+.2f spd %u en %d snd %d | ramped thr %+.2f steer %+.2f max %.2f | %s | L %u R %u us | "
      "hcr sent %lu dropped %lu | dome %s %u us, rad %c%d sent %lu dropped %lu | dome-c3 %d bodyc3 %d | "
      "worst loop %lu us\n",
      (unsigned long)sbus.baud(), (unsigned long)sbus.goodFrames(), (unsigned long)sbus.badFrames(),
      (unsigned long)sbus.flaggedFrames(), link, sbus.lastFailsafe(), f.ch[0], f.ch[1], f.ch[2], f.ch[3],
      f.ch[4], f.ch[5], in.steer, in.throttle, in.speedLevel + 1, in.enable, in.sound, ramp.throttle(),
      ramp.steer(), ramp.speedScale(), armed ? "ARMED" : (arming.waitingForSwitchOff() ? "turn enable OFF first" : "disarmed"),
      feet.leftUs(), feet.rightUs(), (unsigned long)hcr.sentCommands(),
      (unsigned long)hcr.droppedCommands(), droid::domeModeName(dome.activeMode()), domePulse.us(),
      rdh.haveReport() ? rdh.report().mode : '?', rdh.haveReport() ? rdh.report().position : 0,
      (unsigned long)rdh.sentCommands(), (unsigned long)rdh.droppedCommands(),
      radio.peerUp(droid::kNodeDomeC3, now),
      radio.peerUp(droid::kNodeBodyC3, now), (unsigned long)worstLoopUs);
  worstLoopUs = 0;
}

}  // namespace

void setup() {
  // Feet and dome to neutral before anything else.
  feet.begin(kNeutralUs);
  domePulse.begin(dome.neutralUs());

  Serial.begin(115200);
  const bool nvsOk = nvs.begin();
  settings.load();
  hcrEvents.setSoundGapMs(static_cast<uint32_t>(settings.get(body::kSetHcrGapMs)));
  applyHcrAutoDelay();
  hcrEvents.seed(esp_random());

  dome.setConfig(body::domeConfig(settings));
  dome.seed(esp_random());

  sbus.begin();
  hcr.begin();
  rdh.begin();
  // The access point first, fully set up, then the radio link joins it on
  // the same channel.
  loadWifi();
  WiFi.onEvent(onWifiEvent);
  startAccessPoint();
  const bool radioOk = radio.begin();
  radio.onMessage(onRadioMessage, nullptr);

  const bool fsOk = animations.begin();
  customSeqs.begin();
  const bool webOk = webUi.begin();

  // If loop() stalls for kWatchdogSeconds the chip resets and the PWM stops.
  esp_task_wdt_init(body::kWatchdogSeconds, true);
  esp_task_wdt_add(nullptr);

  strcpy(nodeVersion[droid::kNodeBodyS3], droid::kFirmwareVersion);
  strcpy(nodeBuild[droid::kNodeBodyS3], droid::kFirmwareBuild);
  printBanner();
  Serial.println("Type 'h' for help, 'v' for this again and every board's version.");
  if (!nvsOk) Serial.println("NVS unavailable: using default settings, changes won't be saved");
  if (!radioOk) Serial.println("ESP-NOW link off. Type @ for status.");
  if (apOk && !webOk) Serial.println("Web server didn't start: no control page");
  if (!fsOk) Serial.println("LittleFS unavailable: animation edits won't be saved");
}

void loop() {
  esp_task_wdt_reset();
  handleConsole();
  sbus.poll();

  const uint32_t now = millis();
  const uint32_t nowUs = micros();
  if (wifiRestartAtMs && static_cast<int32_t>(now - wifiRestartAtMs) >= 0) {
    wifiRestartAtMs = 0;
    startAccessPoint();
    printBanner();
  }
  const float dtS = lastLoopUs == 0 ? 0.0f : (nowUs - lastLoopUs) * 1.0e-6f;
  if (lastLoopUs != 0 && nowUs - lastLoopUs > worstLoopUs) worstLoopUs = nowUs - lastLoopUs;
  lastLoopUs = nowUs;
  const bool link = sbus.linkOk(now);

  if (link) {
    readInputs(sbus.frame());
  } else {
    in = Inputs{};
  }

  const bool centred = (in.throttle == 0.0f && in.steer == 0.0f);
  const bool armed = arming.update(now, link, in.enable, centred);

  const float scale = body::kSpeedLevels[in.speedLevel];
  if (armed) {
    ramp.update(in.throttle, in.steer, scale, dtS);
    const droid::FeetSpeed s = droid::mixDrive(ramp.throttle(), ramp.steer(), ramp.speedScale(), kMixer);
    feet.writeUs(droid::speedToPulseUs(s.left, kPulses), droid::speedToPulseUs(s.right, kPulses));
  } else {
    // Safety stops skip the ramp: neutral now, ramp state cleared.
    ramp.reset(scale);
    stopFeet();
  }

  // The dome follows the radio link, not drive arming, so it can turn while
  // the feet are disarmed.
  const droid::DomeOutput d = dome.update(now, link, static_cast<droid::DomeMode>(in.domeMode));
  domePulse.writeUs(d.pulseUs);
  if (d.commandLength > 0) rdh.send(d.command, d.commandLength);
  rdh.poll(now, static_cast<int32_t>(rdhEchoUntilMs - now) > 0 ? &Serial : nullptr);

  updateSound(now, link, armed);
  hcr.poll(static_cast<int32_t>(hcrEchoUntilMs - now) > 0 ? &Serial : nullptr);
  radio.poll(now);
  updateUi(now, link, armed);
  updateOta(now, armed);

  printMonitor(now, link, armed);
}
