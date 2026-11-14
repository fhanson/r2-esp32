# R2-AU control

Firmware for R2-AU's control system: three ESP32 boards on an encrypted
ESP-NOW link.

| Board | Where | Does |
| --- | --- | --- |
| **body-s3** (ESP32-S3) | Body | Right-hand radio, drive (PPM to the VESCs), HCR sound, Roam-a-Dome, Wi-Fi access point and control page, settings, animations |
| **dome-c3** (ESP32-C3) | Dome | Left-hand radio (dome spin), panel servos via the Maestro, holoprojectors, logic commands |
| **body-c3** (ESP32-C3) | Behind the utility arm panel | Utility arms, body NeoPixel strip |

One PlatformIO project builds all three.

## Layout

| Path | What |
| --- | --- |
| `platformio.ini` | `body_s3`, `dome_c3`, `body_c3`; espressif32 6.9.0 (Arduino core 2.0.17) |
| `lib/DroidCore/` | Shared logic with no Arduino dependencies, tested on a PC: SBUS, calibration, mixer, arming, HCR, dome, settings, link and page protocols, servo sequences, animations, holo and pixel effects |
| `lib/DroidRadio/` | ESP-NOW link and its console, SBUS receiver, C3 firmware download |
| `src/body/` | S3 firmware. `BodyConfig.h` holds its pins, tuning and the settings table |
| `src/dome/` | Dome C3 firmware. Pins in `DomeConfig.h` |
| `src/bodyc3/` | Body C3 firmware. Pins at the top of `main.cpp` |
| `web/` | Control page and icons, embedded into the S3 build by `tools/embed_web.py` |
| `tools/` | Build helpers; `gen_servo_sequences.py` ports the dome Maestro's sequences from `tools/maestro/` |
| `host_tests/` | PC tests for `lib/DroidCore` |

## Build and flash

```
pio run -e body_s3 -t upload        # or dome_c3, body_c3
pio device monitor -e body_s3
```

- **S3:** use its native USB port (often labelled USB, not UART/COM). The
  console runs there, leaving all UARTs free.
- **C3 won't take an upload** ("No serial data received"): hold **BOOT**, tap
  **RESET**, let go of BOOT, upload, then press **RESET**.
- **More than one board plugged in:** add `--upload-port` (`pio device list`
  shows the ports).
- **C3 on its 5V pin and USB at once:** the Super Mini may have no diode
  between them. Switch the board's supply off before plugging in USB.
- After the first flash, update over Wi-Fi instead (see below).

PC tests (needs only g++):

```
g++ -std=c++17 -Wall -Wextra -I lib/DroidCore/src -I src/body host_tests/test_droidcore.cpp lib/DroidCore/src/*.cpp -o droidcore_tests && ./droidcore_tests
```

## Body S3

| GPIO | To |
| --- | --- |
| 4 | DS-650 #1 (right hand) SBUS-A signal. No inverter needed |
| 9 / 10 | Left / right VESC PPM signal |
| 17 | Teensy pin 28 (RX7) |
| 18 | Teensy pin 29 (TX7); only needed for replies |
| 42 | Roam-a-Dome **Pulse in** (spin speed) |
| 41 | Roam-a-Dome command serial **Rx** (one of its G V Rx rows; V unconnected) |
| 40 | Roam-a-Dome command serial **TX** |
| GND | Common with the receiver, both VESC signal grounds, the Teensy and the Roam-a-Dome |

- The receiver runs on 5 V from the breakout's 5V0, which only comes on with
  the barrel jack powered.
- The Teensy pins match `Connection=Serial7` and `BaudRate=9600` in
  `Config.txt` on its SD card. HCR's default is `Serial1`, so check a
  replacement card.

Console keys: `r` radio and drive monitor, `v` versions and Wi-Fi details,
`s` random sound, `j` / `l` spin the dome, `<...>` raw HCR command, `>...`
raw Roam-a-Dome command, `:` settings, `@` link, `h` help.

## Dome C3

ESP32-C3 Super Mini. Every signal is on its 5–10 / 20 / 21 side.

| GPIO | To |
| --- | --- |
| 5 / 6 | PCA9685 SDA / SCL (holo servos) |
| 7 | 9600 baud serial, through 1 kΩ, to the Maestro RX and the logic board |
| 10 | Pixel Shifter DAT (holo LEDs) |
| 20 | DS-650 #2 (left hand) SBUS, through 1 kΩ |
| 3.3 V | PCA9685 VCC, Pixel Shifter V |
| 5V | From the dome's 5 V buck; the receiver's + can come from the same row |

Leave 8 and 9 (strapping) and 21 (boot messages) unconnected.

- **Panels:** the dome Maestro's own sequences, ported by
  `tools/gen_servo_sequences.py`, played as Set Target commands.
- **Holoprojectors:** Flthy HP v1.8 commands (`A0033|5`, `F0028|4`, `S1`)
  handled on the C3 (`HoloEngine.h`). PCA9685
  channels: front X/Y 0/1, top 2/3, rear 4/5. The LED jewels are chained
  front, top, rear, 7 LEDs each.
- **Logic:** commands go to the logic board unchanged, ending in CR
  (`~RTLE990505`, `0T3`).
- **Left stick X** (CH1) goes to the S3 as dome spin.

| Console | Does |
| --- | --- |
| `Q2` / `Q-` | Run servo sequence 2 (Cantina) / stop |
| `HA0033\|5` | Holo command: all HPs, pulse, green, 5 s |
| `L0T3` | Logic command: alarm |
| `P16,1500` | Move servo 16 (front holo X) to 1500 µs |
| `@` | Link status |

The S3 sends the same text over the link.

## Body C3

| GPIO | To |
| --- | --- |
| 4 / 3 | PCA9685 SDA / SCL: top arm channel 0, bottom 1 |
| 5 | Pixel Shifter DAT (28-pixel strip, count set by `px_count`) |
| 3.3 V | PCA9685 VCC, Pixel Shifter V |

The arms start closed. Effects are in `lib/DroidCore/src/PixelEngine.h`;
animations set them with Lights items.

| Console | Does |
| --- | --- |
| `A0o` / `A0c`, `A1o` / `A1c` | Open / close the top or bottom arm |
| `Q11`–`Q16` | Arm sequences |
| `P32,1500` | Move the top arm servo to 1500 µs |
| `Xrainbow`, `Xswoop #ff8800 > s7 w5`, `Xoff f1` | Strip effects |

## ESP-NOW link

Each node sends an encrypted heartbeat every 100 ms on channel 1 and counts a
peer as down after 500 ms of silence. The drive doesn't depend on the link,
and the link stays off until a node has been set up.

The key and peer addresses are stored on each board, entered on the USB
console, so the key never goes in the repo:

1. Flash each board and type `@`. Note the MAC on the `this node` line.
2. On one board, `@key new`. Copy the key.
3. On the other two, `@key <that key>`.
4. On every board, enter all three (a node skips its own):
   ```
   @peer body-s3 AA:BB:CC:DD:EE:FF
   @peer dome-c3 AA:BB:CC:DD:EE:FF
   @peer body-c3 AA:BB:CC:DD:EE:FF
   ```
5. Restart all three; changes apply at boot.

`@forget` clears a board's setup.

## Control page

The S3 runs an access point, **R2-AU**, on channel 1. Join it and open
**http://192.168.4.1/**.

The password is in `src/body/Secrets.h`, which git ignores. The first
`body_s3` build writes it with a random password (`tools/make_secrets.py`).
Edit it to choose your own and reflash. The S3 prints the name, password and
address at boot and on `v`. The name and password can also be changed from
Settings › Network › Wi-Fi; `:wifi default` on the console goes back to
`Secrets.h`.

| Screen | Does |
| --- | --- |
| Droid Control | Sound tiles (each plays its animation), emotions, dome moves, servo sequences, Stop |
| Settings | Every setting, saved on the S3. Servo endpoints go to the node driving them as they change |
| Animations and servo sequences | Edit what each animation does; make your own sequences |
| Backup / Restore | Download or restore settings, animations and sequences as one file |
| Firmware Update | Update any board over Wi-Fi |

Settings, the library, backup and updates are locked while the drive is
armed. Droid Control keeps working. Opened anywhere but the droid, the page
runs on example data and says "Preview".

The page is embedded in every `body_s3` build. It talks to the S3 over a
WebSocket at `/ws` (`lib/DroidCore/src/UiProtocol.h`). Requests from other
websites are refused (Origin check, plus a custom header on uploads).

### Your own servo sequences

**+ Add sequence** starts one, and **Copy** makes an editable copy of any
sequence. Each frame has a duration and, for each servo (4 pies, 7 panels,
2 arms), a position from Closed (0 %) to Open (100 %) or "hold". Picking a
frame or moving a slider moves the real servo while the drive is disarmed.

They're stored on the S3 (`/sequences.bin`, up to 24 sequences of 40 frames)
and played from there as single-servo moves, so the C3s need nothing extra.
Positions follow each servo's endpoints, so retuning a servo fixes every
sequence that uses it.

### Backup and restore

The file holds the settings changed from their defaults, every animation and
your sequences. Restoring makes the droid match it exactly: other settings
return to default, and animations and sequences not in the file are removed.
The Wi-Fi name and password are never included or changed.

## Settings

`kSettings` in `BodyConfig.h` holds each setting's default and limits. A
saved override wins and survives reboots. Settings cover HCR, the dome, servo
endpoints and the strip. Drive tuning, calibration, pins and safety values
stay in `BodyConfig.h`.

From the console (press Enter after each):

| Type | Does |
| --- | --- |
| `:` | List every setting with default and range |
| `:hcr_vol_a` | Show one |
| `:hcr_vol_a 60` | Set and save |
| `:hcr_vol_a default` | Back to default |

To add one: add it to `Setting` and `kSettings`, and to `applySetting()` in
`main.cpp` if it should take effect immediately.

## Drive

### Radio channels (DS-650 #1)

| Channel | Control | Handset button type |
| --- | --- | --- |
| CH1 | Stick left/right: steer | |
| CH2 | Stick forward/back: throttle | |
| CH3 | Speed 1 / 2 / 3 | 3-position |
| CH4 | Drive enable | Latching |
| CH5 | Dome: manual / auto / home | 3-position |
| CH6 | Random sound | Momentary |

Leave MIXES and constant speed (CCS) off on the handset. MIXES changes what
CH1 and CH2 carry, and CCS holds throttle in a way the droid can't detect.

### Safety

- Arms only with enable on and the sticks centred. If enable was on at power
  up, it must be switched off first.
- If no good frame arrives for 200 ms, the drive disarms. Frames flagged lost
  or failsafe are ignored. Both outputs go to neutral at once, skipping the
  ramp.
- A 1 s watchdog resets a stalled S3. The pulses stop, and the VESCs' PPM
  timeout stops the feet.

### Ramping

Set in `rampConfig()`. The firmware smooths the stick before mixing, so both
feet change together.

| Setting | Default | Meaning |
| --- | --- | --- |
| `throttleAccelS` / `throttleDecelS` | 0.8 / 0.4 | Seconds between rest and full throttle; a reversal slows to zero first |
| `steerAccelS` / `steerDecelS` | 0.3 / 0.15 | Same for steering |
| `speedChangeS` | 0.5 | Easing when the speed level changes while moving |
| `throttleExpo` | 0.3 | 0 is linear; higher softens the start of the stick |

Speed 3 at full stick is about 0.75 m/s (2,100 eRPM). Speed 1 tops out near
0.3 m/s and speed 2 near 0.6.

### VESC settings

Both VESCs: FOC with Hall sensors, PID speed mode at about 2,100 max ERPM,
speed PID Kp 0.003 / Ki 0.008, one VESC inverted. The firmware does the
curve and ramp, so the VESC's own are flat or short:

| Setting | Value | Why |
| --- | --- | --- |
| App → PPM → pulse mapping | 544 / 1472 / 2400 µs | What the S3 sends |
| App → PPM → throttle expo | **0** | The firmware applies expo to throttle only; a VESC curve would also bend each wheel in turns |
| App → PPM → ramp time pos / neg | **0.2 s** | Backstop only |
| Motor → PID → speed PID ramp, if shown | **~10,000 eRPM/s** (about 0.2 s) | Same |
| App → General → timeout | 1 s | Stops the feet if the pulses stop |
| App → General → timeout brake current | **3 A** | A dead controller brakes instead of freewheeling |

In PID speed mode a centre pulse holds 0 RPM, so failsafe and disarm brake
and hold on a slope.

### Mixer

Set in `mixerConfig()`.

| Setting | Default | Meaning |
| --- | --- | --- |
| `steeringFactor` | 100 | Steering curve shape; larger is closer to linear |
| `curveOnSteer` / `curveOnThrottle` | true / false | Which axis the curve applies to |
| `spinInPlace` | false | true allows turning on the spot with no throttle |
| `invertLeft` / `invertRight` / `swapFeet` | false | Direction and side fixes |

## Sound

HCR on the Teensy 4.1, over serial at 9600. Commands are buffered (512 bytes)
and never block the drive loop. If the buffer fills, a command is dropped and
counted.

| Event | Sound |
| --- | --- |
| Radio connects | 0021 |
| Drive arms / disarms (including a lost link while armed) | 0052 / 0053 |
| CH6 | Random 0009–0052, at most one per 5 s (`hcr_gap_ms`) |
| CH5 in auto | Random every 5–12 s (`hcr_auto_min_s`, `hcr_auto_max_s`); `:hcr_auto_on 0` keeps it quiet |
| Startup | WAV volume 80 (`hcr_vol_a`), vocalizer 100 (`hcr_vol_voc`), emotion override on with happy 89 and sad 22 |

Connect, arm and disarm sounds always play, whatever the 5 s gap. For
`<...>` console commands, see the
[HCR command reference](https://docs.humancyborgrelations.com/r2d2/platforms/embedded/commands).

## Dome rotation

The Roam-a-Dome-Home drives the SyRen. The S3 sends it spin speed as an RC
pulse and home or auto moves as text commands. CH5 picks the mode:

| CH5 | Dome |
| --- | --- |
| Manual | Spin from the left-hand stick (relayed by the dome C3), or `j` / `l`. 1000–2000 µs, 1500 stops |
| Auto | Random angle within ±80° of home (`dome_auto_deg`) at 30 % (`dome_auto_speed`), every 6–8 s; random sounds too |
| Home | `:DPH`: turns to home |

- The dome follows the radio link, not drive arming. On a lost link the pulse
  goes to 1500 µs and `:DPZ` cancels any move.
- A relayed spin older than 250 ms counts as zero.
- If the S3 resets, the pulse stops and the Roam-a-Dome stops the dome.
- The Roam-a-Dome's own **AUTO SAFETY** ignores home and auto until the dome
  has been spun by hand once after power-up. `#DPMAXSPEED` caps manual speed.

## Versions and updates

All three boards share the version in `VERSION`. Changes are recorded in
`CHANGELOG.md`. Each build stamps `R2AU-FW|<board>|<version>|<build>|` into
the image and copies it to `dist/r2au-<board>-v<version>.bin`. The build
part is the commit hash, with `-dirty` for uncommitted changes. Boards print
their version at boot; `v` on the S3 lists all three.

### Updating over Wi-Fi

On the page, open **Firmware Update** and pick a file from `dist/`. The page
reads the board and version from the file, and each board refuses an image
tagged for a different board.

- **S3:** written to the spare app slot, then the S3 restarts (about 10 s).
- **C3:** the S3 stores the file and tells that C3, over the link, to join
  the access point and download it. The C3 must be powered and on the link.
- **Rollback:** a new image keeps itself only once it's healthy: the S3 after
  20 s, a C3 once it hears the S3. A crash before that reverts to the old
  image. **After updating the S3, leave it powered for 20 s.**
- Images aren't signed. The Wi-Fi password is what protects updates.
- USB always works as a fallback.

### Releasing

1. Move `[Unreleased]` in `CHANGELOG.md` to a dated version section, bumping
   as its rules say, and put the same version in `VERSION`.
2. Commit, then `pio run -e body_s3 -e dome_c3 -e body_c3`. The build part
   should have no `-dirty`.
3. Update the C3s first and the S3 last, so the page is never newer than the
   boards.

## Bench tests

### Radio and drive (wheels off the ground)

1. **Motors unplugged.** Type `r`: `good` frames should climb and show
   `link 1`. If `good` stays at 0, the monitor shows it trying 100000 and
   115200 baud; one should lock.
2. **Calibrate.** Note CH1–CH4's raw ends and rest and put them in
   `steerCal`, `throttleCal`, `speedCal`, `enableCal` in `BodyConfig.h`.
   Stick right should read `steer +` and forward `thr +`; flip `invert` if
   not.
3. **Safety, before the VESCs:**
   - Power up with enable on: `turn enable OFF first`.
   - Enable on, sticks centred: `ARMED` after 0.3 s.
   - Handset off: `link 0`, both outputs 1472 µs within 0.2 s.
   - Handset back on with the stick pushed: stays disarmed until centred.
4. Apply the VESC settings above to both VESCs.
5. **Wheels in the air.** Forward should drive both feet forward. If not,
   flip `invertLeft`, `invertRight` or `swapFeet` in `mixerConfig()`. Then
   check turns and each speed.
6. **Tune** `rampConfig()` on the floor at speed 1.

### Sound

1. `<QVA>` should reply `<QVA,80>` once the S3 has been up 3 s. No reply:
   check the wiring and `Config.txt`. Then try another Teensy port (such as
   `Serial4`, RX 16 / TX 17; not `Serial2`, which the Audio Shield uses).
2. `s` plays a random sound.
3. Put CH6's off and on values in `soundCal()`. The button must be
   momentary; latching plays only every second press.
4. Check: handset on 0021; arm 0052; disarm or link loss 0053; CH6 press
   plays one sound per 5 s.
5. Power-cycle the S3 and Teensy together, then `<QE>`: it should report
   `89,22,0,0`. If not, raise `kHcrStartDelayMs`.

In the monitor, `hcr sent` should climb and `dropped` should stay at 0.

### Dome rotation

1. **Roam-a-Dome:** from its USB console, or `>` on the S3's, send
   `#DPPWMIN1`, `#DPSERIALIN0`, `#DPRESTART`. If the mode ever changes by
   itself, tie its unused PPM/RC signal pin to ground.
2. `>:DPZ` on the S3: the Roam-a-Dome's console prints `READ:` per
   character. Nothing means a wrong pin on GPIO41 or no shared ground.
3. CH5 manual, press `j` then `l`: the dome turns each way, which also clears
   AUTO SAFETY. Wrong way: `:dome_invert 1`.
4. Note CH5's three raw values in `r`. Low should be manual; set `invert` in
   `domeModeCal()` if not.
5. Home turns home; auto moves every 6–8 s; manual stops.
6. In auto, switch the handset off: the dome stops and the monitor shows
   1500 µs.

### Link

1. `@` on each board: peers `UP`, `missed` and `dropouts` near 0, `rejected`
   at 0.
2. Power one node off: its peers show `DOWN` within 0.5 s. Power it back on:
   `UP`, with `dropouts` up by one.
3. **Encryption:** give one board a wrong key and restart it. It must stay
   down on the others. Put the right key back.
4. On the S3, compare `worst loop` in `r` with the link on and off. It should
   stay well under 200 ms.
5. Repeat step 1 with the body closed and the dome on, for a few minutes.

### Control page

1. Join R2-AU and open the page: the status bar shows Wi-Fi, and the preview
   bar is gone.
2. Settings › Volume › WAV A: `:hcr_vol_a` shows the new value.
3. Arm the drive: Settings locks.
4. Sounds › Cantina: music, plus the panels, holos and logics with the dome up.
5. Reload: settings and animations come back from the droid.
6. Download a backup, change a volume, delete an animation, restore: both
   come back.

### Updates over Wi-Fi

1. Build all three. Update the body C3 from the page: joining, downloading,
   restarting, and its new version appears.
2. Update the S3: the page reconnects, and the console prints `Firmware
   update confirmed` about 20 s after start.
3. **Wrong board:** `curl -H "X-R2AU-Update: 1" --data-binary
   @dist/r2au-body_c3-v0.1.0.bin "http://192.168.4.1/update?node=body_s3"`
   is refused.
4. **Locks:** with the drive armed or enable on, the button greys out and a
   forced upload gets "Disarm the drive first".
5. **Power cut:** switch the body C3 off mid-download. It comes back on its
   old firmware, and the page reports the failure.
6. **Cross-site:** the same `curl` without the `X-R2AU-Update` header gets
   403.
