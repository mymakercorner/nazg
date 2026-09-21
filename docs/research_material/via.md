# VIA — Findings

*Investigated 2026-09-20. Transport figures verified against QMK source; protocol and
ecosystem details are general knowledge unless a source is cited.*

## Summary

VIA is a runtime keymap configurator for QMK keyboards. It talks to the keyboard over
**USB raw HID** and renders a graphical keyboard from a **JSON definition that lives outside
the firmware** — in a central registry, or side-loaded by the user.

VIA is the baseline the others react against: it established that runtime remapping with a
good graphical UI is possible, and its two structural choices (external definitions, browser
client) are what VIAL, XAP and ZMK Studio each set out to fix.

## Transport — USB raw HID

Verified from QMK source:

| Property | Value | Source |
|---|---|---|
| Report size | **32 bytes** (`RAW_EPSIZE`) | `tmk_core/protocol/usb_descriptor.h` |
| Endpoint type | Interrupt IN + interrupt OUT | |
| Polling interval | **`PollingIntervalMS = 0x01`**, hardcoded | `tmk_core/protocol/usb_descriptor.c` |

The raw HID endpoints hardcode a 1 ms interval and are **independent of
`USB_POLLING_INTERVAL_MS`**, which defaults to 10 ms and applies only to the keyboard, mouse
and shared (NKRO/media) interfaces.

### Throughput

| Metric | Value |
|---|---|
| Per-direction ceiling | 32 B/ms = **32 KB/s ≈ 256 kbit/s** |
| Request/response round trip | ~2 ms (OUT in frame N, IN poll in frame N+1) |
| Effective transaction rate | ~500/s |
| Realistic payload throughput | **~15 KB/s** after the command-ID byte |

In practice, host HID-stack overhead makes this worse — hidapi round trips on Windows
frequently exceed the theoretical 2 ms.

**This is sufficient.** A full keymap of 4 layers x 100 keys x 2 bytes is 800 bytes, about 30
transactions, roughly 60 ms. What makes VIA feel slow is **chattiness**, not bandwidth —
which is why `dynamic_keymap_get_buffer` was added to batch reads.

See [README.md](README.md) for the raw HID vs CDC-NCM throughput comparison.

## The definition problem

VIA renders its graphical keyboard from a **KLE-format JSON definition** describing key
positions, sizes and rotations. The definition is *not* on the keyboard. It comes from:

1. The central VIA keyboard registry, or
2. A JSON file the user side-loads via the design/"unsupported" tab

Consequences:

- **A new keyboard does not work out of the box.** It must be accepted into the registry, or
  every user must be handed a JSON file.
- **Version skew.** The definition and the firmware can drift apart with nothing enforcing
  agreement.
- **A gatekeeper exists.** Registry acceptance is a process outside the designer's control.

This is the single complaint that VIAL, XAP and ZMK Studio all independently set out to fix,
and all three fixed it the same way: **put the definition in the firmware**.

## Important: the graphics were never the transport's doing

VIA's UI is a genuinely good graphical keyboard — correctly shaped keys, accurate layout,
rotated keys on split boards, click-to-remap. All of it is rendered from **pure data** over a
32-byte-packet HID pipe.

This matters for design reasoning: a "configuration transport only" protocol does **not**
imply a text-mode UI. A UI *description* is small; the renderer supplies the richness. VIA is
the existence proof. Its failure was *where the JSON lived*, not what it could express.

## Client and browser support

VIA is used through a web application (and an Electron wrapper). The browser path depends on
**WebHID**, which is Chromium-only:

- **Firefox: will not implement.** Mozilla's standards position on WebHID and WebUSB is
  *harmful*.
- **Safari/WebKit: also opposed**, citing fingerprinting and security.

This is a durable position from two of the three engines, not a backlog item. Any
WebHID-based configurator is permanently Chromium-only.

## Security

**VIA has no unlock mechanism.** Any local process with HID access can rewrite the keymap
silently. On a device whose entire function is synthesising keystrokes, that is a
keystroke-injection vector — remap a key to a macro that opens a terminal and fetches a
script and you have code execution.

VIA is the only system in this survey without physical-presence gating. VIAL, XAP and ZMK
Studio all have one.

## Extending with custom features

VIA offers two tiers, and the choice is whether the **stock client** can render your feature.

### Tier 1 — custom channels + V3 `menus` (stock client renders it)

**Firmware:** implement `via_custom_value_command_kb()` (or `_user`), handling
`id_custom_set_value` / `id_custom_get_value` / `id_custom_save` against a
`[channel_id, value_id]` pair.

**Definition:** the V3 keyboard JSON carries a `menus` section declaring UI controls —
sliders, toggles, dropdowns, colour pickers — each bound to a `[channel_id, value_id]`.

**Result:** stock VIA renders your controls. No client work at all.

**Limit:** you are confined to VIA's widget vocabulary. Anything it cannot express is
out of reach.

### Tier 2 — `raw_hid_receive_kb()` (own client required)

Arbitrary commands outside the protocol. Unlimited expressiveness, but invisible to stock
VIA, so you ship your own client and hand-maintain the wire format on both ends with nothing
verifying they agree.

### Assessment

VIA's declarative tier is the **best of the four for simple cases** — it is the only one
where a designer declares a widget and a stock third-party client draws it. Its ceiling is
low, and the fall from tier 1 to tier 2 is a cliff, not a slope.

## Strengths

- **Mature and ubiquitous.** The largest install base; most commercial QMK boards ship
  VIA-enabled.
- **Excellent graphical UI**, and proof that a data-only protocol supports one.
- **In mainline QMK** — no fork required, `VIA_ENABLE = yes`.
- **Low resource cost.** Raw HID is already present; no TCP/IP stack, no extra USB class.
- **Invisible to IT.** One more HID interface; no network adapter, no mass storage, no
  policy triggers.
- **Good worst-case latency.** Interrupt endpoints get a reserved slot in every frame, unlike
  bulk transfers which have no timing guarantee at all.

## Weaknesses

- **External definition registry** — the core complaint. New boards are not supported until
  registered, or require hand-delivered JSON.
- **Chromium-only in the browser.** No Firefox, ever.
- **No security gating.** Silent remapping by any local process.
- **Chatty protocol.** ~500 round trips/s makes per-item requests feel sluggish; partially
  mitigated by buffer commands.
- **Limited scope.** Keymap, layers, macros, and some lighting. No self-described behaviour
  vocabulary — the client must already know what everything means.
- **32-byte reports** are half the 64-byte Full Speed interrupt maximum, so even the available
  headroom is not fully used.

## Relevance to a multi-protocol client

VIA is the **least expressive** of the four and should be treated as a lossy projection of a
richer internal model, not as the model itself. Designing a client around VIA's shape and
stretching it to fit ZMK Studio would be painful; the reverse is straightforward.

The definition-sourcing problem means a VIA backend needs its own answer for where layout
JSON comes from — bundled registry snapshot, user-supplied file, or both.

## Sources

- [qmk_firmware/tmk_core/protocol/usb_descriptor.h](https://github.com/qmk/qmk_firmware/blob/master/tmk_core/protocol/usb_descriptor.h)
- [qmk_firmware/tmk_core/protocol/usb_descriptor.c](https://github.com/qmk/qmk_firmware/blob/master/tmk_core/protocol/usb_descriptor.c)
- [Configuring QMK — USB_POLLING_INTERVAL_MS](https://docs.qmk.fm/config_options)
- [Raw HID — QMK Firmware](https://docs.qmk.fm/features/rawhid)
- [WebHID API — Mozilla standards-positions (harmful)](https://github.com/mozilla/standards-positions/issues/459)
- [WebUSB API — Mozilla standards-positions (harmful)](https://github.com/mozilla/standards-positions/issues/100)
