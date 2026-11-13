#include "LinkProtocol.h"

#include <string.h>

namespace droid {

namespace {

constexpr const char* kNames[kNamedNodes] = {"body-s3", "dome-c3", "body-c3"};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool hexByte(const char* text, uint8_t& out) {
  const int high = hexValue(text[0]);
  if (high < 0) return false;
  const int low = hexValue(text[1]);
  if (low < 0) return false;
  out = static_cast<uint8_t>(high * 16 + low);
  return true;
}

// Copies the next space-separated word and returns what follows it, or
// nullptr if the word doesn't fit.
const char* nextWord(const char* text, char* word, size_t cap) {
  while (*text == ' ') ++text;
  size_t length = 0;
  while (text[length] != '\0' && text[length] != ' ') ++length;
  if (length >= cap) return nullptr;
  memcpy(word, text, length);
  word[length] = '\0';
  return text + length;
}

}  // namespace

const char* nodeName(uint8_t id) {
  if (id == kNodeNone || id > kNamedNodes) return nullptr;
  return kNames[id - 1];
}

uint8_t nodeFromName(const char* name) {
  for (size_t i = 0; i < kNamedNodes; ++i) {
    if (strcmp(kNames[i], name) == 0) return static_cast<uint8_t>(i + 1);
  }
  return kNodeNone;
}

size_t encodeFrame(const Frame& f, uint8_t* out, size_t cap) {
  const size_t size = kFrameHeader + f.length;
  if (f.length > kMaxPayload || size > cap) return 0;
  out[0] = kFrameMagic;
  out[1] = kFrameVersion;
  out[2] = f.src;
  out[3] = f.dst;
  out[4] = static_cast<uint8_t>(f.type);
  out[5] = static_cast<uint8_t>(f.seq & 0xFF);
  out[6] = static_cast<uint8_t>(f.seq >> 8);
  out[7] = f.length;
  memcpy(out + kFrameHeader, f.payload, f.length);
  return size;
}

bool decodeFrame(const uint8_t* data, size_t length, Frame& f) {
  if (length < kFrameHeader || length > kMaxFrame) return false;
  if (data[0] != kFrameMagic || data[1] != kFrameVersion) return false;
  if (data[7] != length - kFrameHeader) return false;
  f.src = data[2];
  f.dst = data[3];
  f.type = static_cast<MessageType>(data[4]);
  f.seq = static_cast<uint16_t>(data[5] | (data[6] << 8));
  f.length = data[7];
  memcpy(f.payload, data + kFrameHeader, f.length);
  return true;
}

void PeerLink::onFrame(uint32_t nowMs, uint16_t seq) {
  if (seen_ && !alive(nowMs)) {
    // Back after a dropout, or rebooted. The gap isn't a count of missed
    // frames any more, so start counting afresh.
    ++dropouts_;
  } else if (seen_) {
    const uint16_t gap = static_cast<uint16_t>(seq - lastSeq_ - 1);
    if (gap < 0x8000) missed_ += gap;
  }
  seen_ = true;
  lastMs_ = nowMs;
  lastSeq_ = seq;
  ++received_;
}

bool parseMac(const char* text, uint8_t out[kMacBytes]) {
  if (strlen(text) != kMacBytes * 3 - 1) return false;
  for (size_t i = 0; i < kMacBytes; ++i) {
    if (!hexByte(text + i * 3, out[i])) return false;
    if (i + 1 < kMacBytes && text[i * 3 + 2] != ':') return false;
  }
  return true;
}

bool parseKey(const char* text, uint8_t out[kLinkKeyBytes]) {
  if (strlen(text) != kLinkKeyBytes * 2) return false;
  for (size_t i = 0; i < kLinkKeyBytes; ++i) {
    if (!hexByte(text + i * 2, out[i])) return false;
  }
  return true;
}

LinkCommand parseLinkCommand(const char* line) {
  LinkCommand c;
  char word[8];
  char value[kLinkKeyBytes * 2 + 1];
  char extra[2];

  const char* rest = nextWord(line, word, sizeof(word));
  if (rest == nullptr) return c;
  if (word[0] == '\0') {
    c.kind = LinkCommand::Kind::Status;
    return c;
  }

  if (strcmp(word, "forget") == 0) {
    rest = nextWord(rest, extra, sizeof(extra));
    if (rest != nullptr && extra[0] == '\0') c.kind = LinkCommand::Kind::Forget;
    return c;
  }

  if (strcmp(word, "key") == 0) {
    rest = nextWord(rest, value, sizeof(value));
    if (rest == nullptr) return c;
    rest = nextWord(rest, extra, sizeof(extra));
    if (rest == nullptr || extra[0] != '\0') return c;
    if (strcmp(value, "new") == 0) {
      c.kind = LinkCommand::Kind::NewKey;
    } else if (parseKey(value, c.key)) {
      c.kind = LinkCommand::Kind::Key;
    }
    return c;
  }

  if (strcmp(word, "peer") == 0) {
    char name[8];
    rest = nextWord(rest, name, sizeof(name));
    if (rest == nullptr) return c;
    rest = nextWord(rest, value, sizeof(value));
    if (rest == nullptr) return c;
    const char* end = nextWord(rest, extra, sizeof(extra));
    if (end == nullptr || extra[0] != '\0') return c;
    c.node = nodeFromName(name);
    if (c.node != kNodeNone && parseMac(value, c.mac)) c.kind = LinkCommand::Kind::Peer;
    return c;
  }

  return c;
}

}  // namespace droid
