# VIA's saved layouts — the `.layout.json` file

*Compiled 2026-10-18 from the VIA app (`../via-app`, shallow clone of 2026-09-24):
`components/panes/configure-panes/save-load.tsx`, `utils/key.ts`, `utils/advanced-keys.ts`,
`utils/key-to-byte/*` and `utils/macro-api/*` -- read, not ported. Implementation level, like
vil-files.md. No file written by the VIA app has been read yet.*

What VIA's **Configure → Save + Load** pane writes and reads: *Save Current Layout* and *Load Saved
Layout*. Vial's `.vil` is another format (vil-files.md); neither app reads the other's.

## The format

**JSON, one object**, pretty-printed compactly (`json-stringify-pretty-compact`). The suggested
file name is the definition's name, lower-cased, every character but `[a-zA-Z0-9]` turned into
`_`, then `.layout.json`.

| Key | Holds |
|---|---|
| `name` | the definition's name -- shown only when the file is refused as another keyboard's |
| `vendorProductId` | the definition's `vendorId * 65536 + productId`, a number |
| `layers` | `[layer][i]`: **every matrix cell, flat, row-major** -- `i = row * cols + col`, `rows * cols` per layer, as `id_dynamic_keymap_get_buffer` returns them. Every layer the firmware has |
| `macros` | optional: one **expression** string per macro slot, the firmware's count (below) |
| `encoders` | optional: `[encoder][layer]` -> `[ccw, cw]` -- **encoder first, then layer**, the reverse of `.vil`'s `encoder_layout`; counter-clockwise first. As many encoders as the definition's highest `ei` plus one, layout options' keys included; `[]` for none |

**Not in it**: layout options, lighting, a board's own menus, the protocol or keycode version. A
file does not say which keycode numbering its names came from.

## Keycodes are VIA's names

Saving goes through `getCodeForByte()` with the table of the board's VIA protocol:

- **The name from VIA's table** when there is one: `"KC_A"`, `"KC_TRNS"`, `"RGB_TOG"`.
- **A layer key or numbered key**: `"MO(1)"`, `"TO(2)"`, `"DF(0)"`, `"TG(1)"`, `"OSL(1)"`, `"TT(1)"`,
  `"MACRO(3)"`, `"CUSTOM(0)"` (`QK_KB`, the definition's `customKeycodes`).
- **A composed keycode** (`advancedKeycodeToString()`): modifiers as nested functions from the
  left or the right set, or one combined name -- `"S(KC_1)"`, `"LCTL(LSFT(KC_A))"`, `"HYPR(KC_A)"`,
  `"RCS(KC_A)"`; `"LT(1,KC_SPC)"`; `"MT(MOD_LCTL | MOD_LSFT,KC_A)"`; `"OSM(MOD_LSFT)"`;
  `"LM(1,MOD_LCTL)"`. The inner keycode is the table's name of the low byte -- **`undefined`** when
  the table has none, so `"LT(1,undefined)"` can be written.
- **Otherwise hex, lower case**: `"0x7c40"`.

**VIA's names are its own tables**, not QMK's and not vial-gui's: `key-to-byte/default.ts` (VIA
protocol 9 and older), `v10.ts`, `v11.ts`, `v12.ts`, and `v13.ts` (protocol 13 on), which is
`v12` with the `RGB_*` names exchanged for QMK's `UG_*` and `RM_*`. Compared 2026-10-18 by a scratch
script against Nazg's tables: of v12's 474 names, 230 are QMK's short names, 213 are vial-gui's
with the same value, **61 are in neither** -- `KC_SLCK`, `KC_NLCK`, `KC_MS_UP`, `KC_WWW_HOME`,
`KC_LANG3` -- and **139 are vial-gui's with another value**: VIA's MIDI keys start at `MI_C =
0x7110`, QMK's and Vial's at `0x7103`. A name means what VIA's table says, right or wrong, so only
VIA's tables give back the value the board held.

The tables differ as numberings go:

| Table | Protocol | Differs from the one before |
|---|---|---|
| `default` | 9 and older | -- (before QMK's renumbering) |
| `v10` | 10 | only `_QK_TO`: `0x5000`, not `0x5010` (QMK's old `ON_PRESS` bit gone) |
| `v11` | 11 | the renumbering: every range and most values |
| `v12` | 12 | `MACRO(n)` from `0x7700` (v11: `0x7702`), `CUSTOM(n)` from `0x7E00` (v11: `0x7F00`), `FN_MO13` / `FN_MO23` at `0x7C77` / `0x7C78` (v11: `0x7700` / `0x7701`) |
| `v13` | 13 on | no value moves: `RGB_*` names out, `UG_*` and `RM_*` in |

526 distinct names across all of them. A name in two post-renumbering tables has one value except
the four above; the two pre-renumbering tables differ only in `TO`.

**Loading** goes through `getByteForCode()`, with the *board's* table: first the
`deprecated-keycodes.ts` map, which rewrites old spellings -- `KC_TILD` to `S(KC_GRV)`, the shifted
symbols alike, `MACRO00`..`MACRO15` to `MACRO(n)`, `USER00`..`USER15` to `CUSTOM(n)`, `SPC_FN1`..`3` to
`LT(n,KC_SPC)` -- then the table, then the layer and numbered forms, then the composed forms
(upper-cased; modifiers `C`, `S`, `A`, `G`, `LCTL`, `LSFT`, `LALT`, `LGUI`, `LCMD`, `LWIN`, the right
ones, `ALGR`, `SGUI` and the combinations `LSG`, `LAG`, `RSG`, `RAG`, `LCA`, `LSA`, `SAGR`, `RSA`,
`RCS`, `LCAG`, `MEH`, `HYPR`; masks `MOD_LCTL` ... `MOD_RGUI`, `MOD_HYPR`, `MOD_MEH`, joined by `|`).

**Every string it cannot read becomes `0`, `KC_NO`, silently** -- `advancedStringToKeycode()`
returns `0` on failure, and `getByteForCode()` takes any number but `null` as success. That includes
**VIA's own hex strings** and the empty string: a key VIA saved as `"0x7c40"` loads back as nothing
in VIA.

## Macros are expressions

One string per slot, as VIA's macro editor shows it (`sequenceToExpression()`):

- plain text is typed as it reads -- `\{` for a literal `{`;
- `{KC_A}` taps a key; `{KC_LCTL,KC_C}` is a chord: each pressed in order, released in reverse;
- `{+KC_LSFT}` presses, `{-KC_LSFT}` releases;
- `{100}` waits 100 ms -- only from protocol 11, the first with delays in the buffer.

Keycode names in braces are upper-cased and looked up in the **table alone**: no composed forms
in a macro. Protocol 10 and older store one byte per keycode, so only basic keycodes.

## How VIA loads one

`loadLayout()`, in this order, and nothing is read back:

1. **Not a save file** -- no `name`, `layers` or `vendorProductId` -- refused.
2. **Another `vendorProductId`** than the definition's -- refused, naming the file's `name`. There
   is no question, as Vial asks.
3. **A layer whose length is not the board's `rows * cols`** -- refused. A file with **more layers**
   than the board throws in that check (`rawLayers[idx]` is undefined) -- nothing written, nothing
   said; fewer layers load, the rest of the board kept.
4. **Macros**, if the board has them and the file holds some: **another count refuses the whole
   file** -- after the keymap checks, before anything is written -- else all are written.
5. **The keymap, every layer whole**, with the bulk buffer command.
6. **Encoders**: every entry, both directions, unchecked against the board's knobs or layers.

## For Nazg

Chosen by Rico, 2026-10-18:

- **One *Load from file…*** for both formats, told apart by their content -- nothing new in the
  board menu.
- **Loading only**: Nazg keeps saving `.vil`, which holds more.
- **Another keyboard's file is asked about**, as a `.vil` is, not refused as VIA does; loaded anyway,
  what does not fit the board is left out and said.

And as follows from the format:

- **A reader only**. Everything the file holds
  already has an adapter -- the keymap, knobs and macros are what `.vil` restores (vil-files.md,
  "For Nazg") -- so it reads into the same model and the same review: no protocol code.
- **VIA's tables as data**, as vial-gui's are (`adapters/vial/NazgVialKeycodeNameTable.cpp`): one
  row per name, its value in each distinct table -- pre-renumbering (`default`; `v10` differs only in
  `TO`), `v11`, `v12` / `v13` -- with each table's ranges. Redone by hand when VIA changes them.
- **Which table**: on a VIA board, its VIA protocol's, as VIA does. A Vial board always reports
  VIA 9, which would mean `default`, before the renumbering -- wrong for every vial-qmk build since
  the renumbering -- so a Vial board's comes from its keycode version instead.
- **Hex is read, not dropped**: a hex string is the raw value of the board's numbering, where VIA
  loses it. A name no table knows is left out and said, as `.vil`'s are.
- **The matrix from the board**: a layer's length must be the board's `rows * cols` to place its
  keys; otherwise the keymap is left out and said, not guessed.
