# Vial's saved layouts — the `.vil` file

*Compiled 2026-10-18 from vial-gui (`../vial-gui`, shallow clone of 2026-10-03): `save_layout()`
and `restore_layout()` in `protocol/keyboard_comm.py`, the per-feature `save()` / `restore()`
methods beside them, `keycodes/keycodes.py` and `any_keycode.py`. Implementation level, like
via-vial-commands.md. No `.vil` file written by a real board has been read yet.*

A `.vil` is what Vial's **File → Save current layout…** writes and **Load saved layout…** reads:
everything a user can edit on the board, so that it survives a reflash or moves to a second unit
of the same model. vial-gui's firmware flasher makes one in memory before flashing and restores
it afterwards (via-registry.md, "The Vial keyboard UID").

## The format

**Plain UTF-8 JSON, one object**, written by `json.dumps` — no compression, no checksum, no schema.
`version` has been `1` since the format existed: every section after `layout` was added later
without bumping it, and restore reads those with a default (`data.get("tap_dance", [])`), so an old
file still loads.

| Key | Holds |
|---|---|
| `version` | `1` |
| `uid` | the Vial keyboard UID (`FE 00`), a JSON integer -- 64 bits, so past 2^53 a JavaScript reader loses digits. **`-1` on a VIA board** (vial-gui opens VIA boards too, "VIA stack") |
| `layout` | `[layer][row][col]`, the **whole matrix**, a keycode per cell; the integer `-1` for a cell vial-gui did not read (no key of the definition on it) |
| `encoder_layout` | `[layer][encoder]` -> `[ccw, cw]` -- **counter-clockwise first**: direction 0 then 1, as `vial_get_encoder` answers (vial-gui's code names index 0 `cw`, wrongly) |
| `layout_options` | `id_layout_options`' raw integer; `-1` when vial-gui did not read it (a definition without layout labels) |
| `macro` | one list per macro slot, each a list of actions `[tag, ...]` (below) |
| `vial_protocol`, `via_protocol` | the board's when saved (`-1` / VIA's on a VIA board) -- **written, never read back** |
| `tap_dance` | per slot `[on_tap, on_hold, on_double_tap, on_tap_hold, tapping_term]` -- four keycodes and ms |
| `combo` | per slot `[input1, input2, input3, input4, output]` |
| `key_override` | per slot `{"trigger", "replacement", "layers", "trigger_mods", "negative_mod_mask", "suppressed_mods", "options"}` -- two keycodes, then the integers of `vial_key_override_entry_t` (via-vial-commands.md, sub-ops) |
| `alt_repeat_key` | per slot `{"keycode", "alt_keycode", "allowed_mods", "options"}` |
| `settings` | QMK settings, `{"<qsid>": value}` -- the key a **string** (JSON object keys), the value an integer at the setting's width, booleans as 0 / 1 |

Macro actions: `["text", "hello"]`, `["delay", 100]` (ms), `["tap", kc, kc, ...]`, `["down", kc, ...]`,
`["up", kc, ...]` -- a tap / down / up action holds **a sequence** of keycodes, one step each.
Unknown tags are skipped on restore.

**Not in it**: lighting, a board's own menus, audio, the definition, the matrix size, the layer
count. The file does not say how many rows or columns its `layout` has except by its shape.

## Keycodes are names

Every keycode goes through vial-gui's `Keycode.serialize()`:

- **The name from Vial's own table** when there is one: `"KC_A"`, `"KC_TRNS"`, `"MO(1)"`, `"TD(3)"`.
- **A masked keycode as outer(inner)**: `"LSFT(KC_5)"`, `"LCTL_T(KC_A)"`, `"LT1(KC_SPC)"` -- only
  for the combinations Vial's table lists (one modifier set per entry, the 16 `LT<n>` keys).
- **Otherwise a hex string**, `"0x7e05"`: any mod-tap with an unlisted modifier set, a `LM`, a
  value past Vial's table.

`restore` reads each with `Keycode.deserialize()` against **the board it is loading onto**: a name
through the table, then `AnyKeycode` (the function forms: `LT(1,KC_A)`, `MT(MOD_LCTL|MOD_LSFT,KC_A)`,
`OSM(...)`, `C(S(KC_A))`..., and numbers), and a plain integer as is. What neither reads becomes `0`
-- `KC_NO`, silently.

**Vial's names are its own, not QMK's** (compared 2026-10-18, scratch script against Nazg's table):
of the ~570 names vial-gui writes, some 480 differ from QMK's current short names -- `KC_ENTER`
for `KC_ENT`, `KC_BSPACE`, `KC_LCTRL`, `RGB_TOG` for `UG_TOGG`, `KC_GESC` for `QK_GESC`, `HPT_TOG`
for `HF_TOGG`, `KC_MS_U` for `MS_UP` -- and `AnyKeycode` knows only Vial's names and their aliases,
**not QMK's short ones**: a file spelled `KC_ENT` loads in Vial as nothing. So a writer must use
Vial's names, and a reader must know them. Vial-only forms besides:

| Vial writes | QMK's name | Nazg's model |
|---|---|---|
| `M0` ... `M<n>` | `MC_0`, `QK_MACRO_0` | `MacroKey` |
| `USER00` ... `USER15` (the board's `customKeycodes` names are accepted as aliases) | `QK_KB_0` ... -- **`QK_KB`, not `QK_USER`**: `USER00` is `0x7E00` | the keyboard keycode |
| `LT0(KC_A)` ... `LT15(...)` | `LT(0,KC_A)` | `LayerTapKey` -- Vial also reads `LT(0,KC_A)` |
| `FN_MO13`, `FN_MO23` | `0x7C77` / `0x7C78`, QMK's `QK_TRI_LAYER_LOWER` / `UPPER` (`0x5F10` / `0x5F11` in Vial's v5 table) | named |

**vial-gui reads a VIA board with its v5 table** -- `Keycode.protocol` is the board's Vial
protocol, `-1` on a VIA board, and every protocol but 6 means v5 -- **and opens only VIA protocol 9
boards** (`SUPPORTED_VIA_PROTOCOL = [-1, 9]`, `keyboard_comm.py`), all from before the renumbering:
v5 is right for every VIA board it can open, so this is no bug. It does mean a VIA file from
vial-gui is numbered as VIA 9's keycodes, and that names chosen by meaning for a later VIA board
would be read wrong. Nazg writes a VIA board's keycodes as raw values.

**In Nazg**: `adapters/vial/NazgVialKeycodeNameTable.cpp` holds vial-gui's names with their v5 and
v6 values, taken as data from `keycodes/keycodes_v5.py`, `keycodes_v6.py` and the names
`keycodes.py` gives keycodes. To redo it by hand when vial-gui changes them: read both tables'
`kc` dictionaries, add the `LT0(kc)`..`LT15(kc)` and `USER00`..`USER63` their loops make, keep the
names `keycodes.py` gives a keycode (`K(...)`, `Keycode(...)`) as Key rows, the `(kc)` masks as
Masked rows and the bases `M`, `TD` and the layer keys count from as Base rows, sorted by name.
v5's `TO(n)` sets QMK's old `ON_PRESS` bit, `0x10`; a value past 16 bits marks a keycode v5 lacks.

**The renumbering trap.** Names carry across keycode versions -- a file saved on a pre-renumbering
firmware (`vial_protocol` < 6, Vial's v5 table) loads by name onto a renumbered one. **A hex string
does not**: it is replayed as the raw value of the target board's version. vial-gui never compares
the file's `vial_protocol` with the board's. A reader that knows both versions -- Nazg has every
one, `qmk::KeycodeVersion` -- can decode a hex value with the *file's* version and re-encode it
with the board's.

## How vial-gui restores one

`keymap_editor.restore_layout()` first compares `uid` with the board's: another UID asks *"Saved
keymap belongs to a different keyboard, are you sure you want to continue?"* -- **Yes loads it
anyway**. Then `keyboard_comm.restore_layout()` writes, one command per value, in this order:

1. **Keymap** -- only the cells vial-gui read (`(l, r, c) in self.layout`); a file with more layers,
   rows or columns has the extra skipped. `set_key()` sends only cells that **differ** from what
   was read. A file from a board with *another matrix width* is not detected: `[layer][row][col]`
   is indexed as is, so its keys land in other cells.
2. **Encoders** -- every entry of the file, **no bounds check** against the board's layers or
   knobs, unlike the keymap. Also diffed.
3. **Layout options** -- one write, if different.
4. **Macros** -- padded or cut to the board's slot count, re-serialised for the board's Vial
   protocol (`SS_QMK_PREFIX` from protocol 2 on), then **cut to the buffer's size** -- a file
   too long for the board loses its last macros' tail silently (`# TODO: log a warning`). Written
   whole, if it differs.
5. **Tap dance, combos, key overrides, alt repeat keys** -- slot by slot, up to the board's count;
   extra slots skipped.
6. **QMK settings** -- each qsid vial-gui's settings table knows; a qsid the board lacks is still
   sent (and echoed, unsupported).

The lock: steps 4-6 go through `Unlocker.unlock()`, which asks for the unlock combo on a locked
board; the keymap and encoders do not need it (via-vial-commands.md, "What the lock gates"). The
flasher skips the combo by enabling insecure mode on the new firmware's first boot.

Nothing is read back, and nothing is undone when a write fails half way: a cancelled unlock leaves
the keymap restored and the macros not.

## For Nazg

- **Every value has an adapter already**: the keymap, the encoder map and layout options are in
  `Keyboard`; macros (`via::Protocol::ReadMacros` / `WriteMacros`), tap dance, combos, key overrides,
  alt repeat keys (`vial::Read*`, `Set*`) and QMK settings (`GetQmkSetting` at its width,
  `SetQmkSetting`) are what the sections use. A `.vil` adds a reader, a writer and the name table
  above -- no protocol code.
- **On a VIA board** the file carries the keymap, encoders, layout options and macros, `uid: -1`:
  what vial-gui writes for one. VIA's own JSON backup is another format.
- **Nazg adds one key of its own**, `"nazg": {"vendor_id", "product_id", "keycode_version"}` -- the
  board's VID:PID (Rico, 2026-10-18), so a VIA board's file (`uid: -1`) can say which board it came
  from, and the keycode version its raw values are numbered in. vial-gui reads only the keys it
  knows, so Nazg's files still load there.
- **Reading a raw value**: with Nazg's `keycode_version`, else the keycodes of the file's Vial
  protocol, else of its VIA protocol (vial-gui's VIA files are VIA 9's), else the board's own. A value of an older numbering that the
  codec cannot place is not replayed: it is said, and the board keeps its own.
- **Matrix shape is the import's job**: compare the file's `layout` extents and slot counts with
  the board's before writing anything, rather than Vial's silent misplacement.
