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

- **Pipeline.** `Task<T>` coroutines, a hidapi transport (`transport/`: what the main thread sees
  in `NazgHidTransport.cpp`, the worker in `NazgHidTransportHidapi.cpp`), the VIA command set
  (`adapters/via`) with the Vial branch deriving from it (`adapters/vial`). Protocol code talks to
  a `DeviceChannel`, so it tests with scripted bytes; the tests are plain CTest executables in `tests/`.
  Opening a board -- protocol, definition, keymap, report -- is `board/NazgBoardLoad.*`, tested
  against a scripted board; the user's definition files (import, replace, export) are
  `library/NazgLibraryActions.*`; paths and file reads are `files/NazgFiles.*`.
- **Definitions.** One parser for both protocols (`adapters/via/NazgKeyboardDefinition.*`) --
  Vial's embedded document IS a VIA definition, inflated from XZ (`adapters/NazgXz.*`). Both
  forms: KLE source and VIA's converted `layouts.keys`. Layout options placed by `PlaceKeys()`
  with VIA's pivot rule; KLE rotation drawn. Labels placed by KLE's alignment as VIA's reader
  does; knobs read in both forms (`DefinitionKey::encoderIndex`, `IsSwitch()`): VIA's `e0` key,
  Vial's two direction keys -- no longer phantom switches.
- **Model.** A board loads into a plain `Keyboard` (`model/NazgKeyboard.h`) with a `BoardReport`
  of what it supports. `Keycode` (`model/NazgKeycode.h`) is a variant; `adapters/qmk/` encodes
  and decodes every QMK keycode version 0.0.1-0.0.9 plus the two pre-renumbering `Legacy` ones,
  round trip exact. A full Vial load is ~65 round trips, 890 ms on the Model F -- nothing above
  the transport may block the frame loop.
- **VIA definition sourcing.** VIA's registry ships as a pinned bundle (`tools/update_via_bundle.py`,
  read by `adapters/via/NazgViaBundle.*`: inflated once at start, ~40 ms, 28 MB kept). Community
  definitions -- vendors' files for boards VIA lacks or has older, 708 from Rico's collection --
  ship as a second bundle from the `nazg-community-definitions` repository, pinned by release
  tag and SHA-256 (`tools/update_community_bundle.py`, `adapters/via/NazgCommunityBundle.*`).
  Every bundled file passes `nazg-validate` (`src/cli/`), the checks of
  `adapters/via/NazgDefinitionCheck.*`. User definitions live in a library (`library/`) in the data folder
  `SDL_GetPrefPath("mymakercorner", "Nazg")`, beside `settings.json`. Several candidates for a board:
  Nazg asks once and remembers the choice per device (via-registry.md).
- **Workspace.** One window: a menu bar header (board menu, protocol, Vial lock, Settings), then
  the keyboard list, the open board or Settings. Screens report clicks; `App` (`app/NazgApp.*`) acts. *Advanced tools*, off by default,
  shows designer and debugging tools only. The board
  screen is a section column, a strip, the board and a panel (`ui/NazgWorkspace.*`).
- **Saved layouts** (ui-design.md, "Saved layouts"; Rico, 2026-10-18): the board menu's Save to file... and
  Load from file..., Vial's `.vil` for every board (vil-files.md). `adapters/vial/NazgVialSavedLayout.*` reads
  and writes the file in vial-gui's own keycode names (`NazgVialKeycodeNames.*`, its v5 and v6 tables);
  Load also reads the VIA app's `.layout.json` (via-layout-files.md) -- `adapters/via/NazgViaSavedLayout.*`,
  VIA's own names (`NazgViaKeycodeNames.*`, its four tables) -- into the same `model/NazgSavedLayout.h`;
  `board/NazgSavedLayoutTransfer.*` reads the board, compares and writes what differs, read back;
  `ui/NazgSavedLayoutReview.*` shows what would change before Apply.
- **Hotplug** (ui-design.md, "Hotplug"): no Refresh. `Main.cpp` polls SDL's
  `SDL_hid_device_change_count()`; `App` lists again half a second after the last change, asks
  only new paths their protocol (`board/NazgDeviceList.*`), and closes the open board once its
  path is gone and no reply from it is awaited, the list saying which one went. Nothing opens by
  itself after start -- a board coming back is only listed (Rico, 2026-10-16).
- **A board Linux does not allow** (ui-design.md, the section of that name; Rico, 2026-10-18): the
  transport tells a refused open (`HidAccessDenied`), the list says "not allowed" with Allow...,
  a board Nazg allowed shows Allowed, which stops it (`ui/NazgPermissionDialog.*`). One udev rule
  file per board, written or deleted through pkexec, or as commands to paste
  (`permission/NazgBoardPermission.*`; build-and-release.md, "Nazg allowing the boards itself").
- **Icon.** The rune ᚾ in a blue ring on a keycap (ui-design.md, "The application icon"):
  `resources/icon/`, exported from its mockup -- the `.ico` in the exe (`src/Nazg.rc`), PNGs for
  the window on Linux (`SDL_SetWindowIcon` in `Main.cpp`); the `.icns` and `Info.plist` in the macOS app, Linux's
  desktop entry in the AppImage; the web manifest prepared in `resources/package/` (`resources/icon/README.md`).
- **Settings.** `settings.json` in the data folder (`settings/NazgSettings.*`): one JSON object,
  `"format": 1`; keys this build does not know are kept when it writes, and a newer format is
  read but never written over. Written whole then renamed, a second after the last change and on
  exit -- `App` notices a change by comparing, so nothing has to mark one. ImGui's own ini is
  off. An earlier build's `imgui.ini` is read once, to migrate, and left as it was.
- **Board look** (ui-design.md, "How the board's look is built"): all five steps --
  key shapes, themes, Arimo legends for 69 host layouts, command keys with category headers and
  bands, colours from `ui/NazgPalette.*`, transparent and `KC_NO` keys faint with ▽ / ✕ on a
  corner badge, peek, the lighting policy. `legend_font` and `palette`
  tests check every keycode and theme.
- **Sections** (`ui/NazgSection.h`; the column planned by `PlanSections()`):
  - **Keymap** -- edit one key, written and read back; the keycode picker (tabs, search, 1u
    tiles; Basic a full-size keyboard in the host layout's shape -- ANSI, ISO, JIS -- and Extra the
    rest as its keycaps, built 2026-10-26, tried by Rico 2026-10-27; the key line with the Any entry,
    Press a key and When held / Sent with). Knobs too (ui-design.md,
    "Knobs"): a VIA knob a round key with its press and turns stacked, Vial's direction keys with a
    ↺ / ↻ badge; the panel's action cards, written at once -- turns through the encoder map read
    with the board (`adapters/vial/NazgEncoderMap.*`), "?" when the firmware keeps none.
  - **Layout** -- option groups, written at once.
  - **Macros** -- a chain edited in place, text typed with the host layout, Save / Revert.
  - **Tap Dance** -- four actions with gesture drawings and fallbacks, Save / Revert. The dances
    are read with the board (`Keyboard::tapDances`), so Keymap draws a TD key from its slot.
  - **Combos** -- the chord (inputs picked on the board or in the picker, then what they send),
    the combo term from QMK Settings, what is wrong said with its fix, Save / Revert. Read when
    the section opens; the board joins the inputs with a tag (`BoardTag`).
  - **Key Overrides** -- the rule (Held + Key -> Sends, the layers), the modifiers not allowed and
    hidden, what it does and what is wrong said, the start and stop options folded, Save / Revert.
    Read when the section opens; an override off is struck through in the strip.
  - **Alt Repeat Key** -- the rule (Last key -> Alt Repeat sends, the arrow turning both ways),
    the modifiers that may be held too, left or right alike, after any other key, what is wrong
    said -- vial-qmk sends right-hand modifiers wrong -- Save / Revert. Read when the section opens.
  - **QMK Settings** -- the groups in the strip, the board lighting what a group or a line
    concerns (Chordal Hold: QMK's guess of each key's hand), QMK's names with what each does beside
    it, the tap-hold decision as one choice of three, both firmware generations (qsid 8, or 22-27),
    written at once, Reset all. Read when the section opens; the combo term shared with Combos.
  - **Lighting** -- backlight, underglow, RGB Matrix (VIA's standard menus, Vial's presets, VialRGB),
    several systems in the strip; the board lighting the keys that change it, one line under the
    pointer narrowing them; underglow's effect with its own variant buttons, colour as hue and
    saturation sliders; written at once -- shown while a slider moves, stored when let go. Read each
    time the section is shown. Three wire formats: `adapters/via/NazgViaLighting.*`.
  - **Audio** (VIA V3's `qmk_audio`, protocol 11 on; Vial has none) -- Audio and Clicky, Clicky greyed
    while audio is off, Play a sound (device indication: the board's song on its own speaker); the
    board lighting the keys that change it; written at once, read each time the section is shown
    (`ui/NazgAudioSection.*`, `adapters/via/NazgViaAudio.*`).
  - **Matrix view** (Tools, every board since 2026-10-18) -- a live test reading the keys the computer
    receives by default, on the layer shown -- picked in the strip, or while a key is held the layer it
    comes from, its entry violet -- or the switches, chosen at the strip's end, falling back where the
    firmware refuses them (Rico, 2026-10-26; ui-design.md, "The key test"; `model/NazgHostKeys.*`); first and started when shown,
    and wiring.
  - **Diagnostics** (Tools, every board answering the Leyden Jar's probe; `ui/NazgDiagnosticsSection.*`)
    -- Key presses (now and seen; the board, QMK's matrix or the controller's), Signal levels (the
    keys at risk judged by `model/NazgKeyHealth.*`: verdicts on the board, bars, the whole board as
    dots, a key's last presses; the threshold margins and Levels, for everyone), Device (the controller's, Vial's and USB's
    information, Enter bootloader, Erase EEPROM). Key output off while it shows: on again is the
    channel's farewell, written whenever the board closes, the transport's shutdown included. Each
    reading is one request series on the transport's worker, not a frame per round trip. A polling
    section holds nothing back (`Section::IsPolling()`): App's `WhenBoardIdle()` stops it first.
  - **Device** (Tools, every board; `ui/NazgDeviceSection.*`) -- enter the bootloader where the
    firmware has the command (a secure Vial build, unlocked) and say how elsewhere, the key holding
    `QK_BOOT` lit; reset the keymap (keys and knobs, as built); empty the macros. Each asks; a
    reset loads the board again. A Leyden Jar board keeps Diagnostics' own too.
  - **A board's own menus** (VIA V3 custom menus, Board group) -- submenus in the strip, lines in two
    columns, showIf, sliders tied by constraints, Keymap's picker under the lines, written at once
    (`ui/NazgCustomMenuSection.*`, `adapters/via/NazgViaCustomMenu.*`). Read per submenu when shown.
  - Every planned section is built; `PlaceholderSection` stays as the fallback for a new kind.
  - Shared panel parts: `ui/NazgKeycodePicker.*`, `ui/NazgKeyLine.*` (Sent with and its popup),
    `ui/NazgSlotParts.*` (what the slot sections repeat: reading, Save / Revert, a tag's words).

## Proven on hardware, and not

Boards at hand: **Model F B104** (Vial 6, Rico's daily keyboard -- let him make the writes),
**Aquanaut** (VIA 12, definition in Rico's QMK fork), **Phoenix Project No 1** (VIA registry),
**Concordia** (VIA), **F77** (Vial 6, Leyden Jar controller; secure mode since 2026-10-18 -- the
one board to unlock).

| Seen working | Never met real hardware |
|---|---|
| Vial and VIA loads, keymap decode, one-key edit (Model F, Aquanaut; a tester's Keychron Q6, 0.1.0-test.1) | |
| Vial unlock and lock, Cancel closing the board, a quit waiting for the unlock (F77 in secure mode, 2026-10-18) | |
| The matrix live test on a Vial board: refused locked, keys unlocked (F77 in secure mode) | |
| All 2029 VIA V3 definitions parse and draw as VIA does (test) | |
| The matrix live test reading keys (a tester's Keychron Q6: Keychron builds every board with `VIA_INSECURE`; stock QMK answers zeroes) |  |
| Official bundle, user library, choice (Phoenix) | Rotated keys on a real board (a forged definition only) |
| Knobs fully working: turns read and written through the encoder map (a tester's Keychron Q6, 0.1.0-test.2); "no encoder map" said (Phoenix, Concordia: forged definitions) | Vial's direction keys on a board |
| Picker, board look, matrix wiring (Model F, Concordia) | Pre-renumbering keycodes (`Legacy`) |
| Macros: save and read-back (Concordia), creation on a Leyden Jar board | A Vial board playing a macro |
| Tap Dance (Model F) | Lighting hover notes on two systems (`UG_*` driving the matrix) and `RM_*` |
| Lighting keys by firmware: picker, faint `RGB_M_*` tiles and their notes with Advanced tools, a mode key's note on the board (Phoenix, firmware older than the overhaul, judged unknown) | |
| QMK settings listed, read at their widths, written and reset from the section (Model F) | |
| Lighting: underglow read, written, sliders live (Phoenix, VIA channels; the tester's Q6) | Lighting in the format before V3, and VialRGB |
| Combos and the combo term, read and written (Model F) | |
| Key overrides, read and written (Model F) | |
| | Alt repeat keys -- no board at hand has them |
| Audio planned from a definition, and a firmware without audio said (Concordia, a test definition) | Audio and Clicky read or written, Play a sound -- no board at hand has audio built in |
| Saved layouts on the Model F (Rico, 2026-10-18): Save; its own file loaded back as "the same"; a changed key found and written back; a .vil from Vial loaded; Nazg's file loaded in Vial; the Concordia's file asked about, its keymap left out. Save on the Concordia (probe). Unlock and apply, a macro and Boot written once unlocked; an ordinary key applied locked (F77 in secure mode). A file the VIA app wrote, loaded and applied on a VIA board (Rico) | |
| A board's own menus: a few EC features (Concordia Type C); all 35909 registry controls parse (test) | |
| Hotplug: boards plugged and unplugged, the list and the open board following (Rico, 2026-10-16) | |
| Key health on Rico's F77 recordings: nothing accused, 26 good and 49 to hold, as the mockup (test) | |
| Diagnostics on Rico's F77: key output off and on again, Key presses, Signal levels, Device; Enter bootloader and Erase EEPROM, the board closed and the list saying where it went (2026-10-17); Key presses and Enter bootloader on the F77 locked, past Vial's lock (2026-10-18) | |
| The Device section on Rico's boards (2026-10-18): the bootloader, said how where the firmware lacks it, keymap reset, macros emptied | |
| The key test (2026-10-18): the Concordia on the computer's keys, hatched keys and the shared-key marks; the F77 on its switches unlocked, on the computer's keys locked; Win and Alt+Tab taken while it reads them | |
| The key test's second round (Rico, 2026-10-26, the F77 among his boards): Computer's keys by default, Switches chosen and falling back, layers shown and followed -- Ctrl+Space and A found on layer 1 | Two layers sending the same keycode: Nazg takes the one shown, else the lowest, and may be wrong |
| The web build (2026-10-24): all tests under Node; the list, Settings, both bundles, settings kept across a reload; in Chrome over WebHID the F77 added, unlocked, key test, keys changed, Leyden Jar diagnostics; in Edge the Concordia with its definition imported, Load from file, Save to file, Export definition (Rico) | The web build on a high-DPI display |
| Linux in WSL (2026-10-18): GCC 13 build, all tests; the F77 lent by usbipd, refused until a udev rule, then Vial, unlock, keymap and Diagnostics (Rico) | Linux on a real desktop: uaccess, X11, the window icon, a locked board's key test (WSLg gets no keys from a board lent to Linux) |
| The AppImage (2026-10-18, released in 0.1.0-test.3): built in the container, 49 tests passing there, glibc 2.27 and libudev only; started in WSL, the F77 shown "not allowed" with Allow... (Rico). On a tester's KDE desktop (2026-10-24), a Keychron on VIA: started as it was, a few keys changed, the key test reading the switches (Keychron's `VIA_INSECURE`) -- his board already allowed, so no Allow... | The AppImage on an old distribution; Wayland with libdecor (GNOME) |
| A board Linux does not allow, in WSL on the F77 (Rico, 2026-10-18): "not allowed", Allow... with the commands pasted (no pkexec there), Nazg's rule file written; Allowed, Stop allowing, the file deleted -- a group rule standing in for uaccess | pkexec's password window; uaccess giving the board; whether Stop allowing acts before a replug |

## Traps already found

- **No board is opened while protocols are being probed**: HID gives every open handle a copy of
  each reply. The transport discards what is waiting before each request, but a copy arriving
  during one is still read as its reply.
- **Some firmware answers a request twice**: Keychron's Hall-effect boards answer
  `id_switch_matrix_state` from their own code, then again from QMK's -- hence the discarding.
- A Vial board's keycode version comes from its **Vial** protocol (vial-qmk always reports VIA 9); a VIA board's from its VIA protocol
  (`adapters/via/NazgViaLoader.*`, keycodes.md).
- Vial replies overwrite the buffer and echo unsupported commands: see via-vial-commands.md.
  A QMK setting must be read at its own width -- past it the reply still holds the request.
- **GCC 13 crashes** (an internal compiler error) on a braced list written inside a `co_await`
  -- `co_await f({ a, b })`: put it in a named variable first.
- **Emscripten's `-fwasm-exceptions` miscompiles a catch around a `co_await` in a loop**
  (5.0.7 and 6.0.13): the web build uses `-fexceptions`; do not switch it back without running
  the tests under Node (upstream-reports.md, 3).
- QMK numbers lighting effects among those built in, so VIA's effect names can be wrong; underglow's
  speed does nothing; Vial's lighting is the format before VIA V3 (via-vial-commands.md, "Lighting").

## Open, deferred, under review

- **Under review**: the user library's one-version backup ("Restore previous") -- Rico weighs its
  value against its complexity. "Export definition..." is for debugging only.
- **Not to be done**: using a definition whose VID:PID differs from the board's (Rico, 2026-10-12) --
  the picker says an import is for another keyboard and why it must not be used (a tester's K10 HE
  reports `3434:0EA7`, which no published file carries; via-registry.md).
- **Deferred**: library export / import (a web-build need).
- **Deferred**: following a display scale change while Nazg runs -- the scale is read once, from
  the primary display, so a window moved to a monitor of another scale keeps its sizes. The fix and
  how to try it: ui-design.md, "Open points" (2026-10-24; Rico has no second monitor).
- **Deferred, very low priority**: the HID console (ui-design.md, "The console") -- QMK Toolbox's
  job, not to be taken from it (Rico, 2026-10-18).

**Next**: the testers' reports on the fifth test build, `0.1.0-test.5` (2026-10-24), the first
**in a browser**, at https://mymakercorner.com/nazg/ -- put online by publishing it -- beside the
Windows zip, the AppImage, the macOS app and the manual page, a GitHub pre-release. `0.1.0-test.4`,
the same day, was the first for macOS and the first built by GitHub from its tag. Above all the app on a colleague's Apple Silicon
Mac: does it open past Gatekeeper, draw, see a board, and does macOS ask for Input Monitoring.
Still open from test.3 (2026-10-18): Linux's Allow... on a real desktop -- the first Linux
tester's (KDE and a Keychron, 2026-10-24: the AppImage, keys and the key test work) needed none;
hotplug, saved layouts, the Device section and the key test on testers' boards. From test.2: the
tester's Keychron Q6 knob works fully (2026-10-15); the K10 HE retest -- the transport fix -- was
due the week of 2026-10-19. CI is done (2026-10-24, `.github/workflows/build.yml`: Debug and
Release on all three systems, 49 tests each; build-and-release.md, "Windows on GitHub, first
run" and "Making one").

**In progress** (Rico, 2026-10-17, ahead of CI): the Leyden Jar diagnostics, built from the two
mockups (ui-design.md, "Plugins", "Built"), seen working on Rico's F77 (2026-10-17) -- never tested
on the B104, his daily board; the choices made in the build wait for his review. Read on the B104 controller (2026-10-17): protocol 0.9.1, 8x18, Beam Spring, one
bin (threshold 300, reference 376), levels 375-378; its controller bits are raw, set = released on
a Beam Spring. A controller wired to no working matrix is forced to all released by the firmware
(FF on a Beam Spring, 0 on a Model F) and every threshold set to a sentinel: the section says so.

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
[docs/research_material/keycodes.md](docs/research_material/keycodes.md). Keychron's own
commands on top of VIA -- the Hall-effect settings among them, read from its QMK fork, not yet
seen on a board -- are in
[docs/research_material/keychron-commands.md](docs/research_material/keychron-commands.md).

**Do not re-derive conclusions already written there.** If something in it is wrong, correct
the document rather than working around it.

# Architecture

```
Shell (Main.cpp)    SDL and the renderer: the window, the GPU, the ImGui backends, the frame loop
    |
App (app/)          Nazg itself, ImGui only: the screens, what a click does, the settings, the fonts
    |
UI (ui/)            sections: each opens the board itself and talks to the protocol it was
                    planned for; App makes them (ui/NazgSectionFactory.*)
    |
Model (model/)      Keyboard + BoardReport: what the board is and has -- the capability model.
                    PlanSections() (ui/NazgSectionPlan.*) turns it into the sections shown
    |
Board load          board/NazgBoardLoad.*: protocol detected, definition sourced, model filled
    |
Protocol layer      nazg::via | nazg::vial (VIA's branch) | nazg::leydenjar | nazg::qmk keycodes | (Keychron, XAP later)
    |
Transport           transport/: the DeviceChannel seam -- HidDeviceChannel::Open(), or a
                    scripted channel in tests -- over HidTransport's worker thread
```

**The capability model is the `Keyboard` and its `BoardReport`**: what a board has decides
which sections it gets, and VIA V3 `menus` and Vial's embedded definition feed in through the
same definition parser. **The UI is not protocol-free, and that is accepted** (Rico,
2026-10-16): a section is a feature module that calls the adapters directly -- Combos speaks
Vial's dynamic entries, Lighting VIA's custom values. A layer between sections and adapters
would be structure without a second producer behind it; it gets designed when one exists
(build order, step 5), its descriptor vocabulary a superset of VIA V3 menus with ZMK-style
typed parameters. What keeps sections thin is shared code below them: the board opened
by `HidDeviceChannel::Open()`, closed on every path, and the protocol's own functions.

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

**`Main.cpp` is the platform shell: every SDL and renderer call, and nothing else.** Nazg
itself is `App` (`app/NazgApp.*`), which knows no SDL: the shell hands it what only the platform
knows or does -- the executable's and data folders, the display scale, the OS's modifier names,
the time, logging, the file dialogs -- in one plain struct, `PlatformServices`, and calls
`BeforeFrame()` and `Frame()` around ImGui's frame. Switching backends is a one-file change.
Keep it that way: nothing outside `Main.cpp` includes SDL, and `nazg_core` does not link it.

**Build tooling: Visual Studio 2022 with the Visual Studio generator**, CMake 3.21+.
Generate with `GenerateBuildForVS2022.bat`, which writes to `build_VS2022/`.
**One script and one build directory per toolchain** — to add an IDE or platform, add a
sibling `GenerateBuildFor<name>` script targeting its own `build_<name>/` directory, so they
coexist without clobbering each other. `.gitignore` covers `build*/`.

**Linux: `GenerateBuildForLinux.sh`**, run in WSL's Ubuntu from this same checkout (`/mnt/c/...`):
GCC, `Ninja Multi-Config` into `build_Linux/`, so the commands are Windows' own --
`cmake --build build_Linux --config Debug`, `ctest --test-dir build_Linux -C Debug`. The packages
it needs, usbipd and the udev rule: build-and-release.md, "First build, in WSL".

**VIA's official definitions are built, not committed**: run
`python tools/update_via_bundle.py` once per checkout (Python 3.8+, standard library only;
it downloads the pinned commit of `the-via/keyboards` once and caches it). It writes
`build_resources/via_definitions.tar.xz`, shared by every toolchain; the build copies it beside
the executable, and the `via_bundle_contents` test parses every definition in it -- skipped
when it is absent. The pin is `resources/via-keyboards.commit`; `--update` moves it to
`master` and lists what changed, `--from-dir <clone>` works offline. Releases ship the bundle.

**The community definitions are fetched, not committed**, the same way:
`python tools/update_community_bundle.py` downloads the release of
`mymakercorner/nazg-community-definitions` pinned in `resources/nazg-community-definitions.release` (tag and SHA-256) to
`build_resources/community_definitions.tar.xz`; the build copies it, and
`community_bundle_contents` checks it. `--update <tag>` moves the pin; `--from-file <path>` takes
a bundle built locally by that repository's `tools/build_bundle.py`.

**A Windows package for testers**: build Release, then `python tools/package_windows.py` writes
`dist/nazg-<NAZG_VERSION>-windows-x64.zip` -- the app, its DLLs, the Visual C++ runtime
app-local, fonts, both bundles, `resources/package/README.txt`, the licences and the user manual
as one HTML page, `nazg-manual.html` -- `docs/user-manual.md` and its screenshots, built by
`tools/build_manual.py` (Python's `markdown` package, `pip install markdown`), which also writes
`dist/nazg-<NAZG_VERSION>-manual.html`, a release asset beside the AppImage. The version is
`NAZG_VERSION` in `CMakeLists.txt` (first: `0.1.0-test.1`, a GitHub pre-release); a release is
tagged `v<NAZG_VERSION>` on the commit it is built from.

**A Linux AppImage for testers**: in WSL's Ubuntu, `python3 tools/package_linux.py` builds Release
in an AlmaLinux 8 container (`tools/package_linux.Containerfile`: glibc 2.28, GCC 13, libdecor from
source; podman) into `build_manylinux/`, everything but glibc and libudev linked in, checks that,
and writes `dist/nazg-<NAZG_VERSION>-linux-x86_64.AppImage` with the static runtime -- no libfuse2.
`--test` runs the tests in the container too. Pins and layout: build-and-release.md, "The AppImage".

**A macOS app for testers**: `python3 tools/package_macos.py`, on a Mac, from a Release build in
`build_macOS/` (`-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`), writes `dist/nazg-<NAZG_VERSION>-macos-arm64.zip`:
`Nazg.app`, SDL3 and hidapi in its `Contents/Frameworks/` (static, their hidapi copies clash),
signed ad hoc. Only the CI runs it -- Rico has no Mac.

**Releases are built by GitHub from a tag** (build-and-release.md, "Making one"; first
`0.1.0-test.4`, 2026-10-24): set
`NAZG_VERSION`, commit and push, then push the tag `v<NAZG_VERSION>`. The workflow builds,
tests and packages all three systems and makes a **draft** release with the zip, the manual page,
the AppImage and the macOS app; Rico writes the notes and publishes. *Run workflow* on the
Actions tab rehearses it all without releasing. **Publishing puts the web build online**: the
Pages workflow builds the tag and deploys it to https://mymakercorner.github.io/nazg/.

On Windows, Ninja and VS2026 were both evaluated on
2026-09-21 and **deliberately deferred** — nothing in the project needs the v145 toolset, and
the current setup is verified working. Revisit later if desired; no known blockers (the
dependency tree looks CMake 4 clean, and VS CMake folder mode keeps full IDE debugging).

**The Emscripten web build: steps 1 and 2 of 3** (2026-10-24; build-and-release.md, "The web
build"): `GenerateBuildForWeb.sh` in WSL with emsdk 6.0.13 into `build_Web/`; it builds, draws
and keeps its settings in IndexedDB, and the tests pass under Node
(`ctest --test-dir build_Web -C Release`). Step 2, the WebHID transport
(`transport/NazgHidTransportWebHid.cpp`) and Add a keyboard... (ui-design.md, "The list in a
browser"), **works on the F77 in Chrome** (Rico, 2026-10-24: unlock, key test, keymap edits,
Leyden Jar diagnostics; no difference in speed from the native app); the app's browser pane has
no device chooser, so boards need Chrome or Edge. The browser's file dialogs -- upload for Import
and Load, download for Export and Save -- written the same day; all four work in Edge, on the
Concordia (Rico). GitHub
Pages, https://mymakercorner.github.io/nazg/, is put online by `.github/workflows/pages.yml` when
Rico publishes a release; CI's `web` job builds and tests it on every push; every file is asked
for with the version (build-and-release.md, "On GitHub Pages"). Served as
`https://mymakercorner.com/nazg/`, the account's custom domain, its certificate fixed 2026-10-24;
online from the first release published after that, `0.1.0-test.5`.

It is a dedicated `#ifdef __EMSCRIPTEN__` path in `Main.cpp` plus a branch in
`CMakeLists.txt` — **not** a renderer abstraction layer, and not a second codebase: the same
`App`, inside another shell. This is only affordable while SDL and renderer calls stay
confined to `Main.cpp`, which is the main reason that rule exists. SDL_GPU has no WebGPU
backend, so the browser draws through `imgui_impl_opengl3` over WebGL 2. **Exceptions are
Emscripten's JavaScript-based ones (`-fexceptions`)**, with `-mtail-call` -- WebAssembly's own
are miscompiled in coroutines, and that choice is also what lets `emscripten_set_main_loop`
keep `main()`'s locals (client-architecture.md, "Exceptions, and the frame loop").

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
- **Each adapter folder is a namespace inside `nazg`**: `adapters/via` is `nazg::via`,
  `adapters/vial` is `nazg::vial`, `adapters/qmk` is `nazg::qmk` (Keychron's will be
  `nazg::keychron`). Names drop the protocol, as they drop "Nazg": `via::Protocol`,
  `vial::Protocol`, `vial::LoadKeyboard`, `qmk::KeycodeVersion` — never `via::ViaProtocol`.
  File names keep it (`NazgViaProtocol.h`). **A name belongs to the command family that
  carries it on the wire**: VialRGB and Vial's macro formats ride VIA's commands, so they are
  `via::`; what goes behind Vial's `0xFE` prefix is `vial::`. Vial and Keychron code use `via::`
  freely; `via::` never names `vial::` or `keychron::`. **Outside its own namespace a name is
  always written qualified** (`via::Protocol`, `nazg::via::Protocol` from global code), never
  through `using`; a test writes `namespace via = nazg::via;` and then `via::X`.
  What every protocol shares stays in `nazg`: `ProtocolError`, `adapters/NazgByteOrder.h`,
  `adapters/NazgXz.h`.
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
- **Vial's firmware updater (vibl)** (Rico, 2026-10-18) — vibl runs on the STM32F103 only, is
  installed with an ST-Link by the maker, and no keyboard in vial-qmk uses it but Vial's own
  example (`VIBL = yes`); last pushed 2022. Flashing, if ever, would be UF2 and DFU
- **Macro recording** -- typing the computer's keyboard into a macro (Rico, 2026-10-10, dropped
  2026-10-18): no sign users want it (ui-design.md, "The Macros section"). Not QMK's Dynamic
  Macros, whose keys the picker offers

**Not** ruled out, despite being far down the list: **ZMK Studio** is a legitimate future
backend, blocked mainly on having no hardware to dogfood against.

# Links

- QMK: https://docs.qmk.fm/ — XAP tracking PR: https://github.com/qmk/qmk_firmware/pull/13733
- Vial: https://get.vial.today/
- GP2040-CE: https://gp2040-ce.info/
- ZMK Studio: https://zmk.dev/docs/features/studio
