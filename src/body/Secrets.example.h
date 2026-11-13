#pragma once
// Example of src/body/Secrets.h, which holds the S3 access point's password
// and is kept out of git (the repo is public).
//
// You don't need to copy this: the first body_s3 build writes Secrets.h with a
// random password and prints it (tools/make_secrets.py). To choose your own, edit
// Secrets.h, then rebuild and flash the S3. WPA2 needs 8 to 63 characters.

namespace body {
constexpr char kApPassword[] = "choose-your-own";
}  // namespace body
