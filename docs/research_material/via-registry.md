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
- **`v3` lists only the boards with no V2 definition**, and the app treats "in v2" as "in v2
  and v3" — so 1484 + 577 = 2061 boards on 2026-09-24. **That assumption is false for 32
  boards**: they have a V2 file and no V3 one (mostly Keychron, `0x3434:02xx`), and their V3
  URL returns the HTML page. There are exactly **2029 V3 files**, one per V3 source. It does
  not bite VIA, which asks for V3 only on protocol 11+ and these boards are presumably older;
  a client reading the index must not assume it either.
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

### Side by side: ISO Macro

A small board with one layout option shows nearly every difference. The source,
`via-keyboards/v3/merge/iso_macro.json`:

```json
{ "name": "ISO Macro", "vendorId": "0x4D65", "productId": "0x1200",
  "matrix": { "rows": 3, "cols": 3 },
  "keycodes": ["qmk_backlight_keycodes"], "menus": ["qmk_backlight"],
  "layouts": { "labels": ["Single Encoder"], "keymap": [
    [ {"d": true}, "2,1\n\n\n0,1",
      {"x": 0.5, "c": "#8f8f8f"}, "2,1\n\n\n0,0",
      {"x": 0.25, "c": "#cccccc"}, "0,0", "0,1", "0,2",
      {"x": 0.25, "w": 1.25, "h": 2, "w2": 1.5, "h2": 1, "x2": -0.25}, "2,0" ],
    [ {"c": "#8f8f8f"}, "2,2\n\n\n0,1",
      {"x": 0.5}, "2,2\n\n\n0,0",
      {"x": 0.5, "c": "#cccccc"}, "1,0", "1,1", "1,2" ] ] } }
```

Served as `v3/1298469376.json` — one line, laid out here, `keys` cut after two of seven:

```json
{ "name": "ISO Macro", "vendorProductId": 1298469376, "firmwareVersion": 0,
  "menus": ["qmk_backlight"], "keycodes": ["qmk_backlight_keycodes"],
  "matrix": { "rows": 3, "cols": 3 },
  "layouts": { "labels": ["Single Encoder"], "width": 5.75, "height": 2,
    "optionKeys": { "0": {
      "0": [ {"row": 2, "col": 1, "x": 0, "y": 0, "r": 0, "rx": -1.5, "ry": 0, "d": false, "h": 1, "w": 1, "color": "mod"},
             {"row": 2, "col": 2, "x": 0, "y": 1, "r": 0, "rx": -1.5, "ry": 0, "d": false, "h": 1, "w": 1, "color": "mod"} ],
      "1": [ {"row": -1, "col": -1, "x": 0, "y": 0, "r": 0, "rx": -1.5, "ry": 0, "d": true, "h": 1, "w": 1, "color": "alpha"},
             {"row": 2, "col": 2, "x": 0, "y": 1, "r": 0, "rx": -1.5, "ry": 0, "d": false, "h": 1, "w": 1, "color": "mod"} ] } },
    "keys": [
      {"row": 0, "col": 0, "x": 1.25, "y": 0, "r": 0, "rx": -1.5, "ry": 0, "d": false, "h": 1, "w": 1, "color": "alpha"},
      {"row": 2, "col": 0, "x": 4.5, "y": 0, "r": 0, "rx": -1.5, "ry": 0, "d": false,
       "h": 2, "w": 1.25, "w2": 1.5, "x2": -0.25, "h2": 1, "color": "alpha"} ] } }
```

| Source | Converted |
|---|---|
| `vendorId`, `productId` as hex strings | one `vendorProductId`, `0x4D65 × 65536 + 0x1200` |
| KLE rows, each property applying to the keys after it | `keys`, one explicit object per key |
| Option in the fourth legend slot, `"2,1\n\n\n0,1"` | moved out of `keys` into `optionKeys[group][choice]` |
| Colour `#cccccc` / `#8f8f8f` | a theme role, `"alpha"` / `"mod"` |
| `{"d": true}` — a decal, drawn blank, no switch | `"d": true`, `row` and `col` = `-1` |
| Fields left out | defaults on every key; `firmwareVersion: 0` |
| Board size, never stated | `width`, `height` computed |

ISO Enter's `w2`/`h2`/`x2` pass through, and so do `menus` and `keycodes`, still by name.

**The conversion also moves keys**, in two steps (`kleLayoutToVIALayout()` and
`extractGroups()` in the reader's `kle-parser.ts`):

1. **Each choice moves onto choice 0.** Its pivot — the topmost key, the leftmost of those,
   **decals counted**, taking that key's second rectangle's corner when it sticks out up or
   left — is moved onto choice 0's pivot. Here choice 1's pivot is its decal at (0, 0), and it
   lands on choice 0's key at (1.5, 0).
2. **The whole board shifts** so that the bounding box of the always-present keys and choice 0
   (rotation included) starts at the origin: left by 1.5 here, key `0,0` going from x = 2.75
   to 1.25, both choices ending at `x: 0`. The same shift is subtracted from `rx`/`ry`, hence
   `"rx": -1.5` on every key.

The app then draws `keys` plus the selected choice's `optionKeys` as stored; nothing is
aligned at run time. It skips decals (`keys.filter((k) => !k.d)`).

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

## Why VIA converts, and what that means for Nazg

The conversion serves VIA: definitions are **validated once, at build time**, so a broken one
fails CI instead of reaching a user; clients need **no KLE parser** (KLE's properties apply
to the keys that follow them, rotation origins carry across rows); **layout options are
sorted out ahead of time** into `optionKeys` instead of hiding in a key's label slot;
defaults are filled in; and every consumer sees the same parse. It does not save space —
explicit fields on every key are, if anything, more verbose than KLE.

For Nazg it replaces nothing: the KLE parser stays, because Vial embeds the source form and a
board's `via.json` is in it. Reading the converted form is a **second entry**, cheap — a
mapping into the same `DefinitionKey` list, with only the layout options needing real work
from `optionKeys`. It is the price of reusing VIA's build output, and the benefit it buys is
that output's validation.

**Nazg aligns layout options itself** — `PlaceKeys()` in `model/NazgKeyboard.h`, run every
frame at no measurable cost, with VIA's pivot rule above. So the conversion's heavy lifting is
not work Nazg lacks.

**Nazg does not convert on disk.** User and community definitions are stored as given (see
"Storage"), and the conversion happens where it already does — in memory, at load: both
entries, source and converted, produce the same `DefinitionKey` list, about 0.1 ms per board.
Converting at import would remove no code, since the KLE parser stays for Vial and for
vendors' `via.json`, and it would lose an export identical to the import, parser fixes that
reach old imports — a bug written to disk is permanent, the original gone — and one source of
truth.

### The converted-form entry — verified on the whole registry

*Implemented 2026-09-24.* `ParseDefinition()` reads either form: `layouts.keymap` means the
source form, `layouts.keys` the converted one (`keys` and `optionKeys`, `vendorProductId`
split into the two ids, a dynamic `name` giving its first option). Keys with `row: -1` that
are not decals — encoders drawn on the board — are skipped, like legend-only keys in the
source form. Decals are kept, flagged: they count for alignment and the board's extent, and
are never drawn or edited.

**Alignment moved from the view into the model**: `PlaceKeys(definition, selection)` in
`model/NazgKeyboard.h`, VIA's pivot rule, which the view draws and the tests can call. On the
converted form every shift comes out zero, since VIA already lined the choices up. The two
forms of one board still differ by one translation — VIA's step 2 — so drawings are
compared **up to a translation**.

**Checked against all of VIA's work** (a scratch program, not in the repo): the 2029 served
V3 files, and each board's source from `via-keyboards`, placed with `PlaceKeys()` for the
default layout and for every choice of every group.

| | Boards |
|---|---|
| Served files that parse | **2029 / 2029** |
| Same drawing from both forms, every choice — first run | 1858 |
| — after fixing the source parser | **2029 / 2029**, rotated boards included (as unrotated) |

The first run's differences were **bugs in Nazg's KLE parser**, not in the new entry — each
also affected Vial boards and vendors' `via.json`, and each now has a unit test:

- **A property object reset what it did not name.** `{"w": 2.25}, {"c": "#777"}, "5,7"`
  (bevi) gave a 1u key; an object changes only what it names.
- **`rx`/`ry` were ignored.** They move the cursor to (`rx`, `ry`), and later rows start at
  `rx` — used without any rotation to place a block of keys, as dz60 places a layout
  alternative with `{"rx": 0.25, "y": 6.5, ...}`, which Nazg drew 18 rows too low.
- **Decals were ignored** — `{"d": true}` before a labelled key made a real key. 260 boards
  have decals inside layout options, where they can be the pivot (ISO Macro's choice 1).
- **Properties left at the end of a row leaked into the next.** bm16a's first row is a lone
  `{"w": 14, "h": 5, "d": true}`; VIA drops it, where kle-serial would make key (0,0) a
  14×5 decal.
- **Spaces in labels** — `"0,8    "` (xelus pachi), `"1 ,0"` (stratos) — dropped a key or an
  option. VIA allows them.
- **String USB ids are always hex**, as VIA reads them: `"414B"`, `"BF00"` with no `0x`
  (ogr, fallacy). Nazg read them in base auto-detect without checking the whole string was
  used, so `"414B"` silently became 414.

**Rotation** — 214 of the 2029 V3 boards rotate keys (`r`): ortho splits' thumb clusters,
Alice-style boards. `DefinitionKey` carries the angle and origin (`rotation`, `rotationX`,
`rotationY`, degrees clockwise, x/y staying the position before rotating). In the source form
`r` holds across keys and rows until the next `r`, and each key turns about the `rx`/`ry`
cluster current when it was placed; the converted form gives all three per key. The sweep,
rerun comparing angle and origin too, still finds **2029 / 2029** boards alike. A layout
choice's shift moves x/y but not the origin, as VIA's does. The view draws a rotated key
unrotated and turns the vertices it emitted about the origin — corners, outline and legends
alike — and hit-tests by turning the mouse back. Arisu and Sofle Choc, rendered from Nazg's
parse with the same formula, look as they should. **Verified in the app 2026-09-24** on the
Aquanaut, loaded with a copy of its `via.json` whose bottom row turns 6° about its left end:
the row, its legends and the view's extent are right, hover follows the tilted keys, and a
tilted key edits and reads back.

## Bundle size

*Measured 2026-09-24* on every file the live index lists, downloaded from `usevia.app`
(Node is not installed here, so VIA's build could not be run locally; the served files are
its output anyway). The 32 missing V3 files are left out.

| Set | Files | Raw | zip (per file, deflate 9) | tar.gz (solid) | tar.xz (solid, -9e) |
|---|---|---|---|---|---|
| V3 only | 2029 | 20.6 MB | 2.20 MB | 1.31 MB | **0.34 MB** |
| V2 + V3 | 3513 | 31.1 MB | 3.41 MB | 1.89 MB | **0.41 MB** |

Plus 71 KB of index files (`supported_kbs.json`, `keyboard_names.json`, `hash.json`),
included above. A V3 file is 7.5 KB median, 145 KB at most.

- **Solid compression is what matters**: the files are near-identical boilerplate, so one
  stream compressing them all together is 6× smaller than zip, which compresses each file
  alone. xz is another 4–5× below gzip.
- **Nazg already decodes xz** — minlzma is there for Vial's embedded definitions. A bundle
  of all official definitions in 0.4 MB costs no new dependency.
- V2 adds only 0.07 MB compressed. Whether to carry it depends only on whether protocol ≤ 10
  boards are worth supporting, not on size.

### Decoding it

*Measured 2026-09-24* with a Release build (MSVC `/O2`, x64) linking Nazg's own
`external/minlzma`, on Rico's desktop — **an AMD Ryzen X3D, single-threaded**: minlzma decodes
one block in one call. The container is a ustar tar, which pads the 31 MB to 35.4 MB. The
benchmark is not in the repo.

minlzma accepts **only a single-block `.xz`**, whole in memory (its README, "Limitations"),
so random access cannot come from blocks inside one stream — only from separate streams.
All three layouts:

| Layout | Size | Decode all | Decode one piece |
|---|---|---|---|
| **One solid `.xz`, V2 + V3** | **0.40 MB** | **37 ms** | — |
| One solid `.xz`, V3 only | 0.34 MB | 28 ms | — |
| One `.xz` per vendor (551) | 0.84 MB | 67 ms | median 0.07 ms, largest 5 ms |
| One `.xz` per file (3513) | 2.86 MB | 185 ms | median 0.05 ms, largest 0.7 ms |

| Parsing with nlohmann/json | |
|---|---|
| Median definition, 7 KB | 0.09 ms |
| Largest, 144 KB | 1.45 ms |
| All 2029 V3 definitions, file reads included | 449 ms |

- **About 1 GB/s** — the definitions are so repetitive that decoding is mostly copying long
  matches. Nazg builds minlzma without `MINLZ_INTEGRITY_CHECKS`, so no CRC is computed;
  these numbers are too.
- **One solid `.xz` is the format.** Splitting doubles the size or worse to save tens of
  milliseconds.
- **Decode once at start and keep it** — revised 2026-09-24, Rico. The first version decoded
  on every board open and kept nothing; but reading the manifest's count at start already
  paid for one decode, and the result — **28 MB** for the source-form bundle, 25.3 MB of
  definitions plus tar padding — is no burden on a desktop or in a browser tab. So
  `ViaDefinitionBundle` decodes once, indexes every file by path, and opening a board is a
  lookup plus a parse. **No disk cache** — there is nothing worth caching, and so no
  invalidation to get wrong.
- **Parse per board, never all at start** — half a second for nothing. A picker's handful of
  candidates costs about 1 ms together, at the largest.

**This is a best case.** Both the core speed and the X3D's large L3 cache, which holds most of
the 35 MB output the decoder copies matches from, favour it. Estimated, not measured: a
recent laptop 2–3× slower (75–110 ms), an old or low-end one 5–10× (200–400 ms), and
WebAssembly on such a laptop perhaps 0.5–1 s. Decoded once at start, even the worst is a
one-off per run. If a slow machine shows otherwise, moving that decode to the background
keeps the format, as would shipping V3 only. The margin is wide enough that no further
measurement is planned.

**WebAssembly memory is not a problem.** The 35 MB peak needs `-sALLOW_MEMORY_GROWTH=1` (or
a large `-sINITIAL_MEMORY`); wasm32 allows up to 4 GB and a desktop tab using hundreds of MB
is ordinary. The one quirk is that wasm memory never shrinks: once freed, the 35 MB returns to
`malloc`, not to the browser, and is reused. WebHID is desktop-Chromium-only, so mobile
memory limits do not apply. The web build can ship the same `.xz` as the native one — 0.4 MB
is less than a typical page's images — rather than fetch file by file as VIA does.

## VID:PID collisions in QMK

*Measured 2026-09-24* on Rico's QMK fork (`rico_forked_qmk_firmware`, branch
`the_concordia`, 2026-09-19 — master plus his boards). Every folder holding a
`keyboard.json` is a buildable keyboard; its `usb`, `manufacturer` and `keyboard_name` are
merged from each `info.json` on the way down to it, as QMK does. The files are read as hjson
(comments, trailing commas) like QMK reads them; 25 of 3762 still failed to parse and are
left out. The script is not in the repo.

**QMK does not enforce unique ids.** A quarter of its keyboards share theirs:

| | Ids | Keyboards |
|---|---|---|
| All keyboards | 2992 | 3762 |
| **Ids shared by more than one keyboard** | **257** | **1002** |
| — on `0xFEED`, QMK's placeholder vendor | 57 | 488 |
| — revisions and variants of one board | 174 | 422 |
| — unrelated boards | 26 | 92 |

- **`0xFEED`**: `0xFEED:0x0000` alone is on 175 keyboards, `0xFEED:0x6060` on 114. VIA
  refuses the vendor outright, so none of these can be in its registry — the hobby and
  handwired boards whose owners would side-load a definition.
- **Revisions and variants** are the dangerous group: one id over different layouts or
  matrices. `planck/rev1`…`rev5` are all `0x03A8:0xAE01`; `gmmk/pro` rev1/rev2 in ANSI and
  ISO are all `0x320F:0x5044`; Kyria rev1, rev2 and the Elora share `0x8D1D:0x9D9D`.
  **VIA serves 128 of the 200 non-`0xFEED` shared ids** — each with one definition standing
  for every board on it, with layout options where the boards differ only in layout.
- **Unrelated boards** come from generic USB stacks — `0x20A0:0x422D` is ps2avrGB's id, on
  13 boards, served by VIA as `ymd75.json`; `0x16C0:0x27DB` is V-USB's, on 11 — or from
  copy-paste: `fortitude60/rev1` and `keebio/nyquist/rev1` on `0xCB10:0x1156`,
  `cannonkeys/instant60` and `kbdfans/d60b` on `0xCA04:0x1600` (served as
  `instant60.json`), `ferris/sweep` and `a_dux` on `0xC2AB:0x3939`. Plugging in the board VIA
  did not pick draws it with the other's layout.

**VIA builds are QMK master.** VIA has no firmware fork — only Vial does — just `VIA_ENABLE`
and a `via` keymap in `the-via/qmk_userspace_via`. Of that userspace's 2224 keymaps (Rico's
clone, 2026-09-08) **one changes the USB identity**: `dm9records/plaid` moves off the shared
V-USB id to `0x0D39:0x0001`. None changes the manufacturer or product string. So a VIA
board reports what QMK master's `keyboard.json` says, short of a user's own build.

### What the device reports does not tell them all apart

QMK builds the USB manufacturer string from `manufacturer`, the product string from
`keyboard_name`, and bcdDevice from `usb.device_version` (set on 3737 of 3762 keyboards);
hidapi reads all three as `manufacturer_string`, `product_string` and `release_number`.
Boards on a shared id still ambiguous:

| Boards on a shared id | Total | By manufacturer + name | Adding device version |
|---|---|---|---|
| All | 1002 | 379 | **245** |
| `0xFEED` | 488 | 84 | 59 |
| Revisions and variants | 422 | 282 | 182 |
| Unrelated boards | 92 | 13 | 4 |

- **Unrelated boards separate** — the strings differ even where the ids collide.
- **Revisions often keep their name** (Planck, GMMK Pro, Kyria); the device version tells
  many of them apart, Planck's and the GMMK Pro's among them.
- **What is left is mostly harmless**: one PCB built for different controllers —
  `prkl30/feather` and `promicro`, `mechwild/obe/f401` and `f411`, `kyria/rev1/base` and
  `proton_c` — one layout, one definition fits all.
- **A few are not**: `dumbpad/v0x` and `v0x_right`, `lazydesigners/dimple/ortho` and
  `staggered` (same name, same version, different layout), `atreus/astar` and
  `astar_mirrored`, `input_club/infinity60/led` and `rev1`.

### A VIA definition's name is not the firmware's

A VIA definition has **no manufacturer**, only `name` — a label typed by whoever submitted
it, never checked against the firmware, and never used for matching: VIA matches on VID:PID
alone. Against QMK's `keyboard_name` for the same id, across the 2061 VIA ids:

| | Ids | |
|---|---|---|
| Identical | 941 | 46% |
| Differs only in case (`aleth42` / `ALETH42`) | 118 | 6% |
| Differs only in spaces and punctuation (`Titan 60` / `Titan60`) | 103 | 5% |
| VIA name is manufacturer + name (`AEboards AEGIS`) | 273 | 13% |
| One contains the other (`sweet16 v1` / `Sweet16`, `Zinc` / `Zinc rev.1`) | 366 | 18% |
| Different (`Crkbd` / `Corne`, `1up60rgb` / `1UP RGB Underglow PCB`) | 117 | 6% |
| Id not in QMK — board removed, id changed, or one of the 25 unparsed files | 143 | 7% |

So the name can rank candidates, loosely (lowercase, alphanumerics only, containment), but
not match them.

### How VIA handles duplicates: it does not allow them

One VID:PID gets exactly one definition, at every level:

- **Registry**: the build fails when two definitions claim one id in one version, so a
  second board on an id cannot get in. `0xFEED` is refused.
- **Side-loading**: IndexedDB is keyed by VID:PID, so side-loading a second definition for
  an id overwrites the first — and overrides the registry's.
- **At runtime**: a connected board is looked up by VID:PID alone. The HID strings are not
  read, nothing is asked, nothing is remembered per device. Of two boards on one id, one is
  drawn wrong.
- The dynamic name below is the only nuance, and it changes the name, not the layout.

### VIA's own answer: a name read from the board

Three Cipulot definitions (`ec_60x`, `ec_65x`, `hybrid_hhkb`) give `name` as an object
rather than a string — a **dynamic name**, new in the app (`src/utils/definition-name.ts`,
`src/store/definitionNameSlice.ts`, present at `935106a`):

```json
"name": { "options": ["EC60X | EC60X-SE", "DC60"],
          "content": ["id_board_variant", 0, 245] }
```

`content` is a custom-menu value reference — channel 0, value id 245 — which the app reads
from the board on connect and uses as an index into `options`. One definition, one id,
several boards, and **the board says which it is**. It only names the board; the layout is
the same definition's. Nazg's parser keeps `name` only when it is a string, so such a
definition loads with an empty name.

### Vial's keyboard UID: unique, enforced by CI

Vial boards do not need any of this — the definition comes from the board — but they carry
an identity VIA lacks. `VIAL_KEYBOARD_UID` is 8 bytes in the Vial keymap's `config.h`, read
with `CMD_VIAL_GET_KEYBOARD_ID`, and generated from 8 random bytes by
`util/vial_generate_keyboard_uid.py`.

- **vial-qmk's CI enforces uniqueness**: `util/ci_vial_verify_uid.py` runs on every build
  ("Verify Vial UID is unique per-keyboard" in `.github/workflows/ci.yml`) and fails on a
  keyboard with a `vial.json` but no UID, or on two keyboards sharing one.
- **Run on Rico's fork** (`rico_forked_vial-qmk`, 2026-08-23): **592 keyboards, no real
  duplicate.** The one it reports, `mario`, is a false positive of running it on Windows: it
  splits paths on `/keymaps/`, so with backslashes the board's `vial` and `default` keymaps
  count as two keyboards.
- **The template UIDs are not reused.** vial-gui warns on four known example UIDs and one
  example prefix (`util.py`, `EXAMPLE_KEYBOARDS`), since people copy the example keyboards;
  in the repository they appear only in `vial_example/*`.
- **It names a model, not a unit** — two Model Fs report the same UID — and uniqueness holds
  only in the repository: an out-of-tree build copying another board's `config.h`
  duplicates it.
- **vial-gui uses it as a safety check** twice: a saved layout (`.vil`) refuses to load onto
  a board with another UID (`keymap_editor.py`), and the flasher compares the `.vfw` file's
  UID with the board's (`firmware_flasher.py`).

For Nazg it is the key for anything saved per Vial board model — saved layouts, and a
per-device choice should overriding an embedded definition ever be allowed.

## Proposed design — not decided

*Written 2026-09-24 as a basis for discussion. Only what is marked agreed has been agreed
with Rico; the rest is proposed.*

The aim is to make adding a definition, especially a user's own, fast. VIA's delay comes
from **its policy, not its technology** — QMK master, then VIA's userspace, then a human
review of the definition (over a week, in Rico's experience). A definition only has to reach
Nazg, not VIA, so none of that applies.

### Official VIA definitions: bundle, refresh on request

- **Ship a snapshot of VIA's built output with each Nazg release** — the converted `v2/` and
  `v3/` files plus `supported_kbs.json`. **0.4 MB as one solid `.xz`** holding a tar, for
  all 3513 files — see "Bundle size" and "Decoding it" above. Works offline and on first
  launch, already validated, GPL-3.0 compatible.
- **Later, a user-triggered "Update VIA definitions"** that downloads into the app's data
  folder beside the bundle, never over it. HTTPS in C++ is a new dependency, so this waits;
  until then a Nazg release brings new definitions.
- **No silent background fetch.** Nazg stays a local tool, and the traps above are real.

### User definitions: a local library

Today a definition is a remembered file path in `imgui.ini`, which breaks when the file
moves. Replace it by **a library folder in the app's data directory**, holding two kinds of
entry:

- **Imported** — copied into the library. The normal case; survives the original's deletion.
- **Linked** — Nazg follows a file on disk and reloads it when it changes. For a designer
  iterating on a `via.json`; roughly what the `imgui.ini` path does today, made explicit.

Both the source and the converted form are accepted, as VIA does. The file is stored as
given; how, and the index beside it, are under "Storage" below.

### Sharing: three levels, cheapest first

**Who this is for.** Many commercial boards ship without official VIA support. The registry
requires QMK master first, and getting a board merged there is long and tedious — rounds of
review with QMK's maintainers — so vendors skip it: they build firmware from their own QMK
tree and hand customers a `via.json` to side-load. Some GeonWorks boards with PCBs by
Gondolindrim are examples. In VIA such a board stays second-class for good: "Load draft
definition" sits behind a hidden Design tab, and the file lives in one browser's IndexedDB —
gone with the site data, repeated on every other machine. These boards, and the 488 QMK
keyboards on `0xFEED` VIA refuses, are what the levels below serve.

1. **By file or URL — agreed with Rico 2026-09-24, first.** Import from a file (dropped on the
   window too) or from a URL — vendors often publish on GitHub — and "Export". Imported once
   into the library, matched by VID:PID on every connect. It already beats VIA for these
   boards. Part of it, not extras:
   - **An empty state that says what to do**: a board with no definition shows "This board
     is not in VIA's registry. Your vendor probably provides a `via.json` — import it."
     Most users do not know side-loading exists.
   - **URL import**, which waits on HTTPS in C++ like the bundle refresh.
2. **A community repository** (say `mymakercorner/nazg-definitions`), **no QMK-master
   requirement**, anything that parses — boards VIA already serves included, since refusing
   them would take a manual check. Shipped as a second `.xz` bundle beside the official one,
   read by the same code; choices point at it as `community:...`. **Seeded by Rico first,
   opened to vendors and designers later** — only once Nazg is visible enough for them to
   care, which is far from certain. Seeding solves the cold start: an empty repository
   attracts nobody, and a few dozen boards make Nazg useful to their owners from day one.
   Files Rico did not write need three things from the start:
   - **The right to redistribute.** A file already under an open licence — a `via.json` in a
     vendor's public GPL QMK tree is GPL — goes in with its source recorded. Otherwise ask the
     vendor ("may I include your `via.json`?"), which costs them far less than submitting
     and is a first contact for later. Otherwise leave it out: a takedown dispute early on
     costs more than one missing board.
   - **A status: verified on hardware, or unverified** — from the vendor's published file,
     Rico not owning the board. The picker shows it. Where the firmware source is public,
     cross-check it: matrix size against its `keyboard.json`, layout keys inside the matrix
     — mostly automatic, in the validator.
   - **Provenance per file**: source URL, licence, commit or download date, who added it,
     status. It makes re-checking after a vendor update, **handing a file over** to a vendor
     who later maintains it, and removing one on request all straightforward.

   First boards: commercial, with a published definition, **absent from VIA's registry**
   (checked automatically against the `via-keyboards` clone), ideally with public firmware.
   Few at first — each is Rico's to maintain until its vendor takes it over. Rico's own
   boards are not candidates; most are not open source.

   Merge rules for when it opens: a **new file** is merged automatically if it parses with
   Nazg's parser, has a VID:PID and a name, stays under a size limit and is signed off (DCO,
   as in Nazg — which also records the right to distribute). **A change to an existing file**
   is merged automatically when it comes from the file's author, reviewed by hand otherwise —
   the guard against squatting and hostile edits. Several definitions per VID:PID are
   allowed; vendors' made-up VIDs will collide, and choices handle it. Someone still reverts
   spam, settles "that is my VID" and keeps CI running — small, permanent work.

   Order of work: the repository and its provenance format can start any time — they are
   files. Nazg reading them needs a **command-line validator** (the CI, and useful on its own
   to anyone writing a definition — a headless target reusing `NazgKeyboardDefinition`),
   then the second bundle. Both after level 1.
3. **The board carries its definition**, as on Vial — the only route with zero delay and zero
   collisions. For a VIA board that means firmware serving it, which is step 5's
   device-served descriptors. **The commercial boards are its strongest case**: their vendors
   already build their own firmware, so adding a module costs them far less than QMK master.
   QMK's community modules may let it ship without forking QMK — to be checked.

**Trust.** A definition is data, but level 2 means parsing files written by strangers: the
parser must hold up against hostile input (size limits, deep nesting, fuzzing). Today a
definition only draws; with step 5's menus it will also describe values written to the board,
still only when the user operates them.

**Not planned: "Import from VIA".** VIA's desktop app keeps side-loaded definitions in its
Chromium profile's IndexedDB (see "The native application"), so a one-click migration would
suit exactly these users — but it means reading LevelDB holding values in V8's own
serialization format, which Chromium does not keep stable. Re-importing the vendor's
`via.json` is nearly as easy.

### Collisions and invalid files

- **An invalid file is rejected at import**, by the same parser that loads boards, with its
  error shown. Nothing broken enters the library.
- **Collisions are normal, not an error.** Unlike VIA's registry, allow several definitions
  per VID:PID — a quarter of QMK's keyboards share their id (see "VID:PID collisions in
  QMK"), and one PCB often has several definitions.
- **Priority: user > community > official**, the order VIA overlays in, used to order the
  candidates — not to pick silently among them.
- **Bind by hand** — for a definition whose VID:PID is wrong, or boards sharing one.

### Choosing a definition on connect

With more than one candidate, **ask once and remember the answer per device** — the main
path, not an edge case: manufacturer, product and device version still leave 245 of QMK's
1002 colliding boards ambiguous.

A **choice** is a remembered answer to "this board is connected: which definition draws
it?", keyed by what the device reports: VID:PID, manufacturer and product strings, device
version (`release_number`), and serial number when the firmware sets one — QMK usually does
not. The OS device path is not used; it changes with the port. Example: a CannonKeys
Instant60 and a KBDfans D60B are both `0xCA04:0x1600`. Plug in the D60B, pick the imported
D60B definition; plug in the Instant60, pick VIA's `instant60.json`. Their strings differ,
so each then connects without a question.

On connect:

1. a choice matching the device → its definition;
2. otherwise exactly one candidate → that one;
3. otherwise ask, candidates ranked by priority, then by how closely the definition's `name`
   matches the product string (lowercase, alphanumerics only, containment).

- **Always show which definition is in use** next to the board, with **"Change
  definition…"** — the same picker, current one marked — and **"Forget choice"**, so Nazg
  asks again. Changing overwrites the one choice. The action is there **even with a single
  candidate**: VIA serves one definition per id, so a D60B owner alone gets only
  `instant60.json`, and the picker needs an "Import a definition…" entry.
- **A wrong choice costs nothing to undo.** The keymap lives in the board by position; a
  definition only changes how it is drawn. The exception is **layout options**, bits whose
  meaning only the definition gives — they are written only when the user changes one
  explicitly.
- **Show each candidate's layout drawn small**, not a list of names — the names are
  unreliable, ANSI against ISO or ortho against staggered is obvious at a glance. Cheap:
  about 1 ms of parsing per candidate, once, and ImGui draws them like any other widget.
- **"Press a key to check" does not work on most VIA boards.** `id_switch_matrix_state`
  returns all zeroes on mainline QMK unless the build defines `VIA_INSECURE`, or
  `SECURE_ENABLE` and is unlocked ([via-vial-commands.md](via-vial-commands.md)). Offer it
  only where the board answers.
- **No automatic correction.** The signals are too weak to overrule a person, and a
  definition that switches by itself is worse than one that is plainly wrong.
- Two boards reporting identical values — `lazydesigners/dimple/ortho` and `staggered` —
  share one choice; "Change definition…" is the way out.

### Replacing a user definition

- **Re-importing for the same VID:PID and name offers "Replace" or "Keep both"**, Replace by
  default.
- **Keep the previous version as one backup**, so a bad edit can be undone. No full history;
  the designer's own git has it.
- **Replacing is safe for the keymap** — it lives in the board, positionally; a definition
  only changes how it is drawn. The catch: **reordered layout-option groups** make the
  board's saved option value point at a different choice. Warn when the option groups
  differ.
- **Linked entries never need replacing** — they follow the file.

### Storage

*First step implemented 2026-09-24* — `library/NazgDefinitionLibrary.*`. **Naming, agreed
with Rico**: *user definitions* are the ones the user imports, *official definitions* VIA's
bundle — community ones will sit between — and the files on disk say so: in Nazg's data folder
(`SDL_GetPrefPath()`), one folder, `user_definitions/`, holds everything — `index.json`, with
`format` and `nextId`, beside the files `<id>-r<revision>.json`, stored byte for byte — so it
is backed up, moved or zipped whole. Import after parsing,
removal, lookup by VID:PID (first entry wins until choices exist), orphan clean-up, and an
unreadable index left untouched. `Main.cpp` imports the paths older builds kept in `imgui.ini`
once. The index has no `choices` yet — adding them later needs no format change, an absent key
meaning none. The layout below says `library.json` and `definitions/`; read those as
`user_definitions/index.json` and `user_definitions/`.
Still to come from this section: choices, linked entries, revisions with a backup,
export/import. `imgui.ini` moved here the same day: in the data folder, not the working
directory, so every build and every way of starting Nazg shares one set of settings; an
`imgui.ini` where Nazg starts is copied in the first time.

**One layout for both builds**; only where it lives and how it persists differ, and that
stays in `Main.cpp` with the rest of the platform code. The library code gets a folder path
and does plain file I/O, knowing neither SDL nor Emscripten.

| What | Written by | Size |
|---|---|---|
| Official bundle | the release, never the app | 0.4 MB |
| Updated bundle, later | "Update VIA definitions" | 0.4 MB |
| User definitions | import, Replace | a few KB each |
| `library.json` — entries and choices | the app | 1–3 KB typical, 30–50 KB for a designer with a hundred definitions |
| `imgui.ini` | ImGui | tiny |

**No cache on disk** — see "Decoding it".

**Native.** The bundle is read-only next to the executable (`SDL_GetBasePath()`,
`Resources/` on macOS), never written — it may sit under Program Files or in a signed
bundle. Data goes in `SDL_GetPrefPath("mymakercorner", "Nazg")`: `%APPDATA%\mymakercorner\Nazg\`,
`~/Library/Application Support/...`, `~/.local/share/...`. Reinstalling or upgrading does
not touch it.

```
library.json            the index: entries + choices
definitions/7-r2.json   entry 7, current revision, stored byte for byte as imported
definitions/7-r1.json   its one backup, after a Replace
updates/                the downloaded bundle, later
imgui.ini               moved here; ImGui defaults to the working directory
```

- **Definitions are stored byte for byte**, in whichever form came in, validated by parsing
  at import — never converted (see "Why VIA converts"). An export gives back what came in,
  and a better parser applies to old imports.
- **Files are named by entry number and revision**, never by board name or VID:PID, and
  **never modified once written**. A Replace writes the new revision, then the index, then
  deletes anything older than the previous revision. A crash leaves at worst an orphan
  file, deleted at the next start because no entry points to it.
- **A linked entry has no copy**, only its path. An edit that no longer parses shows the
  error and keeps the last version that did, in memory only. A file that has gone shows as
  missing, not dropped.
- **`library.json` is the only file that changes.** Read whole at start and kept in memory,
  written whole to a temporary file and renamed over the old one, so a crash leaves the old
  index or the new, never a mix. Plain JSON through nlohmann — no database; readable in an
  editor when something goes wrong.

```json
{
  "format": 1,
  "nextId": 8,
  "definitions": [
    { "id": 7, "kind": "imported", "revision": 2, "previousRevision": 1,
      "origin": "C:/Users/Rico/Downloads/d60b.json", "added": "2026-09-24T18:40:00Z",
      "vendorId": "0xCA04", "productId": "0x1600", "name": "D60B" },
    { "id": 5, "kind": "linked", "path": "D:/Keyboards/concordia/via.json",
      "added": "2026-09-20T10:02:00Z",
      "vendorId": "0x...", "productId": "0x...", "name": "The Concordia" }
  ],
  "choices": [
    { "vendorId": "0xCA04", "productId": "0x1600", "manufacturer": "KBDfans",
      "product": "D60B", "release": "0x0001", "serial": "", "definition": "user:7" },
    { "vendorId": "0xCA04", "productId": "0x1600", "manufacturer": "CannonKeys",
      "product": "Instant60", "release": "0x0001", "serial": "",
      "definition": "official:v3/3389265408" }
  ]
}
```

- **`format`** versions the file's layout, for migrating it later. **`nextId`** only grows:
  a deleted entry's number is never reused, so a stale `"user:7"` cannot come to point at
  another definition. It restarts at 1 only when `library.json` is gone — a new machine,
  cleared site data — and then the choices pointing at old numbers are gone with it, which is
  one reason entries and choices share a file.
- **Entries copy the VID:PID and name** from their definition, to list and match without
  opening every file, and so a linked entry whose file is gone still shows by name.
- **Choices point at a user entry by number, or an official definition by its path in the
  bundle** — keyed by VID:PID, so a newer bundle keeps it valid.
- **Entries do not record device values.** The choices already do; a `device` block per
  entry would duplicate them.
- **Importing an exported library into a non-empty one renumbers** the incoming entries from
  `nextId` and rewrites the choices that point at them.

**WebAssembly.** The same layout, in Emscripten's virtual file system mounted on **IDBFS**
(IndexedDB); `Main.cpp` mounts it at start and calls `FS.syncfs` after each write — a few
KB. WasmFS with the browser's private file system (OPFS) is newer and harder, particularly
around threads, and not needed at these sizes. SDL's pref path in a web build is only a path
in that virtual file system; whether SDL3 mounts anything persistent there is to be checked.
The bundle is a static file beside the `.wasm`, its version in its name so the browser's
HTTP cache handles it. Web-only limits:

- **Eviction**: the browser may evict the data under storage pressure unless the app
  obtains `navigator.storage.persist()`, and clearing site data wipes the library. **Export
  of the whole library** matters more on the web than on native.
- **Linked entries are native-only.** There are no paths; Chromium's File System Access API
  could keep a handle in IndexedDB but asks permission again each session. Web users
  re-import.
- Quota is not a limit: hundreds of MB, against a few KB per definition.

### Decisions to take

- **Agreed**: level 1 first. The community repository (level 2) is wanted, run and seeded
  by Rico, opened to vendors later.
- **Proposed by Rico — implementation starts with the official definitions**, before any
  community work: ~2000 boards with no user action, no new storage, and it builds what the
  rest reuses. Steps: the converted-form entry in the parser (dynamic `name` accepted), with
  two tests — every V3 file in the bundle parses, and each board with a source in
  `via-keyboards` gives the same keys and options from both forms, up to a translation
  (**done 2026-09-24**, see "The converted-form entry"; the two checks run as a scratch
  program until the bundle is in the repo, ISO Macro as a unit test meanwhile); then
  the bundle (solid `.xz` of a tar, a small ustar reader, lookup by id and protocol — **done
  2026-09-24**: `adapters/via/NazgViaBundle.h`, XZ decoding shared with Vial in
  `adapters/NazgXz.h`; against the full 3513-file bundle every file comes back byte for byte,
  a lookup taking 41 ms on Rico's desktop); then wiring (`SDL_GetBasePath()` in `Main.cpp`,
  bundle used when no remembered file — **done 2026-09-24**, verified on Rico's Phoenix
  Project No 1, which is in VIA's registry; the bundle is still placed beside the executable
  by hand, and inflated on the main thread, which a background decode could fix later; since
  2026-09-24 it is inflated once at start and kept, see "Decoding it").
- **Agreed 2026-09-24 — how the bundle is produced.** The no-generator rule is about tools used
  rarely (Rico); refreshing VIA's definitions is a regular job, so it has one:
  `tools/update_via_bundle.py`, **Python, standard library only** — `urllib`, `tarfile`,
  `lzma` (single-block XZ, which minlzma needs), `json` — so it runs as is on a developer's
  machine or a GitHub Actions runner.
  - **Source: the GitHub repository, not usevia.app** — a commit to pin, one tarball to
    download, GPL data rather than a web app's hosting. **No conversion**: the source files go
    in byte for byte, since Nazg's KLE parser draws every board as VIA's conversion does. The
    source form is also smaller: **3513 definitions in 284.5 KB**, against 386 KB converted.
  - **The bundle is not committed; the commit is.** `resources/via-keyboards.commit` pins
    `the-via/keyboards` (first at `9e3e9f4`, the commit the registry sweep verified). The tool
    downloads that commit's tarball once — `codeload.github.com`, checked against the commit
    id `git archive` writes in the tar's pax header, so no GitHub API call and no rate limit —
    and writes `build_resources/via_definitions.tar.xz`, byte-reproducible: sorted names,
    fixed metadata, a `manifest.json` naming the commit. `--update` moves the pin to `master`
    and lists the boards added, removed and changed; `--from-dir` reads a local clone.
  - **V2 included** (Rico): 1484 V2 definitions beside the 2029 V3 ones.
  - **Run by hand** once per checkout (Rico's option (a)); the build copies the bundle beside
    the executable when it exists (`tools/CopyIfPresent.cmake`), releases ship it, and
    `via_bundle_contents` parses every definition in it — all 3513 pass — skipping when there
    is none.
- The board in VIA's registry to verify on is Rico's Phoenix Project No 1 (`0x21C0:0x9901`).
- **Proposed, backed by measurement**: the bundle as one solid `.xz` of a tar, decoded per
  connect; the data directory from `SDL_GetPrefPath` in `Main.cpp`, handed down as a path.
- **Proposed**: choices keyed on VID:PID plus the HID strings, device version and serial, in
  `library.json` with the entries — layout above.

## Open for the next part of the study

- How vial-gui and other third-party clients source VIA definitions, if they do.
- HTTPS in C++ for the later refresh and import-from-URL: which library, and its cost.
- QMK community modules: whether a board can serve its definition through one without a
  QMK fork (level 3), and how mature they are.
- The community repository's provenance format and validator checks, when it starts.
- Whether SDL3 makes its pref path persistent in an Emscripten build, or `Main.cpp` must
  mount IDBFS itself.
- Dynamic names: whether Nazg reads them (a custom-menu value read on connect), and whether
  the same mechanism is worth borrowing to let a board pick among several definitions.
- V2 definitions: needed only for protocol ≤ 10 boards; what differs from V3 beyond
  `lighting`/`customFeatures`/`customMenus`.
