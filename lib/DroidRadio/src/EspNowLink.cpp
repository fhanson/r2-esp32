#include "EspNowLink.h"

#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <string.h>

using droid::Frame;
using droid::kMacBytes;
using droid::kMaxNodes;

EspNowLink* EspNowLink::instance_ = nullptr;

namespace {

constexpr char kNamespace[] = "droidlink";
constexpr char kConfigKey[] = "config";
constexpr int kRxQueueDepth = 8;
constexpr int kMaxFramesPerPoll = 8;

void printMac(Print& out, const uint8_t* mac) {
  out.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace

void EspNowLink::loadConfig() {
  prefsOpen_ = prefs_.begin(kNamespace, false);
  if (!prefsOpen_ || prefs_.getBytesLength(kConfigKey) != sizeof(config_)) return;
  droid::LinkConfig stored;
  prefs_.getBytes(kConfigKey, &stored, sizeof(stored));
  if (stored.version == config_.version) config_ = stored;
}

bool EspNowLink::saveConfig() {
  return prefsOpen_ && prefs_.putBytes(kConfigKey, &config_, sizeof(config_)) == sizeof(config_);
}

bool EspNowLink::forgetConfig() {
  config_ = droid::LinkConfig{};
  return prefsOpen_ && prefs_.remove(kConfigKey);
}

bool EspNowLink::begin() {
  loadConfig();

  // With no setup stored, the radio stays off.
  if (!config_.keySet) return false;

  WiFi.persistent(false);
  // STA for ESP-NOW, keeping an access point already running (the S3 starts
  // its own first).
  WiFi.mode(static_cast<wifi_mode_t>(WiFi.getMode() | WIFI_MODE_STA));

  // Modem sleep would make this node miss frames.
  WiFi.setSleep(false);
  if (esp_wifi_set_channel(droid::kLinkChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;

  rxQueue_ = xQueueCreate(kRxQueueDepth, sizeof(RxFrame));
  if (rxQueue_ == nullptr || esp_now_init() != ESP_OK) return false;
  esp_now_set_pmk(config_.key);

  instance_ = this;
  if (esp_now_register_recv_cb(onReceive) != ESP_OK) return false;
  if (esp_now_register_send_cb(onSent) != ESP_OK) return false;
  if (!addPeers()) return false;

  running_ = true;
  return true;
}

bool EspNowLink::addPeers() {
  for (size_t id = 1; id < kMaxNodes; ++id) {
    if (id == self_ || !config_.macSet[id]) continue;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, config_.mac[id], kMacBytes);
    memcpy(peer.lmk, config_.key, droid::kLinkKeyBytes);
    peer.channel = droid::kLinkChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = true;
    if (esp_now_add_peer(&peer) != ESP_OK) return false;
  }
  return true;
}

// These two run in the WiFi task, so they only copy and count.
void EspNowLink::onReceive(const uint8_t* mac, const uint8_t* data, int length) {
  EspNowLink* link = instance_;
  if (link == nullptr || length <= 0 || length > static_cast<int>(droid::kMaxFrame)) return;
  RxFrame rx;
  memcpy(rx.mac, mac, kMacBytes);
  rx.length = static_cast<uint8_t>(length);
  memcpy(rx.data, data, length);
  if (xQueueSend(link->rxQueue_, &rx, 0) != pdPASS) link->rxDropped_ = link->rxDropped_ + 1;
}

void EspNowLink::onSent(const uint8_t*, esp_now_send_status_t status) {
  EspNowLink* link = instance_;
  if (link != nullptr && status != ESP_NOW_SEND_SUCCESS) {
    link->notDelivered_ = link->notDelivered_ + 1;
  }
}

void EspNowLink::handleFrame(const RxFrame& rx, uint32_t nowMs) {
  Frame frame;
  const bool valid = droid::decodeFrame(rx.data, rx.length, frame) && frame.src > 0 &&
                     frame.src < kMaxNodes && frame.src != self_ && config_.macSet[frame.src] &&
                     memcmp(rx.mac, config_.mac[frame.src], kMacBytes) == 0 &&
                     (frame.dst == self_ || frame.dst == droid::kNodeAll);
  if (!valid) {
    ++rejected_;
    return;
  }
  peers_[frame.src].onFrame(nowMs, frame.seq);
  if (frame.type != droid::MessageType::Heartbeat && handler_ != nullptr) {
    handler_(handlerContext_, frame, nowMs);
  }
}

bool EspNowLink::send(droid::NodeId dst, droid::MessageType type, const uint8_t* payload, size_t length) {
  if (!running_ || dst == self_ || dst >= kMaxNodes || !config_.macSet[dst] || length > droid::kMaxPayload) {
    return false;
  }
  Frame frame;
  frame.src = self_;
  frame.dst = dst;
  frame.type = type;
  frame.seq = nextSeq_;
  frame.length = static_cast<uint8_t>(length);
  if (length) memcpy(frame.payload, payload, length);
  uint8_t data[droid::kMaxFrame];
  const size_t size = droid::encodeFrame(frame, data, sizeof(data));
  if (size == 0) return false;
  if (esp_now_send(config_.mac[dst], data, size) != ESP_OK) {
    ++sendErrors_;
    return false;
  }
  ++sent_;
  // Shared with the heartbeat, so other peers see a gap in the numbers. That
  // only shows in the "missed" count, which is a statistic.
  ++nextSeq_;
  return true;
}

void EspNowLink::sendHeartbeat(uint32_t nowMs) {
  Frame frame;
  frame.src = self_;
  frame.type = droid::MessageType::Heartbeat;
  uint8_t data[droid::kFrameHeader];

  // Encrypted peers can't take a broadcast, so each one gets its own copy.
  for (size_t id = 1; id < kMaxNodes; ++id) {
    if (id == self_ || !config_.macSet[id]) continue;
    if (!peers_[id].alive(nowMs)) {  // not answering: slow down (kQuietPeerHeartbeatMs)
      if (lastQuietBeatMs_[id] != 0 && nowMs - lastQuietBeatMs_[id] < droid::kQuietPeerHeartbeatMs) continue;
      lastQuietBeatMs_[id] = nowMs ? nowMs : 1;
    }
    frame.dst = static_cast<uint8_t>(id);
    frame.seq = nextSeq_;
    const size_t size = droid::encodeFrame(frame, data, sizeof(data));
    if (esp_now_send(config_.mac[id], data, size) == ESP_OK) {
      ++sent_;
    } else {
      ++sendErrors_;
    }
  }
  ++nextSeq_;
}

void EspNowLink::poll(uint32_t nowMs) {
  if (!running_) return;

  RxFrame rx;
  for (int i = 0; i < kMaxFramesPerPoll && xQueueReceive(rxQueue_, &rx, 0) == pdPASS; ++i) {
    handleFrame(rx, nowMs);
  }

  if (nowMs - lastHeartbeatMs_ >= droid::kHeartbeatMs) {
    lastHeartbeatMs_ = nowMs;
    sendHeartbeat(nowMs);
  }
}

void EspNowLink::printStatus(Print& out, uint32_t nowMs) const {
  // Read from eFuse, so this works with the radio off.
  uint8_t mac[kMacBytes] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  out.printf("this node: %s  ", droid::nodeName(self_));
  printMac(out, mac);
  out.printf("  channel %u\n", droid::kLinkChannel);
  out.printf("key: %s  link: %s\n", config_.keySet ? "set" : "NOT SET",
             running_ ? "running" : "off");

  for (uint8_t id = 1; id <= droid::kNamedNodes; ++id) {
    if (id == self_) continue;
    out.printf("%-8s ", droid::nodeName(id));
    if (!config_.macSet[id]) {
      out.println("no address set");
      continue;
    }
    printMac(out, config_.mac[id]);
    const droid::PeerLink& p = peers_[id];
    if (!p.seen()) {
      out.println("  never seen");
      continue;
    }
    out.printf("  %s  last %lu ms ago, received %lu, missed %lu, dropouts %lu\n",
               p.alive(nowMs) ? "UP" : "DOWN", (unsigned long)p.ageMs(nowMs),
               (unsigned long)p.received(), (unsigned long)p.missed(), (unsigned long)p.dropouts());
  }

  out.printf("sent %lu, send errors %lu, not delivered %lu, rx dropped %lu, rejected %lu\n",
             (unsigned long)sent_, (unsigned long)sendErrors_, (unsigned long)notDelivered_,
             (unsigned long)rxDropped_, (unsigned long)rejected_);
}
