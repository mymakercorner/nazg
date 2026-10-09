# Interface styling: font, spacing, ImGui's style

Rico, 2026-10-09: "we'll need to work on alignments, font used, and ImGui general styling to make
it nicer." This is the survey behind the mockup `ui-design/styling.html`; the decisions go in
ui-design.md once taken. The board's own look -- keycaps, legends, their font (Arimo) -- is
settled in ui-design.md, "The board's look", and is not reopened here: this is everything around
the board.

## What Nazg does today

- **Font**: a system one, picked in `Main.cpp`, `LoadInterfaceFont()`: Segoe UI on Windows,
  Arial Unicode or Helvetica on macOS, DejaVu Sans or Noto Sans on Linux -- so Nazg looks
  different on each. Size `c_FontSize = 16`, scaled by the display's content scale. The comment
  there left the choice open: "belongs to the styling of the window, not decided yet".
- **Sizes**: ImGui's defaults, `ScaleAllSizes()` by the display scale and nothing else --
  `FramePadding` 4 x 3, `ItemSpacing` 8 x 4, `WindowPadding` 8 x 8, `CellPadding` 4 x 2,
  **every rounding 0** but tabs (5) and the scrollbar (9), **no frame borders**. Square, tight
  boxes: most of what reads as "not finished".
- **Colours**: by meaning in `NazgPalette.*`, mapped onto `ImGuiCol_*` in `NazgTheme.cpp` --
  Light, Dark, Dracula. Not the problem; the sizes and the font are.
- **Spacing in Nazg's own drawing** -- the column, the strip, the Layout lines, the picker's key
  line -- was set by eye, each in its own constants: no shared scale, so neighbouring regions
  disagree by a few pixels.

## ImGui sizes a font by its line height

`AddFontFromFileTTF(path, 16)` makes the font's **ascent to descent** 16 px, not its em. A font
with a tall line box therefore comes out small. Measured from each font's `hhea` and `OS/2`
tables (2026-10-09; the Google fonts from @fontsource 5.3.0's Latin files):

| Font | Line, in em | x-height | At ImGui 16: em | x-height |
|---|---|---|---|---|
| Segoe UI (today, Windows) | 1.330 | 0.500 | 12.0 px | 6.0 px |
| Arimo (the legends') | 1.117 | 0.528 | 14.3 px | 7.6 px |
| Inter | 1.210 | 0.546 | 13.2 px | 7.2 px |
| Fira Sans | 1.200 | 0.527 | 13.3 px | 7.0 px |
| Roboto | 1.172 | 0.528 | 13.7 px | 7.2 px |
| IBM Plex Sans | 1.300 | 0.516 | 12.3 px | 6.4 px |
| Noto Sans | 1.362 | 0.536 | 11.7 px | 6.3 px |
| Source Sans 3 | 1.424 | 0.486 | 11.2 px | 5.5 px |

So today's interface text is a **12 px** font -- small for a desktop app, where 13-14 px is
usual -- and comparing fonts "at 16" compares different sizes. The mockup sets the size as ImGui
does and converts, so what it shows is what Nazg would draw.

**ImGui does not hint.** It rasterises with stb_truetype, oversampled twice horizontally below
36 px, without hinting or subpixel anti-aliasing: text is softer than a browser's, most at small
sizes and in thin fonts. A font with a large x-height and open shapes holds up best. The other
way is ImGui's FreeType loader (`imgui_freetype`), which hints -- a new dependency (FreeType,
under its FreeType licence or GPL-2.0, either usable with GPL-3.0) that would sharpen the legends
too; worth a separate test, not part of this choice. **The mockup cannot show the softness**: the
browser hints. The chosen font is to be checked in Nazg before it is final.

## What the others do

- **VIA** (via-app): **Fira Sans**, from Google Fonts at run time; GothamRounded in a few
  places, Source Code Pro for code. Corners 4-6 px, text 14-20 px. A web app's look.
- **ZMK Studio**: **Inter**, bundled (`public/Inter.woff2`), for the interface **and the
  keycaps** -- one face throughout. Tailwind's scale: padding and gaps of 4, 6 and 8 px, corners
  4, 6 and 8 px, colours in OKLCH with light and dark variants.
- **Vial** (vial-gui): Qt's Fusion style with its own palettes, the system font. Looks like any
  Qt tool.

Two of three bundle a font, and the one that looks most finished, ZMK Studio, uses one face for
everything and a small spacing scale.

## Candidates for the interface font

Bundled, as the legend fonts are (resources/fonts): the same on every OS, and what the mockup
shows is what ships. All under the SIL OFL 1.1.

- **Arimo** -- already bundled for the legends: nothing to add. Metric-compatible with Arial:
  plain, familiar, a little dated; the largest at a given ImGui size. Interface and keycaps in
  one face, as ZMK Studio does.
- **Inter** -- drawn for screens, ZMK Studio's choice; tall x-height, holds up unhinted. Latin,
  Greek, Cyrillic. About 300 KB a weight.
- **Fira Sans** -- VIA's: a little narrower, humanist. Latin, Greek, Cyrillic.
- **Noto Sans** -- the widest coverage, for a translated interface later (ui-design.md, "Open
  points": the interface language); small at a given ImGui size, its line box being tall.
- **IBM Plex Sans** -- engineered, distinctive; small, like Noto.

A second weight (semibold) would serve headers -- the column's group headers, panel titles --
instead of colour alone.

## A spacing scale

One scale for ImGui's style and Nazg's own drawing, in pixels before DPI scaling, a 2-4 px grid:

| Style variable | ImGui default | Proposed |
|---|---|---|
| `WindowPadding` | 8 x 8 | 12 x 10 |
| `FramePadding` | 4 x 3 | 8 x 5 |
| `ItemSpacing` | 8 x 4 | 8 x 6 |
| `ItemInnerSpacing` | 4 x 4 | 6 x 4 |
| `CellPadding` | 4 x 2 | 6 x 3 |
| `FrameRounding`, `GrabRounding` | 0 | 4 |
| `ChildRounding`, `PopupRounding`, `WindowRounding` (popups) | 0 | 6 |
| `TabRounding` | 5 | 4 |
| `FrameBorderSize` | 0 | 1 (outlined controls, as on the web) |
| `ScrollbarSize` | 14 | 12 |

And for **alignment**: text beside a frame -- a label before a combo, the strip's label -- always
`AlignTextToFramePadding()`; one left edge per region, the panel's padding; a row's height the
frame height, so lines of checkboxes and combos keep one rhythm; labels in a fixed column where
controls stack (the Layout panel does it already).

## Settled (Rico, 2026-10-09, on the mockup)

**Arimo throughout**: the legend font for the interface too, at **ImGui 16** -- a 14.3 px font,
up from today's 12 -- so nothing new is bundled and Nazg looks the same on every OS. Headers --
the column's groups, Settings' parts, the board's name -- in **Arimo Bold**, Arimo having no
semibold. **The spacing scale above**, **6 px corners** on frames (8 on panels, popups and the
column; 6 on tabs) and **outlined controls**. Inter at 17 was the first choice, set aside so as
not to ship a second face.

The mockup's caveat stands: this is to be seen in Nazg, whose unhinted text is softer than the
browser's, before it is final.
