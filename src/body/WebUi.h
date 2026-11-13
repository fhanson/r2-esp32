#pragma once
// The control web page: a small web server on the S3's access point.
//
//   GET /          the page (embedded at build time from web/, gzipped)
//   GET /emoji/... its icons
//   /ws            WebSocket carrying UiProtocol messages, one per frame
//   /update, /fw/  firmware updates (OtaServer.h)
//
// Uses ESP-IDF's esp_http_server, which runs in its own task. Incoming
// messages are queued and taken by the main loop, a few per loop, so the web
// page can never hold up the drive loop. Sends go out without waiting.

#include <Arduino.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "UiProtocol.h"

class WebUi {
 public:
  static constexpr size_t kMaxClients = 4;

  struct Event {
    enum class Kind : uint8_t { Connected, Disconnected, Message };
    Kind kind = Kind::Message;
    int fd = -1;
    uint8_t length = 0;
    uint8_t data[droid::kUiMaxMessage];
  };

  bool begin();

  // False for a browser request made from another site's page (its Origin
  // isn't this host). Requests without an Origin, like a C3's, pass.
  static bool sameOrigin(httpd_req_t* req);
  bool running() const { return server_ != nullptr; }

  // Takes the next event; false when there are none.
  bool poll(Event& event);

  bool send(int fd, const uint8_t* data, size_t length);
  void broadcast(const uint8_t* data, size_t length);

  // Clients the main loop knows about (it adds and drops them on events).
  int clientFd(size_t slot) const { return clients_[slot]; }
  size_t clientCount() const;

  uint32_t dropped() const { return dropped_; }

 private:
  static esp_err_t handlePage(httpd_req_t* req);
  static esp_err_t handleWs(httpd_req_t* req);
  static void onClose(httpd_handle_t hd, int fd);
  void push(const Event& e);
  void addClient(int fd);
  void dropClient(int fd);

  static WebUi* instance_;
  httpd_handle_t server_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  int clients_[kMaxClients] = {-1, -1, -1, -1};
  volatile uint32_t dropped_ = 0;
};
