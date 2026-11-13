#pragma once
// The ESP-NOW link between the droid's nodes: frame format, peer tracking and
// link console commands.
//
// Frame layout, little-endian:
//   0     magic (0xD2)
//   1     protocol version
//   2     source node
//   3     destination node (kNodeAll = every node)
//   4     message type
//   5-6   sequence number
//   7     payload length
//   8...  payload

#include <stddef.h>
#include <stdint.h>

namespace droid {

enum NodeId : uint8_t {
  kNodeNone = 0,
  kNodeBodyS3 = 1,
  kNodeDomeC3 = 2,
  kNodeBodyC3 = 3,
  // 4 and up are free, for example for a touch device.
  kNodeAll = 0xFF,
};

constexpr size_t kMaxNodes = 8;    // highest usable id + 1
constexpr size_t kNamedNodes = 3;  // nodes that exist today

// Name used on the console, or nullptr for an unknown id.
const char* nodeName(uint8_t id);

// Id for a console name, or kNodeNone.
uint8_t nodeFromName(const char* name);

enum class MessageType : uint8_t {
  Heartbeat = 1,
  DomeCommand = 2,     // S3 -> dome C3: one text command (see DomeLink.h)
  ServoEndpoints = 3,  // S3 -> C3s: servo endpoints (see DomeLink.h)
  DomeSpin = 4,        // dome C3 -> S3: left stick dome spin
  NodeStatus = 5,      // C3 -> S3: what the node is doing
  Ui = 6,              // touch display <-> S3: one UiProtocol message
  OtaStart = 7,        // S3 -> C3: update yourself from the S3 (Ota.h)
};

constexpr uint8_t kFrameMagic = 0xD2;
constexpr uint8_t kFrameVersion = 1;
constexpr size_t kFrameHeader = 8;
constexpr size_t kMaxFrame = 250;  // ESP-NOW limit
constexpr size_t kMaxPayload = kMaxFrame - kFrameHeader;

// Fixed on purpose: these decide how fast a dead link is noticed, so they stay
// out of the settings table. Five missed heartbeats count as a lost link.
constexpr uint32_t kHeartbeatMs = 100;
constexpr uint32_t kPeerTimeoutMs = 500;
// A peer that isn't answering (switched off, say) gets a heartbeat this
// often instead. Every unanswered frame is retried many times, so full rate
// to absent boards would keep the radio busy and upset Wi-Fi joins on the
// S3's access point. A board that comes up is heard at once, and full rate
// resumes.
constexpr uint32_t kQuietPeerHeartbeatMs = 1000;

// One channel for ESP-NOW and the S3's access point, which must share it.
constexpr uint8_t kLinkChannel = 1;

struct Frame {
  uint8_t src = kNodeNone;
  uint8_t dst = kNodeNone;
  MessageType type = MessageType::Heartbeat;
  uint16_t seq = 0;
  uint8_t length = 0;
  uint8_t payload[kMaxPayload] = {};
};

// Returns the encoded size, or 0 if the frame doesn't fit in cap.
size_t encodeFrame(const Frame& frame, uint8_t* out, size_t cap);

// False unless the magic, version and length are all right.
bool decodeFrame(const uint8_t* data, size_t length, Frame& frame);

// Tracks one peer from the frames it sends.
class PeerLink {
 public:
  explicit PeerLink(uint32_t timeoutMs = kPeerTimeoutMs) : timeoutMs_(timeoutMs) {}

  void onFrame(uint32_t nowMs, uint16_t seq);

  bool alive(uint32_t nowMs) const { return seen_ && (nowMs - lastMs_) < timeoutMs_; }
  bool seen() const { return seen_; }
  uint32_t ageMs(uint32_t nowMs) const { return nowMs - lastMs_; }

  uint32_t received() const { return received_; }
  uint32_t missed() const { return missed_; }      // frames skipped while up
  uint32_t dropouts() const { return dropouts_; }  // times it went down and came back

 private:
  uint32_t timeoutMs_;
  bool seen_ = false;
  uint32_t lastMs_ = 0;
  uint16_t lastSeq_ = 0;
  uint32_t received_ = 0;
  uint32_t missed_ = 0;
  uint32_t dropouts_ = 0;
};

constexpr size_t kMacBytes = 6;
constexpr size_t kLinkKeyBytes = 16;

// What each node stores to join the link. The key is never in source code,
// because the repo is public.
struct LinkConfig {
  uint8_t version = 1;
  bool keySet = false;
  uint8_t key[kLinkKeyBytes] = {};
  bool macSet[kMaxNodes] = {};
  uint8_t mac[kMaxNodes][kMacBytes] = {};
};

// AA:BB:CC:DD:EE:FF, in either case.
bool parseMac(const char* text, uint8_t out[kMacBytes]);

// 32 hex digits.
bool parseKey(const char* text, uint8_t out[kLinkKeyBytes]);

// One console line, without its leading '@'.
//   (empty)              status
//   peer dome-c3 <mac>   set a peer's address
//   key <32 hex>         set the shared key
//   key new              make a new key and show it
//   forget               clear the stored link setup
struct LinkCommand {
  enum class Kind : uint8_t { Status, Peer, Key, NewKey, Forget, Bad };
  Kind kind = Kind::Bad;
  uint8_t node = kNodeNone;
  uint8_t mac[kMacBytes] = {};
  uint8_t key[kLinkKeyBytes] = {};
};

LinkCommand parseLinkCommand(const char* line);

}  // namespace droid
