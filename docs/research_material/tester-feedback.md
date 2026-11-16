# Testers' feedback

What testers report on a release, what of it is true, and what to do about each -- kept so that a
later session can pick an item up cold. A tester's words are his own; the verdict and the proposal
below each are Claude's, agreed with Rico on the date given, and not yet built unless the table
says so.

*First round: a long report on `0.1.0-test.5` / `0.1.0-test.6`, from a tester with a Leyden Jar
board who runs Nazg on Windows 10, Windows 11 and in a browser (2026-10-27). Analysed and agreed
the same day; built since: items 1 to 8 (2026-11-07), as the table says.*

| # | What | Kind | Verdict | Done |
|---|---|---|---|---|
| 1 | Does not start on Windows 10; Windows 11 and the browser are fine | Bug, blocking | Likely cause found in SDL's source (corrected 2026-11-07), unproven on his machine | 2026-11-07, awaiting his retest |
| 2 | Wrong sizes on a second monitor of another scale | Bug, known | Confirms the deferred item, and worse than it says | 2026-11-07, the scale followed; small windows not |
| 3 | Key names beside the signal levels | Feature | The strongest request of the round | 2026-11-07, from a mockup |
| 4 | The space bar is unlabelled in the matrix and diagnostics views | Bug, small | True where there is no keymap to read it from | 2026-11-07, seen on Rico's F77 |
| 5 | Wiring flashes when the pointer moves between keys | Bug, small | True, mechanism found | 2026-11-07, seen on Rico's F77 |
| 6 | The Levels button is a second way to the same thing | Design | Right about the default, wrong about removing it | 2026-11-07, kept for the session |
| 7 | QMK matrix and Controller show the same thing | Not a bug | Expected; nothing says why both exist | 2026-11-07, a line said, seen on Rico's F77 |
| 8 | The key test counts any keyboard's keys | Limitation | True, not cheaply fixable, undocumented | 2026-11-07, in the manual |
| 9 | The Device information always visible | Design | Mostly answered by 3; two questions back to him | asked |
| 10 | Remove the key test; all Tools top-level; hide Diagnostics | Declined | See below | n/a |
| 11 | Offer Nazg to the Vial people | Not a code question | Rico's alone | n/a |

---

## 1. Does not start on Windows 10

> "The software download does not run on my Win 10 computer with the latest updates, though it
> works fine on my Win 11 computer. There are no special OS or antivirus restrictions. The web
> browsing version works fine on the Win 10 and 11."

`Main.cpp` created the GPU device with

```cpp
SDL_CreateGPUDevice(SPIRV | DXIL | MSL | METALLIB, true, nullptr)
```

**The first verdict was wrong** (2026-10-27): it blamed `debug_mode` (the `true`), on the theory
that D3D12 refuses a device when its debug layer is not installed. SDL does not: when the layer is
missing it logs "Validation layers not found, continuing without validation" and goes on
(`SDL_gpu_d3d12.c`, `D3D12_CreateDevice`; Vulkan the same).

**The likely cause, read in SDL 3.4.16** (2026-11-07). The shader formats Nazg offered were not the
ones ImGui uses: `imgui_impl_sdlgpu3` gives D3D12 its **DXBC** shaders, never DXIL. SDL's
`D3D12_PrepareDriver` refuses D3D12 when:

- DXIL is offered without DXBC and the GPU lacks **Shader Model 6.0**;
- **Tier 2 resource binding** is missing, unless the app sets
  `SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN` (SDL_gpu.h names Intel's
  Haswell and Broadwell graphics as the case).

On Windows 11 with neither DXIL nor Tier 2 asked for, it skips the checks altogether. A Windows 10
machine with an older GPU fails both, and if it has no Vulkan driver either -- Haswell has none on
Windows -- no device is created. `main()` then logged and returned 1, and a windowed application's
log reaches no one: no window, no message. The browser works on the same machine because it draws
through WebGL.

Still unproven: it needs his Windows 10 machine to have such a GPU, and that is not known. A crash,
a missing DLL or another refusal would look the same to him.

**Built** (2026-11-07, `Main.cpp`):

1. **The shader formats ImGui really ships** -- SPIR-V, DXBC, MSL, metallib, no DXIL -- and
   **fewer resource slots allowed** on D3D12 (ImGui uses no storage resources), through
   `SDL_CreateGPUDeviceWithProperties`.
2. **Debug mode only in Debug builds** (`NDEBUG`): the validation layers cost performance
   wherever they are installed.
3. **A message box on every startup failure** (`ReportStartupFailure`): `SDL_Init`,
   `SDL_CreateWindow`, `SDL_CreateGPUDevice`, `SDL_ClaimWindowForGPUDevice`, and the web build's
   `SDL_GL_CreateContext`, each with `SDL_GetError()`. The data folder was never one of them: App
   reports that one itself. Seen with a GPU driver forced that does not exist (`SDL_GPU_DRIVER=metal`
   on Windows).

**Not built**: a runtime fallback to `imgui_impl_opengl3` where SDL_GPU gives no device -- today
a compile-time choice, the work in the shell only. Only if his retest still fails on the GPU device.

**Before the next build reaches him**, the release he has can already say why: SDL writes its log to
the console of whatever started it, so from `cmd` in the unzipped folder,
`set SDL_LOGGING=app=info,gpu=info` then `nazg.exe` prints SDL's reason (for example
"D3D12: DXIL is not supported and DXBC is not being provided", or "Tier 2 Resource Binding is not
supported"). If nothing prints at all, it is a crash, not a refusal.

Then ask him to run the next build -- he is the only known reproduction.

## 2. The display scale on a second monitor

> "My main Win 11 screen is 300% DPI zoom while my secondary screen is 150%. It looks fine on my
> main screen but each element is too large on the secondary screen. This is the issue with both
> moving between screens and closing the program and reopening it directly on another screen."

Known and deferred (CLAUDE.md; ui-design.md, "Open points"): the scale is read once, at start, from
the **primary** display. He is the first person with the hardware to meet it, and his report is
worse than the deferred note implies: it is not only a window dragged to another monitor, but
**Nazg started on that monitor**, which is the ordinary case for someone whose second screen is
where they work. That changes it from a cosmetic edge case to "wrong on a secondary screen of
another scale, always".

**Proposed**: re-weigh the priority on that evidence, and keep the two halves apart --

- following the display's scale (the deferred fix, ui-design.md has the how);
- his larger suggestion, "auto-resized to fit the layout, with scroll bars for small screens or when
  the program is not maximized", which is a separate question about what Nazg does in a small
  window, and does not depend on the first.

**Built** (2026-11-07): the first half -- the scale of the display the window is on, asked every
frame, so Nazg started on his 150 % screen and a window dragged there both follow it (ui-design.md,
"Open points"). Seen following Windows' scaling changed while it ran (Rico); unseen on two
monitors: he is the test. The second half, a small window, is not
started.

## 3. Key names beside the signal levels

> "I suggest combining the key presses and signal levels tabs so that the user can easily see if
> they are pressing the Q key while analyzing the levels. Currently the user would have to count
> ('this is the 15th key from the left which needs adjustment'). **This is the important feature
> missing from the current diagnostic tool.**"

The sharpest observation of the round, and he says plainly that the Leyden Jar Diagnostic Tool
lacks it too. Someone adjusting a capacitive board does two things at once -- finding the key that
is wrong, and watching its numbers -- and Nazg makes him alternate between two screens to do it.

**Proposed:**

- A **combined view**: the keys with their names and their levels together. His shape is a
  reasonable start -- the key's name on the left, the numbers right-aligned, the keys stretched
  wider to fit.
- **The default** when Diagnostics opens, rather than Key presses.
- A **legend always on screen** (high, current, low, column), not a tooltip: a tooltip cannot be
  read while looking at another key.

Keeping the separate views is still worth it for a board at rest; the point is what greets him.

**Found on a closer look** (2026-11-07): Signal levels already lights the key being pressed. What
loses him is the **Levels** button: it puts the tool's numbers on the keys *instead of* their
legends (`DescribeLevels()`), so the key he watches has no name. **Mockup**, first draft for Rico:
[ui-design/levels-on-keys.html](ui-design/levels-on-keys.html) (ui-design.md, "Signal levels, third
round") -- where the name goes, the panel following the key pressed, the legend, and the default.
**Chosen by Rico** (2026-11-07): the name above the numbers, the panel following the key pressed,
the legend a sample key, Levels kept as left for the session, Key presses still first. Built the same
day.

## 4. The space bar has no legend

> "I suggest noting 'Space' or 'Spacebar' especially on the matrix views and diagnostic pages."

True. ui-design settled "the space bar blank" where the board is drawn with its keymap, and there it
is right -- the shape says what it is. In a wiring grid or a levels table there is no keymap to read
it from, and a blank key among named ones reads as a hole.

**Proposed**: label it in the matrix and diagnostics views only, leaving the keymap and picker
views as they are.

## 5. The wiring view flashes on hover

> "The screen flashes briefly with all keys turning a lighter color when changing the mouse hover
> position; it is a little jarring."

True, and the mechanism is in `NazgMatrixView.cpp`: `m_Hovered` is set from the board's hover
events, so while the pointer crosses the **gap between two keycaps** it is over no key at all, the
focus empties, the dimming of the unrelated keys drops, and every key brightens for those frames.
It is not a redraw artefact; it is the gap.

**Proposed**: keep the last focus while the pointer stays within the board, and let it go only when
it leaves -- so the row and column stay lit as the pointer travels from one key to the next.

## 6. The Levels button, and the web build's second click

> "On the web version you have to click signal levels and then click the Levels button, instead of
> clicking levels to automatically set it to view the levels numbers as would be assumed." / "The
> Levels button seems to just toggle the key press and levels screens and it should be removed."

Two sides of one complaint: Signal levels opens on the bars, and the **numbers** are what he came
for. He is right about the default and wrong about removing the control -- the bars are the quick
read of a whole board, the numbers are for one key.

**Proposed**: fold this into item 3. If the combined view is the default, the second click
disappears without taking the bars away.

## 7. QMK matrix and Controller look the same

> "The QMK Matrix and its adjacent Controller button show no visible difference in output."

Expected on a healthy board. They are two independent reads of the same reality -- QMK's own matrix,
and `ReadControllerMatrix` with the Beam Spring inversion applied -- and their worth is that they
**agree**: when they disagree, there is a wiring or firmware fault, which is exactly what a board
with a dead matrix shows. Not a bug; nothing on screen says why both are offered.

**Proposed**: a line in the panel saying what each reads and what a difference between them would
mean. No behaviour change.

## 8. The key test counts any keyboard's keys

> "It also captures all keyboards' presses."

True, and not in the manual's known limits. On *Computer's keys* Nazg counts whatever the computer
receives, so a key pressed on a laptop's own keyboard lights a key on the board under test. SDL
gives no way to tell which device a key came from, so this is not cheaply fixable.

**Proposed**: document it -- the manual's key test section and the release notes' known limits. If
it ever matters enough, only a second, device-aware input path would fix it, which is a large piece
of work for one line of warning.

## 9. The Device information always on screen

> "The Device tab's information should appear always on the left panel when a compatible controller
> is detected, so that the individual key levels and binning and other details (including Enter
> Bootloader and erase eeprom) can remain always visible."

Reasonable, and of a piece with item 3: he wants fewer screens holding essential facts. Weigh
against the room the panel has once the combined view is in it, and against the Device section
existing for every board, not only Leyden Jar ones.

**Proposed**: decide it after item 3 is built, with the real layout in front of you.

**Weighed with item 3 built** (2026-11-07): his first words, "the individual key levels and binning",
are what the line following the pressed key now shows -- now, highest, lowest, threshold, bin -- and
Levels puts each key's bin under its name. What stays on Device alone: what does not change while he
works (protocols, wiring, sizes, Vial, USB) and the two actions, rare, and Erase not to be undone --
not beside controls clicked all the time. The one fact worth having in Signal levels: **every bin's
calibration side by side**, one muted line. **Asked first** (Rico): does the new Signal levels give
him what he wanted; and is it the bootloader and the erase he wants at hand, or a way to
**recalibrate** after adjusting a key -- the controller sets its thresholds at power-up, and the
Leyden Jar's Reboot command (`0x8A`) is not used by Nazg yet. Nothing built until he answers.

## 10. Declined, and why

- **"Remove the Matrix Live test; diagnostics does it better."** He is generalising from the one
  board class that has an alternative. Diagnostics is planned only `if (report.isLeydenJar)`
  (`NazgSectionPlan.cpp`); for every other VIA and Vial board -- almost every user -- the key test is
  the only test there is. The overlap on his own board is real and worth a word in the UI; the
  removal is not.
- **"All of the Tools top-level on the left side menu."** They already are: Device, Diagnostics and
  Matrix sit in the sidebar's TOOLS group. What he is objecting to is the **strip** as second-level
  navigation, which is how every section in Nazg navigates -- lighting systems, QMK settings groups,
  a board's own menus. Changing it for Diagnostics alone would make Diagnostics the odd one out. His
  underlying complaint, too many clicks to see what matters, is answered by item 3.
- **"Hide the diagnostic stuff if not a supported keyboard."** Already so, for the same reason as
  above: he has only ever seen Nazg with a Leyden Jar board attached.

## 11. Offering Nazg to the Vial people

> "I suggest bringing it up to the Vial team as a strong alternative option or even a replacement
> for the current software."

Not a code question, and Rico's alone. Two things worth holding in mind when he weighs it: Nazg was
re-implemented from the wire format precisely to avoid inheriting vial-gui's GPL lineage
(CLAUDE.md, Conventions), and "a replacement for Vial's own app" is a different undertaking from "a
client that speaks Vial" -- it would inherit their roadmap and their users' expectations along with
the name.

---

# Second round

*A user's request, passed on by Rico (2026-11-07), built the same day.*

| # | What | Kind | Verdict | Done |
|---|---|---|---|---|
| 12 | Which definition file and version was loaded | Feature | Half there: said, but hidden and without a version | 2026-11-07 |

## 12. Which definition, which file, which version

> "It would help to show which definition file and version was loaded, especially when both a
> community definition and a user definition match a board. That could make mismatches easier to
> troubleshoot."

**What existed.** A tooltip on the board's name: "Definition: *name* -- *source*" (user, imported
from a path; community, published by someone, unverified; official, from VIA; from the board on
Vial). Change definition... listed every candidate with its source, the one in use marked. **What
was missing**: no file, no version -- a definition has none of its own, but each source has one:
the-via/keyboards commit the official bundle is pinned to, the community release tag, a user
file's revision and import date -- no word on why that definition and not another, nor that
others matched. And the manual sent users to hover the **protocol**, which had no tooltip: the
likeliest reason it was never found.

**Built**: each candidate carries its file and version (`DefinitionCandidate::file`, `version`,
filled where it is made: `NazgBoardLoad.cpp`, `DefinitionLibrary::Candidates()`). The tooltip --
on the name and now on the protocol too -- says the name, the source, the file and version, why
(chosen and remembered, or the only one for the VID:PID) and how many other definitions match,
pointing at Change definition...; the picker shows the file and version on every candidate. No new
line on the screen itself (first-glance simplicity). Seen on Rico's boards the same day.

---

# Third round

*A macOS user's report on keebtalk, passed on by Rico (2026-11-16): Apple Silicon (M4), macOS 27.0.1,
a split board on Vial -- the first macOS report from outside. Analysed the same day; the verdicts
are proposals, **not yet agreed with Rico** unless the table says so.*

**Confirmed**: Nazg opens, draws and works on an M4 Mac -- the first sighting of the macOS build on
a real Mac. Nothing said about Gatekeeper or Input Monitoring.

| # | What | Kind | Verdict | Done |
|---|---|---|---|---|
| 13 | Graphics a bit blurry on macOS | Bug? | Nothing wrong found in the code; needs a clean picture | asked |
| 14 | No word in a WebKit browser (Orion) that WebHID is missing | Bug | The message exists; why it was not seen is unknown | asked |
| 15 | The space key's legend is empty | Design | True on his board: a 1u thumb space has no telling shape | 2026-11-16, agreed with Rico |
| 16 | The Vial unlock bar should be more visible | Design | Thin by Rico's choice (2026-10-18) | -- |
| 17 | Switches as the key test's default | Design | Against Rico's choice (2026-10-26); remembering the choice answers it | -- |
| 18 | The host layout in the menu bar, as in Vial | Design | True: one combo in Settings, hard to find; wanted in the header (Rico, 2026-11-16) | 2026-11-16, from a mockup |
| 19 | International keycodes not recognised; show what `S(KC_7)` types | Feature | True: Nazg knows no `DE_*` name, and draws a shifted key as its keycap | 2026-11-16, from a mockup |
| 20 | Liked: colour coding, wiring view, the picker's international keys | Praise | Wiring as a help to rewire a handwired board | n/a |
| 21 | The icon | Taste | Rico's | -- |

## 13. Blurry on macOS

> "Graphics are a bit blurry, not extremely but noticeable"

**What the code does**, and it looks right: the window asks `SDL_WINDOW_HIGH_PIXEL_DENSITY`, the
bundle's `Info.plist` says `NSHighResolutionCapable`, and ImGui 1.92 rasterises every font at the
viewport's `FramebufferScale` (`imgui.cpp`, `SetCurrentWindow()`: `FontRasterizerDensity`) -- 2 on a
Retina display -- with no density fixed anywhere in Nazg. The board's legends are ImGui text too.

**What the picture says**: little. Keebtalk keeps it as a 2166x949 JPEG of 88 KB, compressed enough
to soften every edge by itself. Rico's colleague's Mac has not been asked this yet.

**Proposed**:

1. Ask him for a **PNG** of a small region at full size (Cmd+Shift+4), and whether the menus are
   blurry or the board alone; the colleague's Mac to compare.
2. A **pixel density** line under *Display scale* in Settings' about lines (`io.DisplayFramebufferScale`),
   so any tester can say whether Nazg draws at 2 -- cheap, and the first thing to know.

## 14. A WebKit browser says nothing

> "trying to use the web-app in a Webkit Browser (Orion) doesn't give me any indication that I have
> to use another browser to support WebHID"

Nazg has the screen: without `navigator.hid` the list is replaced by "This browser cannot reach
keyboards" with Download Nazg (`DrawNoWebHid()`, `NazgKeyboardList.cpp`). So either he never reached
it -- the page stuck on "Loading Nazg..." or blank, a WebGL 2 or WebAssembly failure, which
`resources/package/web/shell.html` reports nowhere -- or Orion defines `navigator.hid` (it imitates
Chrome's extension APIs) and Nazg showed an empty list.

**Proposed**: ask him which of the three he saw. Whatever the answer, the page should say when
the runtime fails (`Module.onAbort` in `shell.html`, its text in `#status`) instead of staying blank.
If Orion has a `navigator.hid` that does not work, the test becomes whether `requestDevice` exists.

## 15. The space key's legend is empty

> "for some reason my spacebar keycode remains empty"

His second picture is Keymap: the thumb key between Tab and Enter, blank, its tooltip saying
`KC_SPC -- Spacebar`. Blank by design -- "its shape says what it is" (`DescribeLegends`, `SpaceBar::Blank`;
item 4 named it only in the matrix and diagnostics views). On a split board the space is a 1u or
1.25u thumb key shaped like its neighbours, and blank there reads as unassigned: the rule's own
reason does not hold.

**Proposed**: blank only on a key at least 2u wide; a narrower space key named "Space" as in the
matrix views. One condition in `DescribeLegends()`, on `geometry.width`.

**Built** (Rico, 2026-11-16): `c_TellingSpaceBarWidth` (2u) in `NazgBoardDescription.h`; on every
view, the picker's full-size keyboards untouched (their space bars are 2.5u and 6.25u). A space key
2u tall and 1u wide, an Ergodox thumb, is named too: its neighbours share its shape.

## 16. The Vial unlock bar

> "the VIAL unlock progress bar should be more visible and present"

`NazgVialUnlock.cpp`: thin, no words, "a frame's height was too heavy" (Rico, 2026-10-18). Rico's to
reopen or not. In between: a little taller, or in the accent colour, which on the dark theme stands
out more than ImGui's default fill.

## 17. Switches by default in the key test

> "for matrix test, making the Switch test default would be better imho, sending computer Keycodes
> can get confusing"

His first picture shows why: two keys sending a comma -- `KC_COMM` and a mod-tap on it -- both
amber with a ? after one press, as built ("keys sending the same thing light together"). Computer's
keys is the default by Rico's choice (2026-10-26), for testing a keymap after programming; this
tester tests switches. **Proposed**: what ui-design already lists as waiting -- the choice remembered
in `settings.json` -- so each user picks once. Reversing the default is Rico's.

## 18. The host layout in the menu bar

> "Im missing the host layout in the menu bar like in VIAL, maybe even with a color change like VIAL
> does"

True. The host layout is one combo in Settings, among the look's choices; nothing asks for it but
Macros' text (`hostLayoutChosen`), so every legend is US until the user goes looking. He would
rather reach it from the header, as in Vial's menu bar. **Kept apart from 19** (Rico, 2026-11-16):
where the layout is chosen is one question, what Nazg makes of international keycodes another.

**Proposed**: the layout in the header, its short name on the right beside Locked and Settings
(`DrawHeader` in `NazgWorkspace.cpp`), a click opening the same list as Settings -- 69 layouts, so a
scrolling list, perhaps with a filter. Its colour: muted while it is the default, the text colour
once chosen, if Vial's "color change" means telling the default apart. Settings keeps its combo.

**Mockup** (2026-11-16): [ui-design/host-layout-header.html](ui-design/host-layout-header.html) --
where (right, left, the board menu), what it says (the name, or "Layout:" before it), its look
while never chosen (same, muted, warning with "?"), the list plain or filtered; a row of keys
following the layout picked.

**Chosen by Rico** (2026-11-16): **left**, after the protocol; **"Layout: German"**; the **same**
look chosen or not; the list **with a filter**; with a **board open** only -- the keyboard list
shows no legends. Settings keeps its combo. Built the same day.

Separately, and smaller: **guessed from the computer on first start** -- on the desktop
`SDL_GetKeyFromScancode()` over the letter and symbol positions, matched against the 69 tables (in
`Main.cpp`, one more `PlatformServices` value); in Chrome `navigator.keyboard.getLayoutMap()`. Kept
until the user picks one; nothing when no table matches.

## 19. International keycodes, and what a shifted key types

> "international QMK keycodes don't seem to be recognized/if combined with a modifier like the 7 in
> my keymap, it would be great to show the resulting key"

QMK's international keycodes are the names in `keymap_extras` -- `DE_SLSH`, `DE_AT`, `FR_EURO` --
each an alias of a basic keycode, alone (`DE_Z` = `KC_Y`), shifted (`DE_SLSH` = `S(KC_7)`) or with
AltGr (`DE_AT` = `ALGR(KC_Q)`). The board stores only the expression. Nazg falls short twice:

- **The names are unknown.** The host layout tables were built from those very files
  (keycodes.md, "Regenerating the host layouts"), but only the legends were kept: no `DE_*` name
  appears anywhere in Nazg (searched 2026-11-16). The tooltip says `LSFT(KC_7)`, and the Any entry
  cannot take `DE_SLSH`. Saved layouts are unaffected: vial-gui and the VIA app write no such names.
- **The result is not drawn.** `S(KC_7)` is "Shift+" over the 7's keycap -- "& 7" on US, the German
  7 with its "/" on German -- by Rico's rule 7 (short-forms.md, 2026-10-04; before it, "!" alone).
  He asks for the character the key types. AltGr the same: `ALGR(KC_Q)` is its modifier over Q,
  where German types "@".

**Proposed**, each Rico's:

1. **The names**, by the host layout chosen: the tables regain each alias's name, regenerated by
   hand from the same QMK files; the tooltip and the Any entry speak them (`DE_SLSH -- /`), the Any
   entry accepting any layout's.
2. **The character on the key**: where the modifiers are exactly Shift, AltGr or Shift+AltGr and
   the host layout types a character there, that character as the main legend -- small "Shift+"
   kept as the header or not. It reopens rule 7 for those three cases only; Ctrl, Alt and GUI keep
   it, as the picker's Shifted group already reads "!" alone.

**Studied** (2026-11-16):

- **The data.** QMK's `data/constants/keycodes/extras/` holds 72 files, **6665 aliases** -- about 90 a
  layout, each `"KC_7": {"key": "DE_7", ...}` or an expression, `S(DE_7)`, resolving through the
  layout's own names. The 69 layouts Nazg has are the same files (keycodes.md, "Regenerating the host
  layouts"), so the names are one more pass over them, by hand, no generator.
- **A name means something only within its layout.** Prefixes are shared: `FR_` by French, French
  AFNOR, French (Mac ISO) and Canadian French -- `FR_A` is `KC_Q` on French, `KC_A` on Canadian
  French; `DE_AT` is `ALGR(KC_Q)` on German, `A(KC_L)` on German (Mac ISO); 16 prefixes in all, `SE_`
  on five. So the host layout chosen decides what a name means, and which name a keycode gets.
  Typed in the Any entry under another layout, a name every layout using it agrees on can be read;
  one they disagree on cannot, and the message says which layout to choose.
- **Where names would show**: the hover (`KC_7 -- DE_7`, `LSFT(KC_7) -- DE_SLSH`), the key line, the
  picker's search; read by the Any entry. The QMK expression stays what is written to files and
  shown first -- the board stores it, and VIA and Vial know no other.
- **The character on the key is already half built**: the picker's tile does it for Shift
  (`NazgKeycodePicker.cpp`, `shiftAlone`: the Shift legend as the plain one, the header dropped).
  The board would take the same step in `LegendFor()`'s `ModifiedKey`, adding AltGr and Shift+AltGr
  from the same table -- on the Mac layouts AltGr is Option, `A(X)`, as the table already reads it.

**Mockup** (2026-11-16): [ui-design/international-keycodes.html](ui-design/international-keycodes.html) --
a row of keys under five host layouts. On the key: today, the character alone, or with its small
header kept; for Shift only, or AltGr and Shift+AltGr too. The name on hover: today, on the line,
a line of its own ("German: DE_SLSH, types /"), or first. The Any entry showing the expression or
the name, and reading names: DE_SLSH, DE_AT, FR_A, CH_EURO, FR_EURO to try.

**Chosen by Rico** (2026-11-16): on the key, the character large with its **header kept**
("Shift+" over "/"); for **Shift, AltGr and Shift+AltGr** (Option on the Mac layouts); the name on
hover on **a line of its own** ("German: DE_SLSH, types /"); the Any entry showing the
**expression**, reading names too -- the chosen layout's, another's where every layout using it
agrees, refused with the layouts named where they differ.

**Built** the same day: `CharacterTyped()` in `NazgKeycapLegend.*` (the legend, and hover's "types");
`ui/NazgHostNames.*` and its data, `ui/NazgHostNameTable.cpp` (keycodes.md, "Regenerating the host
layout names"); the hover line and the Any entry in `NazgKeymapSection.cpp`, the picker's search by
name. Tests: `keycap_legend`, `host_names`.

## 20. Liked

The colour coding of modifiers and custom keys; the wiring view -- "could also be some substantial
help if rebuilding handwires with a already present firmware", a use not written anywhere yet; the
picker's Basic tab showing his layout's keys. Nothing to do; the wiring use could go in the manual.

## 21. The icon

> "the Icon is, sorry for being harsh, atrocious"

Designed with Rico 2026-10-16 (ui-design.md, "The application icon"). One voice; Rico's.
