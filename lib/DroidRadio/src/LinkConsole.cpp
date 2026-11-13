#include "LinkConsole.h"

#include <esp_system.h>
#include <string.h>

using droid::LinkCommand;

bool LinkConsole::feed(char c, Print& out, uint32_t nowMs) {
  if (length_ == 0 && c != '@') return false;

  if (c == '\r' || c == '\n') {
    line_[length_] = '\0';
    length_ = 0;
    run(line_ + 1, out, nowMs);
    return true;
  }
  if (length_ >= sizeof(line_) - 1) {
    length_ = 0;
    out.println("line too long");
    return true;
  }
  line_[length_++] = c;
  return true;
}

void LinkConsole::reportSaved(bool saved, Print& out) {
  out.println(saved ? "saved; restart this board to use it" : "NOT saved: couldn't write to NVS");
}

void LinkConsole::run(const char* line, Print& out, uint32_t nowMs) {
  const LinkCommand c = droid::parseLinkCommand(line);
  droid::LinkConfig& config = link_.config();

  switch (c.kind) {
    case LinkCommand::Kind::Status:
      link_.printStatus(out, nowMs);
      break;
    case LinkCommand::Kind::Peer:
      memcpy(config.mac[c.node], c.mac, droid::kMacBytes);
      config.macSet[c.node] = true;
      reportSaved(link_.saveConfig(), out);
      break;
    case LinkCommand::Kind::Key:
      memcpy(config.key, c.key, droid::kLinkKeyBytes);
      config.keySet = true;
      reportSaved(link_.saveConfig(), out);
      break;
    case LinkCommand::Kind::NewKey:
      esp_fill_random(config.key, droid::kLinkKeyBytes);
      config.keySet = true;
      out.print("new key, use it on the other nodes: ");
      for (size_t i = 0; i < droid::kLinkKeyBytes; ++i) out.printf("%02x", config.key[i]);
      out.println();
      reportSaved(link_.saveConfig(), out);
      break;
    case LinkCommand::Kind::Forget:
      reportSaved(link_.forgetConfig(), out);
      break;
    case LinkCommand::Kind::Bad:
      out.println("usage: '@' status, '@peer dome-c3 AA:BB:CC:DD:EE:FF', '@key <32 hex>',");
      out.println("       '@key new', '@forget'");
      break;
  }
}
