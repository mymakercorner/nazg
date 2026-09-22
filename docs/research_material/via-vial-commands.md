# VIA & VIAL — Command Reference and Protocol History

*Compiled 2026-09-22 by reading firmware source at specific commits: QMK `master`
(`quantum/via.h`, `quantum/via.c`) and vial-qmk `vial` (`quantum/vial.h`, `quantum/vial.c`,
and its forked `quantum/via.h`, `quantum/via.c`). The version histories come from the commit
history of those files, with each historical revision fetched and diffed rather than inferred
from release notes. Where a version number maps to a client-visible feature, that mapping is
taken from vial-gui's own `protocol/constants.py` — read as a statement of fact about version
numbers, not as code to port (see [CLAUDE.md](../../CLAUDE.md) on re-implementing from the
wire format).*

This is the implementation-level companion to [via.md](via.md) and [vial.md](vial.md), which
cover the strategic picture. Nothing here repeats them.

---

## The shape of the protocol

Both protocols are **strict request/response over 32-byte raw HID reports**. The host writes
one 32-byte report; the firmware **echoes the same buffer back**, with fields overwritten in
place. There is no length field, no sequence number, no token: correlation is by discipline
(one outstanding request at a time), which is why the survey puts correlation in the protocol
layer and not the transport.

```
byte  0       1       2       3      ...     31
     [cmd ] [ ---------- command data ---------- ]
```

**Error signalling is minimal and easy to miss.** There is no status code. When the firmware
does not recognise a command — or recognises the command but not its sub-id — it sets
**`data[0] = 0xFF` (`id_unhandled`)** and echoes the rest unchanged. A client must compare the
first byte of the reply against the command it sent. A reply whose first byte still matches
is a success; `0xFF` is the only failure indication the wire carries.

Commands compiled out of the firmware behave identically: `id_eeprom_reset` is only present
when `VIA_EEPROM_ALLOW_RESET` is defined, the encoder commands only under
`ENCODER_MAP_ENABLE`, and each lighting channel only when its feature is enabled. **Absence is
indistinguishable from an unknown command**, so capability detection means probing and
watching for `0xFF`.

`id_bootloader_jump` (0x0B) deserves a note: it is still in mainline's enum but **mainline
VIA no longer implements it** — there is no case for it in `via.c`, so it returns `0xFF`.
Vial kept it, gated behind unlock and excluded from `VIAL_INSECURE` builds.

### Vial commands do NOT use the 0xFF marker

*Verified on hardware 2026-09-22.* The `0xFF` convention above covers VIA commands only.
A Vial sub-command the firmware was built without falls through its `switch` **without
touching the buffer**, so the reply is the request, echoed back byte for byte. On a board
compiled without `ENCODER_MAP_ENABLE`, `vial_get_encoder` "returned" a keycode of `0xFE03` —
which is the prefix and sub-command being read back as data.

So the two halves of the same device announce a missing feature in two different ways:

| | Missing feature answers with |
|---|---|
| VIA command | `0xFF` in byte 0 |
| Vial sub-command | the request, unchanged |

The catch is that commands which legitimately return nothing — `vial_lock`,
`vial_unlock_start`, `vial_qmk_settings_reset` — also echo the request, so "reply equals
request" means *absent* only for a command that was supposed to answer with data.

### Stale reports desynchronise everything after them

*Found on hardware 2026-09-22.* Because correlation is by discipline rather than by token,
**one unread report left in the device's input queue shifts every reply by one command** for
the rest of the session. A fresh process opened a board and got the *previous* run's last
reply: it asked for `0x01` and was handed `0x12`.

A client must therefore **drain the input queue immediately after opening** — read with a
zero timeout until it comes back empty. Anything queued at that moment predates the first
request and cannot be an answer to it. This is cheap, and without it the failure mode is
baffling: every command appears to return the wrong thing, one step out of phase.

---

## VIA — command set

Mainline QMK, protocol `0x000D`. Offsets are absolute byte positions in the 32-byte report.
Multi-byte integers are **big-endian** unless stated.

| ID | Name | Request | Response |
|---|---|---|---|
| `0x01` | `id_get_protocol_version` | — | `[1..2]` version |
| `0x02` | `id_get_keyboard_value` | `[1]` value id, see below | see below |
| `0x03` | `id_set_keyboard_value` | `[1]` value id, `[2..]` value | echo |
| `0x04` | `id_dynamic_keymap_get_keycode` | `[1]` layer, `[2]` row, `[3]` col | `[4..5]` keycode |
| `0x05` | `id_dynamic_keymap_set_keycode` | `[1]` layer, `[2]` row, `[3]` col, `[4..5]` keycode | echo |
| `0x06` | `id_dynamic_keymap_reset` | — | echo |
| `0x07` | `id_custom_set_value` | `[1]` channel, `[2]` value id, `[3..]` value | echo |
| `0x08` | `id_custom_get_value` | `[1]` channel, `[2]` value id | `[3..]` value |
| `0x09` | `id_custom_save` | `[1]` channel | echo |
| `0x0A` | `id_eeprom_reset` | — | echo *(only with `VIA_EEPROM_ALLOW_RESET`)* |
| `0x0B` | `id_bootloader_jump` | — | **`0xFF` on mainline** — no handler |
| `0x0C` | `id_dynamic_keymap_macro_get_count` | — | `[1]` count |
| `0x0D` | `id_dynamic_keymap_macro_get_buffer_size` | — | `[1..2]` size |
| `0x0E` | `id_dynamic_keymap_macro_get_buffer` | `[1..2]` offset, `[3]` size ≤ 28 | `[4..]` data |
| `0x0F` | `id_dynamic_keymap_macro_set_buffer` | `[1..2]` offset, `[3]` size ≤ 28, `[4..]` data | echo |
| `0x10` | `id_dynamic_keymap_macro_reset` | — | echo |
| `0x11` | `id_dynamic_keymap_get_layer_count` | — | `[1]` count |
| `0x12` | `id_dynamic_keymap_get_buffer` | `[1..2]` offset, `[3]` size ≤ 28 | `[4..]` data |
| `0x13` | `id_dynamic_keymap_set_buffer` | `[1..2]` offset, `[3]` size ≤ 28, `[4..]` data | echo |
| `0x14` | `id_dynamic_keymap_get_encoder` | `[1]` layer, `[2]` index, `[3]` clockwise | `[4..5]` keycode |
| `0x15` | `id_dynamic_keymap_set_encoder` | `[1]` layer, `[2]` index, `[3]` clockwise, `[4..5]` keycode | echo |
| `0xFF` | `id_unhandled` | — | the failure marker described above |

**28 bytes is the buffer chunk size** for every `*_buffer` command — three header bytes after
the command id leave 28 of the 32. A 4-layer, 100-key keymap is 800 bytes, so about 29 round
trips; this is the batching that exists because per-key requests were too chatty.

### `id_get_keyboard_value` / `id_set_keyboard_value` sub-ids

| ID | Name | Get | Set |
|---|---|---|---|
| `0x01` | `id_uptime` | `[2..5]` ms since boot | — |
| `0x02` | `id_layout_options` | `[2..5]` u32 | `[2..5]` u32 |
| `0x03` | `id_switch_matrix_state` | `[2]` row offset → `[3..]` packed row bitfields | — |
| `0x04` | `id_firmware_version` | `[2..5]` `VIA_FIRMWARE_VERSION` | — |
| `0x05` | `id_device_indication` | — | `[2]` u8 |
| `0x06` | `id_keycodes_version` | `[2..5]` `QMK_KEYCODES_VERSION_BCD` | — *(protocol 13)* |

### How `id_layout_options` packs its value

*Verified on hardware 2026-09-22.* The u32 is a bit field, not an index. Each layout group
declared in the definition's `layouts.labels` takes as many bits as its option count needs
(one bit for a two-option group, three for a five-option group), and **the first group
occupies the most significant bits**.

A label that is a plain string is an on/off toggle; a label that is an array names the group
in its first element and its options in the rest — `["Enter", "ISO Enter", "ANSI Enter"]` is
one group with two options.

Confirmed against a Model F Labs B104 with six one-bit groups: switching on group 0, "Split
Backspace", made the board report `0x00000020`, which is bit 5. The opposite packing would
have made that same value mean the last group. As a second check, the number of visible keys
rose by exactly one, which is what splitting one key into two does.

`id_switch_matrix_state` is the matrix tester. On mainline it returns **all zeroes** unless
the build defines `VIA_INSECURE`, or defines `SECURE_ENABLE` and is currently unlocked —
QMK's own comment calls the alternative a "wannabe keylogger". Rows per report are
`28 / ceil(MATRIX_COLS / 8)`, so a client pages through the matrix by row offset.

### Custom channels (`id_custom_*`)

| Channel | Name | Value ids |
|---|---|---|
| `0` | `id_custom_channel` | keyboard-defined, declared by the definition's `menus` |
| `1` | `id_qmk_backlight_channel` | `1` brightness, `2` effect |
| `2` | `id_qmk_rgblight_channel` | `1` brightness, `2` effect, `3` speed, `4` colour (hue, sat) |
| `3` | `id_qmk_rgb_matrix_channel` | `1` brightness, `2` effect, `3` speed, `4` colour |
| `4` | `id_qmk_audio_channel` | `1` enable, `2` clicky enable |
| `5` | `id_qmk_led_matrix_channel` | `1` brightness, `2` effect, `3` speed |

Channel `0` is the extension point: `via_custom_value_command_kb()` in firmware, `menus` in
the definition, and stock VIA renders the widgets. This is tier 1 in
[via.md](via.md#extending-with-custom-features), seen from the wire.

---

## VIA — protocol version history

Each row is a *bump*; rows in between add commands without changing the version.

| Version | Date | PR | What changed |
|---|---|---|---|
| `0x0009` | 2020-01 | [#7268](https://github.com/qmk/qmk_firmware/pull/7268), [#7911](https://github.com/qmk/qmk_firmware/pull/7911) | The version VIA entered mainline QMK with. Keymap, macros, layers, layout options, the buffer commands, and lighting under the original `id_lighting_*` names |
| `0x000A` | 2022-07 | [#17734](https://github.com/qmk/qmk_firmware/pull/17734) | **Encoder map**: added `id_dynamic_keymap_get_encoder` / `set_encoder` (0x14/0x15) |
| `0x000B` | 2022-11 | [#18643](https://github.com/qmk/qmk_firmware/pull/18643) | **Keycode renumbering** ("DD keycode migration"). No new commands — the *meaning* of keycode values changed |
| — | 2022-11 | [#18222](https://github.com/qmk/qmk_firmware/pull/18222) | **VIA V3, the Custom UI update.** `id_lighting_*` renamed to `id_custom_*` at the same ids, channels introduced, RGB matrix and audio channels added, and the backlight/RGB light value ids **renumbered** (`0x09,0x0A` → `1,2`; `0x80..0x83` → `1..4`). Landed inside version 11 |
| `0x000C` | 2023-02 | [#19916](https://github.com/qmk/qmk_firmware/pull/19916) | **VIA-specific keycodes removed** so VIA's dictionary can be generated from QMK's own keycode JSON: `MACRO00..15` → `QK_MACRO_*`, `USER00..15` → `QK_KB_*`, `FN_MO13`/`FN_MO23` generalised into tri-layer. More than 16 macros supported. No command changes |
| — | 2023-07 | [#21281](https://github.com/qmk/qmk_firmware/pull/21281) | LED matrix channel (5) added, still version 12 |
| `0x000D` | 2026-04 | [#26001](https://github.com/qmk/qmk_firmware/pull/26001) | **`id_keycodes_version` (0x06)** returns `QMK_KEYCODES_VERSION_BCD`, so a client can pick the right keycode dictionary instead of guessing from the protocol version |

**Versions 1–8 predate mainline.** `quantum/via.h` enters QMK's history already at `0x0009`;
earlier versions lived in Wilba's pre-merge code and are not recoverable from this file's
history. For a client, protocol 9 is the practical floor.

Two observations that matter for a backend:

- **A version bump does not imply new commands, and new commands do not imply a bump.**
  11 and 12 changed only keycode semantics; V3's custom channels and the LED matrix channel
  arrived *without* a bump. Feature detection cannot be driven by the version number alone.
- **The v13 PR description also promises to forward the secure state**, but the merged diff
  touches only `via.c` (+8) and `via.h` (+2/-1) and contains nothing but the keycodes version.
  Treat the secure-state claim as not landed.

---

## VIAL — command set

Vial keeps VIA's command space and adds its own behind a single prefix byte:

```
byte  0       1        2      ...     31
     [0xFE] [sub-cmd] [ ------ data ------ ]
```

`id_vial_prefix = 0xFE`. Responses **overwrite the buffer from byte 0**, unlike VIA's
convention of leaving the command id in place — so the echo check described above does not
apply to Vial commands.

| Sub | Name | Request | Response |
|---|---|---|---|
| `0x00` | `vial_get_keyboard_id` | — | `[0..3]` protocol version **little-endian**, `[4..11]` 8-byte keyboard UID, `[12]` 1 if VialRGB |
| `0x01` | `vial_get_size` | — | `[0..3]` definition size, little-endian |
| `0x02` | `vial_get_def` | `[2..3]` page index, little-endian | 32 bytes of the compressed definition |
| `0x03` | `vial_get_encoder` | `[2]` layer, `[3]` index | `[0..1]` CCW keycode, `[2..3]` CW keycode |
| `0x04` | `vial_set_encoder` | `[2]` layer, `[3]` index, `[4]` clockwise, `[5..6]` keycode | — |
| `0x05` | `vial_get_unlock_status` | — | `[0]` unlocked, `[1]` in progress, `[2..]` (row, col) pairs of the unlock combo; buffer pre-filled `0xFF` |
| `0x06` | `vial_unlock_start` | — | — |
| `0x07` | `vial_unlock_poll` | — | `[0]` unlocked, `[1]` in progress, `[2]` countdown |
| `0x08` | `vial_lock` | — | — |
| `0x09` | `vial_qmk_settings_query` | `[2..3]` qsid greater-than, LE | list of supported qsids; all `0xFF` if unsupported |
| `0x0A` | `vial_qmk_settings_get` | `[2..3]` qsid, LE | `[0]` status, `[1..]` value |
| `0x0B` | `vial_qmk_settings_set` | `[2..3]` qsid, `[4..]` value | `[0]` status |
| `0x0C` | `vial_qmk_settings_reset` | — | — |
| `0x0D` | `vial_dynamic_entry_op` | `[2]` sub-op, see below | varies |

### `vial_dynamic_entry_op` sub-ops

| Sub-op | Name | Request | Response |
|---|---|---|---|
| `0x00` | `get_number_of_entries` | — | `[0]` tap dance, `[1]` combo, `[2]` key override, `[3]` alt repeat; **`[31]`** feature bits: bit 0 Caps Word, bit 1 Layer Lock |
| `0x01` / `0x02` | tap dance get / set | `[3]` index (+ `[4..]` entry on set) | `[0]` status, `[1..]` entry |
| `0x03` / `0x04` | combo get / set | as above | as above |
| `0x05` / `0x06` | key override get / set | as above | as above |
| `0x07` / `0x08` | alt repeat key get / set | as above | as above |

Entry structs are transferred **raw, little-endian, exactly as stored in EEPROM**, with
`_Static_assert`s pinning their sizes:

| Entry | Size | Fields |
|---|---|---|
| tap dance | 10 | `on_tap`, `on_hold`, `on_double_tap`, `on_tap_hold`, `custom_tapping_term` (all u16) |
| combo | 10 | `input[4]`, `output` (all u16) |
| key override | 10 | `trigger` u16, `replacement` u16, `layers` u16, `trigger_mods`, `negative_mod_mask`, `suppressed_mods`, `options` (u8) |
| alt repeat key | 6 | `keycode` u16, `alt_keycode` u16, `allowed_mods` u8, `options` u8 |

`options` carries the enable bit in each case (`vial_ko_enabled = 1 << 7`,
`vial_arep_enabled = 1 << 3`), so "is this entry in use" is a flag in the struct, not a
separate query.

### The definition payload

`vial_get_size` then `ceil(size / 32)` calls to `vial_get_def`, page by page. The result is
**LZMA-compressed JSON** — this is what `minlzma` is in the dependency list for.

*Verified on hardware 2026-09-22*, against a Model F Labs B104 Beam Spring (Vial protocol 6):
the payload is **704 bytes** and begins `FD 37 7A 58 5A 00`, which is the **XZ container
magic** — not a raw LZMA-alone stream. minlzma decodes XZ, so it is the right decoder, and
no wrapper is needed. Note how much smaller that is than the "few KB" the survey estimates:
22 pages, about 22 round trips.

### What the lock gates

Vial's unlock is not advisory; the firmware enforces it, and a client must expect failures
rather than prevent them:

- **While an unlock is in progress**, every command is dropped except `vial_get_keyboard_id`,
  `vial_get_size`, `vial_get_def`, `vial_get_unlock_status`, `vial_unlock_start` and
  `vial_unlock_poll`.
- **While locked**: `id_switch_matrix_state` is refused, `id_dynamic_keymap_macro_set_buffer`
  is refused, and `id_bootloader_jump` is refused.
- **A keycode firewall** rewrites `QK_BOOT` to `0` in every keycode a locked board accepts —
  encoders, tap dance, combos, key overrides and alt repeat keys all pass through it. A write
  can therefore *succeed* and still not store what was sent; reading back is the only way to
  be sure.

---

## VIAL — protocol version history

| Version | Date | What it marks |
|---|---|---|
| `0` | 2020-10 → 2020-12 | Initial protocol: keyboard id, definition download, encoders, and the lock/unlock flow |
| `1` | 2021-02 | Seals the December 2020 work (encoder support, the unlock/lock logic inversion). No client feature gates on it |
| `2` | 2021-03 | **Advanced macros** — delays inside VIA macros (`VIAL_PROTOCOL_ADVANCED_MACROS`) |
| `3` | 2021-04 | **Matrix tester**, reintroduced but gated behind unlock (`VIAL_PROTOCOL_MATRIX_TESTER`) |
| `4` | 2021-07 | **QMK settings** (`0x09`–`0x0C`) and **dynamic entries** — tap dance and combos, plus `vial_dynamic_entry_op` itself |
| `5` | 2022-03 | **2-byte macros** (`VIAL_PROTOCOL_EXT_MACROS`) and **key overrides** |
| `6` | 2023-03 | Bumped in the merge of QMK master that brought the protocol-12-era keycode changes. vial-gui accepts it but gates no feature on it — in practice it signals the keycode renumbering |

The bumps are standalone one-line commits that seal features landed just before, so the
version marks "everything up to here", not a single change.

**Alt repeat key (2025) added two commands with no version bump**, which is the clearest
argument for probing: call `get_number_of_entries` and read the count, rather than deducing
support from version 6.

---

## Implications for the Nazg backend

1. **One backend, a Vial branch — confirmed at the wire level.** Vial reuses VIA's keymap,
   macro, layer and buffer commands unchanged. The divergence is the `0xFE` prefix space,
   the definition source, and the encoder commands.

2. **Vial's fork is a hybrid, and its reported VIA version lies about its capabilities.**
   vial-qmk reports `VIA_PROTOCOL_VERSION 0x0009`, but its enum carries both the old
   `id_lighting_*` names *and* the V3 `id_custom_*` names aliased to the same `0x07`–`0x09`,
   plus the full channel enum. **A client that gates custom channels on VIA protocol ≥ 11
   will wrongly skip them on Vial.** Worse, Vial keeps the *old* backlight and RGB light
   value ids (`0x09`, `0x0A`, `0x80`–`0x83`) where mainline ≥ 11 renumbered them to `1`–`4`.
   Same channel, same command, different sub-ids depending on which firmware answered.

3. **Encoders are the other fork in the road.** VIA ≥ 10 uses `0x14`/`0x15` with a `clockwise`
   flag; Vial uses `0xFE 0x03`/`0x04` and returns *both* directions in one reply. vial-qmk
   does not implement `0x14`/`0x15` at all.

4. **Keycode values depend on the protocol version, not just the keycode.** Protocol 11
   renumbered them and 12 removed the VIA-specific ones. Protocol 13 finally exposes
   `id_keycodes_version` so a client can stop guessing — but only boards on 13 answer it, so
   the fallback table is version-keyed anyway. This belongs in the capability model as an
   explicit keycode-dictionary selection, not as a hardcoded table.

5. **Detection order that actually works:** `id_get_protocol_version` first (it is the one
   command guaranteed at every version), then `0xFE 0x00` to see whether Vial answers, then
   `id_keycodes_version` if protocol ≥ 13, then probe each optional feature and treat `0xFF`
   as "absent". The Aquanaut reports protocol 12, so it sits one below current mainline and
   has no `id_keycodes_version`.

6. **`0xFF` is the whole error model for VIA commands, and Vial has a second one.** Both
   deserve the same exception type, mapped to the same path as a transport failure, so a
   coroutine sequence unwinds identically whether the device refused or the cable fell out.
   But the detection differs per half: `0xFF` for VIA, request-echoed-back for Vial.

7. **Drain the input queue on open.** One stale report puts every later reply one command
   out of phase, and the resulting errors point everywhere except the cause.

8. **Write-then-read-back is required for Vial when locked**, because the keycode firewall
   silently substitutes `0`. A write that reports success has not necessarily stored what was
   asked.

---

## Sources

Read at the commits named above:

- [qmk_firmware/quantum/via.h](https://github.com/qmk/qmk_firmware/blob/master/quantum/via.h) — command enums, `VIA_PROTOCOL_VERSION`
- [qmk_firmware/quantum/via.c](https://github.com/qmk/qmk_firmware/blob/master/quantum/via.c) — `raw_hid_receive`, payload layouts, custom channel dispatch
- [vial-qmk/quantum/vial.h](https://github.com/vial-kb/vial-qmk/blob/vial/quantum/vial.h) — Vial command enum, `VIAL_PROTOCOL_VERSION`, entry structs
- [vial-qmk/quantum/vial.c](https://github.com/vial-kb/vial-qmk/blob/vial/quantum/vial.c) — `vial_handle_cmd`, unlock flow, keycode firewall
- [vial-qmk/quantum/via.c](https://github.com/vial-kb/vial-qmk/blob/vial/quantum/via.c) — lock gating of VIA commands
- [vial-gui/src/main/python/protocol/constants.py](https://github.com/vial-kb/vial-gui/blob/main/src/main/python/protocol/constants.py) — version-to-feature constants
- PRs [#17734](https://github.com/qmk/qmk_firmware/pull/17734), [#18222](https://github.com/qmk/qmk_firmware/pull/18222), [#18643](https://github.com/qmk/qmk_firmware/pull/18643), [#19916](https://github.com/qmk/qmk_firmware/pull/19916), [#21281](https://github.com/qmk/qmk_firmware/pull/21281), [#26001](https://github.com/qmk/qmk_firmware/pull/26001)
