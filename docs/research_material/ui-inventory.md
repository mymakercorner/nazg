# UI inventory — Vial, VIA, ZMK Studio, and Nazg today

*Compiled 2026-09-26, as the starting point for Nazg's visual design. Inventory only — no
design decisions are taken here.*

What each existing configurator shows, how the screens are arranged and how a key gets
edited; then what Nazg draws today, what of it is meant to survive, and what the protocol
layer can already do that no screen shows yet.

## Sources

| App | Where the facts come from |
|---|---|
| **Vial** | The user manual at <https://get.vial.today/manual/> (first use, layers, quantum, macros, tap dance, combos, QMK settings, any key) — thin on the window as a whole, so the tab and menu lists come from `vial-gui`'s `main_window.py`, `keymap_editor.py` and `tabbed_keycodes.py`, **read, not ported** |
| **VIA** | The reference clone `../../via-app` (the-via/app at `935106a`, 2026-09-17): `utils/pane-config.ts`, `components/panes/*`. Image-search screenshots were not usable as a source |
| **ZMK Studio** | The official docs are short — <https://zmk.dev/docs/features/studio> says what it can change, not how it looks; the Mechboards guide (<https://guides-mechboards.gitbook.io/guides/other-guides/using-zmk-studio>) confirms the flow in four screenshots. **The app itself is the documentation**: a reference clone is at `../../zmk-studio` (zmkfirmware/zmk-studio at `1bb6bb8`, 2026-07-02), ~3.3k lines of React, with Storybook stories for the board, the key, the layer list, the layout picker and every parameter picker |

The ZMK Studio stories can be run with no hardware — `npm install` then `npm run storybook`
in the clone — which is the only way to see its screens without a ZMK board.

---

## Vial

### Window

One window: a **device combo box** and a **Refresh** button across the top, then a row of
tabs, each shown only when the board supports it:

Keymap · Layout · Macros · Lighting · Tap Dance · Combos · Key Overrides · Alt Repeat Key ·
QMK Settings · Matrix tester · Firmware updater

Menus:

| Menu | Items |
|---|---|
| File | Load saved layout... · Save current layout... (a `.vil` file) · Sideload VIA JSON... · Download VIA definitions · Load dummy JSON... · Exit |
| Keyboard layout | the host layouts (QWERTY, German, French...) — Vial's name for what Nazg calls the host layout |
| Security | Unlock · Lock · Reboot to bootloader |
| Theme | System plus a fixed list |
| About | About *board*... · About Vial... |

With no board: a "No devices detected" label.

### Keymap tab

- Header: **layer buttons** — square, numbered 0, 1, 2... — and **+ / −** zoom buttons
  (steps of 0.1).
- The board, centred and scrollable, above a **tabbed keycode palette**. The manual calls
  them the "top palette" and the "bottom palette".
- Click a key, then click a keycode: it is written at once, and **selection advances to the
  next key**. Clicking empty space deselects.
- **Double-click** a key, or "Any" in the palette, opens the **Any key** dialog: type a QMK
  expression (`LT(1,KC_A)`, `RALT(RSFT(KC_A))`) or a raw hex value.
- Palette tabs: Basic · ISO/JIS · Layers · Quantum · Backlight · App, Media and Mouse · MIDI ·
  Tap Dance · User · Macro. **Basic and ISO/JIS are drawn as keyboards** (ANSI/ISO 100%,
  80%, 70% — the one fitting the window). **No search box.**
- **Composed keycodes are built in two steps.** Picking `LSft(kc)` or `LT 1 (kc)` from
  Quantum or Layers puts the outer part on the key with an empty inner placeholder; click
  the placeholder, then a Basic key. Keys whose inner part is limited to basic keycodes
  ("masked") restrict the palette accordingly.
- Transparent is drawn as ▽.

### Feature tabs

Macros, Tap Dance, Combos and Key Overrides are **numbered tabs of slots** (M0–M15, TD0...),
the count coming from the firmware's EEPROM. Each has **Save** and **Revert** — unlike the
keymap, these are not written until saved.

- Macros: actions Text, Tap, Down, Up, Delay, reorderable; **Add action**, **Tap Enter**,
  **Record macro**; memory use shown.
- Tap Dance: On tap · On hold · On double tap · On tap + hold, plus tapping term.
- Combos: up to four keys and an output.
- QMK Settings: groups (Magic, Grave Escape, Tap-hold, Auto Shift, Combos, One Shot Keys,
  Mouse Keys) of checkboxes and numbers.

Assigning a tap dance or macro to a key happens back in the Keymap tab, from the Tap Dance or
Macro palette tab.

---

## VIA

### Window

A web app (also wrapped in Electron). A **top menu of panes**, each an icon and a title:

Configure · Key Tester · Design · HID Console · Settings · Debug · Errors

Design and HID Console are hidden unless enabled in Settings. With no board: a loader and an
**Authorize device** button (the WebHID permission prompt). The board's name is a **badge**
that drops down a list of connected boards to switch between.

### Configure pane

- The board across the top — **2D, or 3D** (a Settings choice, with keycap themes) — and the
  selected sub-pane under it.
- A **left column of icons** picks the sub-pane: **Keymap**, **Layouts** (only when the
  definition has layout options), **Macros** (when the firmware supports them), **Save +
  Load**, then one entry per **V3 `menus`** entry — lighting and custom features, rendered
  from the definition.
- **Layer** control: a row of numbered buttons labelled "Layer".
- **Host layout badge**: the keymap-extras list, as in Nazg.
- Keycode picker: category tabs — Basic · Lighting · Media · Macro · Layers · Special ·
  Custom — plus **Any**, a modal taking a QMK expression. The board is only selectable in
  the Keymap sub-pane.
- Save + Load: Save Current Layout / Load Saved Layout (a JSON file).
- Writes are live, key by key; there is no save step for the keymap.

### Other panes

- **Key Tester**: highlights keys as they are pressed; **Test Matrix** switches to the
  firmware's matrix state; Reset Keyboard; a toy synthesiser (key sounds, waveform, scale).
- **Design**: load a draft definition, pick which one draws the board, show the matrix,
  V2 compatibility — the definition author's workbench.
- **Settings**: Show Design tab · Show HID Console tab · **Fast Key Mapping** (the same
  auto-advance Vial does) · Slider Mode · Light Mode · Keycap Theme · Render Mode (2D/3D) ·
  Show Diagnostic Information.

---

## ZMK Studio

### Window

A web app and a Tauri desktop app, same React code. Three bands:

- **Header**: ZMK logo and "Studio" on the left; the **connected device's name** in the
  middle, dropping down *Disconnect* and *Restore Stock Settings*; on the right four icon
  buttons with tooltips — **Undo, Redo, Save, Discard**. Save and Discard are enabled only
  while the device reports unsaved changes (it pushes a notification when that changes).
- **Body**: a left column and the board beside it, with the binding editor below the board.
- **Footer**: copyright, About, licence notices.

Connecting is a **modal**: "Welcome to ZMK Studio", pick USB or BLE, then "Select A Device".
If the device is locked, a second modal — **"Unlock To Continue"** — explains the
`&studio_unlock` key and links to its docs; it closes by itself when the device reports it
is unlocked.

### Main screen

- **Left column**:
  - **Layout** — a drop-down of the board's physical layouts, **each drawn small** in the
    list. Choosing one is undoable.
  - **Layers** — a list of **named** layers, with **+** and **−** buttons (enabled while the
    firmware has spare or removable layers), a **pencil** to rename (a "New Layer Name"
    modal), and **drag to reorder**.
- **Board**: keys show a small **header** — the behaviour's short name (from
  `behavior-short-names.json`) — over the parameter's label. The hovered key **grows to
  125 %** with a shadow; the selected key is filled with the accent colour. A **zoom
  drop-down** at the top right — Auto, 25 % to 200 % — remembered between sessions.
- **Binding editor**, below the board, only while a key is selected:
  - **Behavior** — a drop-down of every behaviour the firmware lists.
  - Then an editor per parameter, **chosen from the parameter's declared type**: a HID usage
    becomes a **searchable combo box** grouped by usage page, with **implicit-modifier
    checkboxes** beside it; a layer id becomes a layer drop-down; a range a number input;
    constants a drop-down. When a behaviour has several parameter sets, the second
    parameter's editor follows the first.

### Save model

Every change is sent at once and takes effect on the board, but lives in RAM until **Save**;
**Discard** throws the session away. Every edit — binding, layer add/remove/move/rename,
physical layout — goes on an **undo/redo** stack whose entries replay the inverse request.
This is the model the survey recommends taking (README, "For a multi-protocol client").

---

## Side by side

| | Vial | VIA | ZMK Studio | **Nazg today** |
|---|---|---|---|---|
| Choosing a board | combo box + Refresh | name badge, Authorize device | connect modal (USB/BLE), device menu | HID table, **Open** per row |
| Top-level structure | tabs per feature | panes, then sub-panes in a left column | one screen | three floating ImGui windows |
| Layers | numbered square buttons | numbered buttons | named list: add, remove, rename, reorder | tabs "Layer 0..n" |
| Zoom | + / − | fit | Auto, 25–200 %, remembered | fit, clamped to 28–64 px per unit |
| Key → keycode | click key, click palette; auto-advance | same; auto-advance is a setting | click key, edit binding below | click key, click picker |
| Picker structure | tabs; Basic drawn as a keyboard | category tabs | behaviour, then typed parameters | one list of collapsing groups |
| Search | no | no | yes, in the HID usage combo | yes, a filter box |
| Composed keycodes (`LT`, `MT`, mods) | two steps, inner placeholder | Any modal | parameters | **not yet** — only layer keys `MO TG TO TT OSL DF` |
| Typed expression | Any key dialog | Any modal | — | — |
| Host layout | Keyboard layout menu | badge | planned ("host locale") | combo box, 69 layouts |
| Layout options | Layout tab | Layouts sub-pane | layout drop-down with drawings | read from the board and drawn, **not editable** |
| Unlock | Security menu | none (VIA has no lock) | automatic modal | status readable, **no UI** |
| Save model | keymap live; features Save/Revert | live | live + Save/Discard + undo/redo | live, with read-back |
| Keymap file | `.vil` save/load | JSON save/load | planned | — |
| Matrix / key tester | tab | pane | — | — |
| Macros, tap dance, combos... | tabs | Macros only | — | — |
| Lighting / custom menus | Lighting tab | V3 `menus` sub-panes | — | — (step 5) |
| Theme | menu | light/dark, keycap themes, 2D/3D | follows the system | ImGui dark, colours hard-coded |
| Where the definition came from | not shown | Design pane only | n/a — always the device | **always shown**, with Change / Forget choice |

The last row is Nazg's own concept: no other client has user and official definitions side
by side, so none of them has a screen for it to copy.

**All three draw the board above and the editor below**, and all three edit by selecting a
key first. They differ in what sits beside the board: nothing (Vial, tabs above), a column
of sub-panes (VIA), or the layer and layout lists (ZMK).

---

## Nazg today

### Screens

Everything below is ImGui windows on the default dark style, laid out by the user and saved
in `imgui.ini`.

**"Nazg" window** — pure scaffolding: tagline, SDL / ImGui versions, GPU backend, display
scale, frame time, a checkbox for ImGui's demo window.

**"HID Devices" window** (`Main.cpp`):

- **Refresh**, and a count: "N keyboard(s)", or keyboards and HID interfaces with the
  checkbox below.
- **Import VIA definition...** (tooltip: what a via.json is).
- "Official definitions: N, from VIA" — or "not found" in amber; hover gives the bundle path
  and pinned commit.
- "User definitions: N" — hover gives the folder — then one line per entry: **Re-import**,
  **Restore previous** (only with a backup; *under review*), **Remove**, then
  `VID:PID  name`, hover giving where it was imported from and when. Messages under it.
- **Show all HID devices** checkbox.
- A table: *Open* · Product · Manufacturer · VID:PID · Usage · Interface.

**"Replace a user definition?" modal** — Replace · Keep both · Don't import, with a
paragraph explaining each.

**"Keyboard" window**, shown once a board is opened:

- *loading...*, or the error in red.
- **Choosing a definition** (`ui/NazgDefinitionPicker.*`): "*product* -- which
  definition?", then each candidate **drawn small** beside its name and source, with *Use
  this one* / *Keep this one* ("in use now"), *Import a definition...*, *Cancel*.
- **The board**: "*name* -- QMK keycodes *version*", the **Host layout** combo box, then
  "Definition: *source*" in grey with **Export definition...** (debugging only),
  **Change definition...** and **Forget choice** (VIA boards).
- **Layer tabs**, then the board (`ui/NazgKeyboardView.*`): keys filled grey, lighter on
  hover, **orange outline** when selected; a small grey secondary legend (Shift character,
  hold action) above the primary one, from the top-left; rotation supported. Hover: the
  keycode's name and the matrix cell.
- Under a separator: "Click a key to change it.", or "Layer L, row R, column C: *keycode*",
  *writing...*, and the result — green, or amber when the board stored something else.
- **Keycode picker** (`ui/NazgKeycodePicker.*`): a filter box ("filter: name or label"),
  then **collapsing headers** per group — *layers* first, then QMK's own groups — of
  fixed-width buttons captioned with the host-layout legend; hover gives the QMK name and
  label.

### What survives the redesign

| Piece | Why it stays |
|---|---|
| `ui/NazgKeycapLegend.*`, `ui/NazgHostLayoutTable.cpp` | legends are a Keycode seen through a host layout — decided in keycodes.md, "Host layouts" |
| Board geometry — `PlaceKeys()`, KLE rotation | verified against all 2029 V3 definitions; the drawing only consumes it |
| Rotating a key by turning its vertices after drawing it straight | cheap, and works for any drawing call — a technique, not a look |
| Previews of a definition drawn small | the one idea ZMK Studio shares (its layout picker) |
| Read-back after a write, and saying when it differs | exposes Vial's keycode firewall; the message can move, not go |
| The definition's source always visible | a wrong definition must be plain to see (via-registry.md) |

Everything else — window arrangement, colours, the font, the picker's structure, the
device table — is marked FIRST DRAFT in its header and is expected to go.

### What the protocol layer can do that no screen shows

| Capability | Where | Screen missing |
|---|---|---|
| Vial unlock status | `VialProtocol::GetUnlockStatus()` | lock state; the unlock flow itself is not implemented |
| Vial entry counts — tap dance, combos, key overrides | `VialProtocol::GetEntryCounts()` | every feature editor |
| Encoders | `VialProtocol::GetEncoder()` | encoder bindings |
| Macro count and buffer size | `ViaProtocol::GetMacroCount()`, `GetMacroBufferSize()` | macros |
| Layout options, read from the board | `Keyboard::layoutSelection` | choosing them (`SetKeyboardValue` not wrapped yet) |
| Keycodes of every kind — mod-tap, layer-tap, modified, tap dance, macro | `Keycode` variant, the QMK codec | composing them in the picker; they display correctly already |

---

## Questions this raises for the design

Listed, not answered:

1. **One screen or several?** ZMK Studio fits on one because it does only the keymap;
   Vial and VIA need tabs or sub-panes once macros and lighting arrive. Nazg's step 5 adds
   descriptor-driven features, so the question is where they go without cluttering the first
   glance.
2. **Device choice**: a table (now), a combo box (Vial), a badge (VIA) or a connect screen
   (ZMK). Nazg also has to fit definition choice in, which none of them do.
3. **Layer list**: numbered buttons, tabs, or ZMK's named column. Named layers exist only on
   ZMK; VIA and Vial have numbers.
4. **Picker**: palette tabs, a keyboard-shaped Basic tab, a searchable list, or ZMK's
   behaviour-then-parameters. The last maps directly onto the `Keycode` variant (a kind,
   then its fields) and is the one route to composing `LT`/`MT` without Vial's two-step
   placeholder.
5. **Auto-advance** after assigning: default in Vial, a setting in VIA, absent in ZMK.
6. **Save model**: VIA and Vial write through; ZMK stages, with Save/Discard and undo/redo.
   Nazg can offer undo on a write-through protocol (replay the old keycode, as ZMK does) —
   whether it should is a first-glance question.
7. **Windows**: ImGui's free-floating windows versus a fixed layout filling the SDL window.
