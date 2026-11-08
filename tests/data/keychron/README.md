# Keychron recordings

| File | What |
|---|---|
| `k10-he-v3-ansi-0ea7-2026-11-08.txt` | A Keychron K10 HE V3 ANSI (3434:0EA7, firmware v1.0.0 2026-07-17-17:42:44, Keychron protocol 4, VIA 12), borrowed by Rico, recorded read-only on 2026-11-08 before it went back to its owner |

Written by `nazg-probe-keychron --matrix 6x21 --profile all --device 3434:0EA7 --record <file>`:
every request sent to the board, `> ` and hex, then its reply, `< ` and hex. Reports the protocol's
reply check read past are not in it. The `keychron_recording` test (`tests/KeychronRecordingTest.cpp`)
answers each request with the reply recorded for it.

What the board held when it was recorded, after a day of writes, each profile reset in Nazg:

- **Default** (profile 0, in use): regular 1.7 mm, sensitivities 0.4, every key following it --
  the firmware's defaults.
- **Gaming** (1): rapid trigger 1.7 / 0.4 / 0.4, every key following it.
- **Gamepad** (2): rapid trigger 1.7 / 0.4 / 0.4, 25 keys in gamepad mode (keychron-commands.md,
  "The three profiles read").
- No DKS or SOCD slot in use; the curve (0, 0) (8, 31) (26, 95) (35, 127), full travel 3.5 mm.
- The keymap, 8 layers of 6 x 21; the switch matrix with no key pressed.

Its wiring is not the published K10 HE ANSI file's: keychron-commands.md, "Its wiring differs too".
