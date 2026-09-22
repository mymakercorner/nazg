# VIAL — Findings

*Investigated 2026-09-20. Transport figures shared with [VIA](via.md) and verified against
QMK source; ecosystem details are general knowledge unless a source is cited.*

## Summary

VIAL is a VIA-derived configurator whose defining contribution is **embedding the keyboard
definition in the firmware**. The keyboard describes itself; no registry, no side-loaded JSON,
no version skew.

It pays for that with a **forked QMK repository**, which is its defining drawback.

**For the wire format** — the `0xFE` command space, the definition download, the unlock flow
and the protocol version history — see [via-vial-commands.md](via-vial-commands.md). This
document stays strategic.

## What VIAL got right — the embedded definition

VIAL ships a **compressed (LZMA) keyboard definition inside the firmware image**. On connect,
the client pulls it over raw HID and renders the graphical keyboard from it.

This single change fixes VIA's central structural problem:

| | VIA | VIAL |
|---|---|---|
| Definition source | external registry or side-loaded JSON | **in the firmware** |
| New board works immediately | no | **yes** |
| Definition/firmware skew possible | yes | **no** |
| Gatekeeper | registry maintainers | **none** |

The payload is small — a few KB compressed, roughly 100 transactions, about 200 ms over raw
HID. Compression exists precisely because the transport is slow; see [via.md](via.md) for the
throughput figures.

**This is the idea worth stealing.** XAP and ZMK Studio both arrived at the same answer
independently.

## Transport

Identical to VIA: **USB raw HID**, 32-byte reports (`RAW_EPSIZE`), 1 ms interrupt polling,
about 15 KB/s of effective payload for request/response traffic. See [via.md](via.md#transport--usb-raw-hid).

## Security — VIAL has an unlock, and had it first

VIAL supports a **physical unlock combination**: a key combination defined by
`VIAL_UNLOCK_COMBO_ROWS` / `VIAL_UNLOCK_COMBO_COLS`, held for a few seconds, before the
protocol will accept modifications.

It can be disabled at build time with `VIAL_INSECURE=yes`.

VIAL reached this design **before** XAP and ZMK Studio. Across the survey:

| | Physical unlock |
|---|---|
| VIA | no |
| **VIAL** | **yes** (optional, `VIAL_INSECURE` to disable) |
| XAP | yes (secure routes + unlock sequence) |
| ZMK Studio | yes (`&studio_unlock`, mandatory) |

Three independent teams converged on physical-presence gating. VIA is the outlier.

### On disabling it

Turning the unlock off is defensible for boards you design, flash and use yourself — you are
the only actor in that threat model, and the convenience gain is real.

The calculus changes when a board ships to someone else. The attack the unlock prevents is a
local process silently remapping a key to a shell macro, which is code execution on every
machine the keyboard touches. ZMK made its unlock **mandatory** specifically because it ships
to strangers. VIAL left it optional; the default should probably be "on" for anything sold or
given away.

## Extending with custom features

Mechanically the same two tiers as [VIA](via.md#extending-with-custom-features):

1. **VIA custom channels + V3 `menus`** — `via_custom_value_command_kb()` against
   `[channel_id, value_id]`, with widgets declared in the definition. Stock client renders it.
2. **`raw_hid_receive_kb()`** — arbitrary commands, own client required.

Plus VIAL's own QMK settings system for exposing build-time options at runtime.

### The practical experience

In practice, non-trivial custom features land in tier 2, and the consequence is that you
write and maintain a bespoke client. The wire format then exists in two hand-written places
— firmware and client — with nothing checking that they agree. There is no codegen, no
schema, and no version negotiation for your own commands; if you change a struct on one side
you find out at runtime.

This is exactly the gap XAP's `xap.hjson` codegen closes
(see [xap.md](xap.md#extending-with-custom-features)): the same custom feature, declared once,
with firmware handlers and client bindings generated from a single source.

### Assessment

Adequate and widely used, but hand-maintained. Since you are already on a fork, there is no
*additional* fork cost to extending — which is the one silver lining of VIAL's main drawback.

## Strengths

- **Self-describing.** The defining advantage; no registry, no JSON hand-delivery, no skew.
- **Physical unlock available**, and it predates the others.
- **Good graphical UI**, inherited from and comparable to VIA's.
- **Richer feature coverage than VIA** — tap dance, combos, key overrides, QMK settings
  exposed at runtime.
- **Same cheap transport.** Raw HID, no extra USB class, invisible to IT policy.
- **Desktop application available**, so the Chromium-only web path is avoidable in practice.
  (The Vial client is **Python + Qt**, not Electron — VIA's is the Electron/TypeScript one.
  See [client-architecture.md](client-architecture.md#implementation-stack) for why that
  distinction matters when picking a stack.)

## Weaknesses

- **The fork.** VIAL maintains a forked QMK repository with changes that make it incompatible
  with mainline VIA. This is the structural cost: ongoing rebase burden, lag behind QMK
  features, ecosystem fragmentation, and a hard choice for board designers.
- **Fragmentation is user-visible.** A board is a VIA board or a VIAL board, and users must
  know which client to use.
- **Same transport ceiling** as VIA — about 15 KB/s, and the same chattiness characteristics.
- **Unlock is optional and off by default in many builds**, so in practice much of the VIAL
  install base has no protection.
- **Fixed vocabulary.** Like VIA, the client must already understand every feature; the
  definition describes layout and enabled features, not a typed parameter vocabulary the way
  ZMK Studio's behaviour metadata does.

## The design lesson

VIAL demonstrates that the two properties are **separable**:

- *Where the definition lives* — VIAL fixed this (firmware).
- *Where the UI lives* — VIAL did not (still an external client).

GP2040-CE closes both; ZMK Studio closes the first and makes the second painless with a
native app. The fork is not inherent to the embedded-definition idea — it is an artefact of
VIAL having to implement it outside mainline QMK. XAP is the attempt to do the same thing
in-tree, and it has been in draft for five years (see [xap.md](xap.md)) — which is a fair
measure of how hard "the same idea, but upstream" turns out to be.

## Relevance to a multi-protocol client

VIAL is the closest existing match to the target behaviour and is the obvious second backend
after VIA. Its embedded-definition retrieval is the model to generalise.

Because VIAL is VIA-derived, a client can share a large amount of the VIA backend code, with
divergence in the definition-retrieval path and the extra feature commands.

## Sources

- Transport figures: see [via.md](via.md) sources
- [qmk_firmware/tmk_core/protocol/usb_descriptor.c](https://github.com/qmk/qmk_firmware/blob/master/tmk_core/protocol/usb_descriptor.c)
