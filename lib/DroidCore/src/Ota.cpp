#include "Ota.h"

#include <string.h>

#include "LinkProtocol.h"

namespace droid {

void FirmwareTagScanner::feed(const uint8_t* data, size_t length) {
  if (found_ || length == 0) return;
  // The last piece's tail plus this one's head, then this piece alone.
  const size_t head = length < kKeep ? length : kKeep;
  memcpy(carry_ + carryLen_, data, head);
  if (findFirmwareTag(carry_, carryLen_ + head, tag_) || findFirmwareTag(data, length, tag_)) {
    found_ = true;
    return;
  }
  // Keep the last kKeep bytes for the next piece.
  if (length >= kKeep) {
    memcpy(carry_, data + length - kKeep, kKeep);
    carryLen_ = kKeep;
  } else {
    const size_t total = carryLen_ + length;
    const size_t drop = total > kKeep ? total - kKeep : 0;
    memmove(carry_, carry_ + drop, total - drop);
    carryLen_ = total - drop;
  }
}

const char* otaErrorText(OtaError e) {
  switch (e) {
    case OtaError::None: return "";
    case OtaError::NoWifi: return "couldn't join the S3's Wi-Fi";
    case OtaError::NoServer: return "couldn't download the image from the S3";
    case OtaError::WrongBoard: return "the file is for another board";
    case OtaError::WriteFailed: return "writing or checking the image failed";
    case OtaError::Timeout: return "timed out";
    case OtaError::Busy: return "already updating";
  }
  return "?";
}

namespace {
bool putText(uint8_t* out, size_t cap, size_t& n, const char* s, size_t max) {
  const size_t len = strlen(s);
  if (len > max || n + 1 + len > cap) return false;
  out[n++] = static_cast<uint8_t>(len);
  memcpy(out + n, s, len);
  n += len;
  return true;
}

bool getText(const uint8_t* data, size_t length, size_t& n, char* out, size_t cap) {
  if (n >= length) return false;
  const size_t len = data[n++];
  if (len >= cap || n + len > length) return false;
  memcpy(out, data + n, len);
  out[len] = '\0';
  n += len;
  return true;
}
}  // namespace

size_t encodeOtaStart(uint8_t* out, size_t cap, const OtaStart& s) {
  if (cap < 8) return 0;
  size_t n = 0;
  for (int i = 0; i < 4; ++i) out[n++] = static_cast<uint8_t>(s.size >> (8 * i));
  for (int i = 0; i < 4; ++i) out[n++] = s.ip[i];
  if (!putText(out, cap, n, s.ssid, sizeof(s.ssid) - 1)) return 0;
  if (!putText(out, cap, n, s.password, sizeof(s.password) - 1)) return 0;
  return n;
}

bool decodeOtaStart(const uint8_t* data, size_t length, OtaStart& out) {
  if (length < 10) return false;
  OtaStart s;
  size_t n = 0;
  for (int i = 0; i < 4; ++i) s.size |= static_cast<uint32_t>(data[n++]) << (8 * i);
  for (int i = 0; i < 4; ++i) s.ip[i] = data[n++];
  if (!getText(data, length, n, s.ssid, sizeof(s.ssid))) return false;
  if (!getText(data, length, n, s.password, sizeof(s.password))) return false;
  if (n != length || s.size == 0 || s.ssid[0] == '\0') return false;
  out = s;
  return true;
}

size_t encodeOtaProgress(uint8_t* out, size_t cap, const OtaProgress& p) {
  if (cap < 4) return 0;
  out[0] = 2;
  out[1] = static_cast<uint8_t>(p.state);
  out[2] = p.percent > 100 ? 100 : p.percent;
  out[3] = static_cast<uint8_t>(p.error);
  return 4;
}

bool decodeOtaProgress(const uint8_t* data, size_t length, OtaProgress& out) {
  if (length != 4 || data[0] != 2 || data[1] > static_cast<uint8_t>(OtaState::Failed) || data[2] > 100 ||
      data[3] > static_cast<uint8_t>(OtaError::Busy)) {
    return false;
  }
  out.state = static_cast<OtaState>(data[1]);
  out.percent = data[2];
  out.error = static_cast<OtaError>(data[3]);
  return true;
}

const char* otaNodeEnv(uint8_t node) {
  switch (node) {
    case kNodeBodyS3: return "body_s3";
    case kNodeDomeC3: return "dome_c3";
    case kNodeBodyC3: return "body_c3";
  }
  return nullptr;
}

uint8_t otaNodeFromEnv(const char* env) {
  for (uint8_t n = kNodeBodyS3; n <= kNodeBodyC3; ++n) {
    if (strcmp(otaNodeEnv(n), env) == 0) return n;
  }
  return 0;
}

}  // namespace droid
