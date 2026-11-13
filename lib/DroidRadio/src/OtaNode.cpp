#include "OtaNode.h"

#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>

#include "LinkProtocol.h"

extern "C" bool verifyRollbackLater() { return true; }  // C linkage: the core is C

namespace {
constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kReadTimeoutMs = 10000;
constexpr uint32_t kReportMs = 500;
constexpr uint32_t kRestartDelayMs = 1500;  // to tell the S3 first

int8_t confirmState = -1;  // -1 not checked yet, 0 nothing to do, 1 pending

// Back on the link's channel, so ESP-NOW carries on.
void leaveWifi() {
  WiFi.disconnect(false, false);
  esp_wifi_set_channel(droid::kLinkChannel, WIFI_SECOND_CHAN_NONE);
}
}  // namespace

static bool otaPendingConfirm() {
  if (confirmState < 0) {
    esp_ota_img_states_t st;
    const esp_partition_t* running = esp_ota_get_running_partition();
    confirmState = (running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY)
                       ? 1
                       : 0;
  }
  return confirmState == 1;
}

void otaConfirmIfHealthy(bool healthy) {
  if (!healthy || !otaPendingConfirm()) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    Serial.println("Firmware update confirmed: this image is kept");
  }
  confirmState = 0;
}

bool OtaClient::start(uint8_t node, const uint8_t* payload, size_t length) {
  if (running()) return false;
  droid::OtaStart s;
  if (!droid::decodeOtaStart(payload, length, s)) return false;
  node_ = node;
  start_ = s;
  percent_ = 0;
  error_ = droid::OtaError::None;
  state_ = droid::OtaState::Joining;
  reported_ = droid::OtaState::Idle;
  lastReportMs_ = 0;
  if (xTaskCreate(task, "ota", 8192, this, 1, nullptr) != pdPASS) {
    fail(droid::OtaError::WriteFailed);
    return false;
  }
  return true;
}

droid::OtaProgress OtaClient::progress() const {
  droid::OtaProgress p;
  p.state = state_;
  p.percent = percent_;
  p.error = error_;
  return p;
}

void OtaClient::fail(droid::OtaError e) {
  error_ = e;
  state_ = droid::OtaState::Failed;
}

void OtaClient::task(void* self) {
  static_cast<OtaClient*>(self)->run();
  vTaskDelete(nullptr);
}

void OtaClient::run() {
  Serial.printf("Firmware update: joining \"%s\"\n", start_.ssid);
  // The access point is on the link's channel, so ESP-NOW keeps working.
  WiFi.begin(start_.ssid, start_.password);
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > kJoinTimeoutMs) {
      leaveWifi();
      fail(droid::OtaError::NoWifi);
      return;
    }
    delay(100);
  }

  state_ = droid::OtaState::Downloading;
  const IPAddress ip(start_.ip[0], start_.ip[1], start_.ip[2], start_.ip[3]);
  const char* env = droid::otaNodeEnv(node_);
  WiFiClient c;
  if (!env || !c.connect(ip, 80)) {
    leaveWifi();
    fail(droid::OtaError::NoServer);
    return;
  }
  c.printf("GET /fw/%s HTTP/1.0\r\nHost: %s\r\n\r\n", env, ip.toString().c_str());

  // Headers: only the status and Content-Length matter.
  long length = -1;
  bool ok200 = false;
  bool first = true;
  uint32_t last = millis();
  for (;;) {
    if (!c.connected() && !c.available()) break;
    if (!c.available()) {
      if (millis() - last > kReadTimeoutMs) break;
      delay(5);
      continue;
    }
    String line = c.readStringUntil('\n');
    last = millis();
    line.trim();
    if (first) {
      ok200 = line.startsWith("HTTP/1.") && line.indexOf(" 200") > 0;
      first = false;
    } else if (line.length() == 0) {
      break;  // end of headers
    } else if (line.substring(0, 15).equalsIgnoreCase("Content-Length:")) {
      length = line.substring(15).toInt();
    }
  }
  if (!ok200 || length <= 0 || static_cast<uint32_t>(length) != start_.size) {
    c.stop();
    leaveWifi();
    fail(droid::OtaError::NoServer);
    return;
  }
  if (!Update.begin(length, U_FLASH)) {
    c.stop();
    leaveWifi();
    fail(droid::OtaError::WriteFailed);
    return;
  }

  droid::FirmwareTagScanner scanner;
  uint8_t buf[1024];
  long done = 0;
  last = millis();
  while (done < length) {
    const int avail = c.available();
    if (avail <= 0) {
      if (!c.connected() || millis() - last > kReadTimeoutMs) break;
      delay(2);
      continue;
    }
    const int want = avail < static_cast<int>(sizeof(buf)) ? avail : static_cast<int>(sizeof(buf));
    const int got = c.read(buf, want);
    if (got <= 0) continue;
    last = millis();
    scanner.feed(buf, got);
    if (Update.write(buf, got) != static_cast<size_t>(got)) break;
    done += got;
    percent_ = static_cast<uint8_t>(done * 100 / length);
  }
  c.stop();
  leaveWifi();

  if (done != length) {
    Update.abort();
    fail(millis() - last > kReadTimeoutMs ? droid::OtaError::Timeout : droid::OtaError::WriteFailed);
    return;
  }
  // Checked again here, before the image can boot.
  if (!scanner.found() || strcmp(scanner.tag().node, env) != 0) {
    Update.abort();
    fail(droid::OtaError::WrongBoard);
    return;
  }
  if (!Update.end(true)) {  // also checks the image itself
    fail(droid::OtaError::WriteFailed);
    return;
  }
  Serial.printf("Firmware update: v%s written, restarting\n", scanner.tag().version);
  percent_ = 100;
  state_ = droid::OtaState::Restarting;
}

void OtaClient::loop(uint32_t nowMs, Send send) {
  const droid::OtaState st = state_;
  if (st == droid::OtaState::Idle) return;
  const bool changed = st != reported_;
  if (changed || nowMs - lastReportMs_ >= kReportMs) {
    uint8_t buf[8];
    const size_t n = droid::encodeOtaProgress(buf, sizeof(buf), progress());
    if (n && send(buf, n)) {
      reported_ = st;
      lastReportMs_ = nowMs;
    }
    if (changed && st == droid::OtaState::Failed) {
      Serial.printf("Firmware update failed: %s\n", droid::otaErrorText(error_));
    }
  }
  if (st == droid::OtaState::Restarting) {
    if (restartAtMs_ == 0) restartAtMs_ = (nowMs + kRestartDelayMs) | 1;
    if (static_cast<int32_t>(nowMs - restartAtMs_) >= 0) ESP.restart();
  }
  // Report a failure for 5 s, then accept a new OtaStart.
  if (st == droid::OtaState::Failed) {
    if (failedAtMs_ == 0) failedAtMs_ = nowMs | 1;
    if (nowMs - failedAtMs_ > 5000) {
      state_ = droid::OtaState::Idle;
      failedAtMs_ = 0;
    }
  }
}
