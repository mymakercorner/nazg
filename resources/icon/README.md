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

## Where each platform takes it

Windows uses it today, Linux's AppImage too (`tools/package_linux.py`, 2026-10-18), and the
macOS app (`tools/package_macos.py`, 2026-10-24) -- never yet opened on a Mac. The web is
prepared, not built (build-and-release.md):

- **macOS** -- a bundle, `Nazg.app/Contents/`: `Info.plist` from `resources/package/macos/`, its
  `@NAZG_VERSION@` and `@CMAKE_OSX_DEPLOYMENT_TARGET@` filled by `tools/package_macos.py`;
  `MacOS/nazg`; `Resources/nazg.icns`. The identifier is `com.mymakercorner.nazg`. In a bundle
  `SDL_GetBasePath()` answers `Contents/Resources/`, so the fonts, the definition bundles and
  `icon/` go there, not beside the executable. macOS 26's grey tile should be avoided by the
  grid of `nazg-macos.svg`.
- **Linux** -- `resources/package/linux/nazg.desktop`, and the PNGs and the SVG laid out as the
  freedesktop icon theme wants: `share/icons/hicolor/<n>x<n>/apps/nazg.png` for 16, 22, 24, 32,
  48, 64, 128 and 256, `share/icons/hicolor/scalable/apps/nazg.svg`,
  `share/applications/nazg.desktop`. SDL names the window after the executable -- Wayland's app
  id and X11's class are both `nazg` -- which is how the desktop matches the window to the entry
  and its icon; a reverse-DNS name (`com.mymakercorner.nazg.desktop`, `SDL_SetAppMetadata`) is
  needed only for Flathub. The AppImage carries the entry and the icons both at its root and in
  `usr/share/`; .deb and .rpm would install them.
- **The web** (Emscripten) -- `resources/package/web/nazg.webmanifest` beside the page, the icons
  in `icon/` beside it, and in the page's `<head>`:

  ```html
  <link rel="icon" href="icon/nazg.svg" type="image/svg+xml">
  <link rel="icon" href="icon/nazg-32.png" sizes="32x32" type="image/png">
  <link rel="apple-touch-icon" href="icon/nazg-180.png">
  <link rel="manifest" href="nazg.webmanifest">
  ```

  The SVG first, for the browsers that take it; the 32 px PNG for those that do not (older Safari).
  `SDL_SetWindowIcon` does nothing in a browser.
