#include "WebUi.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "OtaServer.h"
#include "WebAssets.h"

WebUi* WebUi::instance_ = nullptr;

namespace {
constexpr int kQueueDepth = 16;
}

bool WebUi::begin() {
  queue_ = xQueueCreate(kQueueDepth, sizeof(Event));
  if (queue_ == nullptr) return false;
  instance_ = this;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.lru_purge_enable = true;
  config.close_fn = onClose;
  config.stack_size = 6144;
  if (httpd_start(&server_, &config) != ESP_OK) {
    server_ = nullptr;
    return false;
  }

  httpd_uri_t ws = {};
  ws.uri = "/ws";
  ws.method = HTTP_GET;
  ws.handler = handleWs;
  ws.is_websocket = true;
  httpd_register_uri_handler(server_, &ws);

  // Firmware updates. Before the page, whose "/*" would otherwise catch /fw/.
  ota::begin(server_);

  httpd_uri_t page = {};
  page.uri = "/*";
  page.method = HTTP_GET;
  page.handler = handlePage;
  httpd_register_uri_handler(server_, &page);
  return true;
}

esp_err_t WebUi::handlePage(httpd_req_t* req) {
  // Ignore any query string.
  char path[64];
  size_t n = strcspn(req->uri, "?");
  if (n >= sizeof(path)) n = sizeof(path) - 1;
  memcpy(path, req->uri, n);
  path[n] = '\0';
  if (strcmp(path, "/index.html") == 0) strcpy(path, "/");
  for (size_t i = 0; i < web::kAssetCount; ++i) {
    const web::Asset& a = web::kAssets[i];
    if (strcmp(a.path, path) != 0) continue;
    httpd_resp_set_type(req, a.type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", i == 0 ? "no-cache" : "max-age=86400");
    return httpd_resp_send(req, reinterpret_cast<const char*>(a.data), a.size);
  }
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
  return ESP_OK;
}

bool WebUi::sameOrigin(httpd_req_t* req) {
  char origin[64], host[48];
  if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK) {
    // No Origin: not a browser page. Too long to read: refuse.
    return httpd_req_get_hdr_value_len(req, "Origin") == 0;
  }
  if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
  const char* o = strstr(origin, "://");
  return o && strcmp(o + 3, host) == 0;
}

esp_err_t WebUi::handleWs(httpd_req_t* req) {
  WebUi* self = instance_;
  if (req->method == HTTP_GET) {  // the handshake
    // Another site's page could otherwise drive the droid from a phone that
    // is on its Wi-Fi.
    if (!sameOrigin(req)) return ESP_FAIL;
    Event e;
    e.kind = Event::Kind::Connected;
    e.fd = httpd_req_to_sockfd(req);
    if (self) self->push(e);
    return ESP_OK;
  }
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_BINARY;
  esp_err_t r = httpd_ws_recv_frame(req, &frame, 0);
  if (r != ESP_OK) return r;
  if (frame.len > droid::kUiMaxMessage) return ESP_FAIL;  // closes the socket
  Event e;
  e.kind = Event::Kind::Message;
  e.fd = httpd_req_to_sockfd(req);
  frame.payload = e.data;
  if (frame.len > 0) {
    r = httpd_ws_recv_frame(req, &frame, frame.len);
    if (r != ESP_OK) return r;
  }
  if (frame.type != HTTPD_WS_TYPE_BINARY || frame.len == 0 || self == nullptr) return ESP_OK;
  e.length = static_cast<uint8_t>(frame.len);
  self->push(e);
  return ESP_OK;
}

void WebUi::onClose(httpd_handle_t, int fd) {
  if (instance_) {
    Event e;
    e.kind = Event::Kind::Disconnected;
    e.fd = fd;
    instance_->push(e);
  }
  close(fd);
}

void WebUi::push(const Event& e) {
  if (xQueueSend(queue_, &e, 0) != pdPASS) dropped_ = dropped_ + 1;
}

bool WebUi::poll(Event& event) {
  if (queue_ == nullptr || xQueueReceive(queue_, &event, 0) != pdPASS) return false;
  if (event.kind == Event::Kind::Connected) addClient(event.fd);
  if (event.kind == Event::Kind::Disconnected) dropClient(event.fd);
  return true;
}

void WebUi::addClient(int fd) {
  for (int& c : clients_) if (c == fd) return;
  for (int& c : clients_) {
    if (c < 0) {
      c = fd;
      return;
    }
  }
  // Full: the oldest gives way.
  memmove(clients_, clients_ + 1, sizeof(int) * (kMaxClients - 1));
  clients_[kMaxClients - 1] = fd;
}

void WebUi::dropClient(int fd) {
  for (int& c : clients_) if (c == fd) c = -1;
}

size_t WebUi::clientCount() const {
  size_t n = 0;
  for (int c : clients_) if (c >= 0) ++n;
  return n;
}

// Sends happen in the server's own task (httpd_queue_work), as ESP-IDF
// asks, so they never race the server reading the same socket. The message
// is copied, so the caller's buffer is free as soon as this returns.
namespace {
struct PendingSend {
  httpd_handle_t server;
  int fd;
  size_t length;
  uint8_t data[droid::kUiMaxMessage];
};

void sendWork(void* arg) {
  PendingSend* p = static_cast<PendingSend*>(arg);
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_BINARY;
  frame.payload = p->data;
  frame.len = p->length;
  httpd_ws_send_frame_async(p->server, p->fd, &frame);
  free(p);
}
}  // namespace

bool WebUi::send(int fd, const uint8_t* data, size_t length) {
  if (server_ == nullptr || fd < 0 || length == 0 || length > droid::kUiMaxMessage) return false;
  if (httpd_ws_get_fd_info(server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
    dropClient(fd);
    return false;
  }
  PendingSend* p = static_cast<PendingSend*>(malloc(sizeof(PendingSend)));
  if (p == nullptr) return false;
  p->server = server_;
  p->fd = fd;
  p->length = length;
  memcpy(p->data, data, length);
  if (httpd_queue_work(server_, sendWork, p) != ESP_OK) {
    free(p);
    dropped_ = dropped_ + 1;
    return false;
  }
  return true;
}

void WebUi::broadcast(const uint8_t* data, size_t length) {
  for (int fd : clients_) {
    if (fd >= 0) send(fd, data, length);
  }
}
