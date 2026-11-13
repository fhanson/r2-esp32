# Dome Maestro: channels and sequences

`dome-maestro-settings.txt` is the dome Maestro Mini 18's settings file, pasted
by Franklin on 2026-11-10 (exported from Maestro Control Center). The first
line's leading `<` was lost in the paste and has been put back; nothing else
was changed.

The Maestro stores positions in quarter-microseconds. The values below are in
µs (divide by 4).

## Serial

UART, fixed 9600 baud, device number 12, no CRC, no serial timeout. The Mega
started scripts with `restartScript(n)` over this line, shared with the logic
engine and the Flthy HPs.

## Channels

11 servos are used: 4 pie panels and 7 lower dome panels. Channels 4–6 and
14–17 are unused (home mode Off).

| Ch | Name | Closed (home) | Open (All Open) | Min | Max | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | Pie 1 | 992 | 2000 | 992 | 2000 | |
| 1 | Pie 2 | 992 | 2000 | 992 | 2000 | |
| 2 | Pie 3 | 608 | 2000 | 608 | 2000 | |
| 3 | Pie 4 | 608 | 2000 | 608 | 2000 | |
| 7 | Dome 1 | 608 | 2000 | 608 | 2000 | |
| 8 | Dome 2 | 608 | 2000 | 608 | 2000 | |
| 9 | Dome 3 | 608 | 2000 | 608 | 2000 | |
| 10 | Dome 4 | 608 | 2000 | 608 | 2304 | Ripple drives it to 2304, past its All Open position |
| 11 | Dome 5 | 992 | 2000 | 992 | 2000 | |
| 12 | Dome 6 | 608 | 1504 | 608 | 1504 | Opens less than the others |
| 13 | Dome 7 | 608 | 1504 | 608 | 1504 | Opens less; the "Point" panel |

Every closed position is the channel's min, so each panel closes toward its
low limit. Speed and acceleration are 0 (unlimited) on every channel: moves
are as fast as the servo can go, and sequences get their timing only from the
frame durations.

## Sequences (script subroutines)

The script number is the subroutine's position in the script, which is what
`restartScript(n)` runs. Every sequence ends with `quit`, so each runs once.

| Script | Sequence | Frames | Length | What it does | Mega button (latest sketch) |
| --- | --- | --- | --- | --- | --- |
| 0 | All Closed | 1 | 0.5 s | Every panel to closed | R2+Up (commented "random script or all sequences", but it's All Closed) |
| 1 | All Open | 1 | 0.5 s | Every panel to open | R2+Right, and L1+A (system failure) |
| 2 | Cantina | 17 | 9.7 s | Pies 1–4 open one by one (0.8 s each), each pie half-closes and reopens in turn, twice round (0.5 s steps), then they close one by one | R2+Down ("dance"), and R1+B (Cantina music) |
| 3 | Clap | 9 | 1.35 s | Pies 1 and 3 alternate with 2 and 4, 150 ms apart, three times | R2+Left |
| 4 | Pie 1 Open | 1 | 0.5 s | Pie 1 open, everything else closed | None (L2+Up commented out) |
| 5 | Pie 2 Open | 1 | 0.5 s | Pie 2 open, everything else closed | None (L2+Right's call commented out) |
| 6 | Pie 3 Open | 1 | 0.5 s | Pie 3 open, everything else closed | None (replaced by script 10) |
| 7 | Pie 4 Open | 1 | 0.5 s | Pie 4 open, everything else closed | None (replaced by script 9) |
| 8 | Point | 5 | 0.6 s | Dome 7 opens, flaps twice, closes | L2+X (wolf whistle) |
| 9 | Ripple | 16 | 0.8 s | A wave runs across Dome 1→7 and the pies, 50 ms per frame | L2+Left |
| 10 | Wave | 9 | 2.9 s | Pie 2 opens and flaps four times, then closes | L2+Down, and L2+B (doot doot) |

## For the new firmware

- **Endpoints:** the closed, open, min and max values above are the starting
  defaults for Servo Tuning > Dome panels.
- **Sequences:** all 11 get ported to droid sequences (A14, W11–W12) on the
  dome C3, keeping the same names. The Maestro's frames are instant jumps
  followed by a wait, so they convert to keyframes with "step" easing at the
  same times. Positions convert to 0–1 between each panel's closed and open
  (Ripple's Dome 4 frame at 2304 is past open, so it needs a value above 1 or
  a wider open point).
- **Point** uses Dome 7 only; **Wave** uses Pie 2 only.
- **Maestro role:** the handoff keeps the Maestro as a plain servo driver
  for the 11 panels, with endpoints and sequences in ESP32 firmware. Its
  scripts can stay on the board as a fallback.
