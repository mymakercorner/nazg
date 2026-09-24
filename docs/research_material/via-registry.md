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
  of all official definitions in 0.4 MB costs no new dependency; only the container inside
  the stream (a tar, or one JSON map keyed by id) is to be chosen.
- **Decompressed, it is 31 MB in memory**, parsed per board on demand — no reason to parse
  all 3513 at start.
- V2 adds only 0.07 MB compressed. Whether to carry it depends only on whether protocol ≤ 10
  boards are worth supporting, not on size.

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

## Proposed design — not decided

*Written 2026-09-24 as a basis for discussion. Nothing here is agreed.*

The aim is to make adding a definition, especially a user's own, fast. VIA's delay comes
from **its policy, not its technology** — QMK master, then VIA's userspace, then a human
review of the definition (over a week, in Rico's experience). A definition only has to reach
Nazg, not VIA, so none of that applies.

### Official VIA definitions: bundle, refresh on request

- **Ship a snapshot of VIA's built output with each Nazg release** — the converted `v2/` and
  `v3/` files plus `supported_kbs.json`. **0.4 MB as one `.xz`** for all 3513 files — see
  "Bundle size" below. Works offline and on first launch, already validated, GPL-3.0
  compatible.
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
given, with a small metadata record beside it: origin, import date, bound devices.

### Sharing: three levels, cheapest first

1. **By file or URL** — "Import from URL" (a GitHub raw link, a gist) and "Export". A
   designer publishes the file with the firmware; users import it in one click. No
   gatekeeper, no infrastructure.
2. **A community repository** (say `mymakercorner/nazg-definitions`): PRs validated
   automatically by CI running Nazg's own parser, no QMK-merged requirement, merged by rule
   rather than by a person. Nazg reads it like the VIA snapshot. Workable, but it needs an
   owner — moderation, bad-faith submissions, VID:PID squatting.
3. **The board carries its definition**, as on Vial — the only route with zero delay and zero
   collisions. For a VIA board that means firmware serving it, which is step 5's
   device-served descriptors. The long-term answer for boards Rico designs.

Proposed: level 1 now, level 3 as the long-term answer, level 2 only if users ask for it.

### Collisions and invalid files

- **An invalid file is rejected at import**, by the same parser that loads boards, with its
  error shown. Nothing broken enters the library.
- **Collisions are normal, not an error.** Unlike VIA's registry, allow several definitions
  per VID:PID — a quarter of QMK's keyboards share their id (see "VID:PID collisions in
  QMK"), and one PCB often has several definitions.
- **Priority: user > community > official**, the order VIA overlays in. With more than one
  candidate, ask once and **remember the choice per device** — the main path, not an edge
  case: manufacturer, product and device version still leave 245 of QMK's 1002 colliding
  boards ambiguous. Those three HID values are worth recording with a user definition when
  it is bound, since a VIA definition carries no manufacturer and a `name` matching the
  firmware's only half the time; against the official ones they can only rank candidates.
- **Check a definition against the connected board** where the protocol allows — layout keys
  outside the matrix, say. How much VIA can confirm is to be checked against
  [via-vial-commands.md](via-vial-commands.md); probably not much.
- **Bind by hand** — for a definition whose VID:PID is wrong, or boards sharing one.

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

### Decisions to take

- Whether a community repository (level 2) is wanted at all, and who would run it.
- Bundle format: the `dist/` tree as files, or one archive — measured below; one `.xz`
  looks right.
- Where the data directory is, given SDL calls stay in `Main.cpp` (`SDL_GetPrefPath` there,
  handed down as a path).
- What "remember the choice per device" keys on, and where it is saved — `imgui.ini` holds
  UI settings today; a library needs its own file.

## Open for the next part of the study

- How vial-gui and other third-party clients source VIA definitions, if they do.
- HTTPS in C++ for the later refresh and import-from-URL: which library, and its cost.
- Dynamic names: whether Nazg reads them (a custom-menu value read on connect), and whether
  the same mechanism is worth borrowing to let a board pick among several definitions.
- V2 definitions: needed only for protocol ≤ 10 boards; what differs from V3 beyond
  `lighting`/`customFeatures`/`customMenus`.
