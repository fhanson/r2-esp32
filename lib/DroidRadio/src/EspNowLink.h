#pragma once
// ESP-NOW between the droid's nodes, shared by all three firmwares.
//
// Peers are registered once at boot, encrypted, from the setup stored in NVS.
// Without a key the link stays off. Nothing here can hold up the caller:
// frames arrive through a queue, and a send that can't go now is counted.

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_now.h>

#include "LinkProtocol.h"

class EspNowLink {
 public:
  explicit EspNowLink(droid::NodeId self) : self_(self) {}

  // Call once from setup(). Returns true if the link is running; false means
  // it isn't set up or failed to start, and the rest of the firmware carries
  // on without it.
  bool begin();

  // Call often (every loop). Takes in frames and sends the heartbeat.
  void poll(uint32_t nowMs);

  bool running() const { return running_; }

  // Sends one message to a peer. False if the link isn't running, the peer
  // isn't set up, the payload is too big, or ESP-NOW refused it right now.
  bool send(droid::NodeId dst, droid::MessageType type, const uint8_t* payload, size_t length);

  // Called from poll() for every valid frame other than a heartbeat.
  using Handler = void (*)(void* context, const droid::Frame& frame, uint32_t nowMs);
  void onMessage(Handler handler, void* context) {
    handler_ = handler;
    handlerContext_ = context;
  }
  bool peerUp(droid::NodeId node, uint32_t nowMs) const { return peers_[node].alive(nowMs); }

  // Prints to a console a person is at; see the note in HcrLink.h about USB.
  void printStatus(Print& out, uint32_t nowMs) const;

  // Changes to the stored setup. They take effect at the next boot.
  droid::LinkConfig& config() { return config_; }
  bool saveConfig();
  bool forgetConfig();

 private:
  struct RxFrame {
    uint8_t mac[droid::kMacBytes];
    uint8_t length;
    uint8_t data[droid::kMaxFrame];
  };

  static void onReceive(const uint8_t* mac, const uint8_t* data, int length);
  static void onSent(const uint8_t* mac, esp_now_send_status_t status);

  void loadConfig();
  bool addPeers();
  void handleFrame(const RxFrame& rx, uint32_t nowMs);
  void sendHeartbeat(uint32_t nowMs);

  static EspNowLink* instance_;

  Handler handler_ = nullptr;
  void* handlerContext_ = nullptr;

  droid::NodeId self_;
  droid::LinkConfig config_;
  droid::PeerLink peers_[droid::kMaxNodes];
  Preferences prefs_;
  bool prefsOpen_ = false;
  bool running_ = false;
  QueueHandle_t rxQueue_ = nullptr;
  uint32_t lastHeartbeatMs_ = 0;
  uint32_t lastQuietBeatMs_[droid::kMaxNodes] = {};  // last heartbeat to a peer that wasn't answering
  uint16_t nextSeq_ = 0;

  uint32_t sent_ = 0;
  uint32_t sendErrors_ = 0;           // esp_now_send refused the frame
  volatile uint32_t notDelivered_ = 0;  // sent, but the peer didn't acknowledge
  volatile uint32_t rxDropped_ = 0;     // queue full
  uint32_t rejected_ = 0;             // bad frame, or not from the node it claims
};
