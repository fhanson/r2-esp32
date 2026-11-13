#pragma once
// Firmware updates over Wi-Fi, the S3's web side (lib/DroidCore/src/Ota.h).
//
//   POST /update?node=body_s3|dome_c3|body_c3   the .bin as the body; needs the
//                                                X-R2AU-Update header
//   GET  /fw/dome_c3, /fw/body_c3               the stored image, for that C3
//
// Handlers run in the web server's task; the main loop passes state in and
// takes results out through these.

#include <esp_http_server.h>
#include <stdint.h>

namespace ota {

void begin(httpd_handle_t server);

// Every loop. reason (a string literal) is the reply while not allowed.
void setAllowed(bool allowed, const char* reason);
void setNodeUp(uint8_t node, bool up);

// True once when a new S3 image is written and checked.
bool takeRestart();

// True once per C3 image stored, with what to put in its OtaStart.
bool takeNodeImage(uint8_t& node, uint32_t& size);

}  // namespace ota
