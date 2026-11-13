#pragma once
// Stores setting overrides in NVS flash.

#include <Preferences.h>
#include "Settings.h"

class NvsSettings : public droid::SettingsBackend {
 public:
  // False if NVS can't be opened; the droid then runs on its defaults.
  bool begin() { return open_ = prefs_.begin("droid", false); }

  bool load(const char* key, int32_t& value) override {
    if (!open_ || !prefs_.isKey(key)) return false;
    value = prefs_.getInt(key);
    return true;
  }

  bool save(const char* key, int32_t value) override {
    return open_ && prefs_.putInt(key, value) == sizeof(value);
  }

  bool erase(const char* key) override { return open_ && prefs_.remove(key); }

 private:
  Preferences prefs_;
  bool open_ = false;
};
