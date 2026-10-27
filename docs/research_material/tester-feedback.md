# Testers' feedback

What testers report on a release, what of it is true, and what to do about each -- kept so that a
later session can pick an item up cold. A tester's words are his own; the verdict and the proposal
below each are Claude's, agreed with Rico on the date given, and not yet built unless the table
says so.

*First round: a long report on `0.1.0-test.5` / `0.1.0-test.6`, from a tester with a Leyden Jar
board who runs Nazg on Windows 10, Windows 11 and in a browser (2026-10-27). Analysed and agreed
the same day; nothing below is built yet.*

| # | What | Kind | Verdict | Done |
|---|---|---|---|---|
| 1 | Does not start on Windows 10; Windows 11 and the browser are fine | Bug, blocking | Cause found in the source, unproven on his machine | no |
| 2 | Wrong sizes on a second monitor of another scale | Bug, known | Confirms the deferred item, and worse than it says | no |
| 3 | Key names beside the signal levels | Feature | The strongest request of the round | no |
| 4 | The space bar is unlabelled in the matrix and diagnostics views | Bug, small | True where there is no keymap to read it from | no |
| 5 | Wiring flashes when the pointer moves between keys | Bug, small | True, mechanism found | no |
| 6 | The Levels button is a second way to the same thing | Design | Right about the default, wrong about removing it | no |
| 7 | QMK matrix and Controller show the same thing | Not a bug | Expected; nothing says why both exist | no |
| 8 | The key test counts any keyboard's keys | Limitation | True, not cheaply fixable, undocumented | no |
| 9 | The Device information always visible | Design | Reasonable, weigh against the panel's room | no |
| 10 | Remove the key test; all Tools top-level; hide Diagnostics | Declined | See below | n/a |
| 11 | Offer Nazg to the Vial people | Not a code question | Rico's alone | n/a |

---

## 1. Does not start on Windows 10

> "The software download does not run on my Win 10 computer with the latest updates, though it
> works fine on my Win 11 computer. There are no special OS or antivirus restrictions. The web
> browsing version works fine on the Win 10 and 11."

**The likely cause, from the source.** `Main.cpp`, the GPU device:

```cpp
SDL_CreateGPUDevice(SPIRV | DXIL | MSL | METALLIB, true, nullptr)
```

The second argument is SDL's `debug_mode`, hard-coded `true` in every build, Release included. On
Windows that asks D3D12 for its **debug layer**, which exists only where the "Graphics Tools"
optional feature is installed -- usually not on a clean machine. Where it is refused, the device is
not created, and the next lines do `SDL_LogError` then `return 1`. Nazg is a windowed application,
so **nothing reaches the user**: no window, no message, no log. The web build never takes that path
-- Emscripten goes through `SDL_GL_CreateContext` and WebGL 2 -- which is why the browser works on
the same machine.

Unproven: nobody has seen the error text on his machine, because there is no way for him to see it.
Item 1b is what makes it provable.

**Proposed, in this order:**

1. **`debug_mode` false in Release** (`#ifdef NDEBUG`, or the build type). The debug layer costs
   performance wherever it does work, so this is right regardless of the Windows 10 story.
2. **Say something when startup fails.** All five `return 1` paths in `Main.cpp` -- `SDL_Init`,
   `SDL_CreateWindow`, `SDL_CreateGPUDevice`, `SDL_ClaimWindowForGPUDevice`, the data folder -- are
   silent. `SDL_ShowSimpleMessageBox` with `SDL_GetError()` turns "it does not run" into a report
   worth having, for this failure and every later one.
3. **A runtime fallback to `imgui_impl_opengl3`** where SDL_GPU gives no device. CLAUDE.md calls
   OpenGL "the fallback if it causes trouble", but today that is a compile-time choice: on the
   desktop there is no path from a failed `SDL_CreateGPUDevice` to a GL context. The web build
   already runs the same `App` on that renderer, so the work is in the shell only. Bigger than 1
   and 2; do it only if 1 does not fix his machine.

Then ask him to run it again -- he is the only known reproduction.

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
