# The application icon

Nazg's icon (ui-design.md, "The application icon"): the rune ᚾ inside a blue ring, on a light
keycap. Rico's own art, under the project's licence.

**Its source is the mockup**, `docs/research_material/ui-design/app-icon.html`: the drawing is
code there, and its *Export the icon files* button writes every file below in one zip, from the
"H, refined" card as its switches stand -- their defaults are the icon. To change the icon,
change the mockup, export, and replace this folder's files with the zip's. Rare enough that no
tool in the repository does it.

| File | For | Sizes (px) |
|---|---|---|
| `nazg.svg` | the master, scalable; Linux's `hicolor/scalable`, a web favicon | 256 grid |
| `nazg.ico` | Windows: built into the exe by `src/Nazg.rc` -- Explorer, the taskbar, and the window, whose class SDL registers with the exe's first icon | 16, 20, 24, 32, 40, 48, 64, 256 |
| `nazg.icns` | macOS: the app bundle | 16, 32, 64, 128, 256, 512, 1024 (1x and 2x) |
| `nazg-macos.svg` | macOS's master: the keycap on Apple's grid, 824 of 1024 with a 185.4 corner, so macOS 26 does not set it on a grey tile | 1024 grid |
| `png/nazg-<size>.png` | Linux's `hicolor` theme (16, 22, 24, 32, 48, 64, 128, 256), the web (16 and 32 favicon, 180 Apple touch, 192 and 512 manifest), and the window icon where the platform takes it from the app (`SDL_SetWindowIcon` in `Main.cpp`: 32, with 64 and 256 for high DPI) | 16, 22, 24, 32, 48, 64, 128, 180, 192, 256, 512 |

**Up to 48 px the images are drawn for their size, not scaled**: the rune's stroke is an even
number of whole pixels, centred, so it stays sharp. 64 px and up are the vector drawing scaled.

Not yet used: the `.icns` and the Linux and web files wait for those builds; macOS 26's grey tile
is avoided by the grid, unchecked without a Mac.
