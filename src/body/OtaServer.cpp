#include "OtaServer.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <stdlib.h>
#include <string.h>

#include "LinkProtocol.h"
#include "Ota.h"
#include "Version.h"
#include "WebUi.h"

namespace ota {

namespace {

constexpr size_t kChunk = 4096;
constexpr int kRecvRetries = 5;             // each waits the server's recv timeout
constexpr uint32_t kC3MaxImage = 0x140000;  // a C3's app slot (default.csv)

volatile bool allowed_ = false;
const char* volatile reason_ = "not ready";
volatile bool nodeUp_[droid::kMaxNodes] = {};
volatile bool restart_ = false;
volatile bool nodeImageReady_ = false;
volatile uint8_t nodeImageNode_ = 0;
volatile uint32_t nodeImageSize_ = 0;
volatile int percent_ = -1;
volatile bool busy_ = false;  // handlers all run in the server's one task

const char* imagePath(uint8_t node) {
  return node == droid::kNodeDomeC3 ? "/fw-dome_c3.bin" : "/fw-body_c3.bin";
}

esp_err_t reply(httpd_req_t* req, const char* status, const char* text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Connection", "close");
  return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

// Up to len bytes of the body; <0 on a dead connection.
int recvSome(httpd_req_t* req, uint8_t* buf, size_t len) {
  for (int tries = 0; tries < kRecvRetries; ++tries) {
    const int r = httpd_req_recv(req, reinterpret_cast<char*>(buf), len);
    if (r != HTTPD_SOCK_ERR_TIMEOUT) return r;
  }
  return -1;
}

struct Busy {
  bool ok;
  Busy() : ok(!busy_) { if (ok) busy_ = true; }
  ~Busy() { if (ok) { busy_ = false; percent_ = -1; } }
};

esp_err_t handleUpdate(httpd_req_t* req) {
  // The custom header can't be sent cross-site without a CORS preflight,
  // which this server never answers; the Origin check covers the rest.
  if (!WebUi::sameOrigin(req) || httpd_req_get_hdr_value_len(req, "X-R2AU-Update") == 0) {
    return reply(req, "403 Forbidden", "Updates only from the droid's own page");
  }
  char query[48] = {}, env[16] = {};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "node", env, sizeof(env)) != ESP_OK) {
    return reply(req, "400 Bad Request", "Say which board: /update?node=body_c3");
  }
  const uint8_t node = droid::otaNodeFromEnv(env);
  if (node == 0) return reply(req, "400 Bad Request", "Unknown board");
  if (!allowed_) return reply(req, "409 Conflict", reason_);
  const bool self = node == droid::kNodeBodyS3;
  if (!self && !nodeUp_[node]) return reply(req, "409 Conflict", "That board isn't on the link: is it powered?");
  const size_t total = req->content_len;
  if (total == 0) return reply(req, "400 Bad Request", "Empty file");

  Busy busy;
  if (!busy.ok) return reply(req, "409 Conflict", "Another update is already running");
  uint8_t* buf = static_cast<uint8_t*>(malloc(kChunk));
  if (!buf) return reply(req, "500 Internal Server Error", "Out of memory");

  droid::FirmwareTagScanner scanner;
  const esp_partition_t* part = nullptr;
  esp_ota_handle_t handle = 0;
  File file;
  const char* error = nullptr;

  if (self) {
    part = esp_ota_get_next_update_partition(nullptr);
    if (!part || total > part->size) error = "Too big for the S3's app slot";
    else if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) error = "Couldn't start writing";
  } else if (total > kC3MaxImage) {
    error = "Too big for a C3";
  } else {
    LittleFS.remove(imagePath(node));
    file = LittleFS.open(imagePath(node), "w");
    if (!file) error = "Couldn't store the file on the S3";
  }

  size_t done = 0;
  percent_ = 0;
  while (!error && done < total) {
    // Flash writes stall the CPU for a moment: stop if the drive arms.
    if (!allowed_) {
      error = reason_;
      break;
    }
    const size_t want = total - done < kChunk ? total - done : kChunk;
    const int got = recvSome(req, buf, want);
    if (got <= 0) {
      error = "The upload was cut off";
      break;
    }
    scanner.feed(buf, got);
    if (self ? esp_ota_write(handle, buf, got) != ESP_OK : file.write(buf, got) != static_cast<size_t>(got)) {
      error = "Writing failed";
      break;
    }
    done += got;
    percent_ = static_cast<int>(done * 100 / total);
  }
  free(buf);

  if (!error && (!scanner.found() || strcmp(scanner.tag().node, env) != 0)) {
    error = scanner.found() ? "That file is for a different board" : "That isn't an R2-AU firmware file";
  }
  if (self) {
    if (handle) {
      if (error) esp_ota_abort(handle);
      else if (esp_ota_end(handle) != ESP_OK) error = "The image failed its check";
    }
    if (!error && esp_ota_set_boot_partition(part) != ESP_OK) error = "Couldn't switch to the new image";
  } else {
    if (file) file.close();
    if (error) LittleFS.remove(imagePath(node));
  }
  if (error) {
    Serial.printf("Firmware upload for %s refused: %s\n", env, error);
    return reply(req, "422 Unprocessable Entity", error);
  }

  char text[64];
  snprintf(text, sizeof(text), "v%s accepted", scanner.tag().version);
  Serial.printf("Firmware upload for %s: %s (%u bytes)\n", env, text, static_cast<unsigned>(total));
  if (self) {
    restart_ = true;
  } else {
    nodeImageNode_ = node;
    nodeImageSize_ = total;
    nodeImageReady_ = true;
  }
  return reply(req, "200 OK", text);
}

// Written by hand, not with httpd's chunked responses, so it carries a
// Content-Length for the C3 to check.
esp_err_t handleImage(httpd_req_t* req) {
  const uint8_t node = droid::otaNodeFromEnv(req->uri + 4);  // after "/fw/"
  if (node != droid::kNodeDomeC3 && node != droid::kNodeBodyC3) return reply(req, "404 Not Found", "No such image");
  File f = LittleFS.open(imagePath(node), "r");
  if (!f) return reply(req, "404 Not Found", "No image stored for that board");
  char head[160];
  const int hn = snprintf(head, sizeof(head),
                          "HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %u\r\n"
                          "Connection: close\r\n\r\n",
                          static_cast<unsigned>(f.size()));
  uint8_t* buf = httpd_send(req, head, hn) == hn ? static_cast<uint8_t*>(malloc(kChunk)) : nullptr;
  esp_err_t result = buf ? ESP_OK : ESP_FAIL;
  while (result == ESP_OK) {
    const size_t n = f.read(buf, kChunk);
    if (n == 0) break;
    for (size_t sent = 0; sent < n;) {
      const int s = httpd_send(req, reinterpret_cast<const char*>(buf) + sent, n - sent);
      if (s == HTTPD_SOCK_ERR_TIMEOUT) continue;
      if (s <= 0) {
        result = ESP_FAIL;
        break;
      }
      sent += s;
    }
  }
  free(buf);
  f.close();
  return result;
}

}  // namespace

void begin(httpd_handle_t server) {
  httpd_uri_t up = {};
  up.uri = "/update";
  up.method = HTTP_POST;
  up.handler = handleUpdate;
  httpd_register_uri_handler(server, &up);

  httpd_uri_t img = {};
  img.uri = "/fw/*";
  img.method = HTTP_GET;
  img.handler = handleImage;
  httpd_register_uri_handler(server, &img);
}

void setAllowed(bool allowed, const char* reason) {
  reason_ = reason;
  allowed_ = allowed;
}

void setNodeUp(uint8_t node, bool up) {
  if (node < droid::kMaxNodes) nodeUp_[node] = up;
}

bool takeRestart() {
  if (!restart_) return false;
  restart_ = false;
  return true;
}

bool takeNodeImage(uint8_t& node, uint32_t& size) {
  if (!nodeImageReady_) return false;
  nodeImageReady_ = false;
  node = nodeImageNode_;
  size = nodeImageSize_;
  return true;
}

}  // namespace ota
