#pragma once
// Settings that can be changed without reflashing. The board supplies the
// storage through SettingsBackend.
//
// A table in the firmware gives each setting its default and limits. A saved
// override wins over the default, and clearing it goes back to the default.
// Pins and safety values don't belong here: they stay fixed in the firmware.

#include <stddef.h>
#include <stdint.h>

namespace droid {

constexpr size_t kMaxSettings = 128;
constexpr size_t kMaxSettingKey = 15;  // NVS limit

struct SettingDef {
  const char* key;
  int32_t defaultValue;
  int32_t min;
  int32_t max;
};

class SettingsBackend {
 public:
  virtual ~SettingsBackend() = default;
  virtual bool load(const char* key, int32_t& value) = 0;  // false if not saved
  virtual bool save(const char* key, int32_t value) = 0;
  virtual bool erase(const char* key) = 0;
};

enum class SettingResult : uint8_t {
  Ok,
  OutOfRange,  // nothing changed
  NotSaved,    // in use now, but lost at the next boot
};

class Settings {
 public:
  Settings(const SettingDef* defs, size_t count, SettingsBackend& backend);

  // Reads saved overrides. One that's out of range is ignored, in case the
  // limits have changed since it was saved.
  void load();

  size_t count() const { return count_; }
  const SettingDef& def(size_t index) const { return defs_[index]; }
  int32_t get(size_t index) const { return values_[index]; }
  bool overridden(size_t index) const { return overridden_[index]; }

  // Index of a key, or -1 if there's no such setting.
  int find(const char* key) const;

  SettingResult set(size_t index, int32_t value);
  SettingResult clear(size_t index);

 private:
  const SettingDef* defs_;
  size_t count_;
  SettingsBackend& backend_;
  int32_t values_[kMaxSettings];
  bool overridden_[kMaxSettings];
};

// One console line, without its leading ':'.
//   (empty)        list every setting
//   key            show one
//   key 60         set it
//   key default    clear the override
struct SettingCommand {
  enum class Kind : uint8_t { List, Show, Set, Clear, Bad };
  Kind kind = Kind::Bad;
  char key[kMaxSettingKey + 1] = {};
  int32_t value = 0;
};

SettingCommand parseSettingCommand(const char* line);

}  // namespace droid
