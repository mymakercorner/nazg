# Keychron — Command Reference

*Compiled 2026-10-15 by reading firmware source in Keychron's QMK fork,
[Keychron/qmk_firmware](https://github.com/Keychron/qmk_firmware), at two commits: `2025q3`
(the default branch, `c5d99844`, 2026-09-30) and `hall_effect_playground` (`bc56b3c6`,
2025-12-01), both under `keyboards/keychron/common/`. The `wireless_playground` branch
(2026-03-16) was checked for the top-level commands only. The firmware is GPL-2.0-or-later and
is read here as the specification of the wire format. Keychron's Launcher, the web app that
speaks this protocol, is closed and was not read. Nothing here has been seen on a board yet:
every layout below comes from the source, and the bitfield layouts from how GCC packs them on
ARM (see [The profile](#the-profile)).*

This is the companion of [via-vial-commands.md](via-vial-commands.md) for Keychron's own
commands. It repeats nothing from it: VIA's commands, its report shape and its traps apply
unchanged to every Keychron board, which is a VIA board first.

---

## The shape of the protocol

Keychron's commands ride on VIA's report. QMK's `via.c` hands every command id it does not
know to `via_command_kb()`, which Keychron implements (`keychron_raw_hid.c`, `kc_raw_hid_rx()`)
for the ids `0xA0`–`0xAB`. They do not collide with VIA (`0x01`–`0x15`) or Vial (`0xFE`).
It is the same 32-byte request and reply, with the buffer echoed back and fields overwritten in
place.

```
byte  0       1        2        3 ...
     [cmd ] [sub-id] [status] [ data ... ]      groups 0xA7, 0xA8, 0xA9
     [cmd ] [ data ... ]                        0xA0 - 0xA3
```

**Byte order is little-endian** for Keychron's own fields: the firmware `memcpy`s its
structures on a little-endian ARM core. VIA's fields are big-endian, so the two orders meet in
one report stream. The one exception is the matrix state Keychron answers for VIA (below),
which keeps VIA's big-endian rows.

**The status byte is byte 2, 0 for success and 1 for failure**, in the three groups. Some
commands do not write it, and they are marked in the tables.

**How a missing command answers** depends on where it is missing:

| What is missing | The reply |
|---|---|
| A top-level id the firmware lacks (`0xA8` without Keychron RGB, `0xA9` without HE switches, any `0xAx` on stock QMK or Vial) | `0xFF` in byte 0 — VIA's `id_unhandled` |
| A sub-id of `0xA7` or `0xA8`, or a misc feature compiled out | `0xFF` in byte 0 |
| A sub-id of `0xA9` the firmware does not know | the request, echoed unchanged, as Vial does |
| `0xAA` on a board without wireless, `0xAB` with a bad checksum | **no reply at all**: see [Never send](#commands-a-client-must-never-send) |

So `0xA0` is a safe probe on any VIA or Vial board: anything that is not Keychron answers `0xFF`.

### Which firmware has what

Keychron does not release from one branch. The boards in the field run whatever their branch
held when their firmware was built:

| Branch | Boards | What it answers |
|---|---|---|
| `hall_effect_playground` (last commit 2025-12-01) | the first HE boards: K2, K4, K6, K8, K10, Q1, Q3, Q5 and Q6 HE | `0xA0`–`0xA3` (short replies), `0xA9`, `0xAA`, `0xAB`. **No `0xA7`, no `0xA8`, no notifications.** Answers `id_switch_matrix_state` twice. |
| `wireless_playground` (2026-03-16) | wireless boards | `0xA0`–`0xA3`, `0xA7`, `0xA8`, `0xA9`, `0xAA`, `0xAB` |
| `2025q3` (default, 2026-09-30) | 99 boards, HE ones included (also Q0, Q2, Q4, Q12 HE) | all of it, plus unsolicited reports (`STATE_NOTIFY_ENABLE` is set for every board in `keychron_common.mk`) |
| `master`, `playground` | older boards | none of it (`keychron_raw_hid.c` is absent) |

A tester's K10 HE answered `id_switch_matrix_state` twice (2026-10-13), so it runs
`hall_effect_playground`-era code. Its firmware file is dated 2025-05-26. The analog matrix
command set (`0xA9`) is **the same on both branches**: version byte `0x04`, the same sub-ids
and the same layouts. Between them only the internal EEPROM layout and some key action
details changed.

**A K10 HE ANSI read on 2026-10-29** (borrowed by Rico, the colleague's model) runs firmware **no
published branch has**: `3434:0EA7`, `bcdDevice` 1.0.0 (the source says 1.1.1), `0xA1`
`v1.0.0 2026-07-17-17:42:44`, VIA protocol 12. Its replies:

| Request | Reply (bytes 0-8) | Against the source |
|---|---|---|
| `0xA0` | `A0 04 00 02` | `[1]` is 4, where every branch sends 2 |
| `0xA2` | `A2 00 09 53 00 15 3F F1 11` | bits 0 and 3 (layer, analog matrix) as documented; `[3]` `0x53`, not `0x03`; `[4..8]` unknown |
| `0xA3` | `A3 02 00` | default layer 2 (the Windows base layer) |
| `0xA7 0x01` | `A7 01 00 03 00 00` | answers, unlike `hall_effect_playground`; version 3, not 2; no misc bits |
| `0xA9 0x01` | `A9 01 04 00 1E` | version 4 as documented, then an undocumented `0x1E` |
| `0xA9 0x10` | `A9 10 00 03 D4 03 14 14` | profile 0 of 3, 980 bytes, 20 DKS, 20 SOCD: as documented |
| `02 03 00` (matrix) | twice, all keys up | still the double answer |

So the published source describes neither this board nor, probably, the colleague's: what Nazg
builds on Keychron's commands must be checked on the board, not only read from the fork.

**Keychron's Launcher (v1.5.3) names it "Keychron K10 HE V3 ANSI RGB"** (Rico's screenshot,
2026-11-08), which explains the unpublished firmware and the new PID: it is a third version of the
board. Its HE Mode page:

- **The three profiles, named "Default", "Gaming" and "Gamepad"**, listed beside the board, each
  with a "..." menu, and the hint "press fn + p + z / x / c" to switch to profile 1 / 2 / 3. Profile
  0's `name` read empty, so the names are presumably the Launcher's own for an empty name.
  **Fn + P + X switches the profile on this board** (Rico, 2026-11-08), as the source has it: the
  profile in use can change under a client's feet, from the keyboard. **The Launcher follows it at
  once**, its highlight moving without a reload. So either the firmware reports the switch
  unrequested, or the Launcher keeps asking: not yet told apart. A read-only probe listening while
  the shortcut is pressed would show which. The
  presets behind "Gaming" and "Gamepad" (profiles 1 and 2) have never been read.
- **Calibration** and **Reset** buttons under the profiles.
- Tabs: Actuation Distance, One Key Multiple Commands (DKS), **Long-Press Switch**, SOCD, Gamepad
  Analog, Curve, **PDO**, **Turbocharged**. The three in bold are in no published source, so this
  firmware has more advanced modes than the `adv_mode` values documented below.
- Rapid Trigger with Basic and Advanced sensitivity (probably one value, then press and release apart).
- What the keys show: No Data, Actuation Distance or Rapid Trigger; Select All and Clear.

**Its wiring differs too**, which explains the keys the colleague's board never lit. Read from its
keymap: Right Shift at (4, 13), Right Alt, Right Win, Fn and Right Ctrl at (5, 9)-(5, 12), Numpad .
at (5, 18) -- the published ANSI file has (4, 12), (5, 10)-(5, 13) and (5, 19). With those six moved,
all 104 keys match the keymap and light in Nazg's key test (Rico, 2026-10-29). Re-read on
2026-11-08 straight from the board: layer 0 at (4, 13) is `KC_RSFT` and at (5, 18) `KC_KP_DOT`,
so the moved keys are the board's, not a mis-drawn definition.

### The 2026-11-08 read: the profile decoded

*The same board, read again with a read-only probe (hidapi driven from Python, the whole of
profile 0 pulled with `0xA9 0x12`). Every reply above reproduced byte for byte. What is new:*

**The profile layout is proven.** The computed size for 6 x 21, D = S = 20 is 980 bytes, which is
what `0xA9 0x10` reports, and the 980 bytes decode as the tables below say. `global` read
`45 04 01 00` -- u32 LE `0x00010445` -- giving mode 1 (regular), `act_pt` 17, `rpd_trig_sen` 4,
`rpd_trig_sen_deact` 4, `adv_mode` 0, `adv_mode_data` 0. **Keychron's own Launcher shows 1.7 mm
actuation for this profile** (Rico, 2026-11-08), so `act_pt` is read right. The DKS and SOCD
slots were all zero, `name` empty, and `crc16` `0000` -- confirming the firmware never computes it.

So `analog_key_config_t`'s `mode`, `act_pt` and both sensitivities are **confirmed on hardware**.
Still derived only: `adv_mode` and `adv_mode_data`, zero everywhere on this board, and the whole of
`okmc_config_t` and `socd_config_t`, whose slots are all empty.

**`key_config` is confirmed too, and its indexing with it** -- by luck, a few hours later. The
first read had all 126 entries at 0, every key following the profile, which proves nothing about
the per-key struct. Rico then **ticked Rapid Trigger in Keychron's Launcher**, and a second read
found **104 of them at `0x00010446`**: mode 2, rapid trigger, with the same 1.7 / 0.4 / 0.4,
differing from `global` in the mode bits alone. The 22 entries still at 0 are **exactly** the 22
matrix positions that `0xA9 0x42` reports no calibration for, and 104 is what the K10 HE ANSI
definition draws. A wrong stride or a wrong offset would have scattered those zeroes, so this pins
the layout *and* `key_config[r][c]` at 4 + 4(r·C + c), row-major.

### Rapid Trigger is written per key, not to the profile

*A trap for the UI, found in that same pair of reads (2026-11-08).*

`global` is **byte-identical** before and after the Launcher turned Rapid Trigger on: `0x00010445`,
mode 1, regular. What changed is the 104 keys, each written to mode 2. So Keychron's own app does
not express "rapid trigger on" by setting the profile's mode at all -- it writes every key
individually and leaves `global` saying regular.

Rico then unticked it, which gave the reverse transition. All three states, same profile:

| Read | `global` | The 104 keys | Bytes changed |
|---|---|---|---|
| 1, as the board came | `00010445`, regular 1.7 | all 126 at `00000000`, follow the profile | -- |
| 2, Rapid Trigger ticked | `00010445` **unchanged** | `00010446`, rapid trigger | 312 |
| 3, unticked again | `00010445` **unchanged** | `00010445`, regular -- `global`'s own word | 104 |

**Turning it off does not put the keys back to "follow the profile".** It writes `global`'s word
into each of them, so state 3 behaves exactly like state 1 while looking nothing like it: 104
explicit per-key settings in place of 104 zeroes. Only 104 bytes moved -- one per key, the low byte
holding the mode bits. **The all-zero state is the factory's, and a board never returns to it.**

Three consequences for a client:

- **Reading `global.mode` to answer "is rapid trigger on?" gives the wrong answer** on any board a
  user has configured in the Launcher -- it said "regular" throughout, including while every key
  on the board was in rapid trigger. The honest reading is the keys', with `global` as what a key
  set to 0 falls back to.
- **104 keys all saying the same thing is the normal state of a configured board**, not 104
  deliberate per-key overrides. A UI that lists exceptions to the profile would show every key as
  an exception, and would still do so after the user turned the feature back off. Nazg should fold
  "every key alike" into one statement and keep the per-key view for keys that genuinely differ --
  comparing each key's *effective* settings, not whether its word is zero.
- **"Follow the profile" is not a state a client can restore by writing 0 and expecting the
  Launcher's agreement.** Whether the firmware accepts a 0 written back is untested: it would be a
  write, and nothing has been written to a board yet.

Whether the Launcher also rewrites the profile on merely opening it is **not known**: both writes
followed a deliberate change.

**Full travel on this board is 3.5 mm, not the source's 4.0** (`FULL_TRAVEL_UNIT` 40) --
**measured, not inferred**. The space bar was polled with `0xA9 0x30` while Rico pressed it slowly
to the bottom and released, three times (2257 samples, 2026-11-08): the travel swept every value
from 0.0 to 3.5 mm and **stopped dead at 35**, 966 samples sitting there while the key was held
down. Never 36, never 40.

Everything else on this board is that 35 rather than the source's 40, in the same proportions:

| | Source default | This board | As a fraction of full travel |
|---|---|---|---|
| Curve x | 0, 10, 30, 40 | 0, **8**, **26**, **35** | 0, 25%, 75%, 100% -- floored |
| Curve y | 0, 31, 95, 127 | 0, 31, 95, 127 | unchanged: the y axis is the output, not a distance |
| `global.act_pt` | 20 | **17** | 50% -- floor(0.5 x 35) |

So the defaults are the source's *fractions* of whatever the board's full travel is, floored, and
only the distance axis moves.

**The 215 is the firmware's clamp, not a deeper key** (explained 2026-11-08 from the source, see
[How travel is computed](#how-travel-is-computed)). The live readout's 1/60 mm field reached **215**
where 6 x 35 would be 210. `convert_to_travel()` clamps at (F + 1) x 6 - 1, F being the firmware's
`FULL_TRAVEL_UNIT`. That is 215 exactly for F = 35, and 245 for the source's 40. So the 1/60 field
pinned at that value means "at or past full travel", and **the value it is pinned at reveals F**.
A client should show the 0.1 mm field and use the 1/60 mm one only for a fine readout.

**A key can read past its own calibration.** The ADC went to 1763 against a calibrated bottom of
1857, and the travel still read 35. The firmware clamps rather than report more.

**No command reports a board's full travel**, which is the awkward part: a client cannot hard-code
40. The curve's last x (`0xA9 0x20`) is the estimate that needs no user: 35 here, 40 on a board
built to the source. The curve is user-editable, though, so it is a hint, not an authority. **The
exact value is any key pressed to the bottom**: its 1/60 field pins at (F + 1) x 6 - 1. It is one
value per firmware, not per switch (below). Offering 4.0 mm on this board would let a user set an
actuation point the firmware never reports.

**The feature bits under-report. Do not gate a section on them** -- see
[implication 1](#implications-for-the-nazg-backend). `0xA2` reports `0x09`: default layer and
analog matrix only, with bit 7 (Keychron RGB) clear and `0xA7 0x01` reporting **no** misc bits at
all. Yet both groups answer:

| Request | Reply | Though |
|---|---|---|
| `0xA8 0x01` | `A8 01 00 01` -- RGB protocol 1 | feature bit 7 is clear |
| `0xA7 0x12` | `A7 12 00 03` -- NKRO on, settable | misc bit 7 is clear |
| `0xA7 0x05` | `FF` | misc bit 2 is clear -- genuinely absent |

Only `0xFF` is a reliable "not there". NKRO being settable also contradicts what `2025q3` does
(`APDAPTIVE_NKRO_ENABLE`), which this firmware is not.

**Odds and ends.** `0xA9 0x41` answered `A9 41 03 00`: both rest and bottom calibrated, state 0
(off). `0xA9 0x42` on an unpopulated matrix position -- (0, 1) on this board -- returns rest 0,
bottom 0 and a nonsense scale factor, which is a usable test for "no key here". Its scale factor
varies per key (10.11 to 10.52 over the keys sampled), and `span / scale` is not constant. The
source explains both: the scale is F divided by the key's calibrated span measured through a
polynomial, not through the raw ADC difference
([How travel is computed](#how-travel-is-computed)). Values near 10 rather than near 1 suggest this
firmware's polynomial gives millimetres where the source's gives 0.1 mm: its constants differ, and
they are not published.

### Unsolicited reports

*A trap for any client, not just one speaking Keychron's commands.* On `2025q3` builds, the
firmware sends reports nobody asked for (`state_notify.c`):

| Report | When |
|---|---|
| `[0xA3, default layer, highest active layer]` | **every layer change**, holding Fn included |
| `[0xA7, 0x11]` | a factory reset from the keyboard (Fn+J+Z held) |
| `[0xA7, 0x0D, 0, rate, 0x7F]` | the USB report rate changed from the keyboard |

The layer report is the dangerous one. It goes out whenever a key changes the layer, so it can
arrive between a request and its reply, and it will be read as the reply. Discarding what is
waiting before each request (via-vial-commands.md, "Stale reports") does not cover it. A client
must check that byte 0 of a reply matches the request and **keep reading past a report that
does not match**, rather than fail -- Nazg does since 2026-10-16 ("Implications", point 2). The
Launcher depends on these reports to follow the layer.

### Commands a client must never send

- **`0xAA`** updates the wireless module's firmware (`lkbt51_dfu_rx()`). On a board without
  wireless, the `case` has no `break` and falls into `0xAB`, which ignores the request, so the
  firmware sends **no reply** and the client waits for its timeout.
- **`0xAB`** is the factory test (backlight test patterns, the OS switch, radio carrier, chip
  UID). Every report needs a checksum in bytes 30–31, and a wrong one gets no reply. Nothing a
  configurator needs is behind it.
- **There is no factory reset command.** `FACTORY_RESET` (`0xA7 0x11`) exists only as the
  notification above. `DIP_SWITCH_GET` / `SET` (`0x0F`, `0x10`) are numbered but not handled:
  they answer `0xFF`.

---

## Top-level commands

| ID | Name | Request | Response |
|---|---|---|---|
| `0xA0` | `KC_GET_PROTOCOL_VERSION` | — | `[1]` `0x02`; `2025q3` adds `[2]` 0, `[3]` `QMK_COMMAND_SET` (2) |
| `0xA1` | `KC_GET_FIRMWARE_VERSION` | — | `[1..]` ASCII, NUL-terminated: `v` + `DEVICE_VER` as `x.y.z`, a space, `QMK_BUILDDATE` |
| `0xA2` | `KC_GET_SUPPORT_FEATURE` | — | `[2]` feature bits 0–7; `2025q3` adds `[1]` 0, `[3]` bits 8–15 |
| `0xA3` | `KC_GET_DEFAULT_LAYER` | — | `[1]` default layer; `2025q3` adds `[2]` highest active layer |
| `0xA7` | `KC_MISC_CMD_GROUP` | `[1]` sub-id | [below](#0xa7--the-misc-group) |
| `0xA8` | `KC_KEYCHRON_RGB` | `[1]` sub-id | [below](#0xa8--keychron-rgb) |
| `0xA9` | `KC_ANALOG_MATRIX` | `[1]` sub-id | [below](#0xa9--the-analog-matrix-hall-effect) |
| `0xAA` | `KC_WIRELESS_DFU` | — | never send |
| `0xAB` | `KC_FACTORY_TEST` | — | never send |

**The feature bits** (`0xA2`), byte `[2]`:

| Bit | Feature | Set when |
|---|---|---|
| 0 | default layer | always |
| 1 | Bluetooth | a wireless board |
| 2 | 2.4 GHz | an `LK_WIRELESS` board |
| 3 | **analog matrix** | an HE board — `0xA9` answers |
| 4 | state notify | `2025q3` (all boards); on `wireless_playground`, `INFO_CHANGED_NOTIFY` |
| 5 | dynamic debounce | `0xA7` debounce answers |
| 6 | snap click | `0xA7` snap click answers — never on an HE board, where SOCD replaces it |
| 7 | Keychron RGB | `0xA8` answers |

Byte `[3]` on `2025q3` is always `0x03`: bit 8, quick start, and bit 9, NKRO.

**Firmware version**: `DEVICE_VER` is the board's USB `bcdDevice`, the `device_version` of its
`keyboard.json` (K10 HE ANSI: 1.1.1), so `0xA1` adds only the build date to what the USB
descriptor already says.

---

## `0xA7` — the misc group

`[1]` is the sub-id; replies carry the status in `[2]` unless noted.

| Sub | Name | Request | Response |
|---|---|---|---|
| `0x01` | `MISC_GET_PROTOCOL_VER` | — | `[2]` 0, `[3..4]` version `0x0002` LE, `[5]` the misc bits below |
| `0x02` | `DFU_INFO_GET` | — | `[2]` 0, then tagged: `[3]` 1 (chip), `[4]` length, the MCU name in ASCII (`STM32F401`...), then 2 (bootloader), 1, the type: 1 STM32 DFU, 2 WB32 DFU |
| `0x03` | `LANGUAGE_GET` | — | `[3]` a byte |
| `0x04` | `LANGUAGE_SET` | `[2]` a byte | status |
| `0x05` | `DEBOUNCE_GET` | — | `[2]` 0, `[3]` 0, `[4]` algorithm, `[5]` time in ms |
| `0x06` | `DEBOUNCE_SET` | `[2]` algorithm, `[3]` ms | status |
| `0x07` | `SNAP_CLICK_GET_INFO` | — | `[3]` number of pairs |
| `0x08` | `SNAP_CLICK_GET` | `[2]` first, `[3]` count ≤ 9 | `[3..]` 3 bytes a pair: type, key 1, key 2 |
| `0x09` | `SNAP_CLICK_SET` | `[2]` first, `[3]` count ≤ 9, `[4..]` pairs | status |
| `0x0A` | `SNAP_CLICK_SAVE` | — | status — stores the pairs |
| `0x0B` | `WIRELESS_LPM_GET` | — | `[3..4]` backlight off after, `[5..6]` sleep after, seconds, u16 LE |
| `0x0C` | `WIRELESS_LPM_SET` | `[2..3]`, `[4..5]` as above | status; refused under 5 s and 60 s |
| `0x0D` | `REPORT_RATE_GET` | — | `[3]` divider, `[4]` allowed dividers (`0x7F`) |
| `0x0E` | `REPORT_RATE_SET` | `[2]` divider ≤ 6 | status |
| `0x0F`, `0x10` | `DIP_SWITCH_GET` / `SET` | — | `0xFF`: not handled |
| `0x11` | `FACTORY_RESET` | — | a notification only |
| `0x12` | `NKRO_GET` | — | `[2]` 0, `[3]` bit 0 NKRO on, bit 1 settable, bit 2 adaptive |
| `0x13` | `NKRO_SET` | `[2]` 0 or 1 | status |

**The misc bits** (`0x01`, `[5]`): 0 DFU info, 1 language, 2 debounce, 3 snap click, 4 wireless
power management, 5 report rate, 6 quick start (always set; there is no command behind it),
7 NKRO.

Notes:

- **Writes here are stored at once**, except snap click, which waits for `SNAP_CLICK_SAVE`.
- **Language** is a byte the firmware stores and never reads: presumably the Launcher's own
  setting, kept on the board.
- **Debounce algorithms** are QMK's: 0 `sym_defer_g`, 1 `sym_defer_pr`, 2 `sym_defer_pk`,
  3 `sym_eager_pr`, 4 `sym_eager_pk` (the default), 5 `asym_eager_defer_pk` (time capped at
  127 ms), 6 none. A time of 0 also means none.
- **Snap click** is the SOCD of boards without HE switches, and it works on keycodes rather
  than positions: the 8-bit keycode of each key, so only basic keys qualify. The types are
  0 off, 1 regular, 2 last input wins, 3 first key wins, 4 second key wins, 5 neutral (both
  released).
- **Report rate** applies to boards with high-speed USB (`USB_REPORT_INTERVAL_ENABLE`, the
  "8K" models). The divider is 0 8000 Hz, 1 4000, 2 2000, 3 1000, 4 500, 5 250, 6 125.
- **NKRO cannot be set on a `2025q3` build**: `keychron_common.mk` defines
  `APDAPTIVE_NKRO_ENABLE` (sic) for every board, and `NKRO_SET` then always fails. `NKRO_GET`
  says so with bit 2 set and bit 1 clear.

---

## `0xA8` — Keychron RGB

Keychron's lighting on top of QMK's RGB Matrix: two modes of its own, chosen through VIA's
normal RGB Matrix effect, plus the colour of the lock lights. Status in `[2]`.

| Sub | Name | Request | Response |
|---|---|---|---|
| `0x01` | `RGB_GET_PROTOCOL_VER` | — | `[3..4]` `0x0001` LE |
| `0x02` | `RGB_SAVE` | — | status — **stores everything below** |
| `0x03` | `GET_INDICATORS_CONFIG` | — | `[3]` lights the board has (bit 0 Num, 1 Caps, 2 Scroll, 3 Compose, 4 Kana), `[4]` the ones turned off, `[5..7]` H, S, V |
| `0x04` | `SET_INDICATORS_CONFIG` | `[2]` turned off, `[3..5]` H, S, V | status; V raised to at least 128 |
| `0x05` | `RGB_GET_LED_COUNT` | — | `[3]` count |
| `0x06` | `RGB_GET_LED_IDX` | `[2]` row, `[3..5]` columns wanted, u24 LE | `[3 + col]` LED index, `0xFF` for none |
| `0x07` | `PER_KEY_RGB_GET_TYPE` | — | `[3]` type |
| `0x08` | `PER_KEY_RGB_SET_TYPE` | `[2]` type < 5 | status |
| `0x09` | `PER_KEY_RGB_GET_COLOR` | `[2]` first LED, `[3]` count ≤ 9 | `[3..]` H, S, V per LED |
| `0x0A` | `PER_KEY_RGB_SET_COLOR` | `[2]` first, `[3]` count ≤ 9, `[4..]` H, S, V | status |
| `0x0B` | `MIXED_EFFECT_RGB_GET_INFO` | — | `[3]` regions (2), `[4]` effects per region (5) |
| `0x0C` | `MIXED_EFFECT_RGB_GET_REGIONS` | `[2]` first LED, `[3]` count ≤ 29 | `[3..]` each LED's region |
| `0x0D` | `MIXED_EFFECT_RGB_SET_REGIONS` | `[2]` first, `[3]` count ≤ 28, `[4..]` regions | status |
| `0x0E` | `MIXED_EFFECT_RGB_GET_EFFECT_LIST` | `[2]` region, `[3]` first, `[4]` count ≤ 3 | `[3 + 8i]` 8 bytes an effect |
| `0x0F` | `MIXED_EFFECT_RGB_SET_EFFECT_LIST` | `[2]` region, `[3]` first, `[4]` count ≤ 3, `[5 + 8i]` effects | status |

- **Per-key RGB** is one colour per LED, HSV, with a type: 0 solid, 1 breathing, 2 reactive
  simple, 3 reactive multi-wide, 4 reactive splash.
- **Mixed RGB** gives each LED a region (two by default), and each region plays up to five
  effects in turn. An effect is 8 bytes: effect, hue, saturation, speed, then how long it runs,
  in ms, u32 LE. **The effect is a QMK RGB Matrix mode number**, below Keychron's mixed mode, so
  it carries the same naming trap as VIA's lighting: the number depends on the effects built in
  (via-vial-commands.md, "Lighting").
- **Every set is held in RAM until `RGB_SAVE`.**
- `GET_COLOR` and `SET_COLOR` do not check `first + count` against the LED count, so the client
  must check it.

---

## `0xA9` — the analog matrix (Hall effect)

Every setting of the HE switches: actuation, rapid trigger, the four advanced modes, SOCD, the
gamepad, profiles and calibration (`analog_matrix/analog_matrix.c`, `analog_matrix_rx()`, and
`profile.c`). A request is `[0xA9, sub, ...]`, and most replies carry the status in `[2]`.

### Units

**Distances on the wire are in 0.1 mm**, from 0 to 4.0 mm (`FULL_TRAVEL_UNIT` 40).
Internally the firmware works in sixths of that (`TRAVEL_SCALE` 6, 1/60 mm, 240 at the bottom),
and only the live readout exposes that unit.

**The 40 is not universal.** A K10 HE tops out at 35, measured with the live readout on a key held
to the bottom, and its curve and defaults are the source's fractions of that (see
"The 2026-11-08 read"). No command reports a board's full travel, so a client must not hard-code
the maximum -- offering 4.0 mm there would let a user set an actuation point the firmware never
reports. **It is the firmware's number, not the switch's**: every key's calibrated bottom reads
full travel, whatever switch is fitted ([How travel is computed](#how-travel-is-computed)).

### Sub-commands

| Sub | Name | Request | Response |
|---|---|---|---|
| `0x01` | `AMC_GET_VERSION` | — | `[2]` `0x04` — the low byte of `0x34340004` — **no status** |
| `0x10` | `AMC_GET_PROFILES_INFO` | — | `[2]` current profile, `[3]` profile count (3), `[4..5]` profile size LE, `[6]` DKS slots (20), `[7]` SOCD slots (20) — **no status** |
| `0x11` | `AMC_SELECT_PROFILE` | `[2]` profile | status — **stored at once** |
| `0x12` | `AMC_GET_PROFILE_RAW` | `[2]` profile, `[3..4]` offset LE, `[5]` size ≤ 26 | `[6..]` the bytes — **no status**, see the traps |
| `0x13` | `AMC_SET_PROFILE_NAME` | `[2]` profile, `[3]` length ≤ 28, `[4..]` bytes | status |
| `0x14` | `AMC_SET_TRAVAL` (sic) | see [Actuation](#actuation-and-rapid-trigger) | status |
| `0x15` | `AMC_SET_ADVANCE_MODE` | see [Advanced modes](#advanced-modes) | status |
| `0x16` | `AMC_SET_SOCD` | see [SOCD](#socd) | status |
| `0x1E` | `AMC_RESET_PROFILE` | `[2]` profile | status — back to the board's defaults, **stored at once** |
| `0x1F` | `AMC_SAVE_PROFILE` | `[2]` profile | status — **stores the profile** |
| `0x20` | `AMC_GET_CURVE` | — | `[2..9]` four points, x then y — **no status** |
| `0x21` | `AMC_SET_CURVE` | `[2..9]` four points | status — stored at once |
| `0x22` | `AMC_GET_GAME_CONTROLLER_MODE` | — | `[2]` 0, `[3]` mode bits |
| `0x23` | `AMC_SET_GAME_CONTROLLER_MODE` | `[2]` mode bits | **no status**: the mode is echoed back. Stored at once |
| `0x30` | `AMC_GET_REALTIME_TRAVEL` | `[2]` row, `[3]` col | see [Live readout](#live-readout) |
| `0x40` | `AMC_CALIBRATE` | `[2]` 2 start, 4 save and stop | status |
| `0x41` | `AMC_GET_CALIBRATE_STATE` | — | see [Calibration](#calibration) — **no status** |
| `0x42` | `AMC_GET_CALIBRATED_VALUE` | `[2]` row, `[3]` col | `[4]` status, `[5..6]` rest, `[7..8]` bottom, raw ADC LE, `[9..12]` scale factor, float LE |

**When a setting is stored:** `SET_TRAVAL`, `SET_ADVANCE_MODE`, `SET_SOCD` and
`SET_PROFILE_NAME` change the profile in RAM, and **the board uses the change at once** if the
profile is the current one. They are lost at the next power-up unless `AMC_SAVE_PROFILE`
follows. Selecting a profile, resetting one, the curve, the gamepad mode and calibration store
themselves.

### The profile

A board has three profiles (`PROFILE_COUNT`). One is current, chosen over USB, with the
`PROF1`–`PROF3` keycodes (the board's own, `QK_KB_n`), or with Fn + P + Z, X or C (matrix
positions (2, 10) and (4, 2–4) by default: those letters on a K10 HE). Each profile is one
packed C structure (`analog_matrix_type.h`, `analog_matrix_profile_t`), which is read raw and
written field by field. For a matrix of R rows and C columns, D DKS slots and S SOCD slots:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `global` — the profile's own key settings, the ones a key set to 0 follows |
| 4 | 4 × R × C | `key_config[r][c]`, row by row: key (r, c) at 4 + 4 (r·C + c) |
| 4 + 4RC | 19 × D | `okmc[i]` — the DKS slots |
| … + 19D | 3 × S | `socd[i]` |
| … + 3S | 30 | `name` |
| … + 30 | 2 | `crc16` — never computed |

**Profile size = 2 + 4(RC + 1) + 30 + 19D + 3S**, which is what `AMC_GET_PROFILES_INFO` reports.
On the K10 HE (6 × 21, D = S = 20) that is 980 bytes, 38 reads of 26 bytes, or 114 for all three
profiles. A client must find this size from the definition's matrix and the reported counts
**before parsing anything**. If its own computation disagrees with the reported size, the
definition and the firmware differ, and nothing read can be placed.

**The structures are packed bitfields.** GCC on ARM fills bitfields from the least significant
bit, and Keychron's `__packed__` lets them straddle bytes. The comments' sizes (4, 3 and 3
bytes) confirm that packing. Read each structure as one little-endian integer:

`analog_key_config_t`, 4 bytes, u32 LE `v`:

| Bits | Field | Meaning |
|---|---|---|
| `v & 0x3` | `mode` | 0 follow the profile, 1 regular, 2 rapid trigger |
| `(v >> 2) & 0x3F` | `act_pt` | actuation point, 0.1 mm; 0 follow the profile |
| `(v >> 8) & 0x3F` | `rpd_trig_sen` | rapid trigger, press sensitivity, 0.1 mm; 0 follow the profile |
| `(v >> 14) & 0x3F` | `rpd_trig_sen_deact` | rapid trigger, release sensitivity; 0 follow the profile |
| `(v >> 20) & 0xF` | `adv_mode` | 0 none, 3 DKS, 4 gamepad, 5 toggle — it overrides `mode` |
| `v >> 24` | `adv_mode_data` | the DKS slot, or the gamepad axis or button |

In `global`, `mode` is 1 or 2 (never 0), and the firmware repairs out-of-range values at
power-up: an actuation point of 0 or above 40 becomes 2.0 mm, a sensitivity of 0 or above 39
becomes 0.4 mm.

`okmc_config_t`, one DKS slot, 19 bytes:

| Bytes | Field |
|---|---|
| 0–2 | travel, u24 LE: `& 0x3F` shallow press point, `>> 6` shallow release point, `>> 12` deep press point, `>> 18` deep release point, 0.1 mm |
| 3–10 | four keycodes, u16 LE |
| 11–18 | four actions, 2 bytes each: low nibble shallow press, high nibble shallow release, then deep press, deep release |

`socd_config_t`, 3 bytes: byte 0 bits 0–2 key 1's row, bits 3–7 its column, byte 1 the same for
key 2, byte 2 the type. Rows above 7 or columns above 31 cannot be stored.

**These layouts are derived, not seen.** The first read should check them against the defaults:
a reset profile has `global` with `act_pt` 20 and both sensitivities 4.

### Actuation and rapid trigger

`AMC_SET_TRAVAL` sets the basic mode of the whole profile or of a set of keys:

| Byte | Field |
|---|---|
| `[2]` | profile |
| `[3]` | mode: 1 regular, 2 rapid trigger; 0 (follow the profile) for keys only |
| `[4]` | actuation point, 0–39 (0.1 mm); 0 follows the profile |
| `[5]` | press sensitivity (rapid trigger), 0.1 mm; 0 follows the profile |
| `[6]` | release sensitivity, 0.1 mm; 0 follows the profile |
| `[7]` | 1: set the profile's `global`; 0: set the keys below |
| `[8 + 3r .. 10 + 3r]` | row r's columns, u24 LE — up to 24 columns |

The firmware checks only the mode and the actuation point. A sensitivity above 63 wraps
silently into its 6 bits.

How the keys then behave (`action_regular_trigger.c`, `action_rapid_trigger.c`):

- **Regular**: pressed at the actuation point, released 0.3 mm above it. The 0.3 mm is fixed
  and not settable.
- **Rapid trigger**: first pressed at the actuation point. Then the key releases as soon as it
  rises by the release sensitivity, and presses again as soon as it goes down by the press
  sensitivity, wherever it is in its travel. It releases fully back above the actuation point
  minus 0.3 mm. Near the bottom there is no rapid release: the key must be above 3.8 mm
  (`BOTTOM_DEAD_ZONE`) minus the release sensitivity, so bottoming out does not chatter.

### Advanced modes

`AMC_SET_ADVANCE_MODE` puts one key into an advanced mode, or takes it out:

| Byte | Field |
|---|---|
| `[2]` | profile |
| `[3]` | 0 none, 1 DKS, 2 gamepad, 3 toggle |
| `[4]`, `[5]` | row, column |
| `[6]` | DKS: the slot, < 20. Gamepad: the axis or button, < 45, not 12 |
| `[7..10]` | DKS only: shallow press, shallow release, deep press, deep release points, 0.1 mm |
| `[11..18]` | DKS only: four keycodes, u16 LE |
| `[19..26]` | DKS only: four action pairs, as in the slot |

On the wire the operation numbers (0–3) differ from the stored `adv_mode` values (0, 3, 4, 5).

**DKS** (Keychron's "OKMC", one key, multiple commands; `action_okmc.c`) gives a key up to four
keycodes, each with an action at four points of the travel: going past the shallow point, going
past the deep point, coming back above the deep point, and coming back above the shallow point.
An action is three bits, played in order from bit 0, each a report sent 1 ms apart: bit 0
release, bit 1 press, bit 2 release. So `0b001` release, `0b010` press, `0b110` tap,
`0b111` release then tap. Some details matter to a client:

- **Only basic keycodes and modifiers act** (`IS_BASIC_KEYCODE`, `IS_MODIFIER_KEYCODE`). A layer
  key, a macro or any QMK keycode in a DKS slot does nothing.
- **The shallow release actions play only after the key went deep.** A key that goes past the
  shallow point and comes back without reaching the deep point releases its keycodes and plays
  nothing.
- **The client allocates the slots.** The key names its slot, but clearing the key does not
  clear the slot. A slot is free when no key in the profile refers to it, so finding one means
  reading every key.

**Toggle**: each press past the actuation point flips the key; it stays held until the next one.

**Gamepad**: the key drives a half-axis or a button of the board's game controller. Values
0–11 are half-axes (X left/right, Y down/up, Z −/+, then RX, RY, RZ the same way), 12 is
invalid, and 13–44 are buttons 0–31. In XInput mode the buttons follow the Xbox order
(`xinput_keycodes.h`): 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 View, 7 Menu, 8 L3, 9 R3, 10–13 the
D-pad up, down, left, right, 14 Xbox. The triggers are axes 4 and 5.

### SOCD

`AMC_SET_SOCD` pairs two keys: what to send when both are held (`action_socd.c`).

| Byte | Field |
|---|---|
| `[2]` | profile |
| `[3]`, `[4]` | key 1: row, column |
| `[5]`, `[6]` | key 2: row, column |
| `[7]` | slot, < 20 |
| `[8]` | type; 0 clears the slot |

The types: 1 the deeper key wins, and both stay pressed once both are past 3.8 mm; 2 the deeper
key wins, and past 3.8 mm the last one stays; 3 the last key pressed wins; 4 key 1 wins;
5 key 2 wins; 6 neither (both released). Here too, finding a free slot is the client's job.

### Game controller

The mode bits (`0x22`, `0x23`): bit 0 **XInput** (otherwise a HID joystick), bit 1 **typing**
(otherwise a gamepad key sends no keystroke). XInput is chosen when the board enumerates
(`usb_descriptor_override.c`), so changing it takes effect only at the next replug.

The **curve** maps travel to an analog axis: four points, each x in 0.1 mm (0–40) and y as the
output (0–127). The default is (0, 0), (10, 31), (30, 95), (40, 127). `SET_CURVE` is supposed
to require ascending x, but it checks the old curve instead of the new one. The client must
check it.

### Live readout

`AMC_GET_REALTIME_TRAVEL` reads one key as the firmware sees it:

| Byte | Field |
|---|---|
| `[2]` | status |
| `[3]`, `[4]` | row, column |
| `[5]` | travel, 0.1 mm |
| `[6]` | travel, 1/60 mm |
| `[7..8]` | raw ADC value, LE |
| `[9..10]` | calibrated rest value, LE |
| `[11..12]` | calibrated bottom value, LE |
| `[13]` | `0x11` pressed, `0x10` not |

That is one key per request, so polling a whole board is about a hundred requests. A view
would follow the selected key or a handful of them, not every key at once.

**The VIA matrix state** (`id_switch_matrix_state`) is answered from the analog matrix: each key
counts as pressed by its own mode (actuation, rapid trigger, toggle). The rows are big-endian,
as in VIA.

### How travel is computed

*Read 2026-11-08 at `hall_effect_playground` `bc56b3c6`, `analog_matrix.c`: `convert_to_travel()`,
`update_scale_factor()`. The K10 HE's own firmware is unpublished, but its clamp at 215 fits this
code exactly with F = 35, so the structure is taken to be the same with other constants.*

A Hall sensor's reading does not move linearly with the magnet's distance, so the firmware maps a
raw value through a cubic polynomial, `TRAVEL_POLYNOMIAL`. Its constants were fitted to one
reference switch: rest at ADC 3121, bottom at 1940, about 4.0 mm. Per key, it then:

1. shifts the reading by the key's own rest value against the reference's (`offset = rest - 3121`);
2. takes `P(x) - P(3121)`, the distance from rest along the reference curve;
3. multiplies by the key's **scale factor**, F / (P(bottom - offset) - P(3121)), where F is
   `FULL_TRAVEL_UNIT`;
4. clamps at (F + 1) x 6 - 1 sixtieths.

**So every key's calibrated bottom reads F, whatever the switch.** The firmware never measures
millimetres. It measures the fraction of each key's own calibrated travel, and calls F of them
"full travel". This bears on boards with different switch types:

- **Mixing switches works without the client knowing.** A 4.0 mm switch on a firmware built for
  3.5 mm (F = 35), once calibrated, still reads 0 to 3.5 "mm". An actuation point of 1.7 is half
  its travel, about 1.94 mm physically.
- **The millimetres are exact only for the switch the polynomial was fitted to.** Elsewhere they
  are its fraction of travel, bent by a curve fitted to another magnet. A client should say
  "mm" as the firmware does, but cannot promise physical distances on a mixed board.
- **The maximum of every travel control is F, one value per firmware**, not per key. A per-key
  maximum is not needed.
- **A key fitted with a new switch reads wrong until it is calibrated again**, because its scale
  factor still describes the old one. Auto-calibration (below) repairs that as the key is used.
  A manual calibration repairs it at once.

### Calibration

Calibration is two values per key: the raw value at rest and at the bottom. They are kept in
EEPROM, and three things change them.

**At power-up the board takes the rest values itself**: 8 scans, averaged, minus a 20-count dead
zone. That is why keys must not be held while it is plugged in. A key reading out of range (above
3500, or below 3000 - 900/5) makes the whole power-up calibration invalid, and the old values
stay. A key held at power-up is marked, so auto-calibration waits for its release.

**Auto-calibration runs all the time** (`auto_caliration_check()`, sic, on every scan). It watches
each key for full presses: down past ADC 2200, then back to a steady rest within a second. Each
such press adds confidence, more for a deeper press (a span over 900, 1000 or 1100). Once a key
reaches 12, its rest and bottom are replaced if they moved (rest by more than 10, bottom by more
than 30). They are written to EEPROM if they differ from the stored ones by more than 15 or 50.
**So a board calibrates itself while it is typed on**, a few deep presses per key. The manual
calibration is for doing it at once: after a switch change, or when keys misfire.

**A manual calibration** (`set_calibrate()`, `calibrate()`) redoes both values:

1. `[0xA9, 0x40, 2]`: **every key's values are cleared in RAM**, and the board samples every key
   at rest (8 scans), so nothing may be touched. It then moves itself to state 3.
2. The user presses **every key to the bottom**. A key counts once 8 samples average deeper than
   rest - 800. Its bottom is that average plus a jitter margin, and it is stored only if deeper than
   the bottom it already has. `AMC_GET_CALIBRATE_STATE` shows which keys are done.
3. Three seconds after **every** key is done, the board stores the values and stops.
   Alternatively, `[0xA9, 0x40, 4]` stores and stops early: the keys not pressed get their old
   bottom value back.

Any other value is refused. **Between steps 1 and 3 the keys not yet pressed have no bottom
value**, so the board is not usable for typing until it stops. A client should keep the user in
the calibration until it ends, with a visible way to stop it (`0x40 4`). `AMC_GET_CALIBRATE_STATE` answers `[2]` the calibrated bits (`0x01`
rest, `0x02` bottom), `[3]` the state (0 off, 1 power-up, 2 rest, 3 bottom, 4 storing), and
`[4 + 3r .. 6 + 3r]` row r's keys already pressed to the bottom, u24 LE.

---

## Firmware bugs a client must step around

| Command | Bug | What the client does |
|---|---|---|
| `0xA9 0x12` `GET_PROFILE_RAW` | It checks `offset + size` against all three profiles, not one. Past the profile's end, `size` underflows and the firmware copies up to 255 bytes into the 32-byte report. A `size` above 26 also overflows | Reads only inside one profile, in at most 26 bytes |
| `0xA9 0x12` | No status: a refused read leaves the request in the reply, which reads as zeros | Validates the profile number and offsets itself |
| `0xA9 0x42` with row = col = `0xFF` | Meant to report the matrix size; then reads the key at (255, 255), out of bounds | Takes the size from the definition and never sends `0xFF` |
| `0xA9 0x21` `SET_CURVE` | Validates the old curve, not the new one | Checks that x ascends |
| `0xA8 0x09` / `0x0A` | No check of `first + count` against the LED count | Stays below `RGB_GET_LED_COUNT` |
| `0xA8 0x06`, `0x0E`, `0x0F` | Off by one: row `== MATRIX_ROWS` and region `== EFFECT_LAYERS` pass | Stays in range |
| `0xAA` without wireless | Falls into `0xAB`: no reply | Never sends it |
| `id_switch_matrix_state` on `hall_effect_playground` | Answered twice (via-vial-commands.md, "Stale reports") | Discards waiting reports before each request — done |

---

## Implications for the Nazg backend

1. **A Keychron extension of the VIA branch, found by probing.** After the VIA load: `0xA0`
   (`0xFF` means not Keychron), then `0xA2` for the feature bits, `0xA7 0x01` for the misc bits
   (`0xFF` on `hall_effect_playground` builds), and on an HE board `0xA9 0x01` (version `0x04`)
   and `0xA9 0x10` for the profile geometry. A stock VIA board answers `0xFF` to `0xA0` and looks
   exactly as it does now.

   **Probe each group; do not gate it on the feature bits.** They under-report: the K10 HE read on
   2026-11-08 answers `0xA8` with bit 7 clear and `0xA7 0x12` with every misc bit clear (see
   "The 2026-11-08 read"). **Only `0xFF` means a group is absent** -- for `0xA9` sub-ids, the
   request echoed unchanged. Take the bits as a hint for what to probe first, never as the answer.

2. **Read past unsolicited reports -- done (2026-10-16).** A request carries a reply check
   from the protocol (`ReplyCheck`, `transport/NazgHidTransport.h`); the transport keeps reading
   until a report passes it or the timeout runs out, and drops the rest. VIA's check is the
   request's first bytes echoed -- the command id, then the address the firmware keeps (value
   id, layer/row/column, offset and size, channel and value id, the paged matrix's first row)
   -- or `0xFF` in byte 0; Vial's `0xFE` commands get none, their reply overwriting byte 0. A
   Keychron command will supply its own. Using the layer report on the way waits for a feature
   that wants it, and a reader that keeps the device open. Not met on a board yet: the K10 HE
   runs older code, and the Q6's build is unknown.

3. **"Written at once" means a save after each change.** Nazg writes settings as they are
   changed. Here a write lands in RAM, so each one needs `AMC_SAVE_PROFILE` (or `RGB_SAVE`,
   `SNAP_CLICK_SAVE`) after it. QMK's `eeprom_update_block` rewrites only the bytes that
   changed, so saving a whole profile each time costs little wear.

4. **The profile is the model.** Read all three profiles with the board, decode them with the
   layouts above, and check the computed size against the reported one first. Keychron's
   `via_json` definitions give the matrix, so a board without a definition cannot be read.

5. **The profile layout is proven, `key_config` and its indexing included.** A K10 HE's profile 0
   decoded exactly on 2026-11-08 -- `act_pt` agreeing with Keychron's Launcher, and the per-key
   entries landing on exactly the 104 positions the board has keys at. The adapter's own code read
   it over the real transport (`nazg-probe-keychron`), not only a scripted channel. What no board
   has shown yet: `adv_mode` / `adv_mode_data`, and a populated DKS or SOCD slot. Those still want
   a board before a write depends on them.

   **A board the Launcher has touched is not in its default state** (see "The 2026-11-08 read"):
   it rewrites every key. Read the profile; never assume one.

6. **Calibration last, behind a confirmation**, and only once the rest has been seen on a
   board. A wrong calibration makes the keys misfire until it is redone, and it is a tester's
   board.

7. **Two keycode spaces.** DKS slots hold 16-bit QMK keycodes, but only basic keys and modifiers
   act; snap click holds 8-bit basic keycodes. Both are a restricted picker, not Keymap's.

8. **Mixed RGB effects are RGB Matrix mode numbers**, with the naming trap VIA's lighting
   already has.

9. **Every travel control's maximum is the board's, and the board will not say what it is.**
   A K10 HE tops out at 3.5 mm, not the source's 4.0 (Units; "The 2026-11-08 read"), so an
   actuation or sensitivity slider that runs to 4.0 lets a user ask for a point the switch never
   reaches. Nothing reports it: the exact value needs a key held to the bottom, and the curve's
   last x is only a hint. **The travel maximum therefore belongs in the capability model**, read
   once when the board is opened and carried in the `BoardReport` beside the profile geometry,
   the way the matrix size already is -- not a constant in the UI. It is one value per firmware,
   not per switch: calibration scales every key to it, so a board with mixed switches still has
   one maximum ("How travel is computed"). The curve's last x estimates it without the user, and
   any key's live readout pinned at (F + 1) x 6 - 1 settles it exactly.

---

## Sources

Read at the commits named above, under `keyboards/keychron/common/`:

- `keychron_raw_hid.c`, `keychron_raw_hid.h` — the top-level ids, the feature and misc bits,
  the misc dispatch, the matrix state answer
- `state_notify.c` — the unsolicited reports
- `dfu_info.c`, `language/language.c`, `debounce/keychron_debounce.c`, `snap_click/snap_click.c`,
  `usb_report_rate.c`, `nkro.c`, `wireless/wireless.c` (`wireless_raw_hid_rx`) — the misc group
- `rgb/keychron_rgb.c`, `rgb/keychron_rgb_type.h`, `rgb/rgb_matrix_kb_config.h` — `0xA8`
- `analog_matrix/analog_matrix.c`, `analog_matrix.h`, `analog_matrix_type.h`,
  `analog_matrix_eeconfig.h`, `profile.c`, `action_okmc.c`, `action_rapid_trigger.c`,
  `action_toggle.c`, `action_socd.c`, `action_socd.h`, `game_controller_common.c`,
  `game_controller_common.h`, `xinput_keycodes.h`, `usb_descriptor_override.c` — `0xA9`
- `keychron_common.mk` — what every board is built with
- `wireless/lkbt51.c` (`lkbt51_dfu_rx`), `factory_test.c` — `0xAA` and `0xAB`, read only to
  know what not to send
- `keyboards/keychron/k10_he/`, `q1_he/`, `q6_he/` — matrix sizes, the features an HE board enables
