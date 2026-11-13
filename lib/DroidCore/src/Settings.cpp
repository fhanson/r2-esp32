#include "Settings.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace droid {

Settings::Settings(const SettingDef* defs, size_t count, SettingsBackend& backend)
    : defs_(defs), count_(count > kMaxSettings ? kMaxSettings : count), backend_(backend) {
  for (size_t i = 0; i < count_; ++i) {
    values_[i] = defs_[i].defaultValue;
    overridden_[i] = false;
  }
}

void Settings::load() {
  for (size_t i = 0; i < count_; ++i) {
    int32_t saved = 0;
    const bool valid =
        backend_.load(defs_[i].key, saved) && saved >= defs_[i].min && saved <= defs_[i].max;
    values_[i] = valid ? saved : defs_[i].defaultValue;
    overridden_[i] = valid;
  }
}

int Settings::find(const char* key) const {
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(defs_[i].key, key) == 0) return static_cast<int>(i);
  }
  return -1;
}

SettingResult Settings::set(size_t index, int32_t value) {
  if (value < defs_[index].min || value > defs_[index].max) return SettingResult::OutOfRange;
  // Sliders can repeat a value; don't wear the flash for it.
  if (overridden_[index] && values_[index] == value) return SettingResult::Ok;
  values_[index] = value;
  overridden_[index] = true;
  return backend_.save(defs_[index].key, value) ? SettingResult::Ok : SettingResult::NotSaved;
}

SettingResult Settings::clear(size_t index) {
  values_[index] = defs_[index].defaultValue;
  if (!overridden_[index]) return SettingResult::Ok;
  overridden_[index] = false;
  return backend_.erase(defs_[index].key) ? SettingResult::Ok : SettingResult::NotSaved;
}

SettingCommand parseSettingCommand(const char* line) {
  SettingCommand c;
  while (*line == ' ') ++line;
  if (*line == '\0') {
    c.kind = SettingCommand::Kind::List;
    return c;
  }

  const char* keyEnd = line;
  while (*keyEnd != '\0' && *keyEnd != ' ') ++keyEnd;
  const size_t keyLength = static_cast<size_t>(keyEnd - line);
  if (keyLength > kMaxSettingKey) return c;
  memcpy(c.key, line, keyLength);

  const char* arg = keyEnd;
  while (*arg == ' ') ++arg;
  size_t argLength = strlen(arg);
  while (argLength > 0 && arg[argLength - 1] == ' ') --argLength;

  if (argLength == 0) {
    c.kind = SettingCommand::Kind::Show;
    return c;
  }
  if (argLength == 7 && strncmp(arg, "default", 7) == 0) {
    c.kind = SettingCommand::Kind::Clear;
    return c;
  }

  char* end = nullptr;
  errno = 0;
  const long value = strtol(arg, &end, 10);
  const bool whole = end == arg + argLength;
  if (!whole || errno != 0 || value < INT32_MIN || value > INT32_MAX) return c;
  c.kind = SettingCommand::Kind::Set;
  c.value = static_cast<int32_t>(value);
  return c;
}

}  // namespace droid
