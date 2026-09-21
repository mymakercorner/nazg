# Nazg

Cross-platform native configurator client speaking several keyboard configuration protocols —
VIA and Vial first, QMK XAP later — through one descriptor-driven UI.

Name: Black Speech for "ring" (*ash nazg durbatulûk*). Tagline: *one config to rule them all*.

# Status

Early development. No application code yet — the repository currently holds licence, README
and contribution docs only. The design phase is finished; see below.

# Prior research — read before re-researching anything

A ~2000-line survey of VIA, Vial, XAP, ZMK Studio and GP2040-CE's web configurator lives in
`docs/research_material/`. Start at
[docs/research_material/README.md](docs/research_material/README.md); the architecture
reasoning is in
[docs/research_material/client-architecture.md](docs/research_material/client-architecture.md).

**Do not re-derive conclusions already written there.** If something in it is wrong, correct
the document rather than working around it.

# Architecture

```
UI layer            renders from the capability model; knows nothing about protocols
    |
Capability model    "this board has: <typed fields, widgets, groups>"
    |
Protocol layer      VIA  |  Vial  |  (XAP later)
    |
Transport           async request/response + unsolicited messages
```

The capability model is the pivot — VIA V3 `menus`, Vial embedded definitions, custom
descriptors and later XAP all feed in through the same path. Descriptor vocabulary is a
superset of VIA V3 menus with ZMK-style typed parameters.

Put the transport seam *below* the protocol layer, not above it.

# Stack

**C++ + Dear ImGui + hidapi.** Immediate mode is the architectural fit: the widget tree changes
whenever a different board is plugged in, and the centrepiece is a custom-drawn keyboard no
toolkit provides anyway. Emscripten web build is explicitly deferred.

# Build order

1. **Vial first** — self-describing devices make the pipeline testable end to end without
   touching definition sourcing, and it dogfoods on Rico's own boards
2. **VIA second**, reusing most of it; definition sourcing is its own scoped work item
3. **Custom features** via device-served descriptors
4. **XAP** / **ZMK Studio** as independent future decisions

Vial and VIA are one backend with a Vial branch, not two backends.

# Conventions

- **Licence: GPL-3.0-or-later.** Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-or-later`. GitHub badges the repo "GPL-3.0"; that is
  expected — the *or-later* lives in the file headers.
- **Commits are signed off** (DCO, `git commit -s`). No CLA.
- **"Nazg" is a user-facing name only.** Source modules get boring, greppable names:
  `adapters/via`, `adapters/vial`, `transport/`, `descriptor/`. No lore-themed identifiers —
  naming adapters `narya`/`nenya`/`vilya` was considered and rejected as cryptic.
- **Re-implement protocols from the wire format.** Do not port code from `vial-gui` or the VIA
  app; that would inherit a GPL lineage for no benefit.

# Ruled out — do not re-suggest

- **Embedded HTTP server / USB networking on the keyboard** — keystroke-injection vector, a
  permanent virtual NIC breaks corporate VPN/DLP, and QMK's ChibiOS USB stack has no Ethernet
  class
- **Forking QMK** — fragmentation is a core objection to Vial
- **A browser-based client as the primary target** — Chromium-only via WebHID
- **ZSA** (compile-time; Keymapp's gRPC API drives the app, not the device) and **KMK**
  (Python firmware)
- **AGPL** — its network clause cannot fire for a USB HID desktop client, and AGPL bans at many
  companies would block installs on work machines

**Not** ruled out, despite being far down the list: **ZMK Studio** is a legitimate future
backend, blocked mainly on having no hardware to dogfood against.

# Links

- QMK: https://docs.qmk.fm/ — XAP tracking PR: https://github.com/qmk/qmk_firmware/pull/13733
- Vial: https://get.vial.today/
- GP2040-CE: https://gp2040-ce.info/
- ZMK Studio: https://zmk.dev/docs/features/studio
