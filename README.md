# R2 ESP32 control

Firmware for an ESP-NOW droid control system: an ESP32-S3 in the body
(replacing the Arduino Mega running Padawan360), an ESP32-C3 in the dome
(replacing the Flthy holo Nano), and an ESP32-C3 behind the utility arm panel.
One PlatformIO project, three firmwares.

**Status: drive, sound, dome rotation, the control web page, dome panels,
holoprojectors, logic commands and utility arms are written; nothing past the
body has been on hardware yet.** SBUS from the right-hand HotRC DS-650,
failsafe, arming, throttle curve and ramping, Padawan360's BigHappyDude
mixing, RC pulses to the two Flipsky VESCs, HCR sound on the Teensy 4.1, and
the Roam-a-Dome (manual spin, auto and home). The S3 runs a WiFi access point
and serves the control page. The dome C3 plays the panel servo sequences on
the Maestro, runs the holoprojectors itself (the Flthy Nano retires), passes
logic commands to the WReactor32, and relays the left-hand stick as dome spin.
The body C3 drives the utility arms. The feet use PPM pulses, as Padawan360
did; CAN waits for the CAN Pal. Not built yet: CAN drive, the animation
editor, firmware updates and backup over WiFi, the NeoPixel strip, and the
touch display.

## Layout

| Path | What |
| --- | --- |
| `platformio.ini` | `body_s3`, `dome_c3` and `body_c3` environments, platform pinned to espressif32 6.9.0 (Arduino core 2.0.17) |
| `lib/DroidCore/` | Shared code with no Arduino dependencies: SBUS parser, channel calibration, drive mixer, arming rules, HCR commands and sound triggers, dome control, settings, link protocol, servo sequences, animations, holoprojector engine, UI messages |
| `lib/DroidRadio/` | ESP-NOW link and its console commands, and the SBUS receiver, shared by the firmwares |
| `src/body/` | S3 firmware. **`BodyConfig.h` holds every pin and tuning value.** Also the web server (`WebUi`) and the animation library (`AnimationStore`) |
| `src/dome/` | Dome C3: Maestro panels, holoprojectors, logic commands, left-stick dome spin. Pins in `DomeConfig.h` |
| `src/bodyc3/` | Body accessory C3: utility arms |
| `web/` | The control page and its icons. Built into the S3 firmware by `tools/embed_web.py`. `web/emoji/make_emoji.py` draws the icons |
| `tools/` | `embed_web.py`, `gen_servo_sequences.py` (servo sequences from the Maestro's settings file in `tools/maestro/`) |
| `host_tests/` | PC tests for `lib/DroidCore`, which is plain C++ with no Arduino dependencies so it builds on a PC |

## Build

```
pio run -e body_s3 -t upload
pio device monitor -e body_s3
```

Run the PC tests (needs only g++):

```
g++ -std=c++17 -Wall -Wextra -I lib/DroidCore/src -I src/body host_tests/test_droidcore.cpp lib/DroidCore/src/*.cpp -o droidcore_tests && ./droidcore_tests
```

Plug the S3 into its **native USB** port (often labelled USB, not UART/COM);
the console runs there so all three UARTs stay free.

## Versions and releases

All three boards share one version, in `VERSION`; what changed in each is in
`CHANGELOG.md`. Each build prints `version: <board> v<version> (<commit>)`,
puts that in the firmware (each board prints it at boot, and the S3's `v`
command lists every board's), and copies the image to
`dist/r2au-<board>-v<version>.bin`. Those are the files for OTA updates.
The build part shows `-dirty` when the image came from uncommitted changes,
and `nogit` before the first commit.

To make a release:

1. Move the changes under `[Unreleased]` in `CHANGELOG.md` into a new
   section for the version, with the date. Bump major, minor or patch as the
   changelog's rules say.
2. Put the same version in `VERSION`.
3. Commit, then build all three: `pio run -e body_s3 -e dome_c3 -e body_c3`.
   The build part should be the commit hash, without `-dirty`.
4. Update the C3s first and the S3 last, so the S3 (and the page it
   carries) is never newer than the boards it talks to.

Every image carries `R2AU-FW|<board>|<version>|<build>|`
(`lib/DroidCore/src/Version.h`), so an update can check a file is for the
board it's sent to. The page reads it from the file before sending; the S3
and the C3s look for it while the image streams in, wherever it sits (about
77 KB in for the S3 today, 6 KB for the C3s), and refuse to boot an image
without the right one.

### Updating over Wi-Fi

Join the droid's Wi-Fi, open **Firmware Update**, and choose a `.bin` from
`dist/`. The page shows which board it's for and its version, then uploads
it. Locked while the drive is armed or the enable switch is on. USB stays
the recovery path. Code: `src/body/OtaServer.cpp` (the S3's upload and
download), `lib/DroidRadio/src/OtaNode.cpp` (the C3s' download, and
confirming), `lib/DroidCore/src/Ota.h` (how it all fits).

- **S3:** written to the spare app slot, checked, then the S3 restarts
  (about 10 s; the page reconnects by itself).
- **C3:** the S3 keeps the file, then tells that C3 over the encrypted link
  to join the access point and download `GET /fw/<board>`. The page shows
  its progress. The C3 must be powered and on the link (its version showing
  on the Firmware Update screen). It takes one of the access point's 6
  places while it downloads.
- **Rollback:** a new image keeps itself only once it's healthy: the S3 after
  20 s running, a C3 once it hears the S3 again. If it crashes or restarts
  before that, the bootloader goes back to the old image at the next start.
  So after updating the S3, **leave it powered for 20 seconds.**
- Images flashed over USB aren't affected; they're kept as they are.
- **Who can update:** anyone on the droid's Wi-Fi, from its own page, so
  the Wi-Fi password is what protects it. Images aren't signed. Requests
  from other websites' pages are refused (Origin check plus a custom
  header), for uploads and for the page's WebSocket alike.

### Bench test: updates over Wi-Fi

1. Build all three, so `dist/` has fresh images.
2. With USB still available, update the body C3 from the page. Watch it go
   through joining, downloading and restarting, and its version come back.
3. Update the S3 the same way. The page drops out and reconnects; the S3's
   version shows the new build. On its USB console: `Firmware update
   confirmed` about 20 s after it starts.
4. **Wrong file:** the page picks the board from the file's tag, so test
   from a laptop: `curl -H "X-R2AU-Update: 1" --data-binary
   @dist/r2au-body_c3-v0.1.0.bin "http://192.168.4.1/update?node=body_s3"`
   is refused, "for a different board". Any other `.bin` is refused too.
5. **Locks:** arm the drive, or turn the enable switch on: the button greys
   out, and a forced upload gets "Disarm the drive first".
   Arming during an upload stops it; an S3 that's ready to restart waits
   until the drive is disarmed and the switch is off.
6. **Power cut:** switch the body C3 off halfway through its download. It
   restarts on its old firmware, and the page says it failed.
7. **Cross-site:** `curl -X POST --data-binary @dist/r2au-body_c3-v0.1.0.bin
   "http://192.168.4.1/update?node=body_c3"` (no `X-R2AU-Update` header) is
   refused with 403, as is one with an `Origin` of another site.

## Wiring for the bench test

| S3 pin | To |
| --- | --- |
| GPIO 4 | SBUS-A receiver signal (DS-650 #1). No inverter needed. |
| GPIO 17 | Teensy pin 28 (RX7), the wire from Mega pin 14 |
| GPIO 18 | Teensy pin 29 (TX7), the wire to Mega pin 15. Only needed for replies. |
| GPIO 9 | Left VESC PPM signal (was Mega pin 44). The drive signal until the CAN Pal arrives. |
| GPIO 10 | Right VESC PPM signal (was Mega pin 45). The drive signal until the CAN Pal arrives. |
| GPIO 42 | Roam-a-Dome **Pulse in** signal (spin speed) |
| GPIO 41 | Roam-a-Dome command serial **Rx**, on one of its three G V Rx rows (V unconnected) |
| GPIO 40 | Roam-a-Dome command serial **TX** |
| GND | Receiver ground, both VESC signal grounds, Teensy ground, and a ground beside each Roam-a-Dome signal |

Power the receiver from 5 V. The S3's 3.3 V PWM is enough for VESC PPM inputs.

The S3 and the Teensy 4.1 are both 3.3 V and the Teensy Audio Shield doesn't
touch the serial pins, so wire them directly. The pins above match
`Connection=Serial7` and `BaudRate=9600` in `Config.txt` on the Teensy's SD
card. HCR's own default is `Serial1`, so check this on a replacement card.

**Rollback warning:** Teensy 4.1 pins aren't 5 V tolerant, and the Mega's TX
is 5 V with nothing between them on this droid. If the Mega goes back in, put
a divider on the Mega TX to Teensy RX wire: 1 kΩ in series, 2 kΩ to ground.

## Bench test: wheels off the ground

1. **Radio monitor first, motors unplugged.** Flash, open the monitor, type
   `r`. Check `good` frames climbing and `link 1`. If `good` stays at 0, the
   baud line in the monitor shows the receiver being tried at 100000 and
   115200 every half second; one should lock.
2. **Record your handset's numbers.** Move each control and note the raw
   values for CH1 to CH4: both ends and rest. Put them in `BodyConfig.h`
   (`steerCal`, `throttleCal`, `speedCal`, `enableCal`). Check that stick
   right reads `steer +` and forward reads `thr +`; flip `invert` if not.
3. **Check the safety behaviour before connecting the VESCs.**
   - Power up with the enable button on: monitor says `turn enable OFF first`.
   - Enable on with sticks centred: `ARMED` after 0.3 s.
   - Switch the handset off: `link 0`, both outputs at 1472 us within 0.2 s.
   - Handset back on with the stick pushed: stays disarmed until centred.
4. **VESC Tool:** apply the changes in "VESC settings" below to both VESCs.
5. **Connect the VESCs, wheels in the air.** Push forward: both feet forward.
   Flip `invertLeft` / `invertRight` / `swapFeet` in `mixerConfig()` until it
   does. Then check turns and each speed level.
6. **Tune the feel** in `rampConfig()`, on the floor at speed 1 first. The
   monitor shows the stick (`thr`, `steer`) next to the ramped values the
   feet get.

## Bench test: sound

The feet aren't needed for this, and it can run before the radio is set up.

1. **Check the link.** Type `<QVA>` in the console. The reply should be
   `<QVA,80>` once the S3 has been up for 3 s. If nothing comes back, check
   the wiring and `Config.txt`. If those are right, the Teensy's RX pin may
   have been damaged by the Mega's 5 V: try another port by changing
   `Connection`, such as `Serial4` (RX 16, TX 17). Avoid `Serial2`, because
   the Audio Shield uses its pins.
2. **Play a sound.** Type `s`. A random file from 0009 to 0052 plays.
3. **Record the sound button.** With the radio on, find CH6's off and on
   values in the monitor and put them in `soundCal()`. Set the button to
   momentary: with a latching button, only every second press plays.
4. **Check the events:**
   - Handset on: 0021.
   - Drive armed: 0052. Disarmed, or link lost while armed: 0053.
   - CH6 press: a random sound. Holding it plays one, and pressing again
     within 5 s of any sound does nothing.
5. **Check startup.** Power-cycle the S3 and the Teensy together, then type
   `<QE>`. It should report `89,22,0,0`. If it doesn't, the Teensy needed more
   time: raise `kHcrStartDelayMs`.

In the monitor, `hcr sent` should climb with each sound and `dropped` should
stay at 0.

## Sound

| Event | Mega | S3 |
| --- | --- | --- |
| Radio connects | 0021 | Same |
| Drive enabled / disabled | 0052 / 0053 | Same files, played when the drive actually arms or disarms, so 0053 also warns of a lost link |
| Random sound | Xbox buttons | CH6: random 0009 to 0052 |
| Automation | Random sounds every few seconds | CH5 in auto: random 0009 to 0052 every 5 to 12 s (`hcr_auto_min_s`, `hcr_auto_max_s`), while the dome wanders. `:hcr_auto_on 0` keeps auto quiet |
| Gap between sounds | Any WAV within 5 s of the last was skipped | CH6 waits 5 s (`hcr_gap_ms`). Connect, arm and disarm always play, so a disarm warning is never skipped |
| WAV volume at startup | 80 on A and B | Same, now two settings: `hcr_vol_a` (every WAV we play) and `hcr_vol_b` (unused for now). The handset has no volume control |
| Vocalizer volume | Not set: the Mega's third `SetVolume` call never reached HCR | Set at startup: `hcr_vol_voc`, default 100 (as in `Config.txt`) |
| Emotions at startup | Override on, happy 89, sad 22 | Same |

Each command ends in a newline, as the HCR library sends it. Commands go out
through a 512-byte buffer and never wait, so sound can't hold
up the drive loop. If the buffer is ever full, the command is dropped and
counted. Commands typed as `<...>` at the console go straight to HCR; see the
[command reference](https://docs.humancyborgrelations.com/r2d2/platforms/embedded/commands).

CH5's auto position works like the Mega's automation mode: the dome wanders
and the droid chatters. The first sound comes one pause after auto is
selected. Auto sounds respect the 5 s gap, so one due just after a CH6 sound
waits instead of talking over it.

## Dome

The Roam-a-Dome-Home sits between the S3 and the SyRen. The S3 sends it spin
speed as an RC pulse and home / auto moves as text commands; the Roam-a-Dome
drives the SyRen. CH5 on the right-hand handset picks the mode:

| CH5 | Dome |
| --- | --- |
| Manual (low) | Spin from the stick: pulse 1000 to 2000 us, 1500 is stop. Until the dome node relays the left-hand stick, only the `j` / `l` console keys spin it. |
| Auto (middle) | Turns to a random angle within ±80° of home (`dome_auto_deg`) at 30 % (`dome_auto_speed`), then waits 6 to 8 s (`dome_auto_min_s`, `dome_auto_max_s`). Random sounds play too; see Sound |
| Home (high) | Sends `:DPH` once: the dome turns to its home position |

Safety:

- The dome follows the **radio link**, not drive arming, so it can turn while
  the feet are disarmed. With the link lost, the pulse goes to 1500 us and
  `:DPZ` cancels any auto or home move.
- A spin value older than 250 ms counts as zero, so a lost relay can't leave
  the dome turning.
- If the S3 hangs, the watchdog resets it, the pulse stops, and the
  Roam-a-Dome stops the dome on its own ("No PWM Input").

The Roam-a-Dome's own settings still apply on top: `#DPMAXSPEED` (default 50)
caps manual speed, and **AUTO SAFETY** (on by default) refuses home and auto
moves until the dome has been turned by hand once since power-up. A short spin
with the stick, or `j` / `l`, unlocks it. Moving the stick also cancels a home
or auto move in progress.

Commands go out through a 256-byte buffer and never wait, like HCR's. The
Roam-a-Dome only replies when the dome moves, with `#DP<mode><position>`; the
monitor shows the latest (`@` off, `!` home, `$` random, `%` target).

## Bench test: dome

1. **Roam-a-Dome settings.** In its own USB console, or from the S3's console
   with a `>` in front, send `#DPPWMIN1` (Pulse in on) and `#DPSERIALIN0`
   (SyRen in off), then `#DPRESTART`. Leave `#DPPWMMIN` / `#DPPWMMAX` at their
   defaults, 1000 / 2000. If the dome mode ever changes by itself, its
   unconnected PPM/RC input may be picking up noise, since `#DPPWMIN1` turns
   that on too: tie its signal pin to ground.
2. **Check the command serial.** Type `>:DPZ` on the S3 console. The
   Roam-a-Dome's USB console prints `READ:` for each character. Nothing means
   the GPIO41 wire is on the wrong pin, or the grounds aren't shared.
3. **Radio on, CH5 in manual.** Press `j`, then `l`: the dome turns briefly
   each way. If it turns the wrong way for the stick, `:dome_invert 1`. This
   also unlocks AUTO SAFETY.
4. **Record CH5.** In the monitor (`r`), note CH5's raw value in each
   position. Low should be manual and high home; set `invert` in
   `domeModeCal()` if it's the other way round.
5. **Home and auto.** CH5 to home: the dome turns home. To auto: a move every
   6 to 8 s. Back to manual: it stops. The monitor's `rad` value follows the
   dome's position.
6. **Link loss.** In auto, switch the handset off: the dome stops and the
   monitor shows `1500 us`.
7. **Auto sounds.** In auto, a random sound every 5 to 12 s, and the monitor's
   `hcr sent` climbs. `:hcr_auto_on 0` silences them; the dome keeps moving.

## Control web page

The S3 runs a WiFi access point, **R2-AU**, on channel 1 (the ESP-NOW
channel). Join it and open **http://192.168.4.1/**. The boot message on the
console prints the address too.

**The password is in `src/body/Secrets.h`**, which git ignores, because the
repo is public. The first `body_s3` build on a fresh copy of the project
writes it with a random password and prints it (`tools/make_secrets.py`); edit the
file to choose your own, then rebuild and flash the S3. `Secrets.example.h`
shows the layout. The S3 prints the Wi-Fi name, password and page address on
its USB console at startup, and again whenever you type `v`.

You can also change the name and password from the page: Settings > Network >
Wi-Fi. That pair is saved on the S3 and used instead of the built-in one;
saving restarts the access point, so join again with the new details. "Use the
built-in name and password" on that screen, or `:wifi default` on the S3's
console, goes back to the pair in `Secrets.h`.

The page is built into the firmware from `web/` on every `body_s3` build, so a
new firmware carries its page. It talks to the S3 over a WebSocket at `/ws`
with the small binary messages in `lib/DroidCore/src/UiProtocol.h`.

What works from the page:

- **Droid Control:** sound tiles play their animations (the sound plus the
  Padawan button's panels, holos and logics), emotions, one-off dome moves
  (radio link needed; CH5 keeps the mode), the 11 servo sequences, Stop and
  close.
- **Settings:** every setting, saved on the S3. Servo endpoints go to the node
  that drives them as soon as they change, and again whenever a node comes up.
- **Settings > Animations and servo sequences:** swap an animation's servo
  sequence, holo and logic commands. Saved on the S3's LittleFS.
- Settings and the animation library refuse changes while the drive is armed.
  Droid Control keeps working while driving.

Opened from anywhere else (the published preview), the page runs on example
data and says "Preview".

### Servo sequences you make

Settings › Animations and servo sequences lists every sequence. **+ Add
sequence** starts a new one; **Copy** makes an editable copy of any sequence,
built-in or yours. Yours open in the editor: a row of frames, each with a
time and, per servo (4 pies, 7 dome panels, 2 arms), a position from Closed
(0%) to Open (100%), a little past either allowed, or left where it was.

- **Live:** picking a frame moves every servo to where that frame leaves it,
  and moving a slider moves that servo, while the drive is disarmed. The
  Servo tuning screens move the servo to each endpoint as you change it too.
- **Play** and **Play from frame N** run the saved sequence on the droid.
- They live on the S3 (`/sequences.bin`, up to 24 of 40 frames each,
  `src/body/SequenceStore.cpp`) and play there: each frame goes to the C3s
  as single-servo moves, so the C3s need nothing new
  (`lib/DroidCore/src/CustomSequences.h`). Animations, the Servo Sequences
  tab and the sound tiles use them like the built-in ones. Positions follow
  each servo's endpoints, so retuning a servo fixes every sequence using it.

### Bench test: web page

1. Flash the S3. On the console: `Control page: join WiFi "R2-AU", open http://192.168.4.1/`.
2. Join R2-AU from a phone, open the page. The status bar shows Wi-Fi, and the
   preview bar disappears.
3. Settings > Volume: move WAV A, let go. `:hcr_vol_a` on the console shows the
   new value, and HCR gets the new volume.
4. Arm the drive: Settings locks on the page.
5. Droid Control > Sounds > Cantina: the music plays (and, with the dome node
   up, the Cantina panels, rainbow holos and logics).
6. Reload the page: settings and animations come back from the droid.

## Flashing the C3 boards

If an upload to a C3 fails with "No serial data received" (or can't connect),
put it in download mode and upload again: hold **BOOT**, tap **RESET** (or
replug USB while holding BOOT), let go of BOOT, upload, then press **RESET**
to start the new firmware. It's mostly needed the first time, or when the
program already on the board stops its USB from answering. With the S3
plugged in too, add `--upload-port` so the upload goes to the right board
(`pio device list` shows the ports).

## Dome node

`src/dome/main.cpp`, pins in `src/dome/DomeConfig.h` (ESP32-C3 Super Mini,
**check before wiring**):

| Pin | To |
| --- | --- |
| GPIO7 (UART0 TX) | The shared 9600 baud line: Maestro RX and the WReactor32 serial header |
| GPIO6 (UART1 RX) | DS-650 #2 (left hand) SBUS |
| GPIO4 / GPIO5 | PCA9685 SDA / SCL (holo servos: front X/Y on 0/1, top 2/3, rear 4/5) |
| GPIO10 | Holo LEDs through the Pixel Shifter: front, top, rear jewels chained, 7 LEDs each |

- **Panels:** servo sequences from the dome Maestro's own sequences, ported by
  `tools/gen_servo_sequences.py`. Positions are relative to each panel's
  Closed/Open, so retuning a panel fixes every sequence. The Maestro is only a
  servo driver now (compact protocol Set Target, as the Mega sent).
- **Holoprojectors:** Flthy HP v1.8 commands (`A0033|5`, `F0028|4`, `S1`, ...),
  done on the C3 (`HoloEngine.h`): LED effects, preset and random positions,
  wag, auto twitch and auto LED.
- **Logic:** commands go to the WReactor32 as they are, with a CR
  (`~RTLE990505`, `0T3`, ...).
- **Left stick X** (CH1 of the left-hand radio) is sent to the S3 as dome spin.

Console commands, the same text the S3 sends:

| Type | Does |
| --- | --- |
| `Q2` | Run servo sequence 2 (Cantina). `Q-` stops |
| `HA0033\|5` | Holo command: all HPs, pulse, green, 5 s |
| `L0T3` | Logic command: alarm |
| `P16,1500` | Move servo 16 (front holo X) to 1500 us |
| `@` | Link status and setup |

## Body accessory node

`src/bodyc3/main.cpp`: the NeoPixel strip on GPIO5 (through a 74AHCT125; effects in
`lib/DroidCore/src/PixelEngine.h`, set from animations' Lights items or typed as
`Xrainbow`, `Xswoop #ff8800 > s7 w5`, `Xoff f1` on its console), and utility arms on a PCA9685 (GPIO4 SDA, GPIO3 SCL; top arm
channel 0, bottom 1). They start closed. Console: `A0o` / `A0c` (top arm open /
closed), `A1o` / `A1c`, `P32,1500`. The NeoPixel strip isn't driven yet.

## ESP-NOW link

`body-s3`, `dome-c3` and `body-c3` send each other an encrypted heartbeat every
100 ms on channel 1. A node counts as down after 500 ms of silence. These
timings are fixed in `LinkProtocol.h`. Roam-a-Dome isn't on this link; it stays
wired to the S3.

The link is separate from the drive path: the feet don't depend on it, and it
stays off, with the radio unused, until a node is set up.

### Setting up the nodes

The shared key and each node's peer addresses are stored in NVS on each board.
They're set from the USB console, so the key never goes in this repo. Press
Enter after each line.

1. Flash each board and type `@` on each. Note the MAC address on the
   `this node` line.
2. On one board, type `@key new`. Copy the 32-digit key it shows.
3. On the other two, type `@key <that key>`.
4. On each board, add the other two:
   ```
   @peer body-s3 AA:BB:CC:DD:EE:FF
   @peer dome-c3 AA:BB:CC:DD:EE:FF
   @peer body-c3 AA:BB:CC:DD:EE:FF
   ```
   A node skips its own entry, so the same three lines work on all of them.
5. Restart all three. Changes only take effect at boot.

`@forget` clears a board's link setup. Keep the key out of anything you publish.

### Bench test: link

1. Type `@` on each board. Both peers should show `UP`, with `missed` and
   `dropouts` staying near 0, and `rejected` at 0.
2. Power off one node. Its peers should show `DOWN` within 0.5 s, and `not
   delivered` starts climbing. Power it back on: `UP`, with `dropouts` up by 1.
3. **Check the encryption.** Give one board a different key with `@key`, and
   restart it. It must stay down on the others. If it shows `UP`, the link
   isn't encrypted as intended, so report it before going further. Put the
   right key back afterwards.
4. **Check the drive loop.** On the S3, type `r` with the link running and read
   `worst loop` in the monitor. Compare it with the link off (`@forget` and
   restart). It should stay well under the 200 ms link timeout.
5. **Test it in the droid.** Repeat step 1 with the body closed up and the dome
   on, because the frame can block signal. Run it for a few minutes and watch
   `missed` and `dropouts`.

## Settings

Settings can be changed without reflashing. `kSettings` in `BodyConfig.h` holds
each default and its limits. A saved override wins, and it survives a reboot.
So far this covers the HCR and dome values; drive tuning and calibration are still fixed
in `BodyConfig.h`. Pins and safety values stay out of it by design.

Until the web page exists, use the USB console and press Enter after each line:

| Type | Does |
| --- | --- |
| `:` | List every setting, with its default and range |
| `:hcr_vol_a` | Show one |
| `:hcr_vol_a 60` | Change it now and save it |
| `:hcr_vol_a default` | Go back to the default |

Volume and emotion changes reach HCR straight away. Saving writes to flash,
which pauses the firmware briefly. The VESC pulses keep running through that.

To add a setting, add it to both `Setting` and `kSettings`, and to
`applySetting()` in `main.cpp` if it needs to take effect at once.

## VESC settings

Keep the existing setup (FOC with Hall sensors, current, voltage and
temperature limits, PID speed control with max ERPM about 2,100, speed PID
Kp 0.003 / Ki 0.008, invert on one VESC). Change only these, because the
firmware now ramps and curves the stick:

| Setting | Was | Now | Why |
| --- | --- | --- | --- |
| App → PPM → throttle expo | 0.3 | **0 (linear)** | The firmware applies 0.3 to throttle only. A VESC curve would also bend each wheel during turns, and the two would stack |
| App → PPM → ramp time positive / negative | 0.8 s | **0.2 s** | Backstop only; the firmware does 0.8 s up / 0.4 s down |
| Motor → PID → speed PID ramp (eRPM per second), if shown | about 0.8 s | **about 0.2 s** (about 10,000 eRPM/s at 2,100 max) | Same reason; this ramp acts in PID speed mode |
| App → General → **timeout brake current** | 0 A (default) | **3 A** | The 1 s timeout alone releases the motor; with 0 A the droid freewheels. This makes a dead controller brake |
| App → PPM → pulse mapping | from Padawan | **unchanged** | The S3 sends the same 544 / 1472 / 2400 us pulses |

Leave deadband (0.03 to 0.05), median filter, safe start, the 1 s timeout and
CAN off as they are. The firmware's own 0.15 deadband sits in front of them.

How a stop works with PID speed control: the firmware's failsafe (0.2 s) and
disarm send the centre pulse, and the VESC actively holds 0 RPM, so the droid
brakes and holds on a slope. The VESC timeout and brake current only matter
if the pulses stop altogether, such as the S3 resetting.

Speed levels: full stick at speed 3 sends the full pulse (about 0.75 m/s at
2,100 eRPM); speed 1 tops out near 0.3 m/s and speed 2 near 0.6 m/s.

## Ramping

The firmware smooths the stick before mixing, so both feet change together
(VESC ramping works per foot, so a turn can come out lopsided).

| Setting | Default | Meaning |
| --- | --- | --- |
| `throttleAccelS` | 0.8 | Seconds from rest to full throttle |
| `throttleDecelS` | 0.4 | Seconds from full throttle to rest; a reversal slows to zero at this rate first |
| `steerAccelS` / `steerDecelS` | 0.3 / 0.15 | Same for steering |
| `speedChangeS` | 0.5 | Switching speed level while moving eases between the two top speeds |
| `throttleExpo` | 0.3 | 0 = linear; higher softens the start of the stick for crawling |

Failsafe and disarm skip the ramp: the feet go to neutral at once.

## Radio channels

The DS-650's stick has two axes: CH1 left/right steers, CH2 forward/back is
throttle (pull back to reverse), both centred at rest. CH3 to CH6 are the
four programmable buttons: CH3 is speed (set to 3-position), CH4 is drive
enable (set to latching), CH5 is dome mode (set to 3-position: manual, auto,
home) and CH6 is sound (set to momentary). Leave the handset's MIXES and constant
speed (CCS) modes off: MIXES changes what CH1 and CH2 carry, and CCS holds
throttle in a way the droid cannot detect.

## Differences from the Padawan360 drive

- Padawan360 called `mixBHD(throttle, turn)` into parameters named
  `(stickX, stickY)`. The effect: the steering curve went on the throttle,
  and the feet stopped whenever throttle was centred. Here the roles are
  explicit; set `curveOnSteer = false`, `curveOnThrottle = true`,
  `spinInPlace = false` to feel exactly like the Mega.
- Padawan360's `RAMPING` only applied to its Sabertooth path, so the VESCs
  never got ramping from the Mega; all the smoothing came from VESC Tool.
  Here the firmware ramps (see Ramping), and the VESC ramp becomes a short
  backstop.
- New: arming rules, a 200 ms link timeout that ignores frames flagged
  lost or failsafe, and a 1 s task watchdog. A stalled loop resets the chip,
  the PWM stops during the reset, and the VESCs' PPM timeout stops the feet.
