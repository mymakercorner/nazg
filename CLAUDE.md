# Nazg

Cross-platform native configurator client speaking several keyboard configuration protocols —
VIA and Vial first, QMK XAP later — through one descriptor-driven UI.

Name: Black Speech for "ring" (*ash nazg durbatulûk*). Tagline: *one keyboard configurator to rule them all*.

# Status

Early development, design phase finished: VIA and Vial boards load, draw and edit end to end.
Details live in the code's header comments, [ui-design.md](docs/research_material/ui-design.md)
(every screen and section, with Rico's decisions) and the git history -- this section says what
exists, where, and how far it has been proven.

## What is built

- **Pipeline.** `Task<T>` coroutines, a hidapi transport (`transport/`), the VIA command set
  (`adapters/via`) with the Vial branch deriving from it (`adapters/vial`). Protocol code talks to
  a `DeviceChannel`, so it tests with scripted bytes; the tests are plain CTest executables in `tests/`.
- **Definitions.** One parser for both protocols (`adapters/via/NazgKeyboardDefinition.*`) --
  Vial's embedded document IS a VIA definition, inflated from XZ (`adapters/NazgXz.*`). Both
  forms: KLE source and VIA's converted `layouts.keys`. Layout options placed by `PlaceKeys()`
  with VIA's pivot rule; KLE rotation drawn.
- **Model.** A board loads into a plain `Keyboard` (`model/NazgKeyboard.h`) with a `BoardReport`
  of what it supports. `Keycode` (`model/NazgKeycode.h`) is a variant; `adapters/qmk/` encodes
  and decodes every QMK keycode version 0.0.1-0.0.9 plus the two pre-renumbering `Legacy` ones,
  round trip exact. A full Vial load is ~65 round trips, 890 ms on the Model F -- nothing above
  the transport may block the frame loop.
- **VIA definition sourcing.** VIA's registry ships as a pinned bundle (`tools/update_via_bundle.py`,
  read by `adapters/via/NazgViaBundle.*`: inflated once at start, ~40 ms, 28 MB kept). User
  definitions live in a library (`library/`) in the data folder
  `SDL_GetPrefPath("mymakercorner", "Nazg")`, beside `imgui.ini`. Several candidates for a board:
  Nazg asks once and remembers the choice per device (via-registry.md).
- **Workspace.** One window: a menu bar header (board menu, protocol, Vial lock, Settings), then
  the keyboard list, the open board or Settings. Screens report clicks; `Main.cpp` acts. *Advanced tools*, off by default,
  shows designer and debugging tools only. The board
  screen is a section column, a strip, the board and a panel (`ui/NazgWorkspace.*`).
- **Board look** (ui-design.md, "How the board's look is built"): steps 1-4 and part of 5 --
  key shapes, themes, Arimo legends for 69 host layouts, command keys with category headers and
  bands, colours from `ui/NazgPalette.*`, fallthrough marks ▽ ✕. `legend_font` and `palette`
  tests check every keycode and theme.
- **Sections** (`ui/NazgSection.h`; the column planned by `PlanSections()`):
  - **Keymap** -- edit one key, written and read back; the keycode picker (tabs, search, 1u
    tiles, the key line with the Any entry and When held / Sent with).
  - **Layout** -- option groups, written at once.
  - **Macros** -- a chain edited in place, text typed with the host layout, Save / Revert.
  - **Tap Dance** -- four actions with gesture drawings and fallbacks, Save / Revert. The dances
    are read with the board (`Keyboard::tapDances`), so Keymap draws a TD key from its slot.
  - **Combos** -- the chord (inputs picked on the board or in the picker, then what they send),
    the combo term from QMK Settings, what is wrong said with its fix, Save / Revert. Read when
    the section opens; the board joins the inputs with a tag (`BoardTag`).
  - **Matrix view** (Tools, with *Advanced tools* on) -- wiring and a live test.
  - Every other planned section is a `PlaceholderSection`.
  - Shared panel parts: `ui/NazgKeycodePicker.*`, `ui/NazgKeyLine.*` (Sent with and its popup),
    `ui/NazgSlotParts.*` (what Macros, Tap Dance and Combos repeat: reading, Save / Revert).

## Proven on hardware, and not

Boards at hand: **Model F B104** (Vial 6, Rico's daily keyboard -- let him make the writes),
**Aquanaut** (VIA 12, definition in Rico's QMK fork), **Phoenix Project No 1** (VIA registry),
**Concordia** (VIA).

| Seen working | Never met real hardware |
|---|---|
| Vial and VIA loads, keymap decode, one-key edit (Model F, Aquanaut) | Vial unlock and lock -- the Model F is `VIAL_INSECURE` |
| All 2029 VIA V3 definitions parse and draw as VIA does (test) | The matrix live test reading keys -- stock firmware answers zeroes |
| Official bundle, user library, choice (Phoenix) | Rotated keys on a real board (a forged definition only) |
| Picker, board look, matrix wiring (Model F, Concordia) | Pre-renumbering keycodes (`Legacy`) |
| Macros: save and read-back (Concordia), creation on a Leyden Jar board | A Vial board playing a macro; locked-board writes |
| Tap Dance (Model F) | `QueryQmkSettings()`; lighting hover notes |
| | Combos and the combo term, read or written |

## Traps already found

- **No board is opened while protocols are being probed**: HID gives every open handle a copy of
  each reply, and the transport drains leftovers only at open.
- A Vial board's keycode version comes from its **Vial** protocol (vial-qmk always reports VIA 9); a VIA board's from its VIA protocol
  (`adapters/via/NazgViaLoader.*`, keycodes.md).
- Vial replies overwrite the buffer and echo unsupported commands: see via-vial-commands.md.

## Open, deferred, under review

- **Under review**: the user library's one-version backup ("Restore previous") -- Rico weighs its
  value against its complexity. "Export definition..." is for debugging only.
- **Not done**: noticing an unplugged board (hotplug); the rest of step 5 of the board look.
- **Deferred**: the Leyden Jar diagnostics (far later -- already decided: the device stays open
  while a view polls, and key output is disabled while they show, RAM only, so every close and
  exit path must enable it again); library export / import (a web-build need).

**Next**: the placeholder sections, each researched, mocked up with variants, chosen by Rico,
then built -- Key Overrides, Alt Repeat Key, QMK Settings, Lighting, Audio, custom menus.

# Prior research — read before re-researching anything

A ~2300-line survey of VIA, Vial, XAP, ZMK Studio and GP2040-CE's web configurator lives in
`docs/research_material/`. Start at
[docs/research_material/README.md](docs/research_material/README.md); the architecture
reasoning is in
[docs/research_material/client-architecture.md](docs/research_material/client-architecture.md).

The survey is strategic. The **wire format** — every VIA and Vial command, its payload
layout, and the protocol version history of both — is in
[docs/research_material/via-vial-commands.md](docs/research_material/via-vial-commands.md),
which ends with the traps that shape the backend. Keycodes — their version history, what
VIA and Vial cover, and the protocol-neutral representation — are in
[docs/research_material/keycodes.md](docs/research_material/keycodes.md).

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

**C++17 or later + SDL3 + Dear ImGui + hidapi.** Immediate mode is the architectural fit: the
widget tree changes whenever a different board is plugged in, and the centrepiece is a
custom-drawn keyboard no toolkit provides anyway.

Dependencies live in `external/` as git submodules **pinned to exact commits**, so a build is
never broken by upstream moving. Follow the Leyden Jar Diagnostic Tool's CMake layout — one
top-level `CMakeLists.txt`, proven on Windows, Linux and macOS (Intel and Apple Silicon).

| Dependency | Version | Commit |
|---|---|---|
| Dear ImGui | `v1.92.9b` | `f1cc2ae15e53a861a874c3034aae6798fde194ab` |
| SDL | `release-3.4.16` | `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` |
| hidapi | `hidapi-0.15.0` | `d6b2a974608dec3b76fb1e36c189f22b9cf3650c` |
| nlohmann/json | `v3.12.0` | `55f93686c01528224f448c19128836e7df245f72` |
| minlzma | `v1.1.5` | `761991677bb472b9978177e203be91bdeb0cd348` |

**Renderer backend: `imgui_impl_sdlgpu3`**, with `imgui_impl_opengl3` as the fallback if it
causes trouble. SDL_GPU maps to Metal on macOS, D3D12 on Windows and Vulkan on Linux, which
avoids Apple's OpenGL deprecation entirely. ImGui's shaders ship precompiled in
`imgui_impl_sdlgpu3_shaders.h`, so no shader toolchain is needed. One gotcha unique to this
backend: `ImGui_ImplSDLGPU_PrepareDrawData` **must** be called before the render pass.

Do **not** use `imgui_impl_sdlrenderer3` — ImGui's own header calls SDL_Renderer "largely
obsolete" and steers toward SDL_GPU.

The renderer and all SDL calls stay confined to `Main.cpp`, so switching backends is a
one-file change. Keep it that way.

**Build tooling: Visual Studio 2022 with the Visual Studio generator**, CMake 3.21+.
Generate with `GenerateBuildForVS2022.bat`, which writes to `build_VS2022/`.
**One script and one build directory per toolchain** — to add an IDE or platform, add a
sibling `GenerateBuildFor<name>` script targeting its own `build_<name>/` directory, so they
coexist without clobbering each other. `.gitignore` covers `build*/`.

**VIA's official definitions are built, not committed**: run
`python tools/update_via_bundle.py` once per checkout (Python 3.8+, standard library only;
it downloads the pinned commit of `the-via/keyboards` once and caches it). It writes
`build_resources/via_definitions.tar.xz`, shared by every toolchain; the build copies it beside
the executable, and the `via_bundle_contents` test parses every definition in it -- skipped
when it is absent. The pin is `resources/via-keyboards.commit`; `--update` moves it to
`master` and lists what changed, `--from-dir <clone>` works offline. Releases ship the bundle.

Ninja and VS2026 were both evaluated on
2026-09-21 and **deliberately deferred** — nothing in the project needs the v145 toolset, and
the current setup is verified working. Revisit later if desired; no known blockers (the
dependency tree looks CMake 4 clean, and VS CMake folder mode keeps full IDE debugging).

**Emscripten web build is explicitly deferred**, and note that SDL_GPU has no WebGPU backend
(d3d12/metal/vulkan only) — a web build will need its own renderer path, either
`imgui_impl_opengl3` over WebGL or the `sdl3_wgpu` route.

When that happens it goes in as a dedicated `#ifdef __EMSCRIPTEN__` path in `Main.cpp` plus a
branch in `CMakeLists.txt` — **not** a renderer abstraction layer, and not a second codebase.
This is only affordable while SDL and renderer calls stay confined to `Main.cpp`, which is the
main reason that rule exists.

**Notes on the dependencies:** minlzma decodes Vial's LZMA-compressed keyboard definitions;
it is dormant upstream (last commit January 2022) but small and self-contained. jsoncpp was
used in the Leyden Jar tool; nlohmann/json replaces it here for descriptor walking. ImGui
1.92 reworked the font system with large breaking changes — irrelevant when starting fresh,
but it means code cannot be moved between Nazg and the older Leyden Jar tool unchanged.

# Build order

1. **Vial first — done.** Self-describing devices made the pipeline testable end to end
   without touching definition sourcing, and it dogfoods on the Model F Labs B104 running
   Rico's own `leyden_jar` controller firmware. Protocol, definition decode and the
   keyboard model all landed this way, verified against hardware at each step.
2. **Keycode dictionary** — turning `0x002A` into `KC_BSPC` and back. Version-keyed: QMK's
   keycode spec has nine versions and both values and names move between them. The tables
   are plain committed C++ source derived from QMK's keycode JSON — **no generator in the
   repo**; when QMK adds a version, regenerate them by hand. That rule is about tools run
   rarely: a regular job gets a tool, as the VIA definitions bundle does
   (`tools/update_via_bundle.py`). **Both dogfood boards are
   post-renumbering** — the Model F reports VIA 9 only because vial-qmk hard-codes it, so a
   Vial board's dictionary comes from its Vial protocol, never its VIA one. The keymap holds
   a structured `Keycode`, not a `uint16_t`; the per-version codec sits below the protocol
   layer. All of it is in
   [docs/research_material/keycodes.md](docs/research_material/keycodes.md). Scope it on its
   own; it is the item most likely to grow.
3. **Draw the board, then edit one key.** The custom-drawn keyboard from the definition's
   geometry, then `SetKeycode` with a read-back — which on Vial also exercises the lock and
   the keycode firewall.
4. **VIA is now only definition sourcing.** The protocol layer and the JSON parser are
   already shared and tested, so what remains is where the definition comes from: bundled
   registry snapshot, on-demand fetch, or user side-load, plus V2 vs V3 definition formats.
   No new protocol code, and it can be picked up at any point after step 1.
5. **Custom features** via device-served descriptors — and only here does the descriptor
   vocabulary get designed, with two real producers in front of it rather than one.
6. **XAP** / **ZMK Studio** as independent future decisions.

Vial and VIA are one backend with a Vial branch, not two backends — confirmed at the wire
level, and the definition parser turned out to be shared as well. See
[docs/research_material/client-architecture.md](docs/research_material/client-architecture.md),
"Sharing between protocols: union, not intersection", for what should and should not be
factored together.

# Conventions

- **Licence: GPL-3.0-or-later.** Every new source file starts with
  `// SPDX-License-Identifier: GPL-3.0-or-later`. GitHub badges the repo "GPL-3.0"; that is
  expected — the *or-later* lives in the file headers.
- **Commits are signed off** (DCO, `git commit -s`). No CLA.
- **Commit messages are one short line, no body.** The diff carries the detail.
- **Tests use CTest, no framework.** `tests/` holds plain executables returning 0 or 1,
  registered with `add_test`. This deliberately leaves the framework choice open. On MSVC a
  test's `main()` must redirect CRT asserts to stderr, or a failing assert opens a modal
  dialog and hangs the run. Run: `ctest --test-dir build_VS2022 -C Debug --output-on-failure`.
- **Source files use PascalCase; directories are lowercase.** `Main.cpp`,
  `VialProtocol.cpp`, `DeviceAgent.h` — under `src/`, `adapters/via/`, `external/`. This
  matches the Leyden Jar Diagnostic Tool, so code ported from it keeps its filenames and
  stays diffable against the original.
- **Files whose contents live in `namespace nazg` are prefixed `Nazg`**, so the filename
  hints at the namespace: `NazgTask.h`, `NazgHidTransport.cpp`. Files with global contents
  are not prefixed — `Main.cpp` (global `main()`) and everything in `tests/`.
  **The prefix is on filenames only.** Type names stay unprefixed inside the namespace:
  the class in `NazgTask.h` is `nazg::Task`, not `nazg::NazgTask`, which would stutter.
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
