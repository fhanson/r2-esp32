#include "SequenceStore.h"

#include <LittleFS.h>
#include <string.h>
#include <sys/stat.h>

namespace {
constexpr char kPath[] = "/sequences.bin";
constexpr char kTmp[] = "/sequences.tmp";
constexpr uint8_t kVersion = 1;

bool fileExists(const char* path) {  // stat() is quiet when it's missing; open() logs
  char full[48];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return stat(full, &st) == 0;
}
}  // namespace

void SequenceStore::begin() {
  count_ = 0;
  if (!fileExists(kPath)) return;
  File f = LittleFS.open(kPath, "r");
  uint8_t head[6];
  if (!f || f.read(head, sizeof(head)) != sizeof(head) || memcmp(head, "R2SQ", 4) != 0 || head[4] != kVersion ||
      head[5] > droid::kMaxCustomSeqs) {
    return;
  }
  uint8_t buf[droid::kCustomSeqEncodedMax];
  for (size_t i = 0; i < head[5]; ++i) {
    uint8_t len[2];
    if (f.read(len, 2) != 2) break;
    const size_t size = len[0] | (len[1] << 8);
    if (size > sizeof(buf) || f.read(buf, size) != size) break;
    if (droid::decodeCustomSequence(buf, size, seqs_[count_])) ++count_;  // skip a bad one, keep the rest
  }
}

const droid::CustomSequence* SequenceStore::find(uint8_t id) const {
  for (size_t i = 0; i < count_; ++i) {
    if (seqs_[i].id == id) return &seqs_[i];
  }
  return nullptr;
}

const droid::CustomSequence* SequenceStore::findName(const char* name) const {
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(seqs_[i].name, name) == 0) return &seqs_[i];
  }
  return nullptr;
}

uint8_t SequenceStore::put(const droid::CustomSequence& seq) {
  if (!droid::validCustomSequence(seq)) return 0;
  droid::CustomSequence s = seq;
  if (s.id == 0) {
    for (uint8_t id = droid::kCustomSeqFirst; droid::isCustomSequenceId(id); ++id) {
      if (!find(id)) {
        s.id = id;
        break;
      }
    }
    if (s.id == 0) return 0;
  } else if (!droid::isCustomSequenceId(s.id)) {
    return 0;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (seqs_[i].id == s.id) {
      seqs_[i] = s;
      return s.id;
    }
  }
  if (count_ >= droid::kMaxCustomSeqs) return 0;
  seqs_[count_++] = s;
  return s.id;
}

bool SequenceStore::remove(uint8_t id) {
  for (size_t i = 0; i < count_; ++i) {
    if (seqs_[i].id != id) continue;
    for (size_t j = i + 1; j < count_; ++j) seqs_[j - 1] = seqs_[j];
    --count_;
    return true;
  }
  return false;
}

bool SequenceStore::save() {
  File f = LittleFS.open(kTmp, "w");
  if (!f) return false;
  const uint8_t head[6] = {'R', '2', 'S', 'Q', kVersion, static_cast<uint8_t>(count_)};
  bool ok = f.write(head, sizeof(head)) == sizeof(head);
  uint8_t buf[droid::kCustomSeqEncodedMax];
  for (size_t i = 0; ok && i < count_; ++i) {
    const size_t size = droid::encodeCustomSequence(seqs_[i], buf, sizeof(buf));
    const uint8_t len[2] = {static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8)};
    ok = size > 0 && f.write(len, 2) == 2 && f.write(buf, size) == size;
  }
  f.close();
  // Replace the old file only once the new one is complete.
  return ok && (!fileExists(kPath) || LittleFS.remove(kPath)) && LittleFS.rename(kTmp, kPath);
}
