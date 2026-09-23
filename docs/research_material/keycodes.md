# Keycodes — versions, coverage, and a protocol-neutral representation

*Compiled 2026-09-23, before any keycode code was written. Implementation level, like
[via-vial-commands.md](via-vial-commands.md).*

Covers what a 16-bit keycode read off a VIA or Vial board means, how that meaning has changed
across QMK versions, how much of it the VIA and Vial clients actually know, and how Nazg should
represent keycodes so that a later non-QMK backend (ZMK Studio) is not blocked by the choice.

All figures below were measured, not read from documentation: QMK's keycode spec was merged per
version by a script reproducing QMK's own merge rules, then compared version to version and
against the VIA app's and vial-gui's tables. Coverage counts fixed keycodes only — parameterised
ranges such as `MO(n)` or `LT(l, kc)` are excluded on both sides.

---

## QMK's keycode spec

QMK has described its keycodes as data since the DD keycode migration
([#18643](https://github.com/qmk/qmk_firmware/pull/18643), 2022-11). The source lives in
`data/constants/keycodes/`:

- `keycodes_<ver>.hjson` — the base file of a version: mostly `ranges`
- `keycodes_<ver>_<fragment>.hjson` — per-group fragments (`basic`, `quantum`, `lighting`,
  `midi`, `steno`, …) holding `keycodes`, keyed by value (`"0x7C40": {group, key, label, aliases}`)
- `extras/keycodes_<language>_<ver>.hjson` — keymap-language aliases (`FR_A`, `DE_Z`, …),
  separate from the core spec

**Each version is a delta, not a snapshot.** `load_spec()` in `lib/python/qmk/keycodes.py`
collects every file up to the requested version, merges each fragment type in version order,
then deep-updates the results together. Inside a fragment, `"!delete!"` removes an entry and a
`"!reset!"` key clears the accumulated dictionary (`0.0.2` resets `magic`, `midi` and
`sequencer` wholesale).

The files are real HJSON, not JSON with comments — `keycodes_0.0.4_lighting.hjson` omits a
comma and relies on the newline, and hex keys appear as both `0x` and `0X`. **Do not parse the
raw files.** QMK's API serves every version already merged, as plain JSON:

```
https://keyboards.qmk.fm/v1/constants/keycodes_0.0.9.json     (≈ 74 KB)
```

That is the generator's input: nlohmann parses it directly and no HJSON parser is needed.

`quantum/keycodes.h` carries `QMK_KEYCODES_VERSION` / `QMK_KEYCODES_VERSION_BCD` only since
2025-05 ([#25219](https://github.com/qmk/qmk_firmware/pull/25219)).

## Version history

Dates are the merge into `develop`.

| Version | Date | Fixed keycodes | Change |
|---|---|---|---|
| `0.0.1` | 2022-11 | 628 | The renumbering itself. Same commit as VIA protocol 11 |
| `0.0.2` | 2023-01 | 695 | **141 MIDI keycodes moved** down by 13. Magic keys renamed (`MAGIC_*` → `QK_MAGIC_*`), `SAFE_RANGE` → `QK_KB_0`, 32 `QK_KB_*` and 32 `QK_USER_*` added, swap hands and sequencer normalised |
| `0.0.3` | 2023-05 | 697 | `QK_REPEAT_KEY`, `QK_ALT_REPEAT_KEY` |
| `0.0.4` | 2024-04 | 719 | `RGB_*` renamed `QK_UNDERGLOW_*` (`UG_*`) at the same values; RGB Matrix (`RM_*`) and LED Matrix (`LM_*`) keycodes added |
| `0.0.5` | 2024-07 | 719 | Mouse keys renamed `KC_MS_*` → `QK_MOUSE_*` (`MS_*`) at the same values |
| `0.0.6` | 2024-08 | 732 | **Output keycodes moved** `0x7C20..` → `0x7780..`, new `QK_CONNECTION` range; `QK_LAYER_LOCK`, `QK_PERSISTENT_DEF_LAYER` range |
| `0.0.7` | 2025-02 | 732 | `QK_COMMUNITY_MODULE` range only |
| `0.0.8` | 2025-09 | 736 | RGB/LED Matrix flag next/previous |
| `0.0.9` | 2026-06 | 815 | **Steno rebuilt**: range widened to `0x74C0/0x005F`, 83 keycodes added, `QK_STENO_BOLT` / `GEMINI` / `COMB` / `COMB_MAX` removed |

### Traps in that history

1. **The same value means different things in different versions.** Three times, not once:
   - `0x7110` is `MI_C` (octave 0) in `0.0.1` and `MI_Cs1` from `0.0.2` — 99 MIDI values were
     reused by a different note.
   - `0x7C20` is `QK_OUTPUT_AUTO` up to `0.0.5` and unassigned from `0.0.6`.
   - `0x74F0` is `QK_STENO_BOLT` in `0.0.8` and `QK_STENO_X7` in `0.0.9`.

   A dictionary keyed on value alone is wrong for some board whatever version it picks.

2. **Names are not stable either, and the JSON forgets the old ones.** The renames in `0.0.2`,
   `0.0.4`, `0.0.5` and `0.0.9` do *not* keep the old name as an alias in the spec: `RGB_TOG`,
   `KC_MS_UP`, `MAGIC_SWAP_LALT_LGUI` and `QK_STENO_BOLT` resolve to nothing in `0.0.9`. The
   old names survive only as C macros (`quantum/quantum_keycodes_legacy.h`). A keymap saved as
   names breaks just as a keymap saved as numbers does.

3. **Nothing before `0.0.1` is in the spec.** Protocol ≤ 10 boards use the pre-renumbering
   values, which exist only in QMK's history — `quantum/keycode.h`,
   `quantum/quantum_keycodes.h` and `quantum/via_ensure_keycode.h` at the parent of
   `a69ab05dd6` (#18643). That table has to be extracted from there once and checked in; it
   will never change again.

4. **Ranges overlap.** `QK_UNICODE` is `0x8000/0x7FFF`, which covers both `QK_UNICODEMAP`
   (`0x8000/0x3FFF`) and `QK_UNICODEMAP_PAIR` (`0xC000/0x3FFF`). The three features are
   mutually exclusive in firmware, so the range alone cannot decode a value ≥ `0x8000` — the
   board's feature set has to pick. Absent any feature information, show it raw.

## Picking the dictionary for a board

| Board reports | Keycode version | How known |
|---|---|---|
| VIA protocol ≥ 13 | exact | `id_keycodes_version` (`0x02 0x06`) returns `QMK_KEYCODES_VERSION_BCD` |
| VIA protocol 12 | `0.0.2` … `0.0.8` | Protocol 12 landed 2023-02 after `0.0.2`; protocol 13 landed 2026-04 before `0.0.9` |
| VIA protocol 11 | `0.0.1`, possibly `0.0.2` | Came in with `0.0.1`; `0.0.2` reached `develop` three weeks before protocol 12. Whether a release ever paired 11 with `0.0.2` is unverified |
| VIA protocol ≤ 10 | pre-renumbering | Trap 3 |
| **Vial** protocol 6 | post-renumbering, `0.0.7` today | vial-gui's `v6` table |
| **Vial** protocol ≤ 5 | pre-renumbering | vial-gui's `v5` table |

**Never select a Vial board's dictionary from its VIA protocol version.** vial-qmk hard-codes
`VIA_PROTOCOL_VERSION 0x0009` in `quantum/via.h` regardless of its QMK base — the Model F
reports VIA 9 while using post-renumbering keycodes. Only the Vial protocol means anything
there. vial-qmk last merged QMK on 2025-03-22, so its tree is at `0.0.7`, and it predates
`QMK_KEYCODES_VERSION` entirely.

Both dogfood boards are therefore **post-renumbering**: the Aquanaut (VIA 12) is somewhere in
`0.0.2`–`0.0.8`, the Model F (Vial 6) at `0.0.7`. Neither exercises the pre-renumbering table.

Where the version is a span, the remaining ambiguity is small: within `0.0.2`–`0.0.8` the only
value that moved is the output group at `0.0.6`. Default to the newest version in the span and
let the user override it.

## What VIA and Vial actually cover

Measured against QMK `0.0.9`'s 815 fixed keycodes:

| Client table | Names | Values covered | Largest gaps |
|---|---|---|---|
| VIA app, `key-to-byte/v12.ts` + `v13.ts` | 485 | **429 / 815** | steno 83, MIDI 42, `QK_KB_*` 32, `QK_USER_*` 32, joystick 32, macros 32, programmable buttons 32, 32 quantum (unicode mode, leader, key lock, one-shot / key override / autocorrect toggles, secure, dynamic tapping term, Caps Word, Repeat Key, Layer Lock), magic 15, connection 15, LED Matrix 11, sequencer 9, swap hands 7 |
| vial-gui, `keycodes/keycodes_v6.py` | 507 | **453 / 815** | steno 83, joystick 32, programmable buttons 32, `QK_USER_*` 32, 27 basic (`KC_MENU`, `KC_KB_POWER`, `KC_INT6`–`9`, …), 29 quantum, connection 15, LED Matrix 11, sequencer 9, swap hands 7 |

Some gaps are covered by other means — both clients expose macros through their own editor, and
VIA definitions can declare `customKeycodes` for the `QK_KB_*` range — but most are simply
absent. Both tables are hand-maintained, so they lag QMK by design.

**A value in the table is not a key in the picker.** VIA's `v12.ts` *does* map the haptic
keycodes (`HPT_ON` … `HPT_DWLD`, `0x7C40`–`0x7C4C`, under their pre-`0.0.2` names), but
`src/utils/key.ts`, which builds the picker menus, never lists them — so a VIA user types the
value by hand even though the app could decode it. vial-gui does list them. The codec and the
picker are two different artefacts with independent gaps; Nazg keeps them separate on purpose
(below).

VIA's own versioning is shallow too: `getBasicKeyDict()` throws if a protocol-13 board did not
report a keycodes version, then returns the single `v13` table regardless of its value.
vial-gui keys its two tables on the Vial protocol alone.

## Keycodes beyond QMK

ZMK has no keycodes in QMK's sense. A ZMK binding is `{behavior_id, param1, param2}`, where
`behavior_id` is a device-local handle for a behaviour the device enumerates at runtime
(`&kp`, `&mo`, `&mt`, `&bt`, …) and the parameters are typed by that behaviour's metadata
(see [zmk-studio.md](zmk-studio.md)). There is no fixed numbering to translate to.

What QMK and ZMK genuinely share:

| QMK | ZMK | Notes |
|---|---|---|
| `0x04`–`0xA4` basic, `0xE0`–`0xE7` modifiers | `&kp` + HID keyboard page (`0x07`) usage | Values are the HID usages themselves |
| `0xA5`–`0xA7` system, `0xA8`–`0xC2` media | `&kp` + generic desktop / consumer page usage | QMK packs them into the basic range; the translation is a small fixed table |
| `0xCD`–`0xDF` mouse | mouse behaviours | Different model on each side |
| `QK_MODS` (`LCTL(kc)` …) | modifier bits in the `&kp` usage | Both encode "key + held modifiers" |
| `KC_NO` / `KC_TRNS` | `&none` / `&trans` | |
| `MO` `TG` `TO` `DF` `OSL` `TT` | `&mo` `&tog` `&to` … | Layer operations, parameter = layer |
| `MT`, `LT`, `OSM` | `&mt`, `&lt`, `&sk` | Hold-tap and sticky variants |

Everything else is one-sided. Haptic, steno, MIDI, magic, sequencer and unicode have no ZMK
behaviour; `&bt BT_SEL 0`, `&out` and `&ext_power` have no QMK keycode (QMK's connection group
is the closest). A universal integer that both sides translate into would carry the union of
both vocabularies and then refuse half of it on either backend — it adds a mapping without
removing any case.

## Recommendation: a structured keycode, one codec per version

**Nazg's keymap holds a structured value, never a `uint16_t`.** Roughly:

```
Keycode = Basic      { usage, mods }        // HID usage + held modifiers
        | Layer      { op, layer }          // MO TG TO DF OSL TT PDF
        | ModTap     { mods, key }
        | LayerTap   { layer, key }
        | OneShotMod { mods }
        | Macro      { index }
        | TapDance   { index }
        | Named      { id }                 // any other fixed keycode: haptic, steno, magic, …
        | Unknown    { raw }                // shown and edited as hex
```

- **The QMK codec** is generated per keycode version: `decode(u16) -> Keycode` and
  `encode(Keycode) -> optional<u16>`. `nullopt` means "this firmware cannot store that", which
  is exactly what the picker needs to grey an entry out. It lives under the protocol layer; the
  capability model and the UI never see a raw value except inside `Unknown`.
- **`Named{id}` needs an identity that survives renames.** Use the newest QMK canonical name,
  and let the generator chain history: across consecutive versions, *same value, new name* is a
  rename and *same name, new value* is a move — both keep the identity. So `RGB_TOG` on a
  `0.0.1` board and `UG_TOGG` on a `0.0.9` board are one keycode, and `0x7110` decodes to two
  different identities on either side of `0.0.2`.
- **The picker is a view over the codec**, grouped by QMK's own `group` field, listing every
  keycode the board's version can encode. Coverage is then QMK's, not a hand-curated subset,
  and hex entry is the escape hatch rather than the workflow.
- **A saved keymap is portable** — stored as structured keycodes, it re-encodes onto another
  board or another keycode version, failing per key rather than silently changing meaning.
  This is the immediate benefit, long before any second protocol exists.
- **ZMK later is another codec**, mapping `Basic` / `Layer` / `ModTap` / `LayerTap` /
  `OneShotMod` onto behaviours the device enumerated, and adding its own variants (Bluetooth,
  output selection) when that backend is built. Nothing about it needs designing now; the only
  decision taken today is that the model and UI hold `Keycode`.

### Open questions for the implementation

- **Where the generator runs.** Offline with committed output, or at build time from vendored
  JSON. Either way the input is the nine merged JSON files from QMK's API plus the one-off
  pre-renumbering table.
- **Labels.** The spec's `label` is present for most keycodes but not all; the picker needs a
  short label for every key cap, so some table of overrides is likely.
- **Keymap-language extras** (`extras/`) — whether to render `FR_A` on a French layout. Not
  needed for the first cut.
- **Feature availability.** Vial reports some supported features (`caps_word`, `repeat_key`,
  `layer_lock`, `persistent_default_layer`, …); VIA reports none. The picker can use them to
  hide rather than merely grey out, where known.

## Sources

- [qmk_firmware/data/constants/keycodes/](https://github.com/qmk/qmk_firmware/tree/master/data/constants/keycodes) — the spec; history dated from its git log
- [qmk_firmware/lib/python/qmk/keycodes.py](https://github.com/qmk/qmk_firmware/blob/master/lib/python/qmk/keycodes.py) and `json_schema.py` — `load_spec()`, `merge_ordered_dicts()`, `deep_update()`
- [qmk_firmware/quantum/keycodes.h](https://github.com/qmk/qmk_firmware/blob/master/quantum/keycodes.h) — `QMK_KEYCODES_VERSION*`
- `https://keyboards.qmk.fm/v1/constants/keycodes_<ver>.json` — merged spec per version
- [vial-qmk/quantum/via.h](https://github.com/vial-kb/vial-qmk/blob/vial/quantum/via.h) — `VIA_PROTOCOL_VERSION 0x0009`
- [the-via/app src/utils/key-to-byte/](https://github.com/the-via/app/tree/main/src/utils/key-to-byte) — `dictionary-store.ts`, `v10`–`v13`; [src/utils/key.ts](https://github.com/the-via/app/blob/main/src/utils/key.ts) — picker menus
- [vial-gui src/main/python/keycodes/](https://github.com/vial-kb/vial-gui/tree/main/src/main/python/keycodes) — `keycodes.py`, `keycodes_v5.py`, `keycodes_v6.py`
