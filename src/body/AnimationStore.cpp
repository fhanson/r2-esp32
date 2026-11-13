#include "AnimationStore.h"

#include <LittleFS.h>
#include <sys/stat.h>

namespace {
constexpr char kPath[] = "/animations.bin";
constexpr uint8_t kVersion = 1;
// One encoded animation at its largest: id, name, icon, count, then every
// step with the longest text (Animations.h).
// LittleFS.exists() opens the file and logs an error when it's missing, so
// look with stat() on its mount point instead, which says nothing.
bool fileExists(const char* path) {
  char full[48];
  snprintf(full, sizeof(full), "/littlefs%s", path);
  struct stat st;
  return stat(full, &st) == 0;
}

constexpr size_t kMaxEncoded = 2 + (1 + droid::kAnimNameMax) + (1 + droid::kAnimIconMax) + 1 +
                               droid::kMaxAnimSteps * (7 + 1 + droid::kStepTextMax);
}  // namespace

bool AnimationStore::begin() {
  fsOk_ = LittleFS.begin(true);  // format on first use
  if (!fsOk_ || !load()) loadDefaults();
  return fsOk_;
}

void AnimationStore::loadDefaults() {
  count_ = droid::defaultAnimations(anims_, droid::kMaxAnimations);
}

bool AnimationStore::load() {
  // First boot has no file yet; checking quietly first keeps open() from
  // logging an error for it.
  if (!fileExists(kPath)) return false;
  File f = LittleFS.open(kPath, "r");
  if (!f) return false;
  uint8_t head[6];
  if (f.read(head, sizeof(head)) != sizeof(head) || memcmp(head, "R2AN", 4) != 0 || head[4] != kVersion ||
      head[5] > droid::kMaxAnimations) {
    return false;
  }
  const size_t want = head[5];
  size_t n = 0;
  uint8_t buf[kMaxEncoded];
  for (size_t i = 0; i < want; ++i) {
    uint8_t len[2];
    if (f.read(len, 2) != 2) return false;
    const size_t size = len[0] | (len[1] << 8);
    if (size > sizeof(buf) || f.read(buf, size) != size) return false;
    if (!droid::decodeAnimation(buf, size, anims_[n])) return false;
    ++n;
  }
  count_ = n;
  return true;
}

bool AnimationStore::save() {
  saved_ = false;
  if (!fsOk_) return false;
  const char* tmp = "/animations.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const uint8_t head[6] = {'R', '2', 'A', 'N', kVersion, static_cast<uint8_t>(count_)};
  bool ok = f.write(head, sizeof(head)) == sizeof(head);
  uint8_t buf[kMaxEncoded];
  for (size_t i = 0; ok && i < count_; ++i) {
    const size_t size = droid::encodeAnimation(anims_[i], buf, sizeof(buf));
    const uint8_t len[2] = {static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8)};
    ok = size > 0 && f.write(len, 2) == 2 && f.write(buf, size) == size;
  }
  f.close();
  // Replace the old file only once the new one is complete.
  ok = ok && (!fileExists(kPath) || LittleFS.remove(kPath)) && LittleFS.rename(tmp, kPath);
  saved_ = ok;
  return ok;
}

const droid::Animation* AnimationStore::find(uint16_t id) const {
  for (size_t i = 0; i < count_; ++i) {
    if (anims_[i].id == id) return &anims_[i];
  }
  return nullptr;
}

uint16_t AnimationStore::put(const droid::Animation& anim) {
  droid::Animation a = anim;
  if (a.id == 0) {
    uint16_t next = 200;
    for (size_t i = 0; i < count_; ++i) {
      if (anims_[i].id >= next) next = anims_[i].id + 1;
    }
    a.id = next;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (anims_[i].id == a.id) {
      anims_[i] = a;
      return a.id;
    }
  }
  if (count_ >= droid::kMaxAnimations) return 0;
  anims_[count_++] = a;
  return a.id;
}

bool AnimationStore::remove(uint16_t id) {
  for (size_t i = 0; i < count_; ++i) {
    if (anims_[i].id != id) continue;
    for (size_t j = i + 1; j < count_; ++j) anims_[j - 1] = anims_[j];
    --count_;
    return true;
  }
  return false;
}

void AnimationStore::reset() { loadDefaults(); }
