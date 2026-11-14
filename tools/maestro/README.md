# Dome Maestro: channels and sequences

`dome-maestro-settings.txt` is the dome Maestro Mini 18's settings file,
exported from Maestro Control Center. `tools/gen_servo_sequences.py` reads it
to generate the dome's built-in servo sequences and default endpoints.

The Maestro stores positions in quarter-microseconds. The values below are in
µs (divide by 4).

## Serial

UART, fixed 9600 baud, device number 12, no CRC, no serial timeout. The dome
C3 sends compact-protocol Set Target commands on this line, shared with the
logic board.

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

The script number is the subroutine's position in the script, and is also
the sequence number on the dome C3 (`Q<n>`).

| Script | Sequence | Frames | Length | What it does |
| --- | --- | --- | --- | --- |
| 0 | All Closed | 1 | 0.5 s | Every panel to closed |
| 1 | All Open | 1 | 0.5 s | Every panel to open |
| 2 | Cantina | 17 | 9.7 s | Pies 1–4 open one by one (0.8 s each), each pie half-closes and reopens in turn, twice round (0.5 s steps), then they close one by one |
| 3 | Clap | 9 | 1.35 s | Pies 1 and 3 alternate with 2 and 4, 150 ms apart, three times |
| 4 | Pie 1 Open | 1 | 0.5 s | Pie 1 open, everything else closed |
| 5 | Pie 2 Open | 1 | 0.5 s | Pie 2 open, everything else closed |
| 6 | Pie 3 Open | 1 | 0.5 s | Pie 3 open, everything else closed |
| 7 | Pie 4 Open | 1 | 0.5 s | Pie 4 open, everything else closed |
| 8 | Point | 5 | 0.6 s | Dome 7 opens, flaps twice, closes |
| 9 | Ripple | 16 | 0.8 s | A wave runs across Dome 1→7 and the pies, 50 ms per frame |
| 10 | Wave | 9 | 2.9 s | Pie 2 opens and flaps four times, then closes |

## In the firmware

- **Endpoints:** the closed, open, min and max values above are the defaults
  for Settings › Servo tuning › Dome panels.
- **Sequences:** all 11 are built-in sequences on the dome C3 with the same
  names. Each Maestro frame is an instant jump followed by a wait, so the
  timing matches. Positions are stored relative to each panel's closed and
  open, so Ripple's Dome 4 frame at 2304 is just past open.
