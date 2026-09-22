# Nazg

Cross-platform native configurator client speaking several keyboard configuration protocols —
VIA and Vial first, QMK XAP later — through one descriptor-driven UI.

Name: Black Speech for "ring" (*ash nazg durbatulûk*). Tagline: *one keyboard configurator to rule them all*.

# Status

Early development. The design phase is finished (see below) and the pipeline runs end to end:
CMake build, SDL3 + ImGui window, `Task<T>` coroutines, a HID transport offering
enumerate / open / request / close, and the protocol layer on top of it — the VIA command
set in `adapters/via`, with the Vial branch deriving from it in `adapters/vial`. Protocol
code talks to a `DeviceChannel` rather than the transport, so it is testable with scripted
bytes; five CTest executables cover `Task<T>`, the transport and both protocol halves.

Verified against real hardware (see the VIA half of the adapter reading a keymap off The
Aquanaut). **The Vial-specific half is tested only against scripted bytes** — detection, the
paged definition download, entry counts, unlock status and encoders have never met a
vial-qmk board.

Next: decode the downloaded definition (minlzma, then nlohmann/json) and feed it into the
capability model. The UI is still the placeholder device list.

# Prior research — read before re-researching anything

A ~2300-line survey of VIA, Vial, XAP, ZMK Studio and GP2040-CE's web configurator lives in
`docs/research_material/`. Start at
[docs/research_material/README.md](docs/research_material/README.md); the architecture
reasoning is in
[docs/research_material/client-architecture.md](docs/research_material/client-architecture.md).

The survey is strategic. The **wire format** — every VIA and Vial command, its payload
layout, and the protocol version history of both — is in
[docs/research_material/via-vial-commands.md](docs/research_material/via-vial-commands.md),
which ends with the traps that shape the backend.

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
coexist without clobbering each other. `.gitignore` covers `build*/`. Ninja and VS2026 were both evaluated on
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
