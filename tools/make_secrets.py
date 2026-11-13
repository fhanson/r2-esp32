"""PlatformIO pre-build script: makes sure src/body/Secrets.h exists.

Secrets.h holds the S3 access point's password and is kept out of git (the
repo is public). On a fresh copy of the project there's none, so this writes
one with a random password and prints it, so the build still works and the
password is never a published default. Change it by editing the file.
"""
import secrets
from pathlib import Path

try:
    Import("env")  # noqa: F821  (PlatformIO)
    ROOT = Path(env["PROJECT_DIR"])  # noqa: F821
except NameError:  # run by hand
    ROOT = Path(__file__).resolve().parent.parent

OUT = ROOT / "src/body/Secrets.h"
# No 0/o, 1/l/i: easy to read off a card and type on a phone.
ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"


def new_password():
    return "-".join("".join(secrets.choice(ALPHABET) for _ in range(4)) for _ in range(3))


def write(password):
    OUT.write_text(f"""#pragma once
// The S3 access point's password. Kept out of git (.gitignore): the repo is
// public. Made by tools/make_secrets.py; edit it to change the password, then
// rebuild and flash the S3. WPA2 needs 8 to 63 characters.

namespace body {{
constexpr char kApPassword[] = "{password}";
}}  // namespace body
""")


if not OUT.exists():
    password = new_password()
    write(password)
    print(f"secrets: made src/body/Secrets.h; the R2-AU Wi-Fi password is {password}")
