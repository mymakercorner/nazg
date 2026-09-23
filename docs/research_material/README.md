# Keyboard & Controller Runtime Configuration — Technology Survey

*Compiled 2026-09-20. Investigation only — no implementation was undertaken.*

## Purpose

An examination of how input devices expose **runtime configuration** to a host, prompted by
GP2040-CE's embedded web configurator and framed by a practical question: can a QMK-based
keyboard get a VIA/VIAL-class configuration experience **without** an external definition
registry, a browser dependency, or a firmware fork?

## The documents

| Document | Subject |
|---|---|
| [gp2040-web-config.md](gp2040-web-config.md) | GP2040-CE's USB-networking web configurator; RNDIS/ECM/NCM; NCM maturity audit |
| [via.md](via.md) | VIA — raw HID, external definition registry, the baseline |
| [vial.md](vial.md) | VIAL — embedded definition, forked QMK |
| [xap.md](xap.md) | QMK XAP — official in-tree successor, five years in draft |
| [zmk-studio.md](zmk-studio.md) | ZMK Studio — protobuf over CDC-ACM/BLE, fully self-describing |
| [client-architecture.md](client-architecture.md) | Design notes for a multi-protocol configurator client |
| [via-vial-commands.md](via-vial-commands.md) | **Implementation level** — every VIA and VIAL command, payload layouts, and the protocol version history of both *(added 2026-09-22)* |
| [keycodes.md](keycodes.md) | **Implementation level** — QMK keycode versions, VIA/VIAL dictionary coverage, choosing a dictionary per board, and a protocol-neutral keycode representation *(added 2026-09-23)* |

Each protocol document carries an **"Extending with custom features"** section; they are
summarised under [Adding custom features](#adding-custom-features) below.

The documents above are a **strategic** comparison: which design to build on, and why. The
command reference is the **implementation** layer under it, written when the VIA/VIAL backend
work began. It repeats none of the strategy and the strategy documents contain no command
detail, so the two do not overlap.

---

## The core design axis

Every system here answers two independent questions. Almost all confusion in this space comes
from conflating them.

1. **Where does the device's self-description live?** (the registry problem)
2. **Where does the UI live?** (the install/browser problem)

| | Definition lives | UI lives | Fork required |
|---|---|---|---|
| **VIA** | external registry / side-loaded JSON | external web app (Chromium) | no |
| **VIAL** | **in the firmware** | external app (web or desktop) | **yes** |
| **XAP** | **in the firmware** | external app (Tauri desktop) | no (in-tree branch) |
| **ZMK Studio** | **in the firmware** | external app (Tauri desktop or web) | no |
| **GP2040-CE** | **in the firmware** | **in the firmware** | n/a (own project) |

GP2040-CE is the only one that closes *both*. That is what makes it interesting, and — as
[gp2040-web-config.md](gp2040-web-config.md) argues — what makes it unsuitable for keyboards.

A crucial separate finding: **the transport does not determine how rich the UI can be.**
VIA renders an excellent graphical keyboard over a 32-byte-packet HID pipe. The renderer
supplies the richness; the transport only carries a description. The real distinction is
**declarative description vs arbitrary code**, and only GP2040-CE's HTTP model offers the
latter — which is precisely its security problem.

---

## Comparison matrices

### Transport

| | Transport | Packet / framing | Driverless on |
|---|---|---|---|
| VIA | USB raw HID, interrupt | 32 B reports, 1 ms | all OSes |
| VIAL | USB raw HID, interrupt | 32 B reports, 1 ms | all OSes |
| XAP | USB raw HID | 32 B reports | all OSes |
| ZMK Studio | **CDC-ACM** serial + **BLE GATT** | protobuf v3, `0xAB`/`0xAC`/`0xAD` framing | all OSes (incl. Win 10) |
| GP2040-CE | USB RNDIS / CDC-ECM (dual config) | Ethernet frames, TCP/IP, HTTP | needs the dual-config trick |

### Throughput (USB Full Speed, e.g. RP2040)

| | Ceiling per direction | Realistic | Notes |
|---|---|---|---|
| Raw HID (interrupt, 32 B/ms) | 32 KB/s ≈ 256 kbit/s | **~15 KB/s** for request/response | **Guaranteed** slot every frame |
| CDC-NCM (bulk, 64 B x 19/frame) | ~1.19 MB/s ≈ 9.5 Mbit/s | ~300–600 KB/s (lwIP on Cortex-M0+) | **No** latency guarantee |

About **37x on paper, 20–40x in practice** — and it is the wrong comparison to optimise for:

- **Bulk has no latency guarantee.** Interrupt endpoints reserve a slot in every frame; bulk
  is the lowest-priority transfer type. For small request/response traffic, raw HID's worst
  case is *better*.
- **Payloads are tiny.** A VIAL definition is a few KB compressed (~200 ms). A 4-layer,
  100-key keymap is 800 bytes (~60 ms). Neither is perceptible.
- **What feels slow is chattiness**, not bandwidth — hence VIA's `dynamic_keymap_get_buffer`.
- **The 37x exists to carry a payload that only exists because of the architecture.**
  GP2040-CE's React bundle is several hundred KB — a second or two over NCM, 30+ seconds over
  raw HID. HTTP genuinely *requires* the fast pipe, but it requires it to ship **a user
  interface**, not **configuration**. The configuration itself is ~1000x smaller than the
  application used to edit it.

### Security

| | Physical unlock | Notes |
|---|---|---|
| VIA | **no** | Any local process can silently rewrite the keymap |
| VIAL | yes, optional | `VIAL_UNLOCK_COMBO_ROWS/COLS`; `VIAL_INSECURE=yes` disables. **First to ship this.** |
| XAP | yes | "Secure routes" + unlock sequence + Secure Status broadcast; already merged to mainline QMK |
| ZMK Studio | **yes, mandatory** | `&studio_unlock` keybinding |
| GP2040-CE | n/a | Unauthenticated local HTTP server — acceptable for a gamepad, not for a keyboard |

**Three independent teams converged on physical-presence gating**, VIAL first. VIA is the
outlier. This is strong evidence that it is the correct pattern for any device that can
synthesise keystrokes.

### Expressiveness

| | Layout geometry | Typed behaviour params | Lighting | Audio | Logging |
|---|---|---|---|---|---|
| VIA | yes (external JSON) | no | partial | no | no |
| VIAL | yes (embedded) | no | partial | no | no |
| XAP | yes | route-based | **yes** | **yes** | **yes** (broadcasts) |
| ZMK Studio | **yes (embedded, with rotation)** | **yes** | not yet | no | no |

---

## Adding custom features

How a designer adds something the protocol does not already define. Full detail in each
protocol document.

| | Mechanism | Stock client renders it | Fork needed |
|---|---|---|---|
| **VIA** tier 1 | custom channels + V3 `menus` JSON | **yes**, declared widgets only | no |
| **VIA** tier 2 | `raw_hid_receive_kb()` | no — own client | no |
| **VIAL** | as VIA, plus settings | yes for menus | already on a fork |
| **XAP** | **`xap.hjson` fragment -> reserved 0x02 KB / 0x03 USER** | no UI, but generated bindings + docs | **no** |
| **ZMK** — behaviour | devicetree `display-name` + C parameter metadata | **yes, automatically** | **no** |
| **ZMK** — new subsystem | modify `zmk-studio-messages` protobuf | n/a | **yes** |
| **GP2040-CE** | modify the whole stack (add-on + proto + handler + React) | n/a — you ship the UI | n/a |

### The two kinds of extensibility

These are separate properties, and no system here has both:

- **Protocol extensibility** — can a designer add new commands without forking?
  **XAP wins decisively.** Reserved subsystem IDs, spec fragments that merge with directory
  inheritance, and codegen covering firmware, docs and Python bindings from one source.
- **Semantic discoverability** — can a client that has never heard of the feature render a
  sane UI for it? **ZMK wins.** Typed parameter metadata (`range{min,max}`, `layer_id`,
  `hid_usage`) lets the stock client build an editor automatically.

XAP and ZMK are exact mirror images. VIA sits in between with a low ceiling: its declarative
`menus` tier gives real semantic discoverability, but the drop to `raw_hid_receive_kb()` is a
cliff rather than a slope.

### Three layers of description

The distinction that explains the table:

| | What a declaration describes | GUI derivable? |
|---|---|---|
| VIA V3 `menus` | **UI widgets** — slider/toggle/dropdown + options | Yes; it *is* the UI |
| ZMK behaviour metadata | **Semantics** — range bounds, layer, HID usage | Yes; map semantics to widgets |
| XAP `struct_members` | **Wire format** — field name + byte type | **Partially** |

XAP declares named, typed fields but no ranges, enumerations, units or UI intent — a scan of
all five spec versions finds no `min`, `max`, `range`, `enum`, `values` or `step`. A client
can still derive labels, checkboxes for `bool`, numeric inputs bounded by type, grouping from
struct nesting, and visibility from the capabilities bitmask and `permissions: secure`. It
cannot know that a `u8` is a 0–255 brightness slider or that another is an effect enum.

The gap is small and **additive** — optional `min`/`max`/`enum` keys on `struct_members`
would be backwards-compatible, and the spec format is still open. (Attempting it locally
also means editing `data/schemas/xap.jsonschema`, which sets `additionalProperties: false`
in eight places.)

---

## Design differences that matter

### 1. Registry vs embedded definition

VIA's structural flaw. A new board is unusable until accepted into a central registry or
accompanied by a hand-delivered JSON file, and definition/firmware skew is possible.

VIAL, XAP and ZMK Studio independently reached the same fix: **put the definition in the
firmware**. This is the single most important idea in the survey.

### 2. Declarative description vs arbitrary code

GP2040-CE's HTTP server lets the device ship *executable* UI — arbitrary bespoke behaviour.
Everything else ships a *description* the client interprets.

The trade:

- **Arbitrary code:** unlimited expressiveness; no client update needed for novel features.
  But it is a code-execution channel from a USB device, which is disqualifying on a keyboard.
- **Declarative description:** auditable, safe, but bounded by the client's vocabulary.
  Extending the vocabulary means a client update, not just firmware.

ZMK Studio shows the declarative path need not be limiting: by describing **parameter types**
(`range`, `layer_id`, `hid_usage`, `constant`, `nil`) the device lets a client render a usable
editor for a behaviour it has never seen. That is the resolution of the apparent dilemma.

### 3. Assignment vs definition

ZMK Studio can only *assign* what devicetree already declared — existing behaviours, existing
layer allocations, declared physical layouts. New behaviours or extra layers still require a
rebuild.

This is a deliberate trade for safety and footprint, and it is worth knowing it is the norm
rather than a ZMK peculiarity.

### 4. Interrupt vs bulk — latency vs throughput

Raw HID buys a guaranteed slot per frame; USB networking buys peak bandwidth with no timing
commitment. For configuration traffic the guarantee is worth more than the bandwidth.

### 5. Mode-exclusive vs live

GP2040-CE reboots into a config mode where it is no longer a gamepad — acceptable for a
fight stick. Keyboards want live editing while typing, which is VIA's genuine killer feature
and a reason composite operation matters there.

---

## Strong and weak points at a glance

### GP2040-CE embedded web server

**Strong** — truly zero-install; a browser as the UI toolkit (and the only browser-based
option here that works in **Firefox**, because HTTP needs no hardware API); both definition
and UI on-device; point-to-point isolation; self-contained DHCP/DNS/mDNS/HTTP.

**Weak** — RNDIS security record; no macOS RNDIS support (hence dual config); Full Speed
throughput ceiling; large flash/RAM cost; mode-exclusive; keep-alive disabled to dodge a
lockup bug; DNS catch-all can break host resolution; a USB device spawning a NIC triggers
corporate VPN/firewall/DLP; hardcoded subnet.

### VIA

**Strong** — mature and ubiquitous; excellent graphical UI; mainline QMK; cheap transport;
invisible to IT policy; good worst-case latency.

**Weak** — external definition registry; Chromium-only in the browser; **no security gating
at all**; chatty protocol; limited scope; least expressive of the four.

### VIAL

**Strong** — embedded self-describing definition (the key idea); physical unlock, and first to
have one; good UI; richer feature coverage than VIA; desktop app avoids the browser.

**Weak** — **the forked QMK repository** and the fragmentation it causes; unlock optional and
often disabled; same transport ceiling; fixed client-side vocabulary.

### XAP

**Strong** — best-designed protocol; broadest subsystem coverage (lighting, audio, log
broadcasts); versioned with capability queries; secure routes; in-tree, cleanly mergeable, and
has already fed Secure, encoder mapping and keymap introspection into mainline QMK.

**Weak** — **five years in draft**; spec format still not finalised (#15601), so anything
built against it can be invalidated by a revision; JavaScript bindings not started (Python
ones do work); QMK-wide prerequisites unresolved; effectively no review; **zero ecosystem
adoption** — no shipping firmware uses it.

### ZMK Studio

**Strong** — fully self-describing including layout geometry *and* typed behaviour parameters;
mandatory physical unlock; driverless CDC-ACM on every OS including Windows 10; BLE transport
too; native app sidesteps the browser question; upstream, no fork; multiple runtime-switchable
physical layouts; device-initiated notifications.

**Weak** — ZMK not QMK; assignment-only, bounded by devicetree; keymap-focused (no RGB/audio
yet, encoders deprioritised); RAM-hungry; the "Restore Stock Settings" footgun; constrained
build configuration.

---

## Conclusions

### On the embedded HTTP server

It is the **right design for GP2040-CE** and the **wrong one for keyboards**. The costs are
near-zero for a fight stick configured occasionally on a home PC or console, and severe for a
device that lives in a managed laptop and whose function is synthesising keystrokes:

1. An unauthenticated local HTTP server that can rewrite a keymap is a keystroke-injection
   endpoint.
2. A permanent virtual NIC collides with corporate VPN, firewall and DLP policy.
3. Keyboards must work in UEFI and through KVMs.
4. QMK's ChibiOS USB stack has no Ethernet class and no lwIP — adding them means the very
   kind of long-lived fork that makes VIAL problematic.
5. The ~128 KB flash / 32 KB RAM floor rules out smaller STM32 parts, fragmenting a
   mixed RP2040/STM32 product line.

The idea does not fail on technology. It fails on threat model and deployment environment —
costs invisible when the architecture is judged by the project that uses it well.

### What is actually worth taking from it

The valuable idea is not HTTP. It is that **the device ships its own self-description, so the
UI can never be out of sync and no registry can be missing an entry.** That idea is
transport-independent, and VIAL, XAP and ZMK Studio all implement it over cheap transports.

### For a multi-protocol client application

1. **Model the internal representation on ZMK Studio.** It is the most expressive: geometry
   with rotation, behaviours as typed-parameter descriptors, layers as first-class objects
   with explicit save/discard semantics. VIA and VIAL then become **lossy projections** onto
   it. The reverse direction — starting from VIA and stretching — will hurt.
2. **Realistic backend set today: VIA + VIAL + ZMK Studio.** Not XAP.
3. **Study XAP's spec anyway** for its route/subsystem decomposition and broader coverage.
   Between XAP and ZMK Studio, most of the design space is already explored.
4. **Adopt physical-presence unlock by default** for anything shipped to others. Three
   independent designs converged on it.
5. **Native client, not browser.** Firefox and WebKit have both declared WebHID and WebUSB
   *harmful*; that will not change. A native app makes the question disappear rather than
   working around it. Both ZMK Studio and qmk_xap chose **Tauri** independently.

### What to watch

| Signal | Meaning |
|---|---|
| XAP #15601 closes (spec format declared final) | XAP becoming real; plan a fourth backend |
| A vendor ships XAP firmware | Genuine ecosystem adoption (has not happened) |
| ZMK Studio gains RGB/encoder subsystems | Scope approaching XAP's design |
| GP2040-CE flips to NCM-only | Windows 10 considered dead upstream |

---

## Appendix: OS support for USB networking classes

Relevant only to the GP2040-CE approach, but it is the fact that shapes that whole design.

| | Windows 10 | Windows 11 | macOS | Linux |
|---|---|---|---|---|
| RNDIS | yes | yes | **no** | yes |
| CDC-ECM | **no** | **no** | yes | yes |
| CDC-NCM | **no** (officially) | yes | yes | yes (since 2011) |

**No single class covers all four columns**, which is why GP2040-CE ships two USB
configurations. Windows 10 reached end of support in October 2025; as it drains, NCM-only
becomes viable and the dual-config trick becomes unnecessary.
