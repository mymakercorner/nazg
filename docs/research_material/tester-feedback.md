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
