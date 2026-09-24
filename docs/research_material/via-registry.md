# VIA definition registry — how VIA stores and serves keyboard definitions

*Compiled 2026-09-24, first part of the registry study (step 4 of the build order). Study
only — no approach is chosen here.*

Sources: shallow clones of [the-via/app](https://github.com/the-via/app) (`935106a`,
2026-09-17) and [the-via/keyboards](https://github.com/the-via/keyboards) (`9e3e9f4`,
2026-09-22), kept outside the repo next to GP2040-CE as `via-app/` and `via-keyboards/`;
[the-via/reader](https://github.com/the-via/reader) read on GitHub; the live
`usevia.app` probed on 2026-09-24. `Keyboard_Project/the-via-keyboards` is Rico's fork,
stale since January 2025 — not the one to read.

## The pipeline, end to end

```
the-via/keyboards          source definitions, hand-written JSON
   src/**/*.json    V2       1484 files   8.6 MB
   v3/**/*.json     V3       2029 files    24 MB
        |  npm run build  (scripts/build-all.ts, using @the-via/reader)
        v
   dist/                     validated, CONVERTED definitions
     v2/<vpid>.json, v3/<vpid>.json      one file per board, named by VID:PID
     supported_kbs.json                  the index
     keyboard_names.json                 every board name, sorted
     hash.json                           HMAC-SHA256 of all of it
        |  a push to keyboards fires a GitHub repository_dispatch at the app
        v
the-via/app                bun run build:kbs  -> public/definitions/
        |  vite build, Cloudflare Pages
        v
https://usevia.app/definitions/...        static files, CORS *
```

- **The keyboards repo is the only source.** Its README requires the board in QMK master and
  its `via` keymap in `the-via/qmk_userspace_via` before a definition PR is accepted — the
  gatekeeping described in [via.md](via.md). Licence **GPL-3.0**, so a bundled snapshot is
  compatible with Nazg's GPL-3.0-or-later.
- **Every push to the keyboards repo redeploys the app** (`definition_update.yml` dispatches
  to `the-via/app`, whose `deploy-to-cloudflare.yml` listens for it). There are no releases
  of the definitions: the "version" is whatever `master` was at the last deploy.
  `supported_kbs.json` carries `"version": "0.1.0"`, the package version, which never moves.
- **The app pins nothing.** `package.json` depends on `github:the-via/keyboards` and CI
  runs `bun update --force via-keyboards` before building.

## What the index says

`supported_kbs.json`, 37 KB, from `scripts/build-definitions.ts`:

```json
{ "generatedAt": 1790082981231, "version": "0.1.0", "theme": { ... },
  "vendorProductIds": { "v2": [ ...1484 ids ], "v3": [ ...577 ids ] } }
```

- An id is **`vendorId * 65536 + productId`** as a decimal number (`getVendorProductId()` in
  the reader); the file is `<id>.json`. `0x4040:0xAA66` (enter67) is `1077979750.json`.
- **`v3` lists only the boards with no V2 definition.** Every V2 board also has a V3 file —
  the app treats "in v2" as "in v2 and v3". So 1484 + 577 = **2061 boards** on 2026-09-24.
- A **VID:PID can appear only once** per version; the build fails on a duplicate. So the
  registry is keyed by VID:PID alone — two boards sharing an id cannot both be in it.
  `0xFEED` is refused as a vendor id outright.

## What a served definition looks like — not the source format

**The served files are converted, not copies of the source.** `keyboardDefinitionV3ToVIA
DefinitionV3()` in the reader validates the source and then **replaces `layouts.keymap`
(the KLE rows) by the parsed keys**:

```json
{ "name": "enter67", "vendorProductId": 1077979750, "firmwareVersion": 0,
  "menus": [], "keycodes": [], "matrix": { "rows": 5, "cols": 15 },
  "layouts": { "width": 16, "height": 5, "optionKeys": {},
    "keys": [ { "row": 0, "col": 0, "x": 0, "y": 0, "r": 0, "rx": 0, "ry": 0,
                "d": false, "h": 1, "w": 1, "color": "accent" }, ... ] } }
```

**Nazg's parser (`adapters/via/NazgKeyboardDefinition.cpp`) reads the source form** — it
requires `layouts.keymap` and throws "the definition has no keymap" otherwise, because that
is what Vial embeds and what a board's `via.json` contains. So a definition fetched from
`usevia.app` **does not load in Nazg today.** Either Nazg also reads the converted form —
the parse is already done in it, the keys only need mapping — or it takes the source files
from the keyboards repo, which are not indexed by VID:PID (the path is `v3/<vendor>/<board>
.json`, and the id is inside the file as hex strings).

The other V3 fields pass through unchanged: `menus` (still possibly by name, like
`"qmk_rgblight"` — expanded in the app, not the build), `keycodes`, `customKeycodes`,
`firmwareVersion`.

## Which version a board gets

In the app, `devicesThunks.ts`: **`protocol >= 11 ? 'v3' : 'v2'`**, where `protocol` is the
VIA protocol the board reports. A board on protocol 10 or below needs a V2 definition; the
V3 format is only for protocol 11+. (The Aquanaut is on 12, so V3.)

## How the web app stores definitions

Two stores, both **in the browser**, both per origin:

| What | Where | Key | Lifetime |
|---|---|---|---|
| The index + every registry definition fetched so far | `localStorage["via-app-store"]`, one JSON blob (`src/shims/via-app-store.ts`) | `definitions[<vpid>][v2\|v3]` | until the index changes |
| Definitions the user side-loaded ("Load draft definition", Design tab) | **IndexedDB** through `idb-keyval`, default database | `<vpid>` → `{v2?, v3?}` | until the user removes it |

- **Fetched lazily, one board at a time**: `getMissingDefinition()` fetches
  `/definitions/<v2|v3>/<vpid>.json` only when a board with that id is plugged in.
- **Cache invalidation is by hash.** The build computes `hash.json` and injects it into
  `index.html` (`<script id="definition_hash" data-hash=...>`). On start, `syncStore()`
  compares it with the stored one; on a mismatch it refetches `supported_kbs.json` **and
  throws away every cached definition**. Since any merge to the keyboards repo redeploys,
  the cache is emptied every time any board changes anywhere.
- **Out of space means start over**: if saving to `localStorage` throws, the app calls
  `localStorage.clear()` — wiping settings too — and saves again.
- **Side-loaded definitions win**: `getDefinitions` overlays the IndexedDB entries on the
  registry ones, per id and version.
- **Offline**: the fetch of the index is wrapped in a try with a "TODO: fall back to cache"
  — the stored index is used if the fetch fails, but a board never fetched before has no
  definition.

## The native application

It **is** the web application. The desktop VIA
([the-via/releases](https://github.com/the-via/releases), binaries only; the wrapper's source
ships readable inside the app, see below) is an Electron wrapper, and its v2.2.0 notes say
so: *"the electron wrapper that points to https://usevia.app/"*.

- Latest is **v3.0.0, 2023-03-10** — an Electron update, icons, links, File Save API. No
  release since. Windows, macOS (dmg) and Linux (AppImage, deb); ~60–90 MB each;
  `latest*.yml` means electron-updater.
- **Inspected on Rico's machine (VIA 3.0.0, MSI install).** Installed at
  `%LOCALAPPDATA%\Programs\via-nativia\`; `resources/app.asar` is 537 KB and holds the whole
  wrapper: `package.json` (`via-nativia`, GPL-3.0-or-later, by Olivia Briggs), an ~80-line
  `main.js` and a Linux udev prompt. `main.js` does four things:
  `mainWindow.loadURL("https://usevia.app/")`; grants every HID device without a prompt
  (`setDevicePermissionHandler` returns true for `hid`, and `select-hid-device` picks
  `deviceList[0]`); opens links in the system browser; and asks the GitHub API for
  `the-via/releases` latest tag to nag when the shell is out of date.
- It **stores definitions exactly as the web app does**, in its Chromium profile at
  **`%APPDATA%\via-nativia\`** — named after the package, not `via`:
  `Local Storage\leveldb\` holds the `via-app-store` blob (its `generatedAt`,
  1790082981231, matched the live index on 2026-09-24, so the cache follows every deploy),
  and `IndexedDB\https_usevia.app_0.indexeddb.leveldb\` is idb-keyval's `keyval-store`
  holding the side-loaded definitions — `the_aquanaut` and `the_concordia` on this machine.
  Both are LevelDB, keyed by the `https://usevia.app` origin. It bundles no definitions and
  needs the network for any board it has not seen.
- **Side-loading accepts both forms.** The Design tab (`design.tsx`) takes a file that is
  either a source definition or an already converted one (`isVIADefinitionV3(res) ? res :
  keyboardDefinitionV3ToVIADefinitionV3(res)`) and stores the converted form.
- The earlier 2.0.x desktop versions (2022) predate the wrapper and are the old VIA
  application; not studied.

## Traps for a client of the registry

- **A missing file is not a 404.** `usevia.app` is a single-page app: asking for
  `/definitions/v2/<id>.json` of a V3-only board returns **200 with `text/html`** — the
  app's page. Check the content type (or the index) before parsing.
- **The served JSON is the converted form**, above. VIA itself accepts both forms when a
  user side-loads a file, so a user may reasonably hand Nazg either.
- **No version, no releases, no changelog** for the definitions — only the hash, and the
  deploy time in `generatedAt`.
- `usevia.app` is a web app's hosting, **not a published API**: nothing promises the paths
  stay. The source repo, being GPL data in git, is the stable thing.

## Open for the next part of the study

- How vial-gui and other third-party clients source VIA definitions, if they do.
- Bundled snapshot vs on-demand fetch vs both — size (the whole converted V3 set is small
  next to the 24 MB of sources, to be measured), update cadence, offline use, and what a
  fetch means for a native app (HTTPS in C++ is a new dependency).
- Converted form vs source form for Nazg's parser.
- V2 definitions: needed only for protocol ≤ 10 boards; what differs from V3 beyond
  `lighting`/`customFeatures`/`customMenus`.
