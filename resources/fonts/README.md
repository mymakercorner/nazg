# Legend fonts

The fonts Nazg draws keycap legends with, committed so every build draws the same legends on
every platform (ui-design.md, "The legend font is Arimo" and "How the board's look is built").
The build copies this folder beside the executable.

| File | From | Licence | Covers |
|---|---|---|---|
| `Arimo-Regular.ttf`, `Arimo-Bold.ttf` | [googlefonts/Arimo](https://github.com/googlefonts/Arimo) `fonts/ttf/`, commit `4a6255f` (2026-04-27) | SIL OFL 1.1, `Arimo-OFL.txt` | the legend font: Latin, Greek, Cyrillic, Hebrew, currencies, ◌ ◊ ‡ ‰ ﬁ ﬂ -- 309 of the 386 characters the host layouts can put on a key |
| `NotoSansArabic-Regular.ttf` | [notofonts.github.io](https://github.com/notofonts/notofonts.github.io) `fonts/NotoSansArabic/hinted/ttf/`, commit `e3ff34c` (2026-10-02) | SIL OFL 1.1, `NotoSansArabic-OFL.txt` | the 74 Arabic and Farsi characters (the Farsi host layout) |
| `NotoSansMath-Regular.ttf` | same, `fonts/NotoSansMath/hinted/ttf/` | SIL OFL 1.1, `NotoSansMath-OFL.txt` | ≃ |
| `NotoSansSymbols2-Regular.ttf` | same, `fonts/NotoSansSymbols2/hinted/ttf/` | SIL OFL 1.1, `NotoSansSymbols2-OFL.txt` | ⌨ |

The Noto faces are merged behind Arimo, which ImGui fills missing glyphs from. Coverage was
measured 2026-10-03 against every character in `src/ui/NazgHostLayoutTable.cpp`: nothing is
missing but the Apple logo (U+F8FF), which Nazg does not print. Upstream Arimo holds far more
than the copy Google Fonts serves, which lacks ◊ ‡ ‰ ﬁ ﬂ.

None of the four licences names a Reserved Font Name, and the files are unmodified. The OFL
allows bundling them with any software, GPL included, as long as each keeps its licence beside
it. ui-design.md first noted Arimo as Apache 2.0, from Google Fonts' listing; the files here
come from Arimo's own repository, which licenses them under the OFL.

To update: download the same paths again, rerun the coverage check, and record the new commits
here.
