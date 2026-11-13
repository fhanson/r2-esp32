#pragma once
// Firmware updates over Wi-Fi, the board side (Ota.h). Linking this also
// stops the core confirming a new image at boot (verifyRollbackLater), so
// otaConfirmIfHealthy() decides.

#include <Arduino.h>

#include "Ota.h"

class OtaClient {
 public:
  // Starts the update in its own task. False if the payload is bad or one is
  // already running.
  bool start(uint8_t node, const uint8_t* payload, size_t length);

  bool running() const { return state_ != droid::OtaState::Idle && state_ != droid::OtaState::Failed; }
  droid::OtaProgress progress() const;

  // Every loop: reports progress through send (a NodeStatus payload) and
  // restarts once the new image is written.
  using Send = bool (*)(const uint8_t* payload, size_t length);
  void loop(uint32_t nowMs, Send send);

 private:
  static void task(void* self);
  void run();
  void fail(droid::OtaError e);

  uint8_t node_ = 0;
  droid::OtaStart start_;
  volatile droid::OtaState state_ = droid::OtaState::Idle;
  volatile uint8_t percent_ = 0;
  volatile droid::OtaError error_ = droid::OtaError::None;
  uint32_t lastReportMs_ = 0;
  uint32_t restartAtMs_ = 0;
  uint32_t failedAtMs_ = 0;
  droid::OtaState reported_ = droid::OtaState::Idle;
};

// Confirms a new, unconfirmed image once healthy is true. Cheap every loop.
void otaConfirmIfHealthy(bool healthy);

