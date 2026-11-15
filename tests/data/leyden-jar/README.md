# Leyden Jar recordings

Real level readings from Rico's Model F Labs F77 (Leyden Jar controller, protocol 0.9.1, Model F,
3 bins, offset 10, an 8 x 16 controller), recorded 2026-10-17 read-only, key output off during each
recording and on again after. Test data for the Diagnostics section's Signal levels (ui-design.md,
"Plugins", "Recorded: Rico's F77").

| File | What |
|---|---|
| `f77-every-key-2026-10-17.json.xz` | 4 minutes, 1549 readings: every key pressed slowly, mostly briefly -- 95 of 150 presses lasted two readings |
| `f77-held-keys-2026-10-17.json.xz` | 2 minutes, 775 readings: K, F and Num Lock pressed and held on purpose |
| `f77-five-and-d-2026-11-15.json.xz` | 82 seconds, 512 readings, saved by Nazg's Save readings... (its thresholds one lower by then: 384, 393, 396): 5 pressed twice, D once -- 5 moving Y and 6 |

XZ with a CRC32 check, as minlzma reads it (`adapters/NazgXz.h`). The JSON:

- `board`: the USB product string;
- `info`: `version`, `beamSpring`, `logicalRows` / `logicalColumns`, `controllerRows` /
  `controllerColumns`, `wiring`, `isLeftHalf`, `thresholds` and `referenceLevels` per bin,
  `mappingColumns` / `mappingRows` (logical to controller), `bins[controller column][controller row]`;
- `definition`: the board's own Vial definition, as it serves it;
- `keymap`: layer 0, `"row,col"` to the keycode's name;
- `readings`: `{ t: seconds since the start, levels: [controller column][controller row] }`, one per
  level reading (the sweep, then every column), about 155 ms apart.

Every key works (Rico): K, F and Fn's low holds are the finger's, not faults.
