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
Vial kept it, gated behind unlock and excluded from `VIAL_INSECURE` builds: it answers, waits
100 ms, then jumps; a locked board echoes it and stays (vial-qmk's `via.c`, read 2026-10-18).

`id_eeprom_reset` (0x0A) is less than its name: `eeconfig_init_via()` puts back the layout options'
default, then calls `dynamic_keymap_reset()` and `dynamic_keymap_macro_reset()` -- what `0x06` and
`0x10` do on every firmware, without the build flag. `dynamic_keymap_reset()` puts back every
layer's keys **and**, with `ENCODER_MAP_ENABLE`, its knobs (QMK's `dynamic_keymap.c`, 2026-10-18).
Neither reset reboots the board, and vial-qmk's lock gates neither.

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

*Found again 2026-10-13, and drained before every request since.* **A firmware can answer one
request twice**, and then the queue grows by one report per request until the host's buffer is
full (hidapi asks Windows for 64). Keychron's Hall-effect boards do it for
`id_switch_matrix_state`: `kc_raw_hid_rx()` sends its own reply from the analog matrix
(`send_analog_matrix()`, `keyboards/keychron/common/keychron_raw_hid.c`) and returns `false`,
so QMK's `via.c` answers the same request again from `matrix_get_row()`. A K10 HE's live test,
asking every 20 ms, ran a second behind. WebHID drops a report nobody waits for, so the VIA app
and Keychron's Launcher never notice; a client reading from a queue must discard what is waiting
before each request. Keychron's `2025q3` branch answers once (`return true`), but boards in the
field run the older `hall_effect_playground` code; its newer builds instead send reports nobody
asked for, on every layer change ([keychron-commands.md](keychron-commands.md), "Unsolicited
reports").

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

**Two reply layouts** (read 2026-09-29 in QMK master and vial-qmk master of 2026-08). Both
send each row as `ceil(MATRIX_COLS / 8)` bytes, most significant first — column 0 is the
row's last byte, bit 0.

| Layout | Who | Request | Rows start at |
|---|---|---|---|
| Paged | mainline VIA, protocol 12 and later | `02 03 <first row>` | byte 3 (byte 2 echoes the first row) |
| Whole | VIA before 12, **every vial-qmk** | `02 03` | byte 2 |

The whole layout is filled only when `(MATRIX_COLS / 8 + 1) * MATRIX_ROWS <= 28` (a
compile-time `#if`); a bigger matrix is answered with the request echoed back, every key
released. The VIA app picks the layout by `protocol >= 12` (`use-matrix-test.ts`); a Vial
board reports VIA protocol 9 whatever it is built from, so it always gets the whole layout,
which is what vial-qmk still sends.

**Keychron** builds every board with `VIA_INSECURE` (`keyboards/keychron/common/keychron_common.mk`
in its fork), so its boards answer the matrix -- seen on a Q6, 2026-10-13. Its Hall-effect boards
answer it twice: "Stale reports desynchronise everything after them", above.

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

### A board's own menus on the wire

*Read 2026-10-11 from the VIA app (`935106a`: `store/menusSlice.ts`, `custom/custom-control.tsx`,
`utils/range-constraints.ts`, `utils/keyboard-api.ts`), `@the-via/reader` 1.14.4 (`menu-types`) and
`@the-via/pelpi` 0.0.3; counts from VIA's registry and the vendor collection, 2026-10-11.*

- **Any channel.** A control binds `[name, channel, value id, ...]`; boards use channel 0 and
  also 7-12, which QMK does not define (cipulot: 7 and 8 QMK settings, 9 tap dance, 10 combos,
  11 DKS, 12 controller), and QMK's own 1-3 for their own lighting menus.
- **Numbers after the value id are sent with it**: get `[0x08][ch][id][extra...]`, the value
  from the byte after the last; set `[0x07][ch][id][extra...][value...]` -- a slot index, say:
  `["id_dks_threshold[3][1]", 11, 2, 3, 1]`.
- **Each control's value**: range one byte, **two big-endian when its maximum passes 255**;
  dropdown one byte (or as many as its `[name, value, ...]` options hold); toggle one byte, or
  its two given values (`[[255, 0], [254, 0]]` on 108 controls); keycode two bytes, big-endian;
  colour hue then saturation; button sends its one option (`[0]` mostly); a bound label is a
  string ending at a zero. A reply holds the whole buffer after the value, as QMK settings do.
- **Saved per channel**: the VIA app sends the set, then `[0x09][channel]`, on every change.
- **`showIf`** (pelpi): `{id}` is a value's **first byte** -- the high byte of a two-byte range --
  `{id.1}` its second; numbers; `== != < <= > >=`, `&& ||`, `!`, brackets. `{id_firmware_version}`
  is the 32-bit `id_firmware_version` keyboard value above, not a byte. Pelpi splits at the first
  instance of the loosest operator, so `a || b && c` is `a || (b && c)` as usual.
- **Range `constraints`**: `{operator, reference, offset, onViolation}` -- the value kept beyond
  another plus an offset, either stopped there (`clamp`) or **pushing the other along** (`push`,
  recursively, each control once). 3598 controls, all cipulot's thresholds and curves.
- **The VIA app reads every value of every custom menu when the board connects**, and only on
  protocol 11 or later. On the EC Theca that is about 700 round trips.
- **UI sync, `0x16`**, sent by the firmware: `[0x16][1 version][type][count][...]` -- type 0
  read everything again, 1 the `[channel, value id]` pairs that follow, 2 the value ids that
  follow. QMK's mainline does not send it (2026-09).

### Lighting

The custom value commands `0x07`-`0x09` carry QMK's lighting, in three formats. Read 2026-10-11
from QMK's `quantum/via.c` (Rico's fork, 2026-09, and the 2020 tree), vial-qmk's `quantum/via.c`
and `vialrgb.c` (2026-02); pinned by `tests/LightingTest.cpp`.

**VIA V3's channels** -- mainline from protocol 11. `[0x08][channel][value id]`, the value from
byte 3; set the same with `0x07`; save `[0x09][channel]`. Ids 1 brightness, 2 effect, 3 speed, 4
colour (hue, saturation) on channels 2 (underglow), 3 (RGB Matrix) and 5 (LED Matrix); 1
brightness, 2 breathing on channel 1 (backlight). Brightness is scaled to 0-255 from the
firmware's limit; **the effect reads 0 while off**, and writing 0 turns it off, any other turns it
on, then sets the mode. A channel the firmware lacks answers `0xFF`.

**The format before V3** -- mainline VIA 9 and 10, and **every vial-qmk**, which reports VIA 9:
no channel byte. `[0x08][value id]`, the value from byte 2; save `[0x09]` stores every lighting
system built. Backlight `0x09` brightness (scaled), `0x0A` breathing; underglow `0x80`
brightness -- **unscaled**, QMK's value -- `0x81` effect, `0x82` speed, `0x83` colour. vial-qmk
answers both systems' ids to one request, each ignoring the other's. **The underglow's effect reads
its mode whether on or off**, so off cannot be read; writing it calls `rgblight_mode_noeeprom()`
before enabling, and QMK skips a mode change while off: **an effect written to a light that is off
only turns it on, with the old effect** -- write it twice. Writing 0 sets the mode to 1 (QMK clamps)
and turns it off. vial-qmk's RGB Matrix without VialRGB answers underglow's ids with a speed of 0-3
(`VIA_QMK_RGB_MATRIX_ENABLE`); no Vial preset declares it but one board's.

**VialRGB** -- vial-qmk with `VIALRGB_ENABLE`, flagged in `vial_get_keyboard_id`'s byte 12: commands
in the value id's place, arguments from byte 2, little-endian. Get `0x40` info -- protocol (1),
then `RGB_MATRIX_MAXIMUM_BRIGHTNESS`; `0x41` mode -- effect id (2 bytes), speed, hue, saturation,
brightness on that maximum; `0x42` supported -- the ids above `[2..3]`, up to 15, `0xFFFF` after the
last, asked again from the last while a reply is full; `0x43`/`0x44` LED count and positions (direct
mode only). Set `0x41` mode -- all six at once; `0x42` direct colours. Save `[0x09]`. Effect 0 is
off, 1 the direct mode, a host streaming the colours.

QMK numbers underglow and RGB Matrix effects among those built in (ui-design.md, "The Lighting
section"): only VialRGB's ids name an effect whatever the build.

### Audio

Read 2026-10-11 from QMK's `quantum/via.c`, `quantum/audio/audio.c` and
`quantum/process_keycode/process_clicky.c` (Rico's fork, 2026-09), and vial-qmk's `quantum/via.c`
(2026-02).

**VIA V3's channel 4**, mainline from protocol 11: get `[0x08][4][id]`, the value from byte 3 -- 0
or 1; set `[0x07][4][id][v]`, any non-zero on; save `[0x09][4]`. Id 1 audio on, id 2 clicky on --
two bits of `audio_config`, stored together. **A set changes the bit silently**: no song, no
`audio_on_user()`, unlike the `AU_ON` key. A firmware built without `AUDIO_ENABLE` answers `0xFF`
in byte 0. **Clicks sound only while audio is on** (`process_clicky` checks both bits).

Out of the wire's reach, in RAM and forgotten at unplug: the clicks' pitch (`CK_UP`, `CK_DOWN`,
`CK_RST`), the voice (`AU_NEXT`, `AU_PREV`) and music mode (`MU_*`, the keys playing notes instead
of typing).

**Device indication** (`id_set_keyboard_value`, `0x05`) is the only way to make the board play:
QMK's default plays `STARTUP_SOUND` on value 0 and toggles every lighting system once per call, so
0 then 1 plays it and leaves the lighting as it was. Boards override it (the WT60-XT plays its own
song only). Silent while audio is off.

**Vial has no audio wire**: vial-qmk's `via.c` handles no channel 4 and its QMK Settings no audio
line; vial-gui offers the audio keys only.

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
| `0x09` | `vial_qmk_settings_query` | `[2..3]` qsid greater-than, LE | up to 16 supported qsids, LE, `0xFFFF` after the last; all `0xFF` if unsupported -- query again from the last while a reply is full |
| `0x0A` | `vial_qmk_settings_get` | `[2..3]` qsid, LE | `[0]` status, `[1..]` value, **the setting's own width** (1, 2 or 4 bytes, model/NazgQmkSettings.h): past it the reply still holds the request -- qsid 1 read as four bytes is `0x0100` (Model F, 2026-10-11) |
| `0x0B` | `vial_qmk_settings_set` | `[2..3]` qsid, `[4..]` value | `[0]` status |
| `0x0C` | `vial_qmk_settings_reset` | — | — (echoes the request); every setting to the build's defaults, Magic too |
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

A combo's `input[]` ends at the first `0` (`COMBO_END`), so an input after an empty one is
never matched; a slot with `input[0] == 0` is unused. Only `output` passes the keycode
firewall; the inputs are stored as sent.

A key override's three masks are QMK's 8-bit `MOD_BIT` order (Ctrl, Shift, Alt, Gui left in bits
0-3, right in 4-7); its `options` bits 0-5 are QMK's `ko_option_t`, bit 7 Vial's enable. A reset
slot is all zeros -- off, **no layer**: enabled as it is, it never fires. Only `replacement`
passes the firewall, which matters little: the firmware sends it with `register_code(uint8_t)`,
so only a basic key, with modifiers, is ever sent (ui-design.md, "The Key Overrides section").

An alt repeat key's `allowed_mods` is the same 8-bit order; its `options` are bit 0 default to
this alt key, 1 bidirectional, 2 ignore mod handedness, 3 Vial's enable. A reset slot is all
zeros, off. Both keycodes pass the firewall. **vial-qmk sends right-hand modifiers wrong** in
what Alt Repeat sends -- an 8-bit mask where a keycode holds 5 bits (ui-design.md, "The Alt
Repeat Key section").

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
  `vial_unlock_poll`. "Dropped" means **echoed back unchanged** (`via.c` jumps to its final
  `raw_hid_send`), so a keymap read returns garbage and a write looks successful. Nothing
  cancels an unlock; only succeeding or a restart ends it. A client must not load a board
  whose status says an unlock is in progress.
- **The unlock countdown is paced by the polls.** `vial_unlock_start` sets the countdown to
  50 (`VIAL_UNLOCK_COUNTER_MAX`). Each `vial_unlock_poll` counts one down only if every combo
  key is held **and more than 100 ms have passed** since the last step; otherwise it resets
  the countdown to 50. A poll that comes too soon therefore undoes the progress. Polls have to
  be more than 100 ms apart; Nazg uses 150 ms, about 7.5 s of holding. (`quantum/vial.c`,
  vial-qmk, read 2026-09-26.)
- **While locked**: `id_switch_matrix_state` is refused, `id_dynamic_keymap_macro_set_buffer`
  is refused, and `id_bootloader_jump` is refused. Refused means echoed back unchanged, as
  above: for the matrix state that reads as **every key released**, not as an error, so a
  client asks `vial_get_unlock_status` first rather than trusting silence.
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

## Macros — the buffer and its byte format

Read 2026-10-10 in mainline QMK (`quantum/dynamic_keymap.c`, `quantum/send_string/send_string.c`,
`quantum/via.c`, Rico's fork at 2026-09-19) and vial-qmk (`quantum/dynamic_keymap.c`,
`quantum/vial.c`, Rico's fork at 2026-08-23), with vial-gui's `protocol/macro.py` and
`macro/macro_action.py` and the VIA app's `utils/macro-api/` as readers of the same bytes.

**One buffer, every macro in it.** `id_dynamic_keymap_macro_get_buffer_size` is whatever EEPROM
the keymap (and on Vial, tap dance, combos and key overrides) leaves — a static assert demands at
least 100 bytes. `get_count` is `DYNAMIC_KEYMAP_MACRO_COUNT`, 16 unless the board sets it; the
keycodes `QK_MACRO_0..127` (`0x7700..0x777F`) allow 128 since VIA protocol 12, where 11 had
VIA's own `MACRO00..15`. Macro *n* is the *n*-th NUL-terminated
string: one long macro takes room from all the others, and a client rewrites the whole buffer.

**Playing one** (`QK_MACRO_n` pressed — on press only, nothing on release, no repeat while held)
walks the string. A plain byte is a character, typed through `ascii_to_keycode_lut[128]`: a
**US layout** table unless the firmware was built with a `keymap_extras/sendstring_<lang>.h`,
which no client can see. Bytes ≥ 0x80 index past the table — non-ASCII text is garbage, though
vial-gui writes its text as UTF-8. `\n`, `\t`, `\b` and ESC are Enter, Tab, Backspace and Escape.
**Playback blocks the firmware**: delays are `wait_ms`, so the keyboard scans nothing until the
macro ends.

`0x01` (`SS_QMK_PREFIX`) introduces an action:

| Bytes | Action | Mainline QMK | vial-qmk |
|---|---|---|---|
| `01 01 kc` | tap | `tap_code(kc)` — 8-bit, basic keycodes only | same |
| `01 02 kc` / `01 03 kc` | down / up | `register_code` / `unregister_code`, 8-bit | same |
| `01 04 …` | delay | **ASCII digits, ended by `\|`**: `01 04 '1' '0' '0' '\|'` is 100 ms. **At most 9999**: QMK of protocol 12 (read in a 2023 tree) abandons the macro at a fifth digit, and the VIA app caps it there; QMK of 2026 reads any number | **two bytes** `d0 d1`, ms = `(d0-1) + (d1-1)*255`, up to 65 024; vial-gui caps it at 64 000 |
| `01 05 lo hi` / `06` / `07` | tap / down / up, 16-bit | — | any keycode, run through `action_exec` like a key press — modified keys, layer keys, even `QK_BOOT` or another macro; a low byte of 0 is sent as `FF hi` |

**The two delay encodings are incompatible**: a VIA-written delay on a Vial board is read as two
wrong bytes, and the reverse stops at the first non-digit. The format is chosen by protocol:

| Board | Format |
|---|---|
| Vial protocol ≥ 5 | prefixed, Vial delays, 16-bit actions |
| Vial protocol 2–4 | prefixed, Vial delays, 8-bit actions only |
| Vial protocol 0–1 | **unprefixed**: `01 kc` tap, `02 kc` down, `03 kc` up, no delay |
| VIA protocol ≥ 11 | prefixed, ASCII delays, 8-bit actions |
| VIA protocol ≤ 10 | unprefixed, as Vial 0–1 — what the VIA app writes, and what the firmware of then reads: `send_string` took the prefix with #8244 (2020-02), but `dynamic_keymap_macro_send` of protocol 9 (read in a June 2020 tree) adds it itself before each 1, 2 or 3 |

**An interrupted write.** Playback refuses to run anything while the buffer's last byte is not 0.
The VIA app sets that byte to `0xFF` before writing and clears it last, so a write cut short
disables macros rather than playing half of one; vial-gui writes in order without the guard.

**Locking.** vial-qmk refuses `macro_set_buffer` while locked (see "What the lock gates") but
**not `macro_reset`**, which zeroes the buffer unlocked. Mainline VIA gates neither.

**Not the same feature: QMK's Dynamic Macros** (`DM_REC1`, `DM_REC2`, `DM_RSTP`, `DM_PLY1`,
`DM_PLY2`) record keystrokes on the keyboard itself into RAM — lost at power off, invisible to
any client. A configurator only places their keys.

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
   explicit keycode-dictionary selection, not as a hardcoded table. On a Vial board use the
   Vial protocol instead: vial-qmk always reports VIA 9. Full detail in
   [keycodes.md](keycodes.md).

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
