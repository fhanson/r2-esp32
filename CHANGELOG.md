# Changelog

All three boards (body S3, dome C3, body C3) share one version, from the
`VERSION` file. Every build stamps it in, with the git commit it came from,
and copies the images to `dist/r2au-<board>-v<version>.bin` for OTA updates.

Versions follow [semantic versioning](https://semver.org):

- **Major** (1.0.0 → 2.0.0): something the boards or the page say to each
  other changed in a way older firmware can't follow: the ESP-NOW frame
  (`kFrameVersion`), the page protocol (`UiProtocol.h`), the saved
  animation file, or the settings keys. Update all three boards together,
  S3 last.
- **Minor** (0.1.0 → 0.2.0): new features that still work with boards on
  the previous minor version.
- **Patch** (0.1.0 → 0.1.1): fixes only.

Before 1.0.0, any minor version may break compatibility; each entry says so
when it does. The format follows [Keep a Changelog](https://keepachangelog.com).

## [Unreleased]

### Added
- **Backup and restore**: one JSON file with the changed settings, every
  animation and your servo sequences, downloaded from the page; restoring
  replaces them on the droid. Wi-Fi name and password excluded. Page only.
- **Servo sequence editor** (W30): make, copy, edit and delete servo
  sequences on the page, for the dome panels and utility arms. Picking a
  frame or moving a slider moves the servos live (drive disarmed only), and
  Play runs it from the start or any frame. Saved on the S3, which plays
  them by sending single-servo moves, so the C3s are unchanged. Numbered
  from 64, after the built-ins. New page messages ServoMoves, SeqHeader,
  SeqFrame, SeqCommit, DeleteSequence, CopySequence; RunServoSequence takes
  an optional first frame.
- **Live servo calibration** (W3): on the Servo tuning screens, the servo
  moves to each endpoint as you change it, while the drive is disarmed.
- **Firmware updates over Wi-Fi** (W21): Firmware Update on the page takes a
  `.bin` from `dist/`, shows which board and version it is, and uploads it.
  The S3 updates itself; a C3 is sent its image by the S3 (it joins the
  access point and downloads it), with progress on the page. Every board
  checks the image's board tag first. A new image keeps itself only once
  it's healthy (the S3 after 20 s, a C3 once it hears the S3), otherwise the
  bootloader rolls back. Locked while armed or with the enable switch on.
  New link message `OtaStart` and NodeStatus field 2 (C3 progress); older
  boards ignore both. The access point now takes 6 devices, not 4.
- **Utility arm sequences**: Top Arm Open, Top Arm Close, Bottom Arm Open,
  Bottom Arm Close, Arms Open and Close, and Arms Flap (both open and close
  twice, then three see-saws, ending closed). They're numbered after the
  dome's 11, so saved animations keep pointing at the same dome sequences.
  The S3 sends them to the body C3 (`Q11` to `Q16`), which plays them with
  each arm's endpoints. They show on the Servo Sequences tab and in the
  animation editor. "Stop and close" now closes the arms too.
- **Body lights**: the body C3 drives its NeoPixel strip (GPIO 5, through a
  74AHCT125). Effects: off, solid, rainbow, wipe, swoop (optionally back and
  forth), scrolling pattern, pulse and sparkle, with up to 4 colours, a
  direction, speed, width, fade-in, a run time (then back to what showed
  before) and brightness. A **Lights** item in the animation editor, with a
  live preview strip; strip length and overall brightness in Settings > Body
  lights. Also typed on the body C3's console, e.g. `Xswoop #ff8800 > s7`.
  New animation step kind 9 (older firmware can't load animations that use it).
- **Wi-Fi name and password in Settings** (Network > Wi-Fi). Saved on the S3
  in place of the built-in pair from `Secrets.h`; saving restarts the access
  point, so the page disconnects and you join again. "Use the built-in name
  and password" (or `:wifi default` on the S3's console) goes back. Locked
  while the drive is armed. Names are 1–32 characters, passwords 8–63, plain
  ASCII; no open network.
- The S3 prints its version, Wi-Fi name and password at startup and on `v`.

- **PSIs and per-display logic effects**: the logic item picks any of front
  logics, rear logic, front PSI and rear PSI (Reeltwo display numbers 1, 3,
  4, 5; all four sends one command to display 0). The dome C3 sends several
  `;`-separated Logic Engine commands from one step. Colours are swatches, and
  a live preview draws all four displays (a guide; the real patterns come from
  the Logic Engine), including scrolling text in the scroll effect's colour.

### Fixed
- Sequence and animation names are escaped wherever the page shows them.
- **Page on phones**: long screens spilled out of the screen box and over
  the controls below it (the box was a shrinking flex item of a
  viewport-high body). The "Preview, no droid connected" bar, the Menu back
  button on the menu, the settings' Default buttons and sound search
  non-matches now really hide (their CSS display overrode `hidden`). The
  whole screen is no longer a live region, which made screen readers try to
  read every screen change.
- **Cross-site control**: the control page's WebSocket, and firmware
  uploads, now refuse requests made from another website's page. Before,
  a page opened on a phone joined to the droid's Wi-Fi (loaded over mobile
  data) could send commands, change the Wi-Fi password, or upload firmware.
- **Page on iPhone**: text boxes and dropdowns are 16 px on phones, so
  Safari no longer zooms in when one is tapped. The animation timeline's
  long items no longer pick up the status bar's phone styles (both used the
  class `bar`), which broke them out of the timeline. Fewer time labels on a
  phone-width timeline.
- The logic text symbol buttons (heart, up, down) now insert at the cursor
  instead of always at the end.
- Saving a large animation (many items with long commands) failed silently:
  the S3's save buffer was sized for the old 12-item limit. It's now sized
  from the limits (up to 1,467 bytes per animation).
- First boot no longer logs "/littlefs/animations.bin does not exist" as an
  error; the S3 checks for the file before opening it.

### Changed
- The access point's password moved to `src/body/Secrets.h`, kept out of git;
  the first build writes it with a random password (`tools/make_secrets.py`).
- Dome C3 pins: the holo PCA9685 is now SDA GPIO5 / SCL GPIO6 and the SBUS
  receiver GPIO20, so every dome wire is on one side of the Super Mini.

## [0.1.0] - 2026-11-10

First versioned build. Builds and passes the PC tests (5,465 checks); not yet
run on the droid's hardware.

### Added
- **Three ESP-NOW nodes** replacing the Padawan360 Mega: body S3 (drive,
  dome rotation, sound, the control page), dome C3 (panels, holoprojectors,
  logic displays, the left-hand radio's dome spin) and body C3 (utility arms).
- **Drive**: SBUS from the right-hand DS-650, arming, ramping and speed
  levels to the VESCs over PPM.
- **Dome**: Roam-a-Dome-Home spin, home and go-to, with CH5 manual / auto /
  home modes.
- **Sound**: HCR vocalizer with separate voice, WAV A and WAV B volumes,
  emotions, and random sounds in auto mode.
- **Control page** on the S3's "R2-AU" Wi-Fi at http://192.168.4.1/: Droid
  Control (sounds, emotions, dome moves, servo sequences), Settings (every
  setting, with sliders and servo endpoint sliders), and screens for backup
  and firmware updates (to be finished). Settings and animation edits are
  locked while the drive is armed.
- **Servo sequences**: the 11 dome Maestro scripts, converted so retuning a
  panel's endpoints fixes every sequence that uses it.
- **Holoprojectors** run on the dome C3 with the Flthy HP command set (the
  Flthy Nano can retire). One holo step can carry a command per HP.
- **Logic displays**: any Reeltwo Logic Engine command, including text.
- **Animations**: the 16 Padawan button animations plus Chortle No (a
  chortle and a head shake), stored on the S3. Up to 48 animations of up to
  20 items each.
- **Animation editor**: blocks along a draggable timeline, each holding any
  mix of sound (picked by name from the SD card's 59 files; several play
  one at random), emotion, servo sequence, dome move (face an angle, spin,
  look ahead, stop, or a head shake), holoprojectors (lights and movement
  per HP) and logic displays.
- **Accessibility**: contrast of 4.5:1 or better in light and dark, focus
  that follows each action, headings, announcements, keyboard use of the
  timeline and tabs.
- **Versioning**: this changelog, the `VERSION` file, a version tag inside
  every image (`R2AU-FW|<board>|<version>|<build>|`) so an OTA upload can
  refuse a file for the wrong board, version reports from each C3 to the S3,
  the `v` console command on the S3, and versions on the page's Firmware
  Update screen with a warning when a board doesn't match the S3.

### Compatibility
- ESP-NOW frame version 1. Page protocol: the page and the S3 firmware
  always match, because the page is built into the S3's image.
- Saved animations from earlier unversioned test builds can't hold dome or
  sound-pick steps; reset the library from Settings > Animations if one
  doesn't load.

[Unreleased]: #unreleased
[0.1.0]: #010---2026-11-10
