# UI design — the workspace

*Decided with Rico 2026-09-26, from [ui-inventory.md](ui-inventory.md). Design only — nothing
here is implemented yet. The pictures are wireframes: they fix what goes where, not the look.
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
| **Section column** | Keymap, Layout, Macros... — about 150 px wide, an icon and a label per row. **Only the sections the board has**, and **hidden when there is only one** |
| **Strip** | A row of choices owned by the section: layers in Keymap, slots in Macros (see below). Absent when the section has nothing to choose |
| **Board** | Drawn by Nazg; what each key shows is the section's |
| **Panel** | The section's editor — the keycode picker in Keymap. **Always visible**, not only while a key is selected |

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

### What a section provides

Every section, built in or a plugin, supplies four things:

1. **Its strip entries**, or none.
2. **What the board shows** — each key's colour, legend or value, and what a click on a key
   does. Nazg draws the board; the section only fills it. A section that does not use the
   board (macros, settings-like ones) may fold it away and give the room to its panel.
3. **Its panel.**
4. **Its match rule** — which boards it appears for.

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

**Open: how third-party plugins are delivered.**

| | For | Against |
|---|---|---|
| Compiled in | no risk | only Rico adds one |
| **Declarative** — a descriptor mapping widgets onto a board's custom commands | safe, every platform, the web build too; the same vocabulary as step 5 | bounded by the widget types defined — though a "value per key" board overlay already covers a level heat map |
| Code — native libraries | full power | a build per OS; ImGui across a library boundary; third-party code in a process that rewrites keymaps |
| Code — sandboxed WebAssembly | full power, sandboxed, portable | a new dependency and an API to maintain |

The survey's conclusion (README, "Declarative description vs arbitrary code") points to the
declarative route first, code only if it proves too narrow.

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
Remove, Import) move here from today's HID Devices window. "Back to the board" returns where
you were.

![Settings](ui-design/screen-settings.svg)

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

## Open points

- **Plugin delivery** — see "Plugins".
- **Hotplug**: whether the pinned hidapi 0.15 reports devices arriving and leaving has not
  been checked. Until it is, an unplugged board is noticed by a failed request, and the list
  needs Refresh.
- **Which sections fold the board away**, and whether folding it confuses more than it helps.
- **"Export definition…"**, for investigation and debugging only, has no place yet — the
  board menu, under the definition items, is the obvious candidate.
- The three suggestions Rico accepted with the screens, worth confirming in use: opening a
  lone board directly; listing only the sections a board has; Settings replacing the main
  area rather than opening a dialog.
