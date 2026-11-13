#pragma once
// Servo sequences made on the page, on the S3's LittleFS partition.
// One file, /sequences.bin: "R2SQ", a version byte, a count, then each
// sequence as a 2-byte length and its encoded form (CustomSequences.h).
// Mount LittleFS first (AnimationStore::begin does).

#include "CustomSequences.h"

class SequenceStore {
 public:
  void begin();

  size_t count() const { return count_; }
  const droid::CustomSequence& at(size_t i) const { return seqs_[i]; }
  const droid::CustomSequence* find(uint8_t id) const;
  const droid::CustomSequence* findName(const char* name) const;

  // Adds or replaces by id; id 0 gets the lowest free one. Returns the id,
  // or 0 if it's full or invalid.
  uint8_t put(const droid::CustomSequence& seq);
  bool remove(uint8_t id);
  bool save();

 private:
  droid::CustomSequence seqs_[droid::kMaxCustomSeqs];
  size_t count_ = 0;
};
