# QMK XAP — Findings

*Investigated 2026-09-20 via the GitHub API against `qmk/qmk_firmware` branch `xap`,
PR #13733, the `xap` issue label, and `qmk/qmk_xap`.*

## Summary

**XAP (eXtensible Application Protocol)** is QMK's official in-tree effort to replace VIA's
protocol with a properly designed, versioned, self-describing one. Its core premise is
exactly VIAL's good idea done upstream: the device describes its own capabilities, so no
external registry is needed.

It has been in **draft for five years**. The protocol works; what is missing is not code.

## Status snapshot

| | |
|---|---|
| PR | [#13733](https://github.com/qmk/qmk_firmware/pull/13733), opened **2021-07-27** by tzarc (Nick Brassel) |
| State | Open, still **draft**, `mergeable_state: clean` |
| Branch | `qmk:xap` -> `develop` — an **official org branch**, not a personal fork |
| Size | 98 files, +7830 / -26 |
| Discussion | **2 comments, 2 review comments** — in five years |
| Last `develop` sync | 2026-09-10 |
| Tracking issue | [#11567](https://github.com/qmk/qmk_firmware/issues/11567) |

## Commit archaeology

The recent branch commits are **upstream `develop` merges, not XAP work**. Filtering to
commits that actually touch XAP paths:

| Period | Activity |
|---|---|
| 2021 – mid-2023 | Heavy development |
| Aug 2023 – Jul 2026 | **Dormant** — only lint and clang-format passes (2025-12-07, 2026-02-11) |
| Jul 2026 | **Revived**: "Single reports" (07-08), "XAP command renaming, bootloader jump API naming fix" (07-11), "Add 0.3.1 with keycode version query" (07-14) |

So: not abandoned, but five years in draft with one active author and effectively zero
review.

## Implementation shape

The firmware side is **small** — the substance is the spec, with bindings code-generated
from it.

```
quantum/xap/xap.c            5012 bytes
quantum/xap/xap.h            1449 bytes
quantum/xap/xap_handlers.c   1784 bytes
quantum/xap/handlers/        (directory)

data/xap/xap_0.0.1.hjson      7676 bytes
data/xap/xap_0.1.0.hjson     15362 bytes
data/xap/xap_0.2.0.hjson      7340 bytes
data/xap/xap_0.3.0.hjson     16378 bytes
data/xap/xap_0.3.1.hjson       598 bytes
```

Public API from `xap.h`: `xap_respond_success`, `xap_respond_failure`, `xap_respond_u32`,
`xap_respond_data`, `xap_respond_data_P`, `xap_send`, `xap_broadcast`. Plus
`XAP_SUBSYSTEM_VERSION_KB` / `_USER` for keyboard- and user-level extension.

## Protocol scope — broader than anything shipping

Route map:

| ID | Subsystem |
|---|---|
| 0x00 | XAP core (version, capabilities) |
| 0x01 | QMK (device info, hardware ID, bootloader jump) |
| 0x02 | Keyboard |
| 0x03 | User |
| 0x04 | Keymap |
| 0x05 | Remapping |
| 0x06 | Lighting (backlight, rgblight, rgb_matrix) |
| 0x07 | Audio |

Broadcast messages: **Log message**, **Secure Status**, Keyboard, User.

That covers more ground than VIA, VIAL or ZMK Studio — notably lighting, audio and a logging
broadcast channel. Versioned with capability queries throughout.

## Transport

**USB raw HID** — the `qmk_xap` client architecture diagram shows `Usb Hid` to each device.
So XAP inherits the raw HID performance characteristics documented in [via.md](via.md):
32-byte reports, 1 ms interrupt polling, about 15 KB/s effective.

This is a notable divergence from ZMK Studio, which chose CDC-ACM.

## Security model — secure routes

XAP defines **secure routes**: operations with potentially destructive consequences that
require prior user approval. From the 0.1.0 spec:

> *Secure Route*: A route which has potentially destructive consequences, necessitating prior
> approval by the user before executing.
>
> *Unlock sequence*: A physical sequence initiated by the user to enable execution of secure
> routes.

A `SECURE_FAILURE` response flag signals that a secure route was invoked without a completed
unlock, and a **Secure Status broadcast** notifies clients when lock state changes.

This is the same pattern as VIAL's unlock combination and ZMK's `&studio_unlock`. Three
independent designs converged on physical-presence gating.

**This feature already shipped into mainline QMK** — issue #15627 closed 2022-10.

## XAP has been feeding mainline QMK for years

Even though the protocol has not landed, work done for it has:

| Item | Status |
|---|---|
| "Secure" feature (#15627) | **Merged to mainline** 2022-10 |
| Encoder mapping ([PR #13286](https://github.com/qmk/qmk_firmware/pull/13286)) | **Merged to mainline** 2022-03 |
| Keymap introspection wrappers ([PR #17229](https://github.com/qmk/qmk_firmware/pull/17229)) | **Merged to mainline** 2022-06 |

So XAP is not dead weight, and it is not a fork — it is a cleanly-mergeable branch inside the
QMK organisation that has been a net contributor to mainline.

## What is done

Closed `xap`-labelled issues:

- #15619 finalise the HID protocol packet format (closed 2023-01)
- #15620 implement HID reports for **ChibiOS, VUSB and LUFA** — all three QMK platforms
- #15625 routes for retrieving transformed `info.json`
- #15627 implement "secure mode"
- #15628 keyboard- and user-level `xap.json`
- #15629 binding for dynamic keymap
- #15630 binding for dynamic encoder map
- #17455 first boot initialisation

**The VIA-replacement core — keymap remapping over a finalised packet format on every QMK
platform — is finished.**

## What is missing — 11 open issues

**QMK-wide prerequisites (not XAP code at all):**

| Issue | Title |
|---|---|
| #15597 | [XAP Prereq] External IDs need to be defined in `data/*.json` |
| #15599 | [XAP Prereq] `info.json` layout options |
| #15600 | `info.json` transformation for XAP-related info |

**Foundational, open since Dec 2021:**

| Issue | Title |
|---|---|
| **#15601** | **finalise XAP spec definition format** — the keystone |
| #15621 | finalise firmware-side binding generation |

**Client tooling** (see [How bindings work](#how-bindings-work) — these are *less* missing
than the issue titles suggest):

| Issue | Title | Actual state |
|---|---|---|
| #15622 | generate JavaScript bindings | **not started** — empty placeholder directory |
| #15623 | generate Python3 bindings | **substantially working**, not finalised |

**Console integration:**

| Issue | Title |
|---|---|
| #15624 | reroute `CONSOLE_ENABLE=yes` to XAP broadcasts |
| #15626 | receive XAP logging broadcasts with `qmk console` |

**Feature bindings:**

| Issue | Title |
|---|---|
| #15631 | binding for lighting subsystem |
| #16797 | binding for dynamic macros |

## The real blocker is organisational, not technical

Read the evidence together:

- A cleanly-mergeable branch, five years old
- **Two comments and two review comments** across its entire life
- The remaining prerequisites are QMK-*wide* data-model changes (external IDs, `info.json`
  layout options) requiring broad collaborator consensus
- One primary author, who is also a QMK lead with much else on

This is not a PR blocked on technical objections. It is a PR nobody is reviewing, plus
cross-cutting changes that need organisational buy-in.

**#15601 is the keystone.** Until the spec *format* is declared final, nothing downstream —
codegen, client bindings, third-party implementations — can stabilise.

## How bindings work

XAP is a **spec-driven, code-generated** protocol. The `.hjson` spec files in `data/xap/` are
the single source of truth; firmware handlers, documentation *and* client libraries are all
rendered from them by Jinja2 templates invoked through `qmk` CLI subcommands.

```
data/xap/xap_0.3.1.hjson          <-- single source of truth
        |
        +-- data/templates/xap/firmware/xap_generated.h.j2   -> quantum/xap/xap_generated.h
        +-- data/templates/xap/docs/*.j2                     -> protocol documentation
        +-- data/templates/xap/client/python/*.j2            -> lib/python/xap_client/
        +-- (no javascript templates exist)
```

Generator commands live in `lib/python/qmk/cli/xap/`: `generate_qmk.py`, `generate_docs.py`,
`generate_json.py`, `generate_python.py`.

"Client bindings" therefore means the **generated client-side library** — the route IDs,
enums and struct packing/unpacking that let a host application speak the protocol without
hand-writing the wire format.

### Why generated bindings matter beyond convenience

**Version coupling.** XAP is versioned 0.0.1 -> 0.3.1. When firmware and client are both
generated from the same spec file, a version bump propagates mechanically and mismatches
surface at build time. A hand-written client drifts silently — which is exactly what the Rust
`qmk_xap` client does, since no Rust generator exists.

### Actual state per target

| Target | Generator | State |
|---|---|---|
| **Firmware (C)** | `xap_generated.h.j2` + `.inl.j2` | Working. #15621 "finalise" still open. |
| **Docs** | `data/templates/xap/docs/*.j2` | Working. |
| **Python** | `client/python/{types,routes,constants}.py.j2` via `qmk xap-generate-python` | **Working.** Output committed at `lib/python/xap_client/`, with hand-written `client.py` + `device.py` transport and a `pyproject.toml`. |
| **JavaScript** | none | **Not started.** `lib/python/qmk/xap/gen_client_js/` contains only an empty `__init__.py`; no templates exist. |
| **Rust** | none | `qmk_xap` hand-rolls the protocol. |

The committed Python client is genuinely usable:

```python
from xap_client import XAPClient

devices = XAPClient.devices()
with XAPClient().connect(devices[0]) as dev:
    print(dev.version())
```

Device discovery filters on XAP's **own HID usage page `0xFF51`, usage `0x0058`** — a separate
raw HID interface from VIA's, so the two can coexist on one keyboard. The readme's API
section is still "TODO", which is presumably why #15623 remains open: it works, but it is not
finalised or published.

### The dependency chain

```
#15601  finalise spec definition format          <-- keystone, open since Dec 2021
   |
   +-- #15621  finalise firmware binding generation
   +-- #15622  JavaScript bindings   (not started)
   +-- #15623  Python bindings       (working, not final)
                  |
                  +-- #15624  reroute CONSOLE_ENABLE to XAP broadcasts
                  +-- #15626  receive XAP log broadcasts in `qmk console`
```

The console-integration issues sit downstream of the Python bindings because the QMK CLI is
itself Python. And nothing below #15601 can be declared stable while the format it all
generates from is still in flux.

## Extending with custom features

**This is XAP's headline capability and the best extension story of the four.**

Two subsystems are permanently reserved, per the spec:

> **0x02 Keyboard** — *"This subsystem is always present, and reserved for vendor-specific
> functionality. No routes are defined by XAP."*
>
> **0x03 User** — the same, for keymap/userspace level.

### How you add routes

Drop an `xap.hjson` fragment next to your keyboard or keymap:

```
keyboards/<kb>/xap.hjson                  -> keyboard level  (0x02 KB)
keyboards/<kb>/keymaps/<km>/xap.hjson     -> keymap/userspace (0x03 USER)
```

Real in-tree examples: `keyboards/zvecr/zv48/xap.hjson` and its keymap equivalent.

```hjson
routes: {
    0x01: {
        type: command
        name: Capabilities Query
        define: CAPABILITIES_QUERY_KB
        return_type: u32
        return_purpose: capabilities
        return_constant: XAP_ROUTE_KB_CAPABILITIES
    }
}
```

### Merge semantics

`merge_xap_defs(kb, km)` in `lib/python/qmk/xap/common.py` merges
**base spec -> keyboard spec -> keymap spec**, with:

- Keyboard specs searched **up the directory tree**, so a parent folder's routes are inherited
- Keymap spec beating userspace spec where both exist
- `!reset!` to clear and replace rather than merge

### What gets generated

From the merged spec, automatically:

- Firmware handler scaffolding (`xap_generated.h` / `.inl`)
- Protocol **documentation**
- The **Python client bindings**

Plus `XAP_SUBSYSTEM_VERSION_KB` / `_USER` to version your extensions independently, a
capabilities-bitmask convention so clients can discover which of your routes exist, and
Keyboard/User **broadcast** types for unsolicited messages from your subsystem.

No fork. No hand-maintained wire format. One source of truth for both ends.

### The limit: wire format, not semantics

XAP's type system describes **how to pack bytes**, not what they mean. From the real
backlight route:

```hjson
request_struct_members: [
    { type: u8, name: enable },
    { type: u8, name: mode },
    { type: u8, name: val },
]
```

`val` is brightness; nothing says 0–255 or "slider". `mode` indexes an effect list; nothing
says which values are valid or what they are called.

A keyword scan across **all five spec versions** confirms the vocabulary is closed — no
`min`, `max`, `range`, `enum`, `values`, `options`, `unit` or `step`. The only `bits` usage
is protocol-level `response_flags`. `return_purpose` has just two values, `bcd-version` and
`capabilities`, both codegen hints.

Where does the client's enum knowledge come from, then? `constants.py.j2` generates
`RgblightModes(IntEnum)` from `specs.rgblight.effects` — **QMK's own `data/constants/`
files, not the XAP spec**. A third-party client reading only the spec never learns them.

### Comparison of description layers

| | What a declaration describes | GUI derivable? |
|---|---|---|
| VIA V3 `menus` | **UI widgets** — slider/toggle/dropdown + options | Yes; it *is* the UI |
| ZMK behaviour metadata | **Semantics** — `range{min,max}`, `layer_id`, `hid_usage` | Yes; map semantics to widgets |
| **XAP `struct_members`** | **Wire format** — field name + byte type | **Partially** |

### What a client *can* derive from XAP today

More than nothing — enough for a serviceable auto-generated GUI:

- Field **names** -> labels
- Field **types** -> `bool` (a declared alias of `u8`) becomes a checkbox; integer types
  become numeric inputs bounded by the type's natural range
- **Struct nesting** -> grouped controls
- Route `name` + `description` -> labels and tooltips
- **Capabilities bitmask** -> which controls to show at all
- **`permissions: secure`** -> which controls require an unlock first

What it cannot derive: that `val` wants a 0–255 slider, that `mode` is an enum and what its
members are called, or any semantic picker (keycode, layer, colour).

### The gap is small and additive

Optional `min` / `max` / `enum` / `unit` keys on `struct_members` would be a
**backwards-compatible** spec change — existing routes keep generating, the codegen passes
the extra keys through to the bindings. Given the pipeline is hjson plus Jinja2, this is
modest work, and since the spec format is still open (#15601) it could still go in.

**Practical catch if attempting it locally:** `data/schemas/xap.jsonschema` sets
`additionalProperties: false` in **eight** places, so unknown keys are rejected at
validation. Extending your own fragment means extending the schema too.

### Assessment

XAP's "eXtensible" means the **protocol** is cleanly extensible — not that a client can
**semantically discover** those extensions. Those are different properties.

Consequently XAP is the **weakest of the four as a generic third-party discovery protocol**
and the **strongest as a first-party extension protocol**. If you define the routes and also
write the client, there is no discovery gap, and you gain codegen plus version checking
across both ends.

## The client — `qmk_xap`

| | |
|---|---|
| Repo | [qmk/qmk_xap](https://github.com/qmk/qmk_xap) |
| Stack | Tauri runtime, Vue.js frontend, Quasar UI, Rust backend, TypeScript |
| Transport | USB HID direct to devices |
| Stars | 35 |
| Status | Experimental, not archived |

Activity: primarily Stefan Kerkmann (KarlK90, a QMK collaborator) through May 2024, then
**dormant until May 2026**, then a burst from a new contributor, Yanfei Guo (`arakashic`):
"add broadcast message console", "show full firmware config json in info", "show secure button
only on remap-enabled sections", "format device protocol values".

**Important caveat on reading this as adoption:** that contributor's public repositories show
a `qmk_xap` fork and **no QMK firmware fork**. They are a *tool* contributor, not a keyboard
maker adopting the protocol. The nature of the fixes (e.g. secure-button visibility) implies
hands-on use against real hardware, but that is inference.

The signal that would actually matter — **a keyboard maker or vendor shipping XAP firmware**
— has not appeared.

## Verdict

| Aspect | Assessment |
|---|---|
| Protocol design | **Strong.** The most thoroughly designed of the four. |
| Firmware implementation | **Substantially complete** for the core use case. |
| Spec stability | **Not finalised** (#15601 open since 2021). |
| Client bindings | **Python works, JS not started.** Neither declared final. |
| Ecosystem adoption | **None.** No shipping firmware uses it. |
| Momentum | First real movement since 2023, but two people on a tool, not adoption. |

## Practical guidance

**Do not target XAP today.** No shipped firmware uses it and the spec format is explicitly
not final — so anything you build against it can be invalidated by a spec revision. Note the
blocker is *spec stability and adoption*, not tooling: the Python client already works.

**Do read the spec anyway.** Versioned with capability queries, broader subsystem coverage
than anything shipping, log broadcasts, and a proper security model. For designing an
internal representation that must abstract several protocols, XAP's route/subsystem
decomposition is worth studying alongside ZMK Studio's typed-parameter model. Between them
they have already explored most of the design space.

**Watch signal:** #15601 closing plus JS/Python bindings landing. Worth re-checking in six
months; not worth planning around now.

## Sources

- [PR #13733 — [WIP] XAP, eXtensible Application Protocol](https://github.com/qmk/qmk_firmware/pull/13733)
- [Issue #11567 — XAP (née QMK API) discovery and scoping](https://github.com/qmk/qmk_firmware/issues/11567)
- [`xap` label on qmk/qmk_firmware](https://github.com/qmk/qmk_firmware/labels/xap)
- [qmk/qmk_xap client](https://github.com/qmk/qmk_xap)
- [XAP spec files, branch `xap`](https://github.com/qmk/qmk_firmware/tree/xap/data/xap)
