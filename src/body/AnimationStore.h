#pragma once
// The animation library, kept on the S3's LittleFS partition.
//
// One file, /animations.bin: "R2AN", a version byte, a count, then each
// animation as a 2-byte length and its encoded form (Animations.h). With no
// file, or one that can't be read, the library starts from the defaults.

#include "Animations.h"

class AnimationStore {
 public:
  // Mounts LittleFS (formatting it if it has never been used) and loads.
  // Returns false if the filesystem can't be used; the defaults still load,
  // but changes can't be saved.
  bool begin();

  size_t count() const { return count_; }
  const droid::Animation& at(size_t i) const { return anims_[i]; }
  const droid::Animation* find(uint16_t id) const;

  // Adds or replaces (by id). An id of 0 gets the next free id from 200.
  // Returns the id, or 0 if the library is full.
  uint16_t put(const droid::Animation& anim);
  bool remove(uint16_t id);
  void reset();  // back to the defaults

  bool save();
  bool saved() const { return saved_; }

 private:
  void loadDefaults();
  bool load();

  droid::Animation anims_[droid::kMaxAnimations];
  size_t count_ = 0;
  bool fsOk_ = false;
  bool saved_ = true;
};
