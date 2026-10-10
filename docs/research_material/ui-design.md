# UI design — the workspace

*Decided with Rico 2026-09-26, from [ui-inventory.md](ui-inventory.md). Mostly design: only
the section contract has begun in code (see "Plugins", "Next step"). The pictures are wireframes: they fix what goes where, not the look.
They are drawn by `tools/draw_ui_wireframes.py`; change a picture there and run it again.*

The first-draft screens (three floating ImGui windows) are replaced by **one fixed window**
filling the SDL window, divided into five regions. What each region shows depends on the
board and on the section chosen; the regions themselves never move.

## The regions

![How Vial, VIA and ZMK Studio divide their window, and Nazg's division](ui-design/workspace-comparison.svg)

Nazg takes ZMK Studio's skeleton — a thin header, a column beside the board, the editor under
it — and VIA's idea of a column that switches what the bottom area edits, which is where
features beyond the keymap, custom ones included, find their place. Vial's tab per feature
was not taken: a feature-rich board shows eleven tabs before anything is done.

| Region | What it holds |
|---|---|
| **Header** | The board's name, which is a menu (see "Getting back to the keyboard list"); the protocol; the lock state on boards that have one; the **settings button** |
| **Section column** | Keymap, Layout, Macros... — 180 px wide by default, so every custom menu label in VIA's registry shows whole (Rico, 2026-10-09), an icon and a label per row, icons from **Tabler**. **Only the sections the board has**, and **hidden when there is only one**. **Resizable**: its edge drags the list between 120 and 200 px, and below 100 px it folds to icons only, 44 px; a double-click on the edge folds or unfolds it (Rico, 2026-10-09; see "Open points") |
| **Strip** | A row of choices owned by the section: layers in Keymap, slots in Macros (see below). Absent when the section has nothing to choose |
| **Board** | Drawn by Nazg; what each key shows is the section's |
| **Panel** | The section's editor — the keycode picker in Keymap (see "The keycode picker"). **Always visible**, not only while a key is selected |

Decided: **layers as a strip** (not ZMK's named column: VIA and Vial layers are numbers),
**the panel always visible**, **a settings button** rather than top-level panes like VIA's.
Top-level panes stay possible later, if a full-screen tool such as a matrix tester needs one.

## Sections, and the strip that belongs to them

The strip is only useful for layers in Keymap (and encoders later) — layout options, macros,
tap dance, combos, lighting and settings do not depend on the layer. So the strip belongs to
the section: it is the section's own row of choices.

![The same frame showing Keymap, Macros and a Leyden Jar diagnostics plugin](ui-design/sections.svg)

| Section | Strip |
|---|---|
| Keymap, encoders | layers |
| Macros | M0 … M15 |
| Tap dance, combos, key overrides | their slots |
| Leyden Jar diagnostics | its modes: key presses, signal levels, device |
| Layout options, settings-like sections | none — the strip is absent |

The macro, tap dance and combo slots are Vial's numbered tabs, moved into one fixed place.

**A strip is always one row** (Rico, 2026-10-10): it uses the width the board leaves -- as the
layers do over Keymap's board -- and never takes a second row's height. When its entries do not
fit, **it scrolls sideways**: the mouse wheel over it scrolls it, the selected entry is kept in
view, and the side with more to see says so -- a fade at that edge, and **◀ ▶ at the strip's
ends** (Rico), each scrolling a page, greyed at its end; no scrollbar. Absent while everything
fits. A thin scrollbar under the strip was the other choice, in the mockup
`ui-design/macros-section.html`. Met first with Macros: Rico's Leyden Jar boards have 64 macro slots, about 2 750 px of
strip for some 950. Wrapping into rows, and a grid of all slots behind a button, were the other
ways; VIA lists its slots in a vertical column that scrolls, which costs a column of width.

### What a section provides

Every section, built in or a plugin, supplies four things:

1. **Its strip entries**, or none.
2. **What the board shows** — each key's colour, legend or value, and what a click on a key
   does. Nazg draws the board; the section only fills it. A section that does not use the
   board (macros, settings-like ones) may fold it away and give the room to its panel.
3. **Its panel.**
4. **Its match rule** — which boards it appears for.

### How a section describes the board

The board is where the visual styling will change most, so a section says **what** each key
means and never **how** it looks — colours, fonts and sizes belong to Nazg's board renderer
and theme, and a restyle touches only those. Checked against three coming changes: the
Leyden Jar tool's keys, keycap colour themes like VIA's (Olivia, Dolch, Jamon...), and
sublegends printed on some keycap sets (Hiragana, Hangul).

1. **Legends by position.** A key has slots at KLE's twelve legend positions — top, middle,
   bottom, each left, centre and right, plus the front — each filled or empty and carrying a
   role (label, sublegend, value) the renderer styles. Keymap fills top left and middle left
   (`KeycapLegend`'s two legends today); the Leyden Jar level view fills top, middle and
   bottom left with the maximum, current and minimum levels and bottom right with the bin;
   a sublegend set fills one more slot, from a table like the host layouts. `KeycapLegend`
   grows from two fields to the slots. A sublegend's glyphs also need a font holding them —
   a font matter in `Main.cpp`, not a structural one. *Decorative sublegends were dropped
   2026-10-03 (see "Legends", "Second legends: functional only"): the sublegend role and set
   go with them.*
2. **Fill by meaning**: the keycap's colour class (alpha, modifier, accent — what VIA's
   themes colour), a value from 0 to 1 for a heat map, or neutral. Keycap themes need the
   parser to keep each key's KLE colour, which it drops today. **States** — selected,
   hovered, highlighted, dimmed, warning, pressed — are drawn as an outline or an overlay,
   never as the fill, so they read on every theme; a heat map overrides the theme while shown.
3. **Geometry from the section, when it wants its own.** By default the board's definition;
   the Leyden Jar's controller matrix is a plain grid instead.
4. **Lines drawn over the board**, from key to key — the matrix view's wiring.
5. **Labels around the board's edges** — the matrix view's row and column rulers, the
   Leyden Jar's `R0…`/`C0…` connectors.
6. **Hover shared both ways.** The section hears which key or edge label is hovered or
   clicked, and can highlight keys from its panel — not only "a key was clicked".

**Named colours for panels**: error, warning, success, muted — placeholder values until the
styling, and the only colours a section may use. `Main.cpp`'s hard-coded `TextColored` calls
are what this replaces. **No pixels** anywhere in the contract: the layout sizes the regions,
and a section can at most say it does not use the board.

### How the board's look is built

*Decided with Rico 2026-10-03, when implementing the look started.* Rule 1's slots were the
placement itself; with the decided legends, placement depends on what no section should know --
the legend family, the key's size in pixels, the chain from full word to short form. So a key's
legends come in **one of two forms** (a `std::variant`, as `Keycode` is):

- **Keycap legends** -- what the key *is*, with no positions: the main legend or the plain/Shift
  pair, the AltGr character and Bépo's fourth level, the placement class and its exceptions, the
  numpad's second legend, a command's header and main legend with their short forms and
  category, a tap-hold's hold, transparent or `KC_NO`, the peeked layer's legend. Keymap fills
  these; the renderer places them.
- **Slot legends** -- rule 1's twelve KLE slots with a role, placed exactly as the section says:
  the Leyden Jar's levels and bin, anything a section puts at a meaningful spot of the keycap. A
  new need there is a new role the renderer styles, never pixels.

Three layers, the first two pure and in `nazg_core`:
1. **Content** -- `KeycapLegend` grows from two strings to the keycap legends: a `Keycode` and a
   context (host layout, modifier names, the key's side of the space bar, the lighting state)
   read through the **legend set**, committed tables -- placement classes and exceptions, the
   short forms of short-forms.md, the ink offsets. Board-wide facts are computed once per load.
2. **Layout** -- one key's content, the family, the face's size in pixels and a text measurer in;
   a few primitives out, in the key's own pixels: text with a colour *role*, a drawn arrow, a
   band, a mark. Every placement rule lives here. The measurer is an interface: ImGui's font in
   the app, a fake one for rule tests, and Arimo loaded through ImGui's core (no SDL, no GPU) for
   a test that re-runs the mockup's checks -- every short form fits, no legend overlaps another or
   leaves the face -- so a new QMK keycode that does not fit fails the build.
3. **Drawing** -- `NazgBoardView`: the board's size and its 9 px floor, the keycap's contour
   (shared by fill, border, lip and state outlines), the plate, then the primitives through the
   theme; rotation by turning vertices, as before. `NazgTheme` holds the theme tables and the
   category colour solver, itself pure and tested over every theme, keycap class and category.

Settings -- theme, keycap style, legend family (a setting, Cylindrical by default), modifier
names, host layout -- reach the view as one value, saved in `imgui.ini`. Built in five steps,
each checked on a real board: keycap shape and themes; fonts (Arimo and its Noto fallbacks,
committed under `resources/fonts/` with their licences); standard keys in both families; command
keys; transparent, `KC_NO`, peek and the lighting policy.

**Step 3 as built** (2026-10-03, verified by Rico on the Model F). What the build had to
settle beyond the decisions below, each one constant or table entry to change:
- **Weights.** Arimo comes in Regular and Bold only, so the mockup's 500 became **Bold**: letters
  on both families, modifier text on spherical sets, headers. Cylindrical modifier text, the AltGr
  character, the fourth level and the numpad's second legends stay **Regular**.
- **Sizes are em sizes**, as the mockup's CSS: ImGui sizes a font by its line height, 1.117 em for
  Arimo, so the measurer converts. The Noto faces merged behind it are scaled to Arimo's em too --
  before, ImGui drew them by their own line height, up to 2.1 em, and Arabic letters at half size.
- **Lines are placed by a capital's height**, centred in the room a line takes, not by the font's
  ascent: the same on every font, and within a pixel of the mockup's canvas.
- **The font test caught three things the mockup's checks never drew**: spherical POWER (Bold
  capitals) is too wide for 1u -- short form PWR; a spherical lone letter met a wide AltGr
  character bottom right on the smallest board (German and Turkish Q with @, Portuguese Mac Q with
  Œ) -- it now moves left just enough, as a legend does for a header; and fifteen host-layout
  entries are QMK's names for keys, not characters (Henkan, Hanja, Neo's "layer 3") -- set in words,
  as a modifier, and cut on 1u where they must be.
- **A legend moves aside for the header only when it shares the header's row**: a lone centred
  letter in the middle of the key stays centred.
- **Holds, modified keys and layer keys already print their short headers** -- Ctrl, L1, Ctrl+,
  Hold / L2, from short-forms.md's parts -- top right, uncoloured; bands and category colours are
  step 4. Every other command prints its QMK label, centred, until then.

**Step 4 as built** (2026-10-03, checked by Rico on the Model F, "Ctl ⌘+" included). Command keys print a header over
their main legend, a band along the top of the face, both in the category's colour:
- **The command table** -- `ui/NazgCommandTable.cpp`, written from short-forms.md, 249 entries --
  with the numbered families, Space Cadet and the fallback built in code. Space Cadet is a
  tap-hold whose tap is what Shift with 9 or 0 types on the host: "(" on US, "=" for SC_RCPC on a
  German host, which is what the key does there. MIDI and steno print QMK's name, split after its
  prefix ("STN_" over "RES1"), with no category and no band. A test checks that every QMK feature
  keycode has a header and a category, so a new one cannot be forgotten.
- **Two headers, one band**: `KeycapLegend` carries a `hold` and a `header`, each words and a
  category; the band is the hold's, else the header's. A modified key's "Ctrl+" is a Host header,
  so it **gets a Host band** -- the design gave it the Host colour without saying band or not.
- **Colours** are pure code now, `ui/NazgPalette.*`: the three themes' tables, moved out of
  `NazgTheme.cpp`, and the category solver, which checks the contrast of the colour as packed in
  8 bits. `NazgTheme` remembers each solved colour per face. The **palette test** asserts the
  lightness order of face, plate and lip on every theme; every band legible on every face of
  every theme; every header on every alpha and modifier face. Only one header falls back to the
  legend colour: Firmware on Dark's lifted slate accent.
- **Colour blindness, measured** with Machado's matrices: the search's 0.086 holds where it was
  run, Light's and Dark's alphas and modifiers (0.085 here, after 8-bit rounding), and the test
  asserts 0.08 there. **Two places fall short, both Behaviour against Host under deuteranopia**:
  Dracula's alphas, 0.057, and **Dark's lifted slate accent, 0.016** -- on Dark's Esc and Enter
  caps a violet and a cyan header look the same to a deuteranope; there the headers are near
  white, to reach 4.5:1 on a mid-tone face, and little hue is left. The test prints these and
  does not assert them.
- **Settled: accepted as they are** (Rico, 2026-10-04). Colour is a second cue: every command
  key prints its header in words -- "Media", "Layer", "Boot" -- so a colour-blind user loses the
  grouping at a glance, never what a key does; colour is never the only carrier of meaning. The
  fixes measured would have changed the look for everyone: a pale slate on Dark (`#889ab1` with a
  dark legend, 0.065; `#b5cde7`, 0.080 but Esc and Enter very light on a dark board), or hues
  of Dracula's own (only 0.074 at best, one category at 15° beside Firmware's red). So the bar,
  0.08, holds on Light's and Dark's alphas and modifiers, as the test asserts; on accent caps and
  on Dracula the header's words tell the categories apart. Dracula's hues can be searched again
  with a theme palette tool, once a theme of one's own is designed.
- **The solver walked from black or white, not from the face** -- fixed 2026-10-04. Its walk
  started at pure white on a light cap and pure black on a dark one. On the extreme caps that is
  the same, but on Dark's mid-tone slate black already reaches a band's 3:1, so every band there
  but Firmware's came out black. It now skips the lightnesses on the face's own side; nothing
  else moved, and the palette test checks every solved colour lies past its face.
- **Choices the build made**, one constant or rule each:
  - **Command main legends are Regular on both families**: in Bold, Arimo's nearest to the
    mockup's 500, spherical "Unswap" no longer fitted 1u. Command keys leave the family's rules
    already.
  - **Two modifiers shorten to their shortest words, Cmd as ⌘**, where their words do not fit:
    only Mac's "Ctrl Cmd+" and "Opt Cmd+" (43.3 px against 40.8 of room, 1u at the smallest size)
    -- "Ctrl Win+" (39.7) and "Ctrl Sup+" (40.3) fit. They print **"Ctl ⌘+" and "Opt ⌘+"** there
    (Rico, 2026-10-03: initials, "C G+", read badly, and "Ctrl C+" reads as Ctrl+C). ⌘ is the one
    icon in the legends, on Mac names only, where it is a habit. The same for holds and LM / OSM.
  - **Long holds**: "Alt Gr", "Shift", "Option" or "Super" over W, @ or an accented capital on 1u
    met the tap's legend at the left. A legend the headers would reach **goes below them**, its
    ink clear of theirs by the clearance kept beside them; a pair that then runs off the key
    **drops its Shift character** -- the fallback the mockup had for Large size, hover gives it --
    and words take the room left. Bépo's fourth level keeps the same distance under a hold.
  - **The board's lighting systems** are the union of what the definition says: Vial's or VIA
    V2's `lighting` preset, and VIA V3's keycode modules and standard menus, which the parser now
    keeps (their ids only). LED Matrix comes from an LM_* key on any layer.
  - TD(n) prints "Dance" until the board's tap dances are read; then it is drawn from its slot
    ("The Tap Dance section", "On the Keymap's keys").
- **Checked in Arimo** (`legend_font`): about 520,000 draws on 1u, both families, every size --
  every named keycode with each modifier names, side and three lighting sets; layers 0 to 31,
  LM and OSM with every modifier set, modified keys, TD 0 to 255, macros 0 to 127; every basic
  command under L15, Alt Gr and Ctrl+Shift; every host layout's positions under a layer and seven
  holds. None cut but the host layouts' own words (step 3), none leaves the face, no overlap, and
  every character of every named key's words has a glyph.
- **Not in step 4**: the firmware state (old, new, unknown) behind which lighting keycodes the
  picker offers and the hover warning on a dead one -- the picker is a first draft; Vial's tap
  dances drawn as tap-holds, once their entries are read; a VIA definition's `customKeycodes`
  names for QK_KB_n.

If a need still appears later — icons on keys, say — changing the contract costs one struct
and its implementations, two while they are Keymap and the Leyden Jar. It gets expensive once
other people write plugins, which is why these six points go in from the start, and why the
styling should settle before plugins open to others.

### Plugins

The [Leyden Jar Diagnostic Tool](https://github.com/mymakercorner/Leyden_Jar_Diagnostic_Tool)
already has exactly this shape — a board drawing, a choice of mode (key press monitor, level
monitor), a panel of information and actions (thresholds, bins, bootloader, erase EEPROM) —
so it becomes a "Diagnostics" section with no change to the layout. Its own device list is
the one part it no longer needs.

A plugin section is matched to a board by one of three rules:

| Rule | For |
|---|---|
| **VID:PID** | VIA boards |
| **Vial keyboard UID** | Vial boards — 8 bytes naming a model, not a unit (via-registry.md, "Vial's keyboard UID") |
| **A protocol probe** | a controller used on many boards, such as the Leyden Jar: the plugin asks for its own protocol's version and appears if it is answered. A list of ids would have to name every board built on it |

**Next step, decided:** write the four-point section contract as a C++ interface, with Keymap
as its first implementation and the Leyden Jar diagnostics as its second, **compiled in**.
Two very different sections test the contract before any plugin format is chosen.

*Begun 2026-09-26:* `ui/NazgSection.h` holds points 1 to 3, and `ui/NazgBoardDescription.h`
the six board rules; Keymap implements them. The match rule (point 4) waits for the first
section that is not on every board. The Leyden Jar diagnostics, the planned second
implementation, are deferred far later: they bring many more design questions. Decided for
them already: the device stays open while a view polls, as VIA's and Vial's matrix testers
do; and key output is disabled while they show, as the Leyden Jar tool does. The firmware
keeps that setting in RAM only, so an unplug restores output, but a Nazg bug would not:
**every path that closes the device or exits must enable key output again.** VIA and Vial have
no command to do the same. Two things were left out until something
needs them: folding the board away (an open point below), and edges other than left and top
for labels. `KeycapLegend` keeps its two fields -- Keymap puts them in the top-left and
middle-left slots. (It was to gain a third for sublegends; decorative sublegends were dropped
2026-10-03, and placement by the legend set changes this anyway -- see "Legends".)

**Open: how third-party plugins are delivered.**

| | For | Against |
|---|---|---|
| Compiled in | no risk | only Rico adds one |
| **Declarative** — a descriptor mapping widgets onto a board's custom commands | safe, every platform, the web build too; the same vocabulary as step 5 | bounded by the widget types defined — though a "value per key" board overlay already covers a level heat map |
| Code — native libraries | full power | a build per OS; ImGui across a library boundary; third-party code in a process that rewrites keymaps |
| Code — sandboxed WebAssembly | full power, sandboxed, portable | a new dependency and an API to maintain; authors still compile, once |
| Code — an embedded script language (Lua) | full logic, no compilation: a script is a text file; sandboxable; the web build too | an API frozen once others write against it; no debugger; a sandbox protects the computer, not the keyboard |

The survey's conclusion (README, "Declarative description vs arbitrary code") points to the
declarative route first, code only if it proves too narrow.

*Scripting, discussed 2026-10-01.* If code is needed, a script language is the route for
third parties: Lua is about 30 C files, MIT, builds under Emscripten and has no ABI to match.
Native libraries are impractical -- a plain C boundary, a build per OS, ImGui's context and
exact version shared across it, macOS library validation. A script is code that *emits
descriptions*, so the declarative-or-code choice mostly dissolves. Settled with it:

- **The panel is a small widget set** Nazg lays out and styles, not ImGui bound to Lua -- the
  "what, never how" rule above, and translatable text stays possible.
- **Async through Lua's own coroutines**: a request yields the script and resumes it when its
  `Task` completes, so script code reads straight like Nazg's.
- **Sandbox**: no `io`, `os`, `package` or `debug`; source text only, never bytecode; an
  instruction and memory budget; every call under `lua_pcall`, an error shown in its panel.
  A script can still send any HID command to its board -- keymap writes, bootloader, EEPROM
  erase -- so installing one is an act of trust, as importing a definition is.
- **Installed by the user, never served by the device**: code from a USB device stays
  disqualifying; device-served features (step 5) stay declarative.
- **Lifecycle hooks** run on every close and exit path, even after the script failed -- the
  key-output rule above.
- **Licence**: GPL-3.0 needs an explicit exception for scripts using the API before the first
  outside one, if they may be licensed freely.

**Not before Nazg's own features are solid** (Rico). The script API is the section contract
plus device access, so it binds a contract already proven by compiled-in sections -- the
Leyden Jar diagnostics come compiled in first, and porting them to a script later would be the
proof the API suffices. Keeping the contract plain values, as `NazgBoardDescription.h` is,
keeps that binding cheap.

## The board's look

*Decided with Rico 2026-10-01*, from a mockup on the Model F B104:
[ui-design/board-look.html](ui-design/board-look.html) — open it in a browser. It draws only
what ImGui's draw list can (rounded fills, outlines, polylines, text: no shadows, no
gradients), so whatever it shows can be built in `NazgBoardView` as shown. Its *Scene* choice
shows each screen's states as Nazg sets them: Keymap, Matrix view, Live test, Vial unlock.

- **Colour themes.** A theme is one table of named colours, the board's and the window's:
  keycap colours per class, legends, the plate, every state. **Light, Dark and Dracula** to
  start, with room for more; chosen in Settings.
- **Each theme chooses its own state colours**, and **no state colour may resemble one of the
  theme's keycap colours** -- the mockup's Light theme put an orange selection outline beside
  orange accent keycaps.
- **Keycap style, a setting of its own**, independent of the theme: **Outlined** -- a thin
  border around the fill, legible on dark and light alike -- or **Bottom lip** -- a darker strip
  under the key, as if the board were seen slightly from above, the most legible on light
  themes. **The default is Outlined** (Rico, 2026-10-03); Bottom lip stays a choice in Settings.
  - The lip is the key's shape drawn `lip` lower, behind it, **in the key's own bottom gap**
    (0.06 unit, no more than the gap), so the face keeps the flat key's full size and its
    legend room, and never reaches the key below. Every lip is drawn before any face, so an
    L-shaped key shows its lip only where its contour has a bottom. Raising the face by half
    the lip, to even the gaps between rows with those between columns, only moves the board:
    rejected.
  - **Flat** -- today's look -- is dropped: fine on dark themes, too little contrast on light.
    **Two-tone** -- KLE's skirt and inset top face -- is dropped: noisy on light themes, and
    its face loses about 30% of the width legends need.
- **A plate, always filled**: a rounded shape behind the whole board, 0.35 unit wider on every
  side, giving it a boundary. Not a setting. It must not swallow the lip, so a **theme rule**:
  face, plate and lip keep a fixed order of lightness, the lip clearly apart from the plate --
  on Light, face lightest, plate between, lip darkest (the mockup's first Light plate was the
  lip's grey and muted it). A plate drawn as a border only kept the lip too, but the filled one,
  coloured by that rule, looked better: the border is dropped, unless a theme some day cannot
  meet the rule. **The rule, measured** (2026-10-03, after Rico saw Light's modifier caps merge
  with the plate in Nazg, 0.923 against 0.925): on every theme, the alpha and modifier faces
  lighter than the plate and every lip darker, each **at least 0.04 apart in OKLab lightness**.
  Light's plate went to `#d3d4d8`; Dark's and Dracula's lips, lighter than their plates before,
  went darker. Accent caps stand apart by their hue. To become a test with the category colour
  solver. **One lip colour per theme**, the alphas', under every key (Rico, 2026-10-03: a darker
  shade of each cap's own colour was disturbing, on Dark most) -- on Light a little darker,
  `#acafb7`, so it stays below the slate accent face too; Dark `#12131b`, Dracula `#151725`.
- **Outlines follow the key's contour.** An L-shaped key gets one L-shaped outline: the two
  KLE rectangles traced together (at most 3×3 cells, walked around the edge), outer corners
  rounded, the inner corner rounded the other way (`PathArcTo`, then a closed `PathStroke`) --
  by 0.02 unit, a sixth of the outer corners' 0.12 (Rico, 2026-10-03: the same radius made the
  L's corner too wide, and a third still too round).
  Nested state outlines step inward along the whole contour. The same helper serves state
  outlines, the Outlined border and the lip. **A bug today**: `DrawKey()` strokes the two
  rectangles one after the other, so a selected ISO Enter shows both crossing inside it.

Settled 2026-10-03: legends (below), and the default keycap style, Outlined.

**Keycap colour classes** (Rico, 2026-10-03). KLE colours say only whether a key is an alpha, a
modifier or an accent; **Nazg never renders them** -- the theme colours each class. Most Vial
definitions carry no colours at all (some of Rico's do: VIA's colours kept in a Vial definition,
which Vial ignores but parses), so a class must also be found without them, from the keys
themselves.

**The class comes from the base layer's keycode at the key's position** (Rico, 2026-10-03), and
**classes are on by default** -- every board gets them now, colours in its definition or not:
- **Layer 0, whichever layer is shown**: a physical cap keeps its colour across layers, so the
  colours stay put while the legends change.
- **Alpha**: character keys -- whatever the host layout prints -- numpad digits and `.`, the
  space bar. **Accent**: Esc, Enter, numpad Enter. **Modifier**: everything else -- modifiers,
  Tab, Caps Lock, Backspace, F-keys, navigation, arrows, Num Lock, numpad operators, every
  command.
- **A tap-hold takes its tap's class** (`MT(Ctrl, Esc)` is an accent); **a remapped key follows
  its keycode**, as its legend does -- the board shows what a key does.
- **`KC_NO` on layer 0** has no keycode to go by: wider than 1.25u is a modifier, else an alpha.
- **With KLE colours, the colours group the keys and the keycodes name the groups**: each distinct
  cap colour takes the class most of its keys get above. More robust than "the most common colour
  is the alphas", and the designer's intent survives -- arrows drawn in the Esc colour join the
  accents. The colour values are never drawn.

**F-keys are modifiers** (Rico), as the arrows are by default, though some sets print the F-row
as alphas and many make the arrows accents: a definition's KLE colours override both.

### Legends -- the plan

*Laid out 2026-10-01 and settled with Rico by 2026-10-03: the four questions below are answered
in the parts that follow, the short forms in [short-forms.md](short-forms.md).* Legends are the weakest point of every
keycap style. Four questions, from what the key says to how it is drawn:

**1. What text goes on the key.** Today a key prints QMK's label: "Left Control", "Print
Screen", "Page Down". keycodes.md already foresaw a table of short labels. VIA has one
(`shortName`: Bksp, PgDn, LCtl, arrows as ← ↑ → ↓), used on any key 1.5u wide or less. Three
tools rather than one:
- **Short forms**, in Nazg's own table -- VIA's belongs to VIA, and the project rule is to
  rewrite rather than copy.
- **Two lines**, as printed keycaps do: "Page / Down", "Num / Lock", "Print / Screen" -- the
  full words often fit that way.
- **Symbols** where they read better: arrows at least.

The renderer chooses, not a fixed width threshold: the full label on one line, then on two
lines, then the short form, then shrunk. That keeps the contract's rule -- the section says
*what* the key means, the renderer how it fits -- and means a `Legend` gains a short form
beside its text. Tap-hold and layer keys need the same care: "MT LCTL" over "Esc" could become
"Ctrl" over "Esc".

**2. Where legends sit.** Today all left-aligned: the Shift character top left, the main legend
middle left. Alternatives: centred, or letters centred and larger with the other keys smaller,
as on many keycap sets. A matter of taste: for the mockup.

**3. How big.** Capped at the UI font size today (16 px). ImGui 1.92 rasterises text sharply at
any size, so legends could grow with the key on a large window. The ratio: for the mockup.

**4. Which font.** Measured 2026-10-01: the 69 host layouts can put **277 distinct non-ASCII
characters** on a key --
- extended Latin, Greek, Cyrillic, ten Hebrew letters, about twenty Arabic letters (the Farsi
  layout), currencies (€ ₺ ₽ ₢), maths (≠ ≤ ≥ √ ∞ ∑), typographic punctuation, ⌨;
- **no Chinese, Japanese or Korean at all** -- the Japanese and Korean layouts' legends are
  Latin-script words -- so the Yu Gothic and Malgun fallbacks loaded today are not needed for
  legends; they would have mattered only for decorative sublegends, since dropped;
- two problem cases: the Mac layouts use Apple's logo (U+F8FF), which only Apple's fonts
  contain -- **not printed** (Rico, 2026-10-03): it is a character Option+Shift+K types on nine
  Mac layouts, and a legend no bundled font holds is left off; and six dead-key accents are
  combining characters with nothing to sit on, so they need a base or a standalone form.

Arabic and Hebrew are right to left, but one letter on a key needs no shaping, so ImGui draws
them. Candidates: **one font covering everything** (DejaVu Sans), or **a family merged in
ImGui** (Noto Sans plus its Hebrew, Arabic and symbol fonts -- ImGui fills missing characters
from merged fonts). Both are free to bundle with a GPL program; the look decides, and the
mockup can load them.

**Left over from keycodes.md: the AltGr legend.** Every position already has its AltGr
character in the table (UK € on 4, French @ on à); ISO keycaps print it bottom right. Should
Nazg print it too?

**Proposed next step**: extend the mockup with a *Legends* row -- text (today / short forms /
two lines + short), placement (left / centred), size (fixed / growing with the key), font
(system / two or three candidates), AltGr legend (off / on), and a host layout switch (US, UK,
French, German, Greek, Russian) -- to see it all on the Model F and pick, as with the keycaps.
Or settle some questions first.

#### Rico's answers, 2026-10-02

- **A character is never scaled to make it fit its key.** `DrawLegends()` shrinking a legend
  to 60% goes. The chain becomes: the full label on one line, two lines, the short form or an
  icon -- never smaller -- so short forms are designed to fit a 1u key; past that, a cut with
  "…".
- **Short names for legends that do not fit**: agreed; the names themselves decided later --
  see "Short forms and command keys" below.
- **Icons, text, or both**: keycap sets print modifiers as icons, as text, or as icon and text,
  and people prefer each; all three to be tried.
- **Placement follows the keycap family**: top left on cylindrical keycaps, centred on
  spherical ones.
- **Mimicking physical keycaps**: an option worth studying, though printed legends are not
  always legible -- "Ctrl" on both left and right Control, for one. A possible answer, not
  agreed yet: labels that know their position -- "Ctrl" where the key's side matches where it
  sits on the board, "R Ctrl" only where it does not -- with hover always giving the full name.
- **Legends follow the keycap size** when the window is resized, both ways -- no 16 px cap.
  Proposed with it: a smallest legible size, below which the board stops shrinking and
  scrolls.
- Options found too many after the experiments get filtered out, to stay manageable.

#### What real keycap sets do

Looked at 2026-10-02, from the makers' own pictures:

- **GMK, Cherry legends** ([GMK CYL Classic Beige](https://www.gmk.net/shop/en/gmk-cyl-classic-beige-keycaps/fptk5035.0)):
  cylindrical, **everything top left** -- a letter alone in the corner, a number key's Shift
  character above its plain one, both left-aligned. Modifiers are **small mixed-case words**,
  left-aligned: "Control", "Alt", "Fn", "Caps Lock" -- whole words, wrapped onto two lines on a
  short key ("Caps / Lock" on 1.25u), never shrunk. **Icon and text** on some: ⇧ Shift,
  ↵ Enter, ⇤⇥ over "Tab". Arrows are icons only; the page keys abbreviated, "Pg Up", "Pg Dn".
  The GUI key says **"Code"** -- an old Cherry name, no OS logo -- and the right-hand keys print
  exactly as the left. A Helvetica-like grotesque; GMK does not name it, and Cherry's legend
  font is not to be had.
- **GMK Neue legends** ([maxvoltar, at GMK](https://www.gmk.net/en/products/keycaps-keyboards-accessories/maxvoltar)):
  GMK's modern legends, open to every designer, ship their modifiers in **three sets: icon,
  icon + text, and text** -- Rico's three scenarios, as a maker's own offer. Text in **Proxima
  Nova Soft**, a commercial typeface; icons drawn from scratch at the text's stroke weight, so
  the two look alike. Letters and number pairs centred, modifiers left-aligned.
- **Signature Plastics SA** ([SA-P Flex](https://spkeyboards.com/products/sa-p-flex-keycaps)):
  spherical, **everything centred**, letters and stacked number pairs alike. Its font is
  **Gorton Modified**, an engraving face from IBM's era
  ([SP's answer](https://pimpmykeyboard.zendesk.com/hc/en-us/articles/204416325-What-Font-is-used-on-Signature-Plastics-standard-keycaps)).
  Text modifiers are **capitals, abbreviated**: ESC, TAB, CAPS LOCK, CTRL, ALT, PRINT, SCRLK,
  PGUP, PGDN, INS, DEL; the GUI key is **"SUPER"**, OS-neutral; left and right alike. Its icon
  kit uses the **ISO 9995-7** keyboard symbols -- ↖ Home, ↘ End, ⇞ ⇟ the page keys, ⌦ Delete, a
  pause sign, a printer for Print Screen -- most of which Unicode has, in Miscellaneous
  Technical: ⎋ ⌫ ⌦ ⇥ ⇪ ⏎ ⎙ ⎀.
- **Keyreative KAT Operator**, designed by Biip ([its page](https://keyreative.store/products/kat-operator-thickened-double-shot-pbt-keycaps)):
  spherical, centred letters in a bold geometric face, **every modifier an icon** (⇥, ⇪, ⇧,
  ⌃, ⌥). And **second legends in another colour** -- a choice of this set, not of KAT keycaps
  in general: the function layer printed on the same
  caps -- F13 to F20 under F1 to F8, symbols beside the Q row's letters -- and the Mac ⌘ beside
  the GUI icon. Nazg's counterpart would be another layer's keycode as a coloured sublegend;
  the legend slots already have room for it.

What follows for Nazg:

1. **Two families, two coherent styles**: *cylindrical* -- top left, mixed-case words, icon and
   text on some keys -- and *spherical* -- centred, capitals, short words or icons. Placement,
   case and wording go together on real sets, so rather than independent switches, **legend
   presets named after the family**, each with text / icons / text + icons for the modifiers.
2. **A font chosen for its look, Noto behind it for coverage.** None of the real faces can be
   bundled; free look-alikes exist -- a Helvetica-like grotesque for Cherry (Arimo, Inter), a
   soft rounded face for Neue (Nunito), a geometric one for KAT (Montserrat) -- but none covers
   Hebrew, Arabic or the maths symbols, so Noto fonts merge behind the legend font and ImGui
   fills the gaps. This replaces "DejaVu Sans or Noto" above.
3. **Icons from ISO 9995-7**, the standard real sets use, most of it in Unicode: from a symbol
   font (Noto Sans Symbols, DejaVu), or drawn with ImGui shapes at the legend's stroke weight,
   as Neue drew its own.
4. **Real sets never shrink a legend to fit**: they wrap or abbreviate it -- Rico's rule.

Next: the mockup's *Legends* row -- preset (cylindrical / spherical), modifiers (text / icons /
text + icons), font (look-alikes from Google Fonts), size following the key down to the
smallest legible size. Host layouts and font coverage in a second pass.

#### Special cases are data: placement classes

On cylindrical sets (Rico, 2026-10-02, from his GMK Dolch R5) a 1u modifier is centred both
ways (GMK's Delete, End, Pg Dn); a wider one sits at the left, centred vertically; so does the
function row, Esc and the F-keys -- so Esc, a 1u modifier, breaks its class's rule. Such cases go in the **legend set**, a table like the host layouts: per keycode,
its name, short form, icon and a **placement class** -- letter, character pair, function row,
modifier, arrow, numpad, blank -- plus an explicit placement where a key breaks its class's
rule, as Esc does. Each preset has a few rules per class, applied by the renderer, so the rules
that depend on a key's width live in one place and nothing in the drawing code names a key.
Placement follows the keycode, not the position: Esc mapped where Caps Lock sits still prints as
an Esc cap would -- the board shows what a key does.

**The numpad** is the largest group of such cases on cylindrical sets (Rico, from GMK):
- Num Lock, /, * and − at the left, centred vertically; the tall + and Enter centred both ways.
  The operators print as mathematics, not as the characters they type: **÷** for divide, **×**
  for multiply, the true minus sign **−** for minus (Rico, 2026-10-02 and 2026-10-03).
- The digits and . top left, each with a **second legend** for what it does with Num Lock off:
  text at the bottom left -- Ins on 0, Del on ., End on 1, Pg Dn on 3, Home on 7, Pg Up on 9 --
  and arrows at the bottom right -- ↓ on 2, ← on 4, → on 6, ↑ on 8, at letter size and heavier
  than the text. 5 has none.
- The arrow keys keep the top left too, like letters -- the arrow class's cylindrical rule.
- The keycode does both, so printing both still shows what the key does. Each is one entry in
  the legend set: a placement, and a sublegend with its corner. Spherical sets print none (see
  "Spherical text rules").

**Spherical text rules**, from Signature Plastics' SA kits (2026-10-03,
[SA-P Flex](https://spkeyboards.com/products/sa-p-flex-keycaps): base, modifier, TKL and numpad
kits):
- **Everything centred**, both ways: letters, stacked pairs, modifiers, F-keys, arrows, the
  numpad. No placement exceptions, so no `place` is read on this preset.
- **Capitals, in SA's own words**, not the cylindrical ones uppercased: ESC, TAB, CAPS LOCK,
  SHIFT, CTRL, ALT, MENU, BACKSPACE, ENTER, PRINT, SCRLK, PAUSE, INS, HOME, PGUP, DEL, END,
  PGDN; WIN and ALT GR as decided above. **NUM LOCK**, wrapped on two lines, rather than SA's
  NMLK: other spherical profiles, URSA for one, print it whole, and Rico prefers it.
- **Modifier text at the letters' weight**, as SA's single stroke -- lighter modifiers looked
  thin there (Rico). Cylindrical keeps them lighter than the letters, as GMK prints them.
- **The numpad has no second legends** -- the digits alone, centred -- and its operators are
  **/ and *** with a true minus **−**, where GMK prints ÷ and ×. So the operators are family
  data too.

**Icon modifiers: dropped** (Rico, 2026-10-03), and text + icons with them. Modifiers print in
words on every preset; the arrows alone are icons, as on text sets too, and are drawn. Why:
- Nazg's board says what each key *does*, and icons read worse for that: ⎀ Insert, ⇭ Num Lock,
  ⇳ Scroll Lock, ⎉ Pause are known to few, ⌃ and ⌥ are Mac habits -- on a physical board one
  knows one's keys, on a configurator's screen one reads them.
- Only about 25 keycodes have a standard icon. Layers, macros, tap dance, media, lighting and
  QMK's feature keys have none, so on a customised keymap most special keys would stay text.
- Font icons do not match one another (the arrows showed it), so an icon mode meant drawing an
  icon set -- design work for a mode that reads worse.
- One more setting, against first-glance simplicity.

This supersedes "Icons, text, or both" and point 3 of "What follows for Nazg" above.

**Second legends: functional only** (Rico, 2026-10-03). A second legend Nazg prints says
something the key *does*: the numpad's Num-Lock-off functions, and the AltGr character, always
printed (see "Host layouts in the mockup"). **Decorative sublegends -- Arabic, Hiragana, Hangul on caps bought for
their looks -- are dropped**: a US host types nothing of them, so on a configurator's screen
they would be noise beside the legends that matter. With them go:
- the contract's `LegendRole::Sublegend` and the planned sublegend set: the slots carry label
  and value only;
- any CJK font for the legends -- the host layouts need none -- so the Yu Gothic and Malgun
  fallbacks in `Main.cpp` can go when the fonts are done. A translated interface may need such
  fonts for its own text: added then, nothing structural (see "The interface's language").

Kana typists, who do use kana legends, are not covered -- but QMK's Japanese host layout holds
no kana either; to revisit only if one asks.

**Host layouts in the mockup** (2026-10-03): its *Host layout* switch -- US, UK, French
(AZERTY), German, Greek, Russian -- takes every character key's legends from Nazg's own table,
`NazgHostLayoutTable.cpp`, so French prints "1 over &", "2 over é", A and Z where US has Q and
W, as AZERTY caps do (Rico). A position is one legend or a pair by what the layout gives it --
on Greek the Q position is "; :". Nothing changes in the placement rules. Two findings:
- **The AltGr character is printed, always** (Rico, 2026-10-03): bottom right, as ISO caps print
  it -- French ~ # { [ | ` \ @, the € on E, UK's € on 4 -- wherever the host layout has one. It
  says what the key types, so it is a functional second legend, with nothing to switch off; plain
  US has no AltGr level, so a US board is unchanged. This settles the question keycodes.md left
  to the visual design. (Rico's note: an AZERTY extension kit brings new number-row and letter
  caps but no new right Alt -- which is also why base kits print "Alt Gr" for everyone.)
- **Font coverage**: Arimo, Inter and Noto Sans hold Greek, Cyrillic and the accented Latin
  themselves; **Montserrat and Nunito have no Greek**, so a Greek board would mix in Noto's
  letters. A point against them in the font choice.

**The fourth level, Bépo, and spherical AltGr** (Rico, 2026-10-03):
- **Shift+AltGr is printed on the layouts that need it, as data per host layout**: Bépo, where 31
  of 50 positions have one and the level is part of the layout. **Top right**, where ISO 9995
  places it, at the AltGr character's size. Every other layout prints three levels -- most
  national caps do -- and hover can list all four. **French AFNOR** (NF Z71-300, the 2019
  "AZERTY amélioré", rarely used) gets no special case.
- **Never on spherical sets**: no room for it, and KAT Napoleonic's spherical Bépo kit
  (NoPunIn10Did) leaves it off too.
- **A combining character is printed on a dotted circle**, ◌ (U+25CC), as Unicode's charts and
  OS keyboard viewers show one alone -- else it floats over nothing. Six exist in the host
  layouts (hook above, horn, dot below, comma below, double grave, inverted breve), nearly all on
  the fourth level: Bépo's three are the ones Nazg prints.
- **AltGr on spherical sets follows KAT Napoleonic's AZERTY and Bépo kits**: a key with an AltGr
  character moves its pair left of centre and prints the AltGr character to its right, level
  with the lower legend and smaller; a lone legend keeps the centre, its AltGr character bottom
  right. KAT prints it in a second colour; hue now belongs to the command categories, so Nazg
  sets it apart by **lightness** -- a muted shade of the legend colour (the mockup's *Spherical
  AltGr: Muted*, against *Same*).

**The legend font is Arimo** (Rico, 2026-10-03), of the look-alikes tried: a Helvetica-like
grotesque like GMK's Cherry legends, free to bundle (Apache 2.0, compatible with GPL-3.0). Of the
host layouts' 277 non-ASCII characters it holds Latin, Greek, Cyrillic, Hebrew and every currency
itself -- checked on its Google Fonts version. Merged behind it, for the rest: **Noto Sans
Arabic** (the Farsi layout), **Noto Sans Math** (∂ ∆ ∏ ∑ √ ∞ ∫ ≈ ≠ ≤ ≥) and a symbols face (⌨ ◊
‡ ‰, the ﬁ ﬂ ligatures). Whether the interface's own text uses Arimo too belongs to the
styling of the rest of the window.

**As shipped** (2026-10-03, [resources/fonts/README.md](../../resources/fonts/README.md)): the
files come from Arimo's own repository, which licenses them under the **SIL OFL 1.1**, not Apache
2.0 -- just as free to bundle with a GPL program, each licence beside its font. Upstream Arimo
holds more than the Google Fonts copy checked above: ◊ ‡ ‰ ﬁ ﬂ and ◌ too, and the maths symbols
but ≃ -- 309 of 386 characters: every non-ASCII character in the host-layout table's source,
its comments included, a superset of the 277 above, plus ◌ and …. So three Noto faces are merged behind each Arimo
weight, Regular and Bold: **Noto Sans Arabic** for 74 characters, **Noto Sans Math** for ≃ and
**Noto Sans Symbols 2** for ⌨. Nothing is left out but the Apple logo. 2.8 MB in all. The
interface keeps a system font for now; the Yu Gothic, Malgun and Noto CJK fallbacks it carried
for legends are gone.

**Modifier text is 3/5 of the letter size** by default (Rico, 2026-10-03), near GMK's
proportions: at that size nothing on the Model F needs a short form or a cut, in either family.
Its cost: with the smallest text held at 9 px -- the header, below -- the Model F needs about
1420 px of width before the board scrolls.

**Names of the modifiers** (Rico, 2026-10-03):
- **The GUI key prints "Win"**, after QMK's own name (`KC_LWIN`, `KC_RWIN`) -- "WIN" on
  spherical sets -- on Windows names; see **Modifier names** below for Mac and Linux.
- **Right Alt prints "Alt Gr"** -- with a space, as on Rico's caps; "ALT GR" on spherical sets
  -- on every host layout, as keycaps do, his US ANSI ones too; and never names its side: Alt Gr
  already says which Alt it is.
- **Left and right, as the physical keycap**: Ctrl, Shift, Alt and Win print plain, the same on
  both sides, and **name their side only where the keycode's side is not where the key sits** --
  a Right Ctrl keycode on the left half prints "Right Control" ("R CTRL" on spherical sets).
  Caps Lock and Left Ctrl swapped keeps a plain "Ctrl" at the Caps Lock position, as the caps
  sold for that swap do. Not "is it at its usual position": nothing tells Nazg where a board's
  modifiers usually sit -- the definition gives geometry, not a stock keymap -- while a side is
  computed for any board.
- **The side is measured from the space bar's centre**, or from the board's where there is no
  space bar (splits, orthos). A full-size board's own centre falls near Backspace, because of
  the numpad, and would put right Alt on the left. A key straddling the line is on neither
  side, so it names its side. The mockup's *Keymap* switch shows it: Caps ↔ Ctrl, and both
  Ctrls swapped.

**Modifier names follow a setting, defaulting to the computer's OS** (Rico, 2026-10-03). The
host layout cannot tell a Mac: only 11 of the 69 say Mac, and most Mac users pick US, UK or
another layout shared with PCs. The computer Nazg runs on can -- a keyboard is configured on the
computer it is used with. So a setting, *Modifier names: Windows / Mac / Linux*, beside the host
layout in Settings and saved in `imgui.ini` with it, set **at first launch** from
`SDL_GetPlatform()` -- read in `Main.cpp`, where SDL stays, and passed down as a value -- and the
user's choice after that. One place decides, so a Mac host layout does not override it.

| | Windows | Mac | Linux |
|---|---|---|---|
| GUI | Win | Cmd | Super |
| Alt | Alt | Option | Alt |
| Right Alt | Alt Gr | Option | Alt Gr |

Mac keyboards print "option" on both Alt keys, and on Mac layouts the right one plays AltGr's
role, so "Alt Gr" would be wrong there. Ctrl stays "Ctrl"; Mac caps print "control", the full
name, with "Ctrl" its short form. Spherical sets print them in capitals. The side rule above
applies unchanged.

**Some glyphs are placed by their ink** (Rico, 2026-10-03), against a capital's, not where
the font puts them -- from the glyph's bounds (`ImFontGlyph`'s Y0 and Y1), in a short table,
data as VIA's app keeps a table of offsets for the same reason:
- **centred** on the capital height: "-" (at mid x-height in a font) and "_" (below the
  baseline), which made the "_ over -" key look low beside "! over 1"; and "`", which sat high;
- **low**, resting on the baseline level with the bottom of "!": "~".

Others may join it as more legends are looked at.

**Arrows are drawn, not taken from a font** (2026-10-02). From fonts their weights did not
match: Arimo, Inter and Montserrat have ↑ and ↓ but not ← and →, which then come from another
face, and no font draws arrows at a legend's weight. A shaft and a filled head -- `AddLine` and
`AddTriangleFilled` -- with the stroke a share of the legend size, so all arrows match each other
and the text, whatever the font. The same will likely hold for every icon: the case for drawing
them, as GMK Neue drew its own.

**A change to the section contract when this is built**: today a section chooses the legend
*slots* (Keymap fills top left and middle left), so the slot is the placement. With presets the
placement depends on a global setting no section should know, so a key carries its legend with
its class and the renderer places it by the preset. The twelve explicit slots stay for sections
whose positions mean something themselves, as the Leyden Jar's level view.

#### Short forms and command keys

Settled 2026-10-03 with Rico, shown in the mockup's *Layer: Features*. Beyond the standard keys,
some 600 named keycodes remain once steno and MIDI are left out -- too many to write by hand,
and most of QMK's labels are *system + action* ("RGB Matrix Saturation Down").

**Short forms are generated, with overrides** -- formalised in
[short-forms.md](short-forms.md): the rules, the vocabulary and the complete table, every entry
checked to fit 1u at the smallest size.
- A **word table** of a few dozen entries, applied word by word to QMK's label: Brightness Bri,
  Saturation Sat, Speed Spd, Previous Prev, Volume Vol, Button Btn, Wheel Wh, Acceleration Acc,
  Bluetooth BT... New QMK keycodes get a short form with no new entry.
- **Overrides** per keycode where the rule reads badly -- data, as the placement exceptions are.
- **+ and − for a quantity, Up and Dn for a direction**: Vol +, Hue +, Dwell −, but Pg Dn, Wh Dn.
- **A toggle prints On/Off under the feature's header**: HF_TOGG is "Haptic / On/Off"; the
  explicit forms keep their word.
- **The last resort is QMK's own short name** (SQ_TMPU) -- seven characters at most by QMK's
  convention, so it always fits 1u -- for what nobody types on a configurator board: joystick,
  programmable buttons, the sequencer, the AS/400 keys.
- Spherical sets: the same forms in capitals, plus overrides where capitals need other words.
- Hover always gives QMK's name and what the key does, so short forms may be terse.

**Command keys look different from character keys.** A command is anything QMK adds that a
stock keyboard does not have; Enter, F-keys and the navigation keys are on real caps and keep
the modifier style. Like ZMK Studio's keys, a command has a **header** -- what it acts on --
over the **main legend**, the action: "Media / Vol +", "Mouse / Btn 1", "Light / Hue +". The
header is one line, its own short form or cut; the main legend has what room is left.

**Command keys leave the family's rules** (Rico, 2026-10-03): following GMK's or SA's size, case
and placement constrained them too much, SA's capitals most. So, on both families alike:
- **the header at 1/2 the letter size** -- every tested header fits 1u that way, "Firmware" and
  "Toggle" included; only "Caps Word" still needs its short form;
- **mixed case**, GMK's wording, on spherical sets too;
- **Header corner**: the header in the **top right** under the band, as a tap-hold key's hold
  (Rico, 2026-10-03: one rule for every command); the main legend centred both ways **on the
  whole key**, as any key's, pushed below the header only where a wide legend would run into it.
  A key with both a hold and a command tap **always stacks** its two headers, each in its own
  colour, both right-aligned: the hold top right, the tap's header right under it, the main legend
  centred -- "L1 / Media / Play", "Boot / Media / Play" (Rico, 2026-10-03: side by side, "Media"
  and "Boot" collided on 1u, and two commands read better one above the other). Checked in the
  mockup over 600 draws -- every font, both families, every size, keys 50 to 110 px: no two
  legends overlap and none leaves the face. Before it, *Header top* centred the header above and the main legend in the room under
  it, which set command legends 6.8 px lower than their neighbours' at a 90 px key. The mockup
  keeps Header top, Top left, Middle left and Centred to compare.

**The floor is on the header** (Rico, 2026-10-03, judged in the mockup at 100% zoom on his QHD
screen): **no text on the board below 9 px**, and the header is the smallest. The board first
stopped shrinking at 9 px of modifier text, which left headers at 7.5 px -- too small to read; 3/5
of the letter size, 9 px there, was his limit. Two ways out were weighed:
- **Headers at 3/5, the floor unchanged** -- rejected: at 9 px on a 50 px key, 11 headers no
  longer fit even shortened (Firmware, One shot, Set base, Combos, Tap term, Caps Wd, the Magic
  swaps' Ctl/Caps, Esc/Caps, Ctl/Super, Alt/Super, Opt/Cmd), and each would have needed a terser
  form -- worst for Firmware, the category that must read best.
- **Headers at 1/2, the floor moved to them** -- chosen: at the smallest size every legend is 20%
  larger (keys 60 px, letters 18, modifier text 10.8, headers 9), proportions unchanged, so every
  check in short-forms.md still holds. The cost: the board scrolls sooner -- the Model F (22.5
  units) below about 1420 px of width, against 1190 before. Above the floor nothing changes.

The width below which a board scrolls, plate included, and with the section column (180 px, its default) a
board with several sections shows:

| Board | Units | Scrolls below | With the column |
|---|---|---|---|
| 60% | 15 | 970 px | 1150 px |
| 65%, 75% -- the most popular (Rico) | 16 | 1030 px | 1210 px |
| TKL | 18.25 | 1170 px | 1350 px |
| Full size, Model F B104 | 22.5 | 1420 px | 1600 px |
| Model F F122 / B122, two more columns on the left (estimated) | about 25 | 1570 px | 1750 px |

A window half a QHD screen wide (1280 px, Rico's) holds a 65% or 75% with the column shown and a
TKL without it; the big boards scroll there, and fit a 1920 px window without the column.

Nazg scales its UI by the display's content scale (`SDL_GetDisplayContentScale`), so these are the
browser's CSS pixels: the mockup at 100% zoom shows the sizes Nazg will draw. Nazg's text is softer
than Firefox's, though -- ImGui without FreeType does not hint -- so the floor is to confirm in
Nazg once the board is drawn; it is one constant.

- **Layers: the layer large, the operation as the header** -- "Hold / L2" for MO(2), "Toggle",
  "To", "Once", "Base", "Tap tog". Ready for layer names: "Hold / Nav" once a layer has one.
  QMK's notation (MO 2) was the alternative: what VIA and Vial users already read, and VIA's app
  writes MO(1) on its keys (`via-app/src/utils/key.ts`).
- **Tap-hold keys: the hold is the header**, under the band like every command's, and the tap
  keeps its own legends below: "Ctrl" over "Esc" for MT(LCTL, Esc), "L1" on the space bar for
  LT(1, Space), "L1" over "! 1" for LT(1, KC_1). The hold prints plain -- "Ctrl", not "Hold Ctrl"
  (Rico, 2026-10-03): short, and a pair still fits under it. The bottom of the key stays free for
  the AltGr character and the numpad's second legends, so French "2" as LT(2, KC_2) carries four
  legends: L2, 2 over é, ~. A pair fits on 1u because the header is small and the pair packs to
  line height 1.0 with the top padding gone -- the glyphs never shrink; where even that fails
  (Large size) the Shift character goes, and hover gives it. Today's "MT LCTL" goes.
- **The hold takes the top right corner, on both families** (Rico, 2026-10-03): above, it
  pushed a pair down from where its neighbours' sit. Cylindrical legends keep the left column and
  the bottom right; spherical ones the centre, a pair moving left for an AltGr character on the
  lower line -- so the top right is free either way, and the tap's legends stay exactly as on any
  key: no packing, no dropped Shift character. **Every hold goes there**, for uniformity (Rico,
  2026-10-03) -- the space bar's too. On Bépo the fourth level moves just below the hold, so the
  right column reads hold, fourth level, AltGr. A centred spherical legend that would reach the
  hold -- a wide "@" under "L2" at Large size -- moves left just enough to clear it, as a pair
  does for an AltGr character. Checked over 5,400 draws -- US, French and Bépo, every font, both
  families, every size, keys 50 to 110 px: no two legends overlap and none leaves the face.
- **The hold's header is coloured by what the hold does**, and the band with it. MT and LT can
  only hold a modifier or a layer -- a behaviour, magenta -- but a **Vial tap dance** holds any
  keycode (`on_hold`, via-vial-commands.md), and one with a tap and a hold is drawn as a tap-hold:
  "Boot" held shows red. A command hold collapses to one line, its main legend. A tap that is a
  command -- `LT(1, KC_MPLY)`, or the tap dance's tap -- keeps its own small header in its own
  colour below: "L1" over "Media / Play". So a key can show two categories, and the dangerous
  case -- Boot behind a harmless tap -- is the most visible. One band per key, the hold's. A tap
  dance's double tap and tap + hold stay off the board: hover and the tap dance panel.
- **Lighting** names no system when the board has one -- the header is "Light". With two, it
  names them: UGlow (underglow), Matrix, LEDs, Backlit. **The definition says which a board has,
  never the keycode** -- checked in QMK's source 2026-10-03, below. The mockup's *Lighting*
  control shows the words on F9-F12 of the feature layer.

**What a lighting keycode drives** (QMK's source and history, 2026-10-03: a 2020 tree, `d029c1c`,
and Rico's 2026 fork, `1b02212`; vial-qmk, `db24cf2`, as 2026's). Dates are QMK releases.
`RGB_*` and `UG_*` are **the same values** (`0x7820..0x782A`); only the names and the firmware
around them changed, in four periods:
1. **Until 2020-06**: `process_rgb.c` calls `rgblight_*()`. On a board with RGB Matrix and no
   underglow, `rgb_matrix.h` `#define`s each `rgblight_*` to its `rgb_matrix_*` twin, so `RGB_TOG`
   toggles the matrix. With both systems, the underglow only; the matrix has no keycodes.
2. **2020-06 to 2024-11** (#7677): `process_rgb.c` calls **both** systems, each unless the board
   defines `RGBLIGHT_DISABLE_KEYCODES` or `RGB_MATRIX_DISABLE_KEYCODES` (25 board files did). Four
   mode keys reach the matrix too -- `RGB_M_P` solid colour, `_B` breathing, `_R` cycle left-right,
   `_SW` pinwheel; the other `RGB_M_*` the underglow only. Keycode version 0.0.4 renamed `RGB_*`
   to `UG_*` (2024-05, #23656) and added `RM_*` -- but **`RM_*` did nothing yet**: their handler
   was written (#23896) and not built until period 3. So firmware reporting 0.0.4 or 0.0.5 has
   dead `RM_*` keys.
3. **From 2024-11** ("RGB Keycode Overhaul", #23679, #24490; keycode version 0.0.6 on, for
   mainline): `process_underglow.c` and `process_rgb_matrix.c` replace `process_rgb.c`. `RM_*`
   drive the matrix only. `UG_*` drive underglow **and** RGB Matrix, unless the board defines
   `RGB_MATRIX_DISABLE_SHARED_KEYCODES`, the old two defines gone. The sharing is for backward
   compatibility, marked "TODO: Remove this" and deprecated in `docs/features/rgblight.md`, still
   there in 2026. Of the 22 boards in QMK's tree that enable both systems, 14 define it -- added
   for them by #24490 -- and 8 still share (among them crkbd rev1, Noah LD, Adelais RGB rev3). Any
   user keymap can change it either way; a client cannot see the define.
4. **Later, announced**: QMK will drop the `RGB_*` names and the sharing.

Smaller differences in period 3: **`RGB_M_*` mode keys are handled nowhere in QMK's core** -- dead
keys, "deprecated" in the docs, still in the keycode table; `UG_*` act on **press**, `RM_*` on
**release** unless `RGB_TRIGGER_ON_KEYDOWN` (period 2: release by default for all); underglow has
no On and Off, only Toggle; `VK_TOGG` (Velocikey) moved into the underglow handler.
`BL_*` drives the backlight only, `LM_*` the LED Matrix only, in every period.

**Where the definition says it.** VIA V3: the `keycodes` modules -- `qmk_backlight_keycodes`,
`qmk_rgblight_keycodes`, `qmk_rgb_matrix_keycodes`, `qmk_backlight_rgblight_keycodes` -- and,
behind the fallback module `qmk_lighting`, the standard `menus` (`via-app/src/utils/key.ts`,
`getQMKLightingKeycodes()`). VIA V2: `lighting`. Vial: `lighting` -- `qmk_backlight`,
`qmk_rgblight`, `qmk_backlight_rgblight`, `vialrgb` (RGB Matrix), or none -- and the VialRGB flag
in `vial_get_keyboard_id`. **Neither knows LED Matrix**: no VIA module or menu, no Vial value, so
"LEDs" can only come from an `LM_*` keycode on a board that declares something else.

**How often.** VIA's 2029 V3 definitions: 825 no lighting, 1,165 one system, **291 two or more**
-- backlight + underglow 243, underglow + RGB Matrix 38, backlight + RGB Matrix 4, all three 6.
None uses the `qmk_lighting` fallback.

**What VIA and Vial do about it** (via-app `935106a`, 2026-09-17; vial-gui `aef8222`, 2026-05-25):
- **VIA keys everything on its protocol version.** Up to 12 it offers one set, `RGB_*` with the
  `RGB_M_*` modes, whatever the board declares beyond "has lighting" -- including on VIA 12
  firmware built after 2024-11, where the `RGB_M_*` are dead and `RM_*` would work. From 13 it
  offers `UG_*` and/or `RM_*` from the definition's `keycodes` modules (menus behind
  `qmk_lighting`, both sets when nothing is known), drops `RGB_M_*` from its dictionary, and
  its designer tab warns about a `qmk_lighting` definition without standard menus. Legends name
  the keycode family -- "RGB Toggle", "UG Toggle", "RM Toggle" -- not what lights up.
- **vial-gui does not sort it out.** One *Backlight* tab holds `BL_*`, `RGB_*` with every mode and
  `RM_*`, on every board, whatever its `lighting` says: on Vial 6 firmware from before vial-qmk's
  2025-02 merge, `RM_*` are offered and do nothing. On Vial 5 the `RM_*` entries carry fake values
  (`0x9990..`, a TODO in `keycodes_v5.py` says so). Legends "RGB Toggle", "RGBM Togg".
- **Vial has a better signal than either uses.** Since vial-qmk `15a3bb1` (2025-06-28, after its
  2025-02 merge of the overhaul) the last byte of `get_number_of_entries` carries feature bits --
  bit 0 Caps Word, bit 1 Layer Lock -- and both features default to on. A Vial 6 board with
  either bit set is new firmware. Bits clear: built before 2025-06, or both disabled.

**The rule** (Rico, 2026-10-03): **Light** on a board with one system, or none declared; with
several, the header names the system the key acts on -- Backlit, UGlow, Matrix. Backlight +
underglow, the common pair, is then exact in every period. `RGB_*` / `UG_*` on a board with
backlight + RGB Matrix drive the matrix, in every period: Matrix. **On a board with underglow, they
are UGlow, in every state** -- even where they drive the matrix too: always on *old* firmware, on
*unknown* perhaps, and on *new* firmware unless it opts out (14 of the 22 such boards in QMK's tree
do). Light, beside Matrix, did not say clearly enough what the key acts on; UGlow says what the
keycode is named for, what most of QMK's both-system boards do and what QMK is moving to. Hover
gives the rest ("and RGB Matrix, unless the firmware opts out"; on old firmware, "and RGB
Matrix"). **UGlow, not Glow**, wherever underglow is named: the U ties it to underglow and to QMK's
`UG_` prefix, where Glow alone read as any light.

**Which lighting keycodes a board gets** (decided with Rico, 2026-10-03). Nazg sorts the board's
firmware into one of three states, *old* (one set of keycodes drives every system: QMK before
2024-11, vial-qmk before 2025-02), *new* (`UG_*` and `RM_*` apart) or *unknown*, by the first of
these that answers. A Vial board is judged by its Vial protocol only: vial-qmk always reports
VIA 9 (keycodes.md).

1. **VIA protocol 13 or later**: new -- protocol 13 came in 2026-04, long after the overhaul.
2. **VIA protocol 9 to 11, Vial protocol 5**: old. (VIA 9 from before 2020-06 drives only the
   underglow on a board with both systems: rare, and drawn as old.)
3. **Vial protocol 6 with a feature bit set** -- Caps Word or Layer Lock, in the last byte of
   `get_number_of_entries`, which Nazg reads already for the entry counts: new.
4. **VIA protocol 12 with an `RM_*` key in the keymap**, any layer or encoder: new. On old firmware
   `RM_*` did nothing, and the VIA app never offered them to a protocol 12 board (it offered
   `RM_*` from 2026-07, to protocol 13 only), so one in the keymap came from a keymap compiled for
   new firmware -- QMK's default keymaps took `RM_*` in the overhaul. **Not for Vial**: vial-gui
   offers `RM_*` on every Vial 6 board, old firmware included, so there they prove nothing.
5. Otherwise **unknown** -- VIA 12, and Vial 6 built before 2025-06 or with both features off.

**Unknown is the common state, and a normal one, not a failure.** Rule 4 only ever proves *new*,
and it rarely fires: many keymaps have no lighting keys at all -- the firmware never put any
there, or the user removed them in VIA or Vial -- and the absence of `RM_*` says nothing. The
definition cannot date the firmware either: a registry definition serves every build of its
board, and Vial's embedded one carries no version. It says only which systems the board has.
The state is found again at every load, never stored: a board can be reflashed. It is a pure
function of what the board reports, tested with literals.

What each state offers in the picker, for the systems the definition declares, legends by the header rule above:

| | Backlight | Underglow | RGB Matrix | `RGB_M_*` modes |
|---|---|---|---|---|
| **Old** | `BL_*` | the one set (`UG_*` values) -- it also drives the matrix | the same set | offered where they reach: P, B, R and SW the matrix too, the others the underglow only |
| **New** | `BL_*` | `UG_*` | `RM_*` | not offered: dead since 2024-11 |
| **Unknown** | `BL_*` | the one set | the same set -- the matrix through the alias (old) or the sharing (new); no matrix-only board in QMK's tree opts out | not offered |

- **Unknown is what works on both sides**: the one set does in every period. Its cost is on a board
  with underglow and RGB Matrix, which gets no separate matrix control -- as in VIA up to protocol
  12. `RM_*` and `RGB_M_*` are left out because each does nothing in one of the periods.
- **Nazg never makes evidence**: since it offers `RM_*` only on new firmware, nothing Nazg writes
  can turn rule 4 on later.
- **Keys already on the keymap are always drawn**, legends by the rule above. One that may do
  nothing on this firmware -- `RM_*` on unknown, `RGB_M_*` on unknown or new -- says so on hover
  ("does nothing on firmware built before November 2024"); no mark on the board.
- **LED Matrix** cannot be declared (neither VIA nor Vial knows it), so `LM_*` are never offered;
  one on the keymap is drawn, "LEDs".
- **With *Advanced tools* on** (Rico, 2026-10-09; provisional until then), the picker lists every
  lighting keycode for the systems declared -- both sets and every `RGB_M_*` mode -- whatever the
  state, for a board whose firmware the user knows better than Nazg. Those that may do nothing
  are **faint**, as a transparent key's legends are, and hover says why.
- **As built** (2026-10-09): `LightingNoteOf()` (`ui/NazgBoardDescription.*`, tested) gives what
  hover adds, on the board and on a tile -- "does nothing on firmware from before QMK's November
  2024 RGB overhaul" for `RM_*` on unknown firmware (vial-qmk's February 2025 merge on a Vial
  board), the mirror for `RGB_M_*`, a mode only an underglow has on a board with an RGB Matrix
  alone, and for `UG_*` on a board with both systems "drives the underglow, and the RGB Matrix
  unless the firmware opts out" ("and the RGB Matrix" on old firmware).

**Four categories, each a colour** -- Rico chose **D′** of the variants sketched: a **band**
along the top of the face and the **header** in the category's colour, the **main legend in the
legend colour**, so the text read most keeps the best contrast. (D, the main legend coloured too,
stays in the mockup to compare; a coloured main legend went muddy on accent caps.)

| Category | Hue (OKLCH) | Covers |
|---|---|---|
| Behaviour | violet, 300 | layers, tap-hold holds, one-shot, tap dance, Caps Word |
| Host | cyan, 200 | media, mouse, system keys, macros, MIDI, the sequencer, steno, joystick -- what goes to the computer |
| Board | amber, 80 | lighting, haptic, audio, Magic, combos -- the keyboard's own settings -- and the custom and user keys, which code on the keyboard decides |
| Firmware | red, 30 | Boot, Reboot, Clear EEPROM, Debug -- the keys that can hurt |

Four because the state colours already take much of the wheel -- orange selected, blue
highlighted, magenta second, green checked, red warning; red for firmware shares "careful" with
the warning, a fitting overlap. More categories would mean revisiting the state colours.

**The palette is chosen for colour-blind users** (Rico, 2026-10-03). The hues first picked by
eye -- magenta 345, cyan 195, amber 75, red 27 -- fell apart under deuteranopia: solving every
category to the same contrast gives them the same lightness, hue is left as the only cue, and
magenta and cyan came within 0.038 of each other in OKLab, about two just-noticeable steps. The
hues above come from a search that simulates protanopia, deuteranopia and tritanopia (Machado's
matrices) on light and dark caps and maximises the smallest distance between categories while
keeping clear of the selection and highlight colours: **at least 0.086 under every deficiency**,
0.152 with normal vision. Behaviour moved from magenta to violet; the rest barely changed. And
**Firmware is solved to a higher contrast** -- 7:1 on text, 4.5:1 on its band, against 4.5:1 and
3:1 -- so it differs in **lightness** too, the one cue no colour deficiency removes, for the
category that matters most.

**Hues are theme tokens; Light and Dark share one palette** (Rico, 2026-10-03), the standard
choices, states included: Dark's selection moved from amber to Light's orange, its highlight,
second highlight and warning to Light's hues, lighter. The second highlight moved from violet to
**magenta** on both, clear of Behaviour's violet -- it is the matrix view's column colour. Dracula,
Rico's own theme, takes the shared state colours too: its full palette had a state on every
category's hue. Its purple accent caps still share Behaviour's hue; the band's solved lightness
keeps it legible there.

**The selection is neutral, the accent caps slate** (Rico, 2026-10-03), both on Light and Dark:
- **Selection**: the theme's text colour -- near black on Light, near white on Dark -- not the
  orange of before, too loud. With no hue it meets no category, highlight or warning colour,
  never merges with a command's band, and survives colour blindness: it differs in lightness.
  The UI accent blue was the alternative, but it merges with the blue highlight during a Vial
  unlock.
- **Accent caps** (Esc, Enter, when KLE colours are kept): a **slate** grey-blue at low chroma,
  about a third of the command colours', so it competes with no band or selection -- the orange
  was loud and sat on Board's amber. Light `#afbfd5`, lip `#8a9aae`, legend `#18222f`. On Dark,
  the same slate was only 0.09 lighter than the main caps, against 0.20 darker on Light --
  lightness, not hue, was the gap (neutral dark caps barely helped) -- so Dark takes a **lifted**
  slate: `#5a6a80`, lip `#415166`, legend `#e8eff9` at 4.8:1, 0.16 from the main caps. A middle
  lightness is a trap there: no legend reads well on it. Sage, graphite, a pale dark slate with a
  dark legend and the orange stay in the mockup to compare. Dracula keeps its own purple.

**Legible on any theme and keycap colour, by computation.** A theme gives a category only its
hue. Its lightness is solved per keycap face: from the face's own lightness, darker on a light
cap and lighter on a dark one, to the first that reaches **4.5:1 for text and 3:1 for the band**
(WCAG's figures for text and for graphics), so the colour stays as vivid as legibility allows;
chroma is clamped to sRGB. Where no lightness reaches it -- a mid-tone cap -- the text falls back
to the legend colour and the band alone carries the category. It is a pure function of (face,
hue, target), so **a test asserts every theme × keycap class × category**: a new theme, or KLE
keycap colours once the parser keeps them, cannot make a command illegible without failing the
build.

Seen in the mockup, to settle:
- **Hues against each theme's states**: Dark's amber selection sat on Board's hue and Dracula
  had a state on every category's hue -- settled above by a shared palette. The test should check
  distance under the simulated deficiencies as well as contrast, so a new theme cannot undo it.
- **Header words at the smallest size**: at the modifier text's size, "Firmware", "Caps Word",
  "Tap tog" and spherical "TOGGLE" were cut on 1u -- the reason for the smaller, mixed-case
  header above. "Caps Word" still uses its short form, "Caps Wd", as "Tap dance" uses "Dance".
- **The band goes under the marks** (Rico, 2026-10-03): drawn with the face, before the
  selection and highlight outlines, so a selected or highlighted command key keeps its whole
  outline. The mockup first drew it with the legends, over the outlines' top edge; fixed there.
- **The ▽ is always bottom right** (Rico, 2026-10-03). At legend size it landed on the faint
  AltGr of French 6, 7, 8; small and in the face's corner padding, it meets no glyph.

**Transparent keys** (Rico, 2026-10-03). `KC_TRNS` makes a layer partial: QMK looks for a key's
keycode from the highest active layer down, and a transparent key says "keep looking"; `KC_NO`
says "stop, do nothing". The search is made at press time and kept for the release; it falls to
the next *active* layer, so what a transparent key does depends on the board's state; and on the
default layer it falls to nothing. VIA and Vial print ▽, ZMK Studio "Transprnt": *that* a key is
transparent, not what it does. Nazg shows both:
- **the keycode it falls through to, faint** -- command keys and tap-hold keys with their bands
  and headers -- **with a small ▽** in the face's very corner, half strength, clear of every
  legend (Rico: a mark, not a legend), drawn as the arrows are. The board stays whole
  on a partial layer, the layer's own keys stand out, and the ▽ tells "transparent, shows A"
  from "this layer says A", which differ when the base changes;
- **resolved by walking down the layer numbers** to the first keycode -- exact while one layer
  is on at a time, the common case; hover says which layer it fell to;
- **`KC_NO` shows the key it disables, faint, with a small ✕** in the ▽'s corner (Rico,
  2026-10-03): working on a layer, one must see *what* one is disabling, and an empty key hid
  it. The marks are a pair -- ▽ "shows the key below", ✕ "disables the key below". Hover says
  which. First drawn empty and dimmed, dropped: dimmed is already a state (the matrix view, the
  Vial unlock). **Struck through, dropped** (Rico, 2026-10-04, seen in the app): a thin line
  across the face, corner to corner, was to make a disabled key stand out from a distance, but
  it ran through the legends, and its angle changed with the key's shape -- steep on 1u, flat
  on the space bar. A transparent key on the default layer falls to nothing: drawn as `KC_NO`
  with nothing below.

**The other direction: peek** (Rico, 2026-10-03; replaced 2026-10-04, below). From a lower layer, what the layers
above put on each key -- what laptop Fn legends, side-printed caps and Biip's Operator set show.
Hovering a layer in the strip lays that layer's own keys over the board until the mouse leaves,
as **second legends**: the main legend, or the hold, bottom right at the header's size, in the
key's category colour, with no fill. While a key is peeked its own bottom legends -- AltGr, the
numpad's second ones -- step aside. Keys the peeked layer leaves transparent get none, so what it
reprograms stands out. Chosen over a permanent front legend (one layer only, a taller lip, one
keycap style) and markers (silent on what is there). **Tried first and dropped: a filled strip**
along the bottom of each key -- too much at once, and it ran over the glyphs (Rico); the mockup
keeps it as a switch.

**Second legends, dropped; peek is the layer itself** (Rico, 2026-10-04, seen in the app). Built
as described above, the second legends collided with the key's own, spherical ones most: a
centred pair or a command pushed below its headers reaches the bottom right, and a peek there has
no room left. Hovering a layer in the strip now **shows that layer, as choosing it would**, until
the mouse leaves -- any layer, below the one chosen too. Nothing is added to a key, so nothing can
collide, and the code is the layer view's own. What the second legends gave is mostly still there:
on the hovered layer its own keys are at full strength and the ones it leaves transparent faint,
so what it reprograms stands out. Lost: the two layers at once on one key. A click or a keycode
written still goes to the chosen layer -- the mouse is on the strip meanwhile. The strip tells its
section which entry is hovered: `Section::OnStripHovered()`, every frame, before the board is
described.

**Stuck-layer warnings: deferred** (Rico, 2026-10-03) -- too complicated for results that cannot
be sure. A keymap traps you when a sticky layer key -- TG, tapped TT, TO, DF, layer lock, and
PDF, which persists the default layer in EEPROM so unplugging does not help -- leads to a state
with no way back. The same-position rule ("TG(1) on layer 0 needs TG(1) or a transparent key there
on layer 1") gives false alarms and misses traps; the exact answer is a reachability search over
layer states, momentary holds included -- small pure code, but it needs QMK's layer semantics
pinned down from its source, and it cannot see custom keycodes (`QK_KB_*`, `QK_USER_*`), Vial's
tap dance and combos until their entries are read, nor the board's current default layer.
**If it comes back: an explicit "Check keymap" button** that roughly checks the keymap on
demand and words what it finds as possibilities, not a live warning on the board.
- **Modified keys** (Ctrl+C) are Host: "Ctrl+ / C", the header in Host's colour (short-forms.md,
  rule 7).

## The keycode picker

*Designed with Rico 2026-10-04, through the mockup
[ui-design/picker-look.html](ui-design/picker-look.html) -- open it in a browser; it draws the
picker under a 60% board, every keycode with the app's own words. It replaces the first draft
(`ui/NazgKeycodePicker.*`), which lists QMK's whole table grouped. The alternatives weighed --
Vial's Basic tab drawn as a keyboard, ZMK Studio's kind then parameters, keycaps or text buttons
as items, the key's line beside the layers or as a card at the side, plain group titles -- are
in the mockup's history before commit 5bbebb1.*

The picker is the Keymap section's panel: **a key line on top, then the tabs and their search
box, then the keycodes as tiles.** Click a key on the board, then a tile: it is written at once
and read back, as today.

### Its size

- **The whole width**, the board centred over it with the room it leaves on both sides (Rico,
  2026-10-05: the picker gets the surface the board does not need, and the board can be smaller
  than the window). It replaces "as wide as the board", which read as one column but tied the
  panel to the board. At least 980 px, so a small board's tabs still fit on one line.
- **A splitter between board and panel**, dragged to share the height and remembered in
  `imgui.ini`. It stops where the board would fall below its legibility floor, 60 px a unit
  ("The floor is on the header") -- the board is never squeezed to give the picker room; the
  panel keeps at least the key line, the tabs and three rows of tiles, about 275 px. A window
  too small for both gives the panel its minimum first.
- **The splitter sizes the board, the window first the room around it** (Rico, 2026-10-05:
  shrinking the window shrank the board). The splitter remembers the board's height in pixels,
  not a share; dragged down, it stops where the board fills the window's width. A shorter
  window takes from the panel, and stops at its minimum. A narrower window takes the room
  beside the board, then the board, down to its legibility floor, and stops there or at the
  panel's 980 px (Rico: blocked as soon as the board filled the width, the window could not be
  narrowed at all). Both stops are `SDL_SetWindowMinimumSize`, each frame from what the view
  could spare. Nazg never grows the window: a board wider than it, just opened, fits its width,
  down to its floor, and scrolls beyond. The first time, the board may take 60% of the height.

### The key line

One line over the tabs, next to the keycodes it shapes (Rico: it sat beside the layer buttons
for a while, at no height of its own; with the tiles compact the height became affordable).

- **Where the key is**: row and column.
- **The keycode in QMK's words, editable** -- the Any entry, as Vial's "Any" dialog (Rico,
  2026-10-03): type `LT(1,KC_SPC)`, `MT(MOD_LCTL,KC_A)`, `C(S(KC_T))`, `OSM(MOD_LSFT)`, `MO(2)`
  or a hex value and press Enter, and it is written. What does not parse is outlined red, "not a keycode Nazg
  knows". It is `FormatKeycode()` in reverse.
- **The composer**: **When held** -- Nothing, Modifiers ▾, Layer ▾ -- and **Sent with ▾**. Their
  choices open in small popups that stay open while toggled, so the line never wraps; each change
  is written at once. Sent with lists **None first** -- back to the plain key in one click --
  then Ctrl, Shift, Alt, Win, and Left / Right, greyed while no modifier is on. They show only
  where they apply: on a plain key, or one already holding (MT, LT) or modified; Sent with goes
  while a hold is set, a tap-hold's tap being a plain key.
- **A tile picked keeps the key's hold or modifiers.** With "When held: Layer 1" on a key,
  picking Space writes `LT(1,KC_SPC)` -- one step, where Vial takes two (its placeholder, then
  the key). With Shift sent, every pick is sent with Shift. A tile's tooltip says what the click
  writes when that is not the tile's own keycode.
- **The message**: what the board stored, read back; a write the board changed or refused is a
  warning, as today.

### The tabs

**Category tabs and one search box through all of them** -- the search takes a name, QMK's name
or its label. The box takes what the tabs leave, up to 220 px; where that is too little for its
hint, the hint is just "Search". Each tab carries its category's colour as a dot; its keys are of
that category, Macros' tap dances aside.

| Tab | Dot | Holds |
|---|---|---|
| **Keys** | -- | letters, digits, punctuation (the two ISO keys last), **Shifted** -- Shift and a key in one click, VIA's symbols for a symbol layer -- editing keys, modifiers, navigation, F1 to F24, numpad, international (INT 1-9, Lang 1-9), the rarer HID keys (Undo, Copy, SysRq, the locking keys...), Transparent and None |
| **Layers** | Behaviour | the seven operations -- Hold, Toggle, To, Tap tog, Once, Base, Set base -- for each layer the board has; one-shot modifiers (eight, Meh, Hyper); layer lock and tri layer |
| **Media & mouse** | Host | media, volume, screen, system, apps, web; then the mouse -- move, buttons, wheel, speed (Rico: one tab, as Vial's "App, Media and Mouse") |
| **Lighting** | Board | by the lighting policy ("Which lighting keycodes a board gets", above); **absent** when the definition declares no lighting |
| **Features** | Behaviour | Caps Word, Repeat, Grave Esc, Leader, Key Lock; Space Cadet; Auto Shift, Autocorrect, Combos, Key overrides, One shot, Tap term, Swap hands; Secure |
| **Macros** | Host | keys that play what is set elsewhere, as many as the board has: macros, **tap dances** (Rico: here, by the macros, though drawn in Behaviour's violet), dynamic macros |
| **Special** | Board | the settings kept on the board (Rico: VIA's word): QMK's Magic keycodes, a group per swap so each reads Swap, Unswap, On/Off; Win key, NKRO, EE Hands; audio, clicky, music mode, Velocikey, haptic; the Unicode input mode; output and Bluetooth |
| **Custom** | Board | on every board (Rico): the keys the firmware's own code handles -- the board's, `QK_KB_n`, by the names its definition gives them (`customKeycodes`: 416 of VIA's 3513 definitions name some, Keychron's macOS keys the commonest) or else numbered -- VIA's "Custom", what Vial calls "User" -- then the keymap's, QMK's user range `QK_USER_n`, which neither app offers |
| **Devices** | Host | the board acting as another device: MIDI, the sequencer, steno, joystick, programmable buttons |
| **Firmware** | Firmware | last and apart: Boot, Reboot, Clear EEPROM, Debug, Make |

**Every keycode of the board's keycode version is in a tab, and search finds it** -- but MIDI past
its basic set and steno's extra chord keys, which take the expression box. There is no
catch-all: the rare keys went where they are looked for -- the HID ones to the end of Keys -- and
the rest made Special, Custom and Devices.

**A group is left out only when the board says it cannot work**: the lighting the definition
declares, the macro count (VIA and Vial), the tap dance count (Vial), Vial's feature bits --
Caps Word, Layer Lock -- and its alt repeat count, which the Repeat keys need; `PDF` exists from
keycode version 0.0.6. What nothing tells -- audio, haptic, Unicode, steno -- is offered, as Vial
and VIA do, hover saying what it needs. A tab left with no group is not shown.

### The tiles

- **Every tile 1u, 58 x 50 px.** Its text area is a 1u keycap's legend box at the board's
  smallest unit -- 60 px, padded to about 40 px wide, words at 11 px -- so every name fits as
  `legend_font` checks it does on the board: one line, two, the short form, then cut. The tile
  is then drawn 1.25 times that, text and gaps with it (`c_TileZoom`; Rico, 2026-10-05: 46 x 40
  was small) -- the fit does not change with scale. The words
  are the board's own (`NazgKeycapLegend`), with the keycap's face, band and colours.
- **A header the group's title says is left out**: "Media" over Play in the Media group,
  "Ctrl↔Caps" over Swap in its group, "MIDI" over CHND. The Shifted group's tiles show the Shift
  character alone ("!") -- on the board `S(KC_1)` is "Shift+" over "! 1", one rule for every
  modifier (Rico, short-forms.md rule 7).
- **One grid**: cells 62 px; groups packed side by side where they fit, no empty cell between
  them; a group longer than the row wraps under its first tile. The group's title sits in a box
  two cells wide spanning all its rows -- its first words; the rest on hover. The box is a
  recessed neutral, darker than the panel on Light, lighter on Dark, never a key's or a text
  field's: Light `#d1d4da`, text `#2f3238`; Dark `#1d1e25`, text `#9a9daa`.
- **The selected key's keycode is outlined** among the tiles.

### Hover, and after a pick

- **Tooltips** give QMK's name and label, with ImGui's tooltip timing
  (`ImGuiHoveredFlags_ForTooltip`): the mouse resting about 0.15 s, then no wait while moving on
  to the next tile.
- **Hover preview** (Rico: with the tooltip): once a tile's tooltip shows, the selected key on
  the board shows what the click would write, outlined in blue -- composer included, which the
  tile alone does not show. Sweeping across the panel to reach a tile leaves the board still.
  After a pick, the tile under the mouse previews nothing until the mouse leaves it.
- **After a pick the selection stays on the key** (Rico), as ZMK Studio's. Moving to the next
  key -- what Vial always does, VIA by default (its Fast Key Mapping) -- is **only a preference in
  Settings, off by default**: the writes are live and there is no undo, so a second click meant to
  correct a pick would land, written, on the key after. With it on: the next key in board order,
  top to bottom then left to right; past the last key the selection clears, as VIA's, rather
  than wrapping to the first, as Vial's; only a pick or Enter in the expression box advances, never
  the composer's buttons, which shape the key in place.
- **The key just written flashes** briefly, and once the selection has moved the message names
  it -- a write is seen where it landed.

### What the app needs for it

- **The definition parser keeps `customKeycodes`** (name, title, short name) for Custom's names,
  and Vial's `midi` field, should MIDI come to be gated on it (see "Open points").
- **What the board reports reaches the picker**: Vial's entry counts and feature bits
  (`VialEntryCounts`, already read), the macro count (`GetMacroCount()`), the layer count, the
  keycode version, the lighting state.
- **The splitter** in `NazgWorkspace`, in place of `c_BoardMaxShare`, its board height in `imgui.ini`.
- **The Settings screen** gains the after-a-pick preference.
- **Found by the mockup**: `KC_SPC` always prints blank (`PlacementClass::Blank`), so
  `LT(1,KC_SPC)` on a 1u key shows only "L1"; Space should be blank only on keys 3u and wider.

## The Layout section

The board's layout options -- split backspace, ISO Enter, bottom rows -- chosen in the panel.
Decided with Rico 2026-10-09 on the mockup `ui-design/layout-section.html`, real definitions in
it: ZX60 (a 10-option and a 9-option choice), the Model F B104, Cypher, and Promenade RP24S, the
worst case of VIA's registry with 11 groups. **1168 of VIA's 2029 V3 definitions have layout
options**, most of them 1 to 6 groups; a choice runs to 13 options.

- **In the column** when the definition has layout options (`layouts.labels`); no strip.
- **The board** is drawn with the stored choice and layer 0's legends, so its keys are known.
- **The panel: one line per group** -- a **checkbox** for a toggle (a plain label), a **combo**
  for a choice (a label that is an array: the group's name, then its options), as VIA and Vial
  do and ImGui draws. In columns, as many as the panel's width holds. A choice's **combo follows
  its name directly**, as a checkbox's label follows its box; the name is shown whole up to **20
  characters**, longer ones cut with "…" and given whole on hover (Rico, 2026-10-09). The combo
  is **as wide as its longest option**, not the rest of the line -- capped at what the line has
  left (Rico, the same day).
- **On hover**, an option -- a combo's item, or a checkbox for the state a click gives -- shows
  **its drawing** in a tooltip: the group's keys in that option, moved to where option 0 sits,
  every option of the group at one scale so they compare; **always inside the window** (Rico).
  **The board does not preview it**: it changes only once a choice is written and read back
  (Rico, 2026-10-09, after trying it in Nazg -- chosen in the mockup first, then dropped).
- **Written at once**, as Keymap writes a key: `id_set_keyboard_value` with `id_layout_options`,
  the whole packed value (via-vial-commands.md, "How `id_layout_options` packs its value"), then
  read back; the board then draws what was stored. A line under the groups says so. **The
  keymap is untouched**: a key hidden by an option keeps its keycode.

Tried and set aside in the same mockup: **buttons**, every option of a choice in a row -- they
wrap on ZX60's ten; **pictures**, each option drawn small all the time -- too much room overall
(Rico), hence the drawing on hover; and **lighting the group's keys** on the board, with a key
of a group lighting its line in the panel.

## The Macros section

The board's macros -- keys that type a sequence -- edited in the panel. What a macro can hold,
and how each firmware stores it, is in via-vial-commands.md, "Macros — the buffer and its byte
format". Decided with Rico 2026-10-10 on the mockup `ui-design/macros-section.html`, after
comparing Vial's and VIA's editors, redrawn from their source with the same macros, in
`ui-design/macro-editors-vial-via.html`. Real definitions in it: the Model F B104 (Vial 6, and
locked) and ZX60 (VIA 12, and VIA 10, which has no waits).

- **In the column** when the board reports macros (`GetMacroCount()` above zero); **the strip
  holds the slots**, M0 to the count, an empty one outlined and unfilled -- one row, scrolling sideways when
  the count is large (64 on Rico's Leyden Jar boards; see "Sections, and the strip"). **No names** (Rico): the board stores
  none, and Nazg keeps none -- nor does hovering a macro key in Keymap show its contents.
- **The board stays drawn** (Rico) and says where the macros are: the keys that play one are
  marked with it, the selected macro's lit, "L1" when the key is on another layer.
- **The panel: the macro as a chain, left to right** -- VIA's way, not Vial's rows (Rico: more
  visual; Vial's rows take the height and leave the width empty, where Nazg's panel is wide and
  short). Unlike VIA's chain, which only records, **it is edited in place**: a click selects a
  step, a click between two steps places a + where the next one goes. **A chain longer than the
  panel wraps, never clipped, and each row's end is linked to the next row's start** (Rico): the
  line leaves the last step to the right, turns down into the gap between the rows, runs back
  left and drops into the first step -- a carriage return, so the chain reads as one line. The
  rows are spaced for that line to pass between a press's ▼ and the next row's ▲. Its steps:
  - **a key**, drawn as a keycap. **Sent with** -- Keymap's own control, its popup of None, the four
    modifiers and the side (Rico, 2026-10-10, over the four toggles built first) -- makes Win+R one step, whatever the board stores: one 16-bit action on Vial 5 and later,
    four presses and releases on VIA;
  - **text**, typed in place;
  - **a wait**, in milliseconds, typed in place;
  - **a press** and **a release**, drawn as **VIA's two marks** (Rico): the key with ▼ under it
    where it is pressed, with ▲ over it where it is released. They are **steps of their own**, as
    the board stores them -- not a pair the model enforces: the firmware takes any order and any
    number, so they may cross (Ctrl and Shift released in the order pressed) or stand alone. They
    are **paired for reading**: a release goes with the last unreleased press of its key, hovering
    one lights the other, a key picked for one changes both, Remove takes both. **One left alone
    is said** and outlined in the warning's colour: a press never released leaves the key held after the macro ends;
    a release never pressed releases the user's own key. A bracket around the held steps was the
    other choice, and modelled the pair as one nested step -- set aside with it.
- **Keymap's picker under the chain** gives the keys: a tile replaces the selected key or is
  added at the +, so several picks add several keys. Between chain and picker, a line of tools
  for the selected step -- Sent with, move before / after, Remove -- and **Add: Text, Wait, Held
  key**, the last a press and a release with the + between them.
- **Text is typed the way the computer types it** (Rico, 2026-10-10): every character the host
  layout can type, on every layout Nazg supports. The firmware's "characters" are keys of a US
  layout in disguise -- byte `q` is the key where US QWERTY has Q, which types `a` on French --
  so Vial's and VIA's text is right on US computers only ("cmd" comes out "c,d" on French) and
  can hold nothing outside ASCII. One rule replaces it: **a text step shows what it types with
  the host layout, and is stored as whatever makes that layout type it.**
  - **Each character becomes keystrokes**: on a key directly, at one of its four levels (`é` is
    AltGr+E on US International); else its accent's dead key, then its base letter, Unicode's
    decomposition saying which (`ê` = `e` + ◌̂: `^` then E); else a dead key's own character,
    the dead key then Space (`'` on US International). Otherwise it cannot be typed on this
    layout, and the panel says so. **Hovering a text shows how each character is typed.**
  - **Stored compactly**: a keystroke the firmware's US table reaches -- a key plain or with
    Shift -- is one byte, the US character on that key (`q` for `a` on French); AltGr keys and
    keys outside the table (ISO's `<`) are actions -- one 16-bit tap on Vial, presses and
    releases on VIA. "Best regards," is 13 bytes on every layout; "Café crème, l'été même" 23 to
    34 bytes on Vial, depending on the layout.
  - **Read back the same way**: stored bytes are read through the host layout, dead keys
    composing, so **text another app wrote shows what it really types here** -- "l'eau" from VIA
    reads "léau" on US International, whose `'` is a dead key, "lùequ" on French. Nothing is
    converted; retyping it stores it Nazg's way.
  - **The host layout is asked for** the first time a macro holds text, until the user has
    chosen one -- the setting Keymap's legends use, in Settings too; afterwards a text's tools
    say which layout it is typed for ("Typed for US International ▾"). Whether Nazg can guess
    it -- SDL3 reports what each key types under the current layout -- is to be tried.
  - **Limits, said**: the macro is right on computers with that layout only (any text macro
    is; Vial and VIA assume US); a wrong host layout setting shows and writes wrong text, as it
    shows wrong legends in Keymap; a firmware built with QMK's `sendstring_<lang>.h` table reads
    the bytes differently, and no client can tell.
  - **What the layout table needs**: Nazg's strips QMK's "(dead)" marks and draws letters as
    capitals (a keycap's legend); typing needs the dead keys -- 49 of QMK's 70 layouts have
    some, Bépo 19 -- and each level's real case, which a rule gives (a lone capital is the lower
    case, its capital one level up), to be checked on all 69. **What a dead key types alone is
    not in QMK's data**, which names a dead key by its accent: US International's `'` key adds
    `´` but types `'` before Space -- written by hand where it differs. Compositions are mostly
    the same on Windows, macOS and Linux, not always (Windows' US International makes `ç` of
    `'` then C), hence the direct key first. A test can type every character of every layout
    and read it back.
- **Written by Save, undone by Revert** (Rico), as Vial: a dot on each changed slot, "Not
  written yet" on the macro's line. Unlike Keymap and Layout, which write at once -- a macro is
  typed and arranged step by step, and every write rewrites the whole buffer. **Unsaved changes
  are kept** while the user moves between sections; Nazg asks **Save / Discard** only when the
  board changes or Nazg closes (Rico, 2026-10-10).
- **Read only as far as the macros go, written only from the first change.** A board's buffer
  can be large: Rico's Leyden Jar boards (B104, B122, beamspring) have 64 macros and 16 KiB of
  emulated EEPROM, about 15 KB of it macros -- some 540 reads of 28 bytes, 7 s at the Model F's
  ~14 ms a round trip, which is what the VIA app costs, reading the whole buffer. Nazg reads
  until it has seen as many terminators as there are macros, as vial-gui does (64 empty macros
  are 64 bytes, three reads), and only when the section first opens: nothing else needs the
  buffer. Save writes from the first changed byte to the end of the macros, inside VIA's guard
  (the buffer's last byte `0xFF` first, `0` last).
- **The macro's line** says which slot, the room it takes in the board's memory, shared by all
  macros (a gauge, the other macros in grey: "26 bytes · 815 of 896 free"), and what was written.
  **Hovering the macro's name gives it in VIA's script syntax**, `{KC_LGUI,KC_R}{300}cmd{KC_ENT}`,
  read-only: the chain edits everything the board can store, so an editable script would only
  be a second way to do the same (Rico). **Clear**, right after the name, removes every step of
  the macro at once (Rico, 2026-10-10, after trying the built section) -- written by Save, undone
  by Revert, as any edit; greyed on an empty macro.
- **What the board cannot do is said where it matters**: no waits before VIA protocol 11 and
  Vial protocol 2 (Wait greyed, a wait already there in red); basic keys only on VIA (the
  picker's other tabs greyed); a locked Vial board shows its macros but writes none, its unlock
  keys outlined on the board (screen 5 below).

Set aside: **writing at once**, as Keymap and Layout; **text as characters**, VIA's and Vial's
way -- and with it a choice between characters and host keys, the first being the second on a
US computer; **the board folded away**, the panel saying where the macro is in words; **an editable
script line** over the chain.

**Recording is left out** (Rico, 2026-10-10) -- typing the computer's keyboard into a macro, as
both apps offer. Little sign that users want it: Vial's recorder has never existed on macOS (a
user asked in 2022, vial-gui#130: "not supported", nothing since), needs `pkexec` on Linux and a
system-wide hook on Windows, and its one open issue is the US-layout bug the rule above solves
(vial-gui#218); VIA's records only in full screen on Chromium, and the-via/app has no issue about
it at all. For Nazg it would be cheap -- SDL's key events while its window has focus, no hook,
positions as a macro stores them, the host-layout rule for free; Win and Alt+Tab need SDL's
keyboard grab -- so it can come later as **one Record button on the macro's line**.

Not to be confused with QMK's **Dynamic Macros**, recorded on the keyboard into RAM, which no
client sees: used little -- 71 of 7660 keymaps in QMK's tree of April 2023, the last with users'
keymaps, and 6 of vial-qmk's 573 Vial keymaps enable it -- so Nazg only offers its keys, in the
picker's Macros tab.

## The Tap Dance section

A tap dance is one key doing up to four things, told apart by how it is pressed -- **tap**,
**hold**, **double tap**, **tap then hold** -- with a **tapping term** in milliseconds separating
them: a press shorter than it is a tap, a second press inside it makes a double. Vial only (VIA
has none); 4 to 32 slots by the board's EEPROM size, 10 bytes each, read and written one slot per
round trip (via-vial-commands.md, `vial_dynamic_entry_op`). Decided with Rico 2026-10-10 on the
mockup `ui-design/tap-dance-section.html`, the Model F B104 (Vial 6, 32 slots).

What the firmware does, from vial-qmk's `quantum/vial.c` (`dance_step`, `on_dance_finished`,
`process_record_vial`, read 2026-10-10):

- **An empty action falls back on the others**: no hold -> the tap, held; no double tap -> the
  tap twice; no tap then hold -> the tap, then the hold held (the tap held when there is no hold);
  no tap -> a quick press sends nothing. Three taps send the tap three times.
- **When the tap is sent**: at release, only when the dance has a tap and a hold and nothing
  else; with a double tap or a tap then hold, it waits out the term, counted from the press, for
  a second tap. A dance with only a tap waits for nothing.
- Each slot keeps its own term (`TAPPING_TERM_PER_KEY`, forced by `build_vial.mk`); a reset slot
  has the firmware's `TAPPING_TERM`. The keycodes pass Vial's firewall: a locked board stores
  Boot as nothing, and nothing else changes.

The section:

- **In the column** when the board reports tap dances (`VialEntryCounts::tapDance` above zero);
  **the strip holds the slots**, TD 0 to the count, an empty one -- four actions empty -- outlined and unfilled. **Solid outlines, not dashes** (Rico, 2026-10-10, after seeing both sections built: dashes added little to see and many lines to draw) -- the same for Macros' empty slots.
- **The board stays drawn**, the keys holding a tap dance marked, the selected one's lit, "L1"
  when on another layer -- as in Macros.
- **The slot's line**: its name (hover: `TD(n)` and where it is), **Clear**, where it is on the
  board, what was written, and Save / Revert on the right.
- **The four actions side by side**, then the tapping term -- a row, where Vial has a grid: the
  panel is wide and short. Each action is its name, **a small timing drawing of the gesture**,
  centred (the key held as bars on a time line: short for a tap, long for a hold; schematic, the
  term not drawn -- a dashed mark for it was tried and dropped, Rico), and its key. A click
  selects an action; the picker's key sets it. **An empty action shows its fallback, faint, with
  words under it** ("Esc, held", "Esc twice") -- set aside: a dash alone.
- **A line says when the tap is sent**, since that delay is what surprises users: "Space is sent
  as soon as the key is released; held past 200 ms, it is L1", or "A tap waits 200 ms before Esc
  is sent"; a dance with only a tap, with no tap, or a term under 100 ms is warned about.
- **The tools** for the selected action: Sent with (Ctrl+C), Keymap's own popup, Empty it; then **Keymap's picker**. TD keys are greyed (a tap dance cannot
  play another), and Boot on a locked board, said in a line over the picker with the unlock keys
  outlined.
- **Written by Save, undone by Revert** (Rico), as Macros: a dot on each changed slot, "Not
  written yet" on the line, and Save / Discard asked when the board changes or Nazg closes.
  Writing at once -- cheap here, one 10-byte write per slot, and Vial's way for keys (its term is
  written by Save) -- was the other choice.
- **Read with the board**, every slot, one round trip each -- some 0.45 s more on the Model F's
  32 -- so Keymap can draw them; the section reads them itself only if that failed.

### On the Keymap's keys

Decided with Rico 2026-10-10 on the mockup `ui-design/tap-dance-keys.html`, the six example dances
drawn today and once read. A TD(n) key is drawn from its slot:

- **A tap and a hold: as a tap-hold** (decided 2026-10-03, "Short forms and command keys") -- tap
  Space, hold L1 is drawn exactly as LT(1, KC_SPC); the hold one line, in the colour of what it does,
  so Boot held behind Esc shows red, Ctrl+C held is Host's.
- **A tap without a hold: the tap under a "Dance" header** (Rico) -- what a press types, the violet
  header and band saying the key waits for a second tap. A tap that is a command keeps its own
  header; "Dance" then takes the hold's place, top right. Set aside: "Dance / TD n" unchanged, and
  the tap with a small TD mark in the corner.
- **No tap, or not read**: "Dance / TD n".
- **The double tap and tap then hold stay off the key**, with no mark that there is more (Rico: a
  ·· in the corner was the other choice) -- hover lists all four actions and the term.
- The picker's TD tiles stay "Dance / TD n": they are chosen by slot.

## The common screens

**1. No board open.** The keyboards found, each with its protocol and *Open*. No section
column, no strip. **With exactly one board plugged in, Nazg opens it directly**, so most
people never see this screen.

![No board open](ui-design/screen-no-board.svg)

**2. A board with a keymap only** — the most common case and the simplest screen: header,
layer strip, board, keycode picker. No column, since there is one section. Click a key, it is
outlined; click a keycode, it is written.

![Keymap, board with a keymap only](ui-design/screen-keymap-only.svg)

**3. A board with several sections.** Screen 2 plus the column, listing only what this board
has. The header shows the lock state, Vial only.

![Keymap, board with several sections](ui-design/screen-keymap-sections.svg)

**4. Choosing a definition** — only when more than one definition matches a VIA board. With
two or three candidates, each is drawn small beside its name and where it came from; more are
handled as in the next section. The answer is remembered per board, as it is today.

![Choosing between two definitions](ui-design/screen-choose-definition.svg)

**5. Macros on a locked board.** The strip lists the macro slots; the panel, the selected
macro's actions. Vial refuses a macro write while locked (via-vial-commands.md, "What the
lock gates"), so the panel says so, and the board highlights the keys to hold — the firmware
reports them in `vial_get_unlock_status`. Once unlocked, the message goes and the actions
become editable. The keymap needs no such screen: Vial accepts keymap writes while locked,
`QK_BOOT` aside, which the read-back already reports.

![Macros, board locked](ui-design/screen-macros-locked.svg)

**6. Settings**, from the settings button. It replaces the main area; the header stays, so
the open board is still named. The host layout and the user definitions library (Re-import,
Remove, Import) move here from today's HID Devices window, and the keycode picker's one
preference, moving to the next key after a pick, off by default (see "The keycode picker").
"Back to the board" returns where you were.

![Settings](ui-design/screen-settings.svg)

Settings also holds **Advanced tools**, a switch **off by default** (Rico, 2026-09-29).
Designer and debugging tools stay out of an ordinary user's way: turned on, the board menu
gains an *Advanced* submenu (see "Getting back to the keyboard list"). VIA does the same with
its Design pane, hidden until enabled in its settings.

## Choosing among many definitions

VIA's registry allows one official definition per VID:PID, so many candidates are always the
user's own: hobbyists on QMK's placeholder id (`0xFEED:0x0000` alone is on 175 QMK keyboards,
via-registry.md, "VID:PID collisions in QMK"), designers whose prototypes and revisions keep
one id, "Keep both" adding an entry each time.

![A ranked list and one large preview](ui-design/screen-many-candidates.svg)

1. **A ranked list and one large preview**, past three candidates. The ranking is the one
   already decided (via-registry.md, "Choosing a definition on connect"): user, community,
   official, then how closely the name matches the product string. The names that match form
   "Best matches"; the top one is preselected, so the usual answer is one click. The filter
   box appears only for a long list, about eight or more. Two or three candidates keep the
   side-by-side cards of screen 4, which compare better directly.
2. **The preview shows the board's real keymap, not blank keys.** Layer 0 starts at offset 0
   of the keymap buffer whatever the matrix, so Nazg reads it once — sized for the largest
   candidate, a few hundred bytes, some 10–20 round trips against the 890 ms of a full load —
   and draws it through each candidate. The right definition shows the user's own keymap in
   sensible places; a wrong matrix scatters the legends. People recognise their own keymap at
   once, even when two drawings have the same shape.

   ![The same layer 0 read through the right and a wrong definition](ui-design/preview-right-vs-wrong.svg)
3. **The preview uses the board's stored layout options**, read from the board, instead of
   the first choice of each option — what the user would get by picking it.

**Untested:** point 2 has not been tried; the first test is the Phoenix Project No 1 with the
forged ortho definition. A layer 0 that is mostly `KC_TRNS` or blank would show little.

**Provisional:** a "not chosen by any board" hint in Settings, to help remove stale
definitions — an extra whose value is unproven.

## Getting back to the keyboard list

![The board's name as a menu](ui-design/board-menu.svg)

The board's name in the header is a menu, as in ZMK Studio and VIA:

- **Switch to** — the other keyboards plugged in, one click each: the usual reason to go back.
- **Change definition… / Forget choice** — VIA boards only; they leave the board screen.
- **Advanced** — only with *Advanced tools* on in Settings: **Export definition…** (the
  definition drawing the board, byte for byte, for investigation and debugging). The picture
  shows it open, with **Show matrix…**, which left it for the column's Tools group (2026-10-09,
  see "The matrix view").
- **All keyboards** — closes the board and shows the list.

It adds nothing to the first glance — the name is already there — and it is the only way to
the list when Nazg opened a lone board directly. The list also comes back by itself when the
open board is unplugged, saying which one went.

![The keyboard list with every HID interface shown](ui-design/keyboard-list-all.svg)

**Show all HID devices** is a toggle on the list itself, **off at every start** — today's
checkbox already behaves so. Turned on, keyboards stay on top with *Open*, and every other
HID interface is listed under them, dimmed, with VID:PID, usage page and interface number,
and cannot be opened. The technical columns exist only in that view. (The ids in the picture
are illustrative.) **Refresh** stays on the list.

**Each list is framed** (Rico, 2026-10-09: with no visible limit the list was confusing): a
rounded, outlined frame with a header row, its count in its heading -- "Keyboards: 2", "Other HID
interfaces: 37" -- and at most 900 px wide, the column centred in the window. Each frame is as
tall as its rows; the other interfaces' up to 24 of them and never past the window's bottom,
then it scrolls inside, its header row staying, so Refresh and the checkbox never scroll away.

## The matrix view

How the board is wired: which row and which column of the switch matrix each key sits on.
Mostly for designers and anyone debugging a build, so it is **a section in the column's Tools
group, only with *Advanced tools* on** (Rico, 2026-10-09) -- with the setting off, a section would
bring the column back on every keymap-only board, since every board has the data. Until then it
was opened from the board menu's Advanced submenu ("Show matrix…") and took the sections' place
until closed. Its live test keeps the board open, so it stops as soon as another section is
shown (`Section::WhileHidden()`).

### What VIA and Vial do

- **VIA** draws the structure with "Show Matrix", in its Design pane — hidden until enabled in
  Settings — and in its Debug pane (`components/three-fiber/matrix-lines.tsx`,
  `components/n-links/matrix-lines.ts` in `the-via/app`). Every row is a pink line and every
  column a grey one, **all at once, unlabelled**; points are joined in screen order — rows by
  x, columns by y — not in column or row number order, so a line zigzags wherever the wiring
  does not follow the key positions; and the keys of **every** layout option are included
  (only encoders and decals are left out), so alternative keys stack and lines run through
  them. Its Key Tester also has a live "Test Matrix" mode.
- **Vial** draws no structure. Its **Matrix tester** tab lights keys on the board as they are
  pressed, polled every 20 ms, and asks for an unlock first.

### What it needs

- **The structure is definition data only**: each key's row and column (its KLE `"r,c"`
  legend) and the matrix size. No protocol, every VIA and Vial board, even with no board
  plugged in — and Nazg already has it.
- **The live test needs the firmware's consent**, deliberately: `id_switch_matrix_state`
  (via-vial-commands.md). Mainline VIA answers **all zeroes** unless built with
  `VIA_INSECURE`, or with `SECURE_ENABLE` and unlocked — QMK calls the alternative a "wannabe
  keylogger" — and all zeroes cannot be told from "nothing pressed", so the screen must say
  that if nothing lights, the firmware turns the test off. Vial needs protocol 3 or later
  and an unlocked board. It is cheap: `28 / ceil(cols/8)` rows per reply, the whole matrix in
  one request on most boards.

### One view, with rulers

One view, the board, with a **row ruler on the left and a column ruler on top**. (A first
draft had a second view, the matrix as a grid in the panel; one view proved far more legible.)

No selection:

![The matrix view with nothing selected](ui-design/matrix-view.svg)

A key selected and pinned — V, on row 3 and column 5:

![The matrix view with V selected](ui-design/matrix-view-selected.svg)

**Interactive version:** [ui-design/matrix-view.html](ui-design/matrix-view.html) — open it
in a browser; GitHub and Markdown previews show only its source.

- **Hover a key**: its row and its column light, on the board and in both rulers — the row
  in one colour, the column in another — with their keys **joined by the shortest links**
  (a minimum spanning tree over the key centres). Everything else dims. A row is a *set* of
  keys: its column numbers say which pin reads each key, not where the trace runs, so no
  order is drawn. A first build joined keys in number order, and on the Model F B104 — rows
  numbered out of screen order — a column became a zigzag from F2 down to Left Alt and back
  up (2026-09-29); VIA's screen order zigzags too, wherever a row is staggered. On a regular
  board the links are the straight line through the row.
- **Hover a ruler label**: that whole row or column.
- **Click** pins the selection, to move the mouse away.
- **Positions with no key** — what the grid showed and the board alone cannot: with a row
  selected, the columns with no key in it are **struck through** in the column ruler, and
  the other way round. In the picture, C1 (the ISO key's position) and C12 on row 3, R4 on
  column 5.
- A ruler label is **centred on its key nearest the edge** — a column's on its top key, a
  row's on its leftmost — nudged apart so none overlap, counting one key per position, the
  first in the definition, so a key wired in parallel far away does not take the label. (The
  first build placed it at the average of its keys: a column's keys differ in width, so on the
  Concordia C3 sat right of Esc, above nothing — Rico, 2026-09-29.) On a regular board they
  line up with the keys; on a board whose wiring does not follow its
  layout — split halves numbered 5 to 9, a Model F's matrix — they cannot, the ruler is then
  an ordered list, and the drawn line shows where the row really runs.
- **Live test** (the strip: *Wiring | Live test*): keys turn green as they are seen, with a
  count — a checklist for a freshly soldered board. A ruler label turns green when its whole
  row or column was seen; one that stays grey points at that trace, and a key lighting
  unpressed points at ghosting from a missing diode. **A view of its own** (2026-09-29): no
  hover, no wiring lines, no dimming -- a first build kept the wiring under the test, and the
  two could not be told apart. The board under test types into Nazg meanwhile, so keyboard
  navigation is off while it runs.
- **The panel**: the status line, the seen count, what the definition says about its matrix
  (below), and *Close*, back to the keymap.

### What the definition says about its matrix

Nothing here prevents anything: every definition loads and draws as before. The matrix view
only says what it finds, in its panel, and nothing is checked on import — an ordinary user
could do nothing about it. Decided with Rico 2026-09-29, after a scan of all 3513 of VIA's
official definitions (2029 V3, 1484 V2) with Nazg's parser:

| Situation | In the official definitions | Shown as |
|---|---|---|
| **Keys at different spots on one position** | 88 definitions (49 V3), 126 pairs | **A fact.** Two switches wired in parallel to one position are a legitimate design, unusual as it is (Rico): pressing either closes the same switch. Hovering one lights both, and the panel says "2 keys at row 5, column 11" rather than naming one. |
| **Two keys drawn exactly on top of each other** | 10 of those pairs (AEKISO60, TIDBIT…) | **A note.** Invisible on the board, so almost certainly a leftover copy in the definition. |
| **A key outside the matrix size** | none — VIA's registry rejects it (`validateKeyBounds()`, `the-via/reader`) | **A warning.** The firmware has no such position: that key can never be read, remapped or tested. Only an imported file or a Vial board can bring one. |
| **Positions no key uses** | 3015 definitions (86%), 12% of all cells | **Nothing.** Normal on most boards; the rulers already strike them through for the row or column in focus. |

QMK checks the same things at build time — keys inside the matrix, no position twice in one
`LAYOUT` (`lib/python/qmk/info.py`) — but in `keyboard.json`, not in the VIA definition, which
it never reads. Nothing ties the two files together, so a firmware that builds cleanly can ship
a definition with a mistyped `"r,c"`.

## The console

The text a QMK firmware prints — `print`, `uprintf`, `dprintf` — shown in Nazg, so debugging
a firmware needs no second app. Today that app is QMK Toolbox, whose output cannot be selected
or copied, or PJRC's `hid_listen`.

**Off by default, three ways.** The *Console* button exists only on a board that has a
console interface; the drawer is **closed at every start**, like "Show all HID devices"; and
Nazg **does not open the console interface at all** until the drawer is opened — nothing is
read, nothing is kept, until the user asks.

![The console button in the header, drawer closed: the default](ui-design/console-closed.svg)

![The console drawer open under the keymap](ui-design/console-open.svg)

### What QMK sends

`CONSOLE_ENABLE` adds a **second HID interface** to the board, next to VIA's raw HID one
(`0xFF60`/`0x61`): usage page **`0xFF31`**, usage **`0x74`** — PJRC's Teensy convention,
the one QMK Toolbox and `hid_listen` listen on. It goes **one way**, board to host: the text
in 32-byte input reports, buffered by `sendchar()` and flushed by `console_task()`. There
are no commands — the board cannot be asked anything through it, and `dprintf` prints only
while the firmware's `debug_enable` is set, which the firmware sets itself (in code, or with
the `DB_TOGG` key). Vial is QMK underneath and sends the same. (QMK:
`tmk_core/protocol/usb_descriptor.c`, `tmk_core/protocol/chibios/usb_main.c`,
`docs/faq_debug.md`.)

### What it takes

- **Transport**: a second open handle on the same board, and a reader for reports that
  arrive unasked. Today's transport does request and response only; the architecture
  already puts "async request/response + unsolicited messages" in the transport, and this is
  its first use. **No protocol code** — plain text, no VIA or Vial involved.
- **Pairing the interface with the board**: hidapi lists it as a separate entry. It is
  attached to the open board by VID:PID, HID strings and serial number. Two identical boards
  without a serial number are ambiguous; Nazg then asks which.
- **Boards without VIA**: a board with only `CONSOLE_ENABLE` can be listened to as well — a
  *Console* button on its row of the keyboard list, among the interfaces "Show all HID
  devices" reveals.
- **Surviving a reflash**: the console is for firmware work, so the board goes and comes
  back often. The log is kept, marked where the board left and returned, and the console is
  reattached to the same board by itself — which needs hotplug events or, while the drawer is
  open, re-listing devices about once a second (see "Open points").
- **Text that can be selected and copied** — what QMK Toolbox lacks. ImGui has no selectable
  text for free: lines are selected with its multi-select (click, shift-click, Ctrl+A),
  **Copy** takes the selection, **Save…** writes the log to a file. Also **Pause**, **Clear**,
  a filter, timestamps, and a cap on the number of lines kept, so a chatty board cannot use
  up memory.
- **Privacy**: with the firmware's `debug_keyboard` on, QMK prints every keyboard report it
  sends to the host (`tmk_core/protocol/host.c`) — the log can hold all that was typed. It lives **in memory only**, is written to disk only by
  *Save…*, and is gone when Nazg closes.

### Where it sits

**A drawer at the bottom of the window**, like an IDE's terminal: the regions above shrink
and stay usable. Not a section, and not a view replacing the main area like the matrix view —
the point is watching the output *while* using the keymap or the matrix live test. It opens
from the *Console* button in the header.

The same drawer could later show logs from other sources: XAP's log broadcasts, ZMK's USB
logging — which is a serial port, not HID, so it needs a serial transport, the one a ZMK
Studio backend needs anyway. Flashing, QMK Toolbox's other job, is not part of this.

## Open points

**The points about the board's look -- which sections fold the board away, folding the section
column, row labels on rows that share a line, the matrix view's layout options -- wait until the
look is built** (Rico, 2026-10-03): they are judged on the real thing, not on a mockup.

- **Plugin delivery** — see "Plugins".
- **Hotplug** -- checked 2026-09-26:
  - **hidapi has none, pinned or released.** 0.15.0 has no hotplug API, nor does upstream
    `master` (2026-08-14). It is being built on branches (`connection-callback`, the
    `hotplug-*` ones, active into September 2026) as `hid_hotplug_register_callback()` for
    **0.16.0**, unreleased. Its callbacks arrive on hidapi's own thread.
  - **SDL3 has a change counter, and Nazg already links SDL.** `SDL_hid_device_change_count()`
    (since 3.2.0) returns a number that grows when a device may have come or gone -- not which
    one. It is fed by the OS: `RegisterDeviceNotification` on a message-only window on Windows
    (any device interface, so several ticks per board), IOKit notifications on macOS, udev or
    inotify on `/dev` on Linux; with none of them it ticks every 3 s instead. Polling it every
    frame costs nothing -- on Windows its messages arrive through `SDL_PollEvent()`. The first call
    runs `SDL_hid_init()` on SDL's own copy of hidapi, compiled in under other names, so it
    does not clash with Nazg's. An SDL call, so it stays in `Main.cpp`.
  - **So: poll the counter, re-enumerate on change** -- after a short pause, since one board
    brings several notifications and its HID interfaces appear one by one. Only paths not
    seen before are probed for their protocol, so the open board is never probed under a
    write. A board whose path has gone is closed, and the list says which one went. Not yet
    tried against a real plug and unplug.
  - **Deferred** (Rico, 2026-09-26): too soon. It becomes necessary the day the Refresh
    button is removed, so it is built then. Until then an unplugged board is noticed by a
    failed request and the list needs Refresh.
- **The ▽ and ✕ marks are hard to see** (Rico, 2026-10-04, in the app): small, in the corner, at
  half strength. Accepted for now; a better way to show transparent and disabled keys is to be
  found later. The strike through a KC_NO key, which made it visible from afar, was dropped the
  same day (see "Transparent keys"). **And the ✕ can be mistaken for a legend** (Rico, 2026-10-04):
  on US ANSI International the `=+` key prints its AltGr character, ×, bottom right -- the mark's
  corner, at a similar size. The better way must not look like any character a host layout prints
  there.
- **Dragging the window jitters on a 60 Hz display** (Rico, 2026-10-06, on Windows; smooth at
  144 Hz, and Visual Studio or the Claude app stay smooth on the same screen). Found by bisecting:
  - **Not Nazg's code.** A bare SDL3 window with an SDL_GPU swapchain, presenting every frame,
    does it too; the same window without a swapchain, or with one that has not presented for a
    while, drags smoothly. D3D12 or Vulkan, VSYNC or MAILBOX, two or three swapchain buffers
    (`SDL_SetGPUAllowedFramesInFlight`): no difference.
  - **It is the presenting *before* the drag.** Drawing nothing during the drag still jitters;
    a window idle for a few seconds does not, and a drag turns smooth after 2-3 s without
    presents. Nazg itself keeps up: every move -- one a millisecond, with a 1000 Hz mouse --
    was handled on time, and nothing in SDL's move handling is slow.
  - **Likely, not confirmed:** Windows moves a window that presents steadily onto a hardware
    overlay (independent flip, MPO), whose movement stutters with some drivers, and takes it off
    after a few idle seconds. PresentMon's "Present Mode" column would confirm it.
  - **Not done:** drawing only on demand would avoid it while idle, but goes against ImGui's
    immediate mode and every poll, flash and coroutine that relies on a steady frame (Rico,
    2026-10-06). What remains is outside Nazg: the GPU driver, or the system's MPO setting.
  - **Kept from it:** a frame drawn from inside Windows' move-and-resize loop no longer waits for
    VSYNC (`Main.cpp`, `drawFrame`): it is skipped when no swapchain texture is ready. Windows'
    timer ticks there every ~16 ms, though SDL asks for 10 -- hard-coded, no hint changes it.
- **Which sections fold the board away**, and whether folding it confuses more than it helps.
- **MIDI in the picker's Devices tab**: the basic set is offered on every board. A Vial definition
  says `"midi": "basic"` or `"advanced"` (vial-gui shows MIDI keys only then); whether Nazg
  hides MIDI without it, and offers the advanced set with it, is to decide when it is built.
- **Folding the section column to icons in a narrow window** (Rico, 2026-10-03). The column is
  150 px, shown on every board with more than one section -- most Vial boards, once Macros, Tap
  Dance and Combos are sections -- and with the 9 px floor it is what pushes a TKL past a 1280 px
  window ("The floor is on the header"). Icons only, about 40 px, would give that back.
  **Settled 2026-10-06: the list stays.** The mockup `ui-design/section-column.html` set the 150 px
  list beside a navigation rail (icon over a two-line label, 76 px) and icons only (44 px, the
  label on hover), on real section lists up to NEVEREST 60's eleven; Rico, who had pushed for
  icons only: the list looks better. **Icons: Tabler** (MIT), chosen over Lucide and Phosphor in
  the same mockup.
  **Then made resizable** (Rico, 2026-10-09, tried in the same mockup): the list stays the
  default, **180 px** wide so "Switch Configuration" (170 px) and every other label show whole, and each user folds it if they want the room. A bar on the column's edge drags the
  list between **120 and 200 px** -- 200 fits every custom menu label in VIA's registry, the
  longest ("PMK Custom Settings") 174 px -- and dragged below **100 px** the column snaps to
  **icons only, 44 px**: the group headers go, the lines between groups stay, every label shows
  on hover. **No width in between**: labels cut to a few letters would be the worst of both, so
  between 100 and 120 px the edge holds at 120. Dragging back past 100 px unfolds it; a
  **double-click on the edge** folds or unfolds it at once. One width for every board, kept in
  `imgui.ini`. Manual only: the column never folds by itself when the window narrows.
- **Nazg's sections, then the board's menus** (Rico, 2026-10-06, in the same mockup): the
  sections Nazg provides -- Keymap, Layout, Macros, Lighting as VIA or Vial build it in, Vial's
  features -- come first; a VIA V3 definition's custom menus (`{label, content}` entries of
  `menus`) follow after a separator, in the definition's order, even when one is named like a
  Nazg section: NEVEREST 60's Tap Dance, Combos and QMK Settings are cipulot's own, on value
  channels 7-12 that VIA does not define. **Tools** -- sections that are neither, such as the
  Leyden Jar diagnostics (see "Plugins"), compiled in or a plugin later -- form a third group,
  **last** (Rico, 2026-10-06). **Each group opens with a text header** -- **the protocol's name,
  *VIA* or *Vial*, for Nazg's sections**, ***Board*** for the definition's menus, *Tools* -- with
  a line before every group but the first; **a group the board lacks takes no room**, no line
  and no header, and a board with only
  Nazg's sections shows no header at all. The headers also settle two entries sharing a name or
  an icon -- Vial's Tap Dance and a board's own, Lighting and Indicators: their group tells them
  apart. **The matrix view joins the Tools group** while *Advanced tools* is on (Rico,
  2026-10-09), last, with Tabler's `chart-grid-dots`; the board menu's "Show matrix…" is gone.
- **A custom menu's icon** (Rico, 2026-10-06), first match wins:
  1. later, an `icon` field on the menu in the definition (step 5) -- the author's choice;
  2. **a table keyed on the label**, kept by us: labels repeat across a family (cipulot's
     "Switch Configuration" is one menu on about 55 boards), and VIA's registry has only 22;
  3. **then VID:PID + label**, for labels the table does not know -- "Advanced Features" or
     "Custom Features" on one board;
  4. the keyword rule of the mockup's table;
  5. a monogram.
  Tables 2 and 3 are data, so they could live with the definitions in the community
  repository (via-registry.md, "Sharing") rather than in a Nazg release. "Switch Configuration"
  (actuation points, Rapid Trigger, calibration) wants a travel icon, not the magnet:
  `arrow-bar-to-down`, a key pressed to a point (below).
- **The icon of every section: settled** (Rico, 2026-10-09), in the review mockup
  `ui-design/section-icons.html` -- every kind of section with Tabler candidates, the column
  previewed at 18 px; the names are Tabler 3.47.0's outline set. A board's own Tap Dance, Combos,
  QMK Settings and Lighting take the icon of Nazg's section of that name; the group header tells
  them apart.

  | Group | Section or label | Tabler icon |
  |---|---|---|
  | Nazg's | Keymap | `keyboard` |
  | | Layout | `layout` |
  | | Macros | `player-play` |
  | | Lighting (VIA's built-in menus, Vial's; a board's "Lighting") | `bulb` |
  | | Audio (VIA's `qmk_audio`) | `volume` |
  | | Tap Dance | `hand-click` |
  | | Combos | `arrows-join` |
  | | Key Overrides | `replace` |
  | | Alt Repeat Key | `repeat` |
  | | QMK Settings | `adjustments-horizontal` |
  | Board | Switch Configuration (55 definitions) | `arrow-bar-to-down` |
  | | DKS (51) | `gauge` |
  | | Controller (51) | `device-gamepad-2` |
  | | System (51), Board System (13) | `settings` -- shared: the same meaning, never on one board |
  | | SOCD (50) | `arrows-left-right` |
  | | Indicators (22) | `circle-dot` -- not Lighting's bulb, which a board with both would show twice |
  | | Advanced Features (12) | `sparkles` |
  | | Custom Features (3) | `components` |
  | | PMK Custom Settings (2) | `adjustments-horizontal` |
  | | Haptic Feedback (2) | `device-mobile-vibration` |
  | | Knob (1); the keywords encoder, dial | `rotate-clockwise` |
  | | Display (no label yet; the keywords display, OLED, LCD, screen) | `device-desktop` |
  | Tools | Diagnostics (the Leyden Jar's) | `activity` |
  | | Matrix -- if the matrix view joins Tools, still open | `chart-grid-dots` |
  | | A plugin bringing no icon of its own | `puzzle` |

  Every custom label in VIA's V3 definitions is in the table, which is the label table of the
  lookup above; a label it lacks still falls to VID:PID + label, the keywords, then a monogram.
- **Row labels on a board whose rows share a line** (Rico, 2026-10-01, from the mockup): on
  the Model F, R3 and R4 both have their leftmost key on the Caps Lock row, so spreading them
  apart puts R4 between two rows and nudges R2 off its own. To revisit when the board's look
  is built -- one idea: rows sharing a line sit side by side on it, "R3 R4".
- **The matrix view's layout options**: it draws the board's stored choice, as the
  many-candidates preview does, and the keys of the other choices are not shown — the first
  build's default, which the panel states. How those keys could show is still open.
- **"Export definition…"**, for investigation and debugging only: put in the board menu,
  under the definition items, with the workspace frame. Moves to the Advanced submenu with
  *Advanced tools* (2026-09-29), built the same day.
- The three suggestions Rico accepted with the screens, worth confirming in use: opening a
  lone board directly; listing only the sections a board has; Settings replacing the main
  area rather than opening a dialog.
- **The interface's language** (Rico, 2026-09-29): a future feature -- the text of Nazg itself
  in another language, chosen in Settings. Not built, but it shapes the design as a whole, so
  the screens should not rule it out. What it asks of them, as far as seen today:
  - **No fixed text widths.** A translation runs longer -- German often by a third -- so a
    button, a strip entry or a panel line is sized by its text, and long lines wrap (the matrix
    panel already does).
  - **Whole sentences, not pieces.** "Seen 3 of 20 keys" built from fragments cannot be
    translated: word order and plurals differ. A message is one format with its numbers in
    it, and the numbers can move.
  - **An ImGui label is also its ID.** Translated, the ID would change with the language; a
    widget whose label is shown text needs a fixed `##id`.
  - **The font must cover the script.** The system font loaded in `Main.cpp` is a first draft;
    ImGui 1.92 loads glyphs as they are needed, but only from fonts it was given. A Chinese,
    Japanese or Korean interface needs CJK fonts for its own text -- the legends need none
    (2026-10-03) -- added with that translation: a font-loading change in `Main.cpp`, nothing
    structural. How they are delivered -- bundled, fetched on first use, or the system's --
    weighs their size and is decided then.
  - **Right-to-left scripts are out of reach**: ImGui has no bidirectional text or shaping.
  - **What is not translated**: keycode names (`KC_ESC`), and legends, which already follow
    the host layout -- a separate setting from the interface's language. What a board or a
    definition names -- a keyboard, a layout option, a VIA menu -- comes as its author wrote it.
