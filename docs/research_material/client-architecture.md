# Multi-Protocol Configurator Client — Architecture Notes

*Drafted 2026-09-20. Design thinking only; nothing has been built.*

## Goal and priorities

A cross-platform configurator client that:

1. **Supports other people's keyboards well** — generic VIA and VIAL support is the primary
   value, since that is where the install base is
2. **Keeps XAP in view** as a future backend, without designing against its unfinished spec
3. **Lets custom features plug in** on equal footing with stock ones

Non-goals: ZSA (compile-time model, wrong category) and KMK (Python firmware).

ZMK Studio is **not ruled out** — it is a legitimate future backend, but a very low priority
while there is no hardware to dogfood against.

## The key observation

Goals 2 and 3 are **the same requirement**. Both need the client driven by **declarative
capability descriptors** rather than hardcoded feature knowledge. Get that right and custom
features and a future XAP backend arrive through the same door.

The corollary: in the common case they cost nothing to support. Most VIA/VIAL firmwares
implement no custom features at all, so descriptor-driven UI is not a tax on the 90% case —
it is just a clean way to build the 90% case that happens to make the 10% free.

## Layering

```
UI layer            renders from the capability model; knows nothing about protocols
    |
Capability model    "this board has: <typed fields, widgets, groups>"
    |
Protocol layer      VIA  |  VIAL  |  (XAP later)
    |
Transport           async request/response + unsolicited messages
```

The **capability model is the pivot**. Everything feeds into it:

| Source | Path in |
|---|---|
| VIA V3 `menus` | Already a widget descriptor — feeds in directly |
| VIAL embedded definition | Same |
| **Your custom features** | Descriptors you author — identical path, no special case |
| XAP (later) | Its hjson spec is already a descriptor format; generate from it |

## Descriptor vocabulary

Make it a **superset of VIA's V3 menus, with typed parameters in the ZMK style**:

- `bool` -> checkbox
- `range{min, max, step?, unit?}` -> slider or spin box
- `enum{named members}` -> dropdown
- `keycode` -> keycode picker
- `layer_id` -> layer picker
- `colour` -> colour picker
- `group` -> nesting and layout

This is not invention — it is the union of three designs that already exist
([VIA](via.md#extending-with-custom-features) supplies the widget set,
[ZMK](zmk-studio.md#extending-with-custom-features) the typed-parameter semantics, and it
covers what [XAP](xap.md#extending-with-custom-features) would need if it ever gains
`min`/`max`).

**ImGui fits this well.** Immediate mode means walking the descriptor tree each frame and
emitting widgets — descriptor-driven UI is awkward in retained-mode toolkits and natural
here.

## Ship your own descriptors from the device

Do not hardcode your own boards into your own client. Put the descriptor blob **in the
firmware** and pull it over a custom raw HID command — the same mechanism VIAL uses for its
definition.

Consequences:

- Your boards become self-describing in the same way VIAL boards are
- The client holds zero per-board knowledge
- A firmware update that adds a feature makes it appear in the UI **with no client release**

That is the property worth taking from GP2040-CE, achieved over a transport that costs
nothing. See [README.md](README.md) for why the HTTP route was rejected.

## The one non-obvious thing to get right at the start

**Model the protocol layer as asynchronous with correlation IDs, even though VIA does not
need it.**

| | Model |
|---|---|
| VIA / VIAL | Strictly synchronous request/response |
| **XAP** | **Token on every message** + **broadcasts** (log messages, secure-status changes) |
| ZMK Studio | Request / RequestResponse / **Notification** |

A synchronous core — "send bytes, block, read bytes" — means reworking the spine of the
application to add XAP later. Token-correlated async costs almost nothing on VIA (one
trivial correlation layer with a single outstanding request) and removes the rework entirely.

Two secondary seams, both cheap:

- **Capability gating as a first-class concept.** XAP uses per-subsystem capability bitmasks
  and versioned subsystems; VIA/VIAL barely need it. If the model can already express
  "this route exists / does not," XAP slots in.
- **Abstract the transport at "request/response," not "32-byte VIA packet."** XAP is also raw
  HID but with its own usage page (`0xFF51` / usage `0x0058`), its own framing and route
  addressing. Abstract at the right level and XAP is a new *protocol*, not a new *transport*.
  The distinct usage page also means XAP and VIA interfaces can coexist on one keyboard.

## Build seams, not abstractions

XAP's spec format is explicitly **not final** (issue #15601, open since Dec 2021), so anything
designed *against* it today aims at a moving target. The three seams above are
protocol-agnostic good design that merely happen to make XAP cheap later — defensible even if
XAP never ships. A speculative XAP abstraction layer would not be.

## The real cost of generic support: definition sourcing

Not extensibility. Knowing what the keyboard *is*.

| | Definition source | Cost |
|---|---|---|
| **VIAL** | Pulled from the device, always | **Near zero.** Every VIAL board works, including ones not yet designed. |
| **VIA** | External registry, or user side-loads JSON | **The actual work.** |

VIA options, each with a downside:

- **Bundle a snapshot** of the definitions repo — offline-capable, goes stale, and inherits a
  distribution/licensing question worth checking before shipping
- **Fetch on demand** — always current, needs network, depends on someone else's repo layout
- **User side-loads** — always works, worst UX, and it is the thing that motivated this
  investigation in the first place
- Plus **V2 vs V3 definition formats** to handle

Counterintuitive result: **VIA is the expensive backend and VIAL is the cheap one** — the
opposite of what "VIAL is a fork" suggests.

## Suggested build order

VIAL is VIA-derived, so this is **one backend with a VIAL branch**, not two backends.
Shared: transport, 32-byte packet framing, most command IDs, the keymap model. Divergent:
definition retrieval, VIAL's extra feature commands, the unlock flow.

1. **VIAL first**, despite the smaller install base. Self-describing devices mean the whole
   pipeline can be built and tested end-to-end without touching the registry problem, and
   you dogfood on your own boards from day one.
2. **VIA second**, reusing most of it, with definition-sourcing as its own scoped work item.
3. **Your custom features** via device-served descriptors — by this point they are just
   another descriptor source.
4. **XAP** and **ZMK Studio** as independent future decisions with their own triggers
   (see the watch-signals table in [README.md](README.md)).

Doing VIA first would front-load the messiest problem before there is a working client to
validate against.

## Implementation stack

**Recommendation: C++ + Dear ImGui + hidapi.**

### Why immediate mode is architecturally right, not merely familiar

- **It fits descriptor-driven UI.** Retained-mode toolkits (Qt, GTK, DOM) require constructing
  and tearing down widget trees whenever the descriptor changes — which happens every time a
  different keyboard is plugged in. Immediate mode just walks the tree each frame.
- **The centrepiece is custom-drawn regardless.** The dominant visual element is a rendered
  keyboard with correctly shaped, positioned and rotated keys. No toolkit provides that; you
  custom-paint in Qt or on a canvas in a web frontend too. `ImDrawList` is very good at it, so
  the usual "prettier widgets" argument largely evaporates — stock widgets are just chrome
  around a canvas you are drawing anyway.
- **Empirical evidence, from two different stacks.** VIA's client is Electron/TypeScript;
  VIAL's is **Python + Qt**. An existing C++/ImGui tool outperformed *both* on speed and
  stability. Two unrelated stacks with the same result points at what they share — an
  interpreted or JIT'd runtime, and retained-mode widget trees being rebuilt as the UI
  changes — not at any one technology being bad.

### Fixing the known weakness: it looks dated by default

Most of that is a handful of defaults, not the library. In order of impact:

1. **The font — roughly 80% of it.** The default ProggyClean bitmap at 13px is the tell.
   Load a real TTF (Inter, Source Sans 3, Noto Sans) and build with **`imgui_freetype`**
   rather than the default stb_truetype rasteriser, for proper hinting and antialiasing.
2. **DPI scaling.** Scale font size by monitor DPI and call `ImGuiStyle::ScaleAllSizes()` to
   match.
3. **Breathing room.** Raise `FramePadding`, `ItemSpacing`, `WindowPadding`; set
   `FrameRounding` / `GrabRounding` to ~4; drop most borders; replace the default palette.
4. **Use tables for layout.** `BeginTable()` with `ImGuiTableFlags_SizingStretchProp` instead
   of `SameLine()` with magic offsets. This matters doubly for generated UI — alignment should
   fall out of the descriptor structure rather than be hand-tuned per field.
5. **An icon font** merged into the atlas (Font Awesome or Lucide, via IconFontCppHeaders).

Roughly two days of work, against weeks for a rewrite in an unfamiliar stack.

### Transport and threading — put the seam below the protocol

```
                         common design          recommended
UI                       main thread            main thread
Capability model         main thread            main thread
Protocol (VIA/VIAL/XAP)  -- worker thread --    main thread
Transport (hidapi)          worker thread    -- worker thread --
```

The queues carry **opaque byte buffers**, not protocol messages. The thread knows nothing
about VIA, VIAL or XAP — it blocks on hidapi read/write and moves reports. Correlation stays
in the protocol layer, where it belongs (XAP matches on its token; VIA on its
single-outstanding discipline).

Three benefits:

1. The protocol layer becomes **platform-independent** — a web port swaps the transport and
   leaves protocol code untouched.
2. **One transport serves all three protocols.** It is a dumb pipe.
3. The protocol layer becomes **testable without hardware or threads** — feed it a fake queue.

**The honest cost:** you lose blocking sequential protocol code. "Read the whole keymap" as a
loop of blocking request/response is very readable; as a main-thread state machine it is not.
The standard answer is **C++20 coroutines**, which make async sequences read almost like
blocking code.

**Therefore:** introduce the seam now (cheap, good design independently, and what makes an XAP
backend drop in later), but defer moving the protocol off its worker thread until you either
commit to the web build or move to C++20. Restructuring working protocol code to enable a
hypothetical Chromium-only web build would be optimising for the secondary goal.

### Web build (Emscripten) — secondary priority

**ImGui itself compiles to WebAssembly** via Emscripten and renders to a WebGL2 canvas.
The toolkit is not the blocker. Two things are:

**hidapi does not work, and the obvious workaround is blocked.** Its backends are Windows
`hid.dll`, macOS IOKit, Linux hidraw/libusb — none exist in the browser sandbox. The tempting
path (hidapi's libusb backend -> libusb's real Emscripten/WebUSB backend -> WebUSB) is closed:
WebUSB enforces a protected-interface-class blocklist that includes **HID (0x03)**, and
`claimInterface()` returns `SecurityError`. Chrome blocks it precisely because **WebHID** is
the sanctioned route — and hidapi has no WebHID backend upstream.

So you write a **WebHID transport**: not a hidapi backend, a second implementation behind the
transport interface. The API surface is small (`requestDevice`, `open`, `sendReport`,
`sendFeatureReport`, `receiveFeatureReport`, and an `inputreport` event), bridged from C++ via
`EM_JS`/embind into the same queues.

Two WebHID specifics:

- **`requestDevice()` requires a user gesture** — no auto-connect on load. The web build needs
  an explicit "Connect" step the desktop build does not.
- **The usage pages are fine.** WebHID restricts protected top-level collections (standard
  keyboard, pointer), but VIA sits on vendor page `0xFF60` and XAP on `0xFF51` — accessible,
  and partly *why* they are chosen that way.

**C++11 threads work, but should not be used here.** Emscripten maps pthreads onto Web Workers
+ SharedArrayBuffer, so `std::thread`, `std::mutex`, `std::condition_variable` and
`std::atomic` all function. But: cross-origin isolation is **mandatory**
(`Cross-Origin-Opener-Policy: same-origin` + `Cross-Origin-Embedder-Policy: require-corp`), a
hosting requirement that rules out static hosts which do not let you set headers;
`Atomics.wait` does not work on the main browser thread, so long blocks like `pthread_join`
can **deadlock** against worker proxying (mitigated by `-sPROXY_TO_PTHREAD`); and
`PTHREAD_POOL_SIZE` must pre-spawn workers because `pthread_create()` otherwise needs to
return to the event loop.

None of which matters, because **WebHID is already async and event-driven** — the web build
needs no I/O thread at all. Build it single-threaded and skip the cross-origin isolation
requirement entirely.

| Layer | Desktop | Web (Emscripten) |
|---|---|---|
| UI (ImGui) | shared | shared |
| Capability model | shared | shared |
| Protocol | shared | shared |
| **Transport** | **hidapi + I/O thread** | **WebHID + async callbacks** |
| Threads | `std::thread` etc. | none needed |

One seam, one new implementation behind it, everything above untouched.

### Assets under Emscripten: compile them in

No filesystem in the browser, fetches are async, and ImGui's font atlas must be built before
the first frame. `--preload-file` (MEMFS) and `--embed-file` both work, but the right answer
is **compiled-in C arrays**, because ImGui already works that way and ships the tool:
`misc/fonts/binary_to_compressed_c.cpp`.

```cpp
io.Fonts->AddFontFromMemoryCompressedTTF(MyFont_data, MyFont_size, 16.0f * dpi_scale);

ImFontConfig cfg; cfg.MergeMode = true;
static const ImWchar ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
io.Fonts->AddFontFromMemoryCompressedTTF(FontAwesome_data, FontAwesome_size, 16.0f, &cfg, ranges);
```

No filesystem, no preload, no async, **no `#ifdef` between desktop and web**.

- `imgui_freetype` works on the web via Emscripten's official port
  (`--use-port=freetype`, legacy `-sUSE_FREETYPE=1`).
- **DPI on the web is `window.devicePixelRatio`**, queried from JS; scale font size and
  `ScaleAllSizes()` by it *and* size the canvas at CSS-size × devicePixelRatio. Getting this
  wrong is the main cause of blurry ImGui in a browser.
- If real bitmaps are needed, **`stb_image.h` works in wasm** — embed PNG bytes as a C array,
  `stbi_load_from_memory()`, upload to a GL texture. Same code both platforms.

For this application the entire asset budget is **one UI font, one subset icon font, possibly
a logo** — the keyboard itself is vector work through `ImDrawList`, and icons come from the
font atlas. Asset management disappears on both platforms. Notably better than a web-frontend
stack, where font loading, FOUT and asset URLs all need managing.

### The web app is Chromium-only regardless of toolkit

Any browser-hosted configurator needs device access, which means **WebHID** — and Firefox and
WebKit have both formally declared it *harmful*. Choosing Tauri to "keep the web option open"
buys a Chromium-only web app just the same. The constraint is browser device access, not the
GUI toolkit. Native-first with web as a secondary channel is the sound position.

### Alternatives, and when they would win

| Option | When to prefer it |
|---|---|
| **Tauri** (Rust + web frontend) | A polished web build is a *primary* deliverable. What **both** ZMK Studio and `qmk_xap` chose independently; far lighter than Electron. Cost: two toolchains. |
| **Qt** | Native platform integration and polished stock widgets matter more than dynamic-UI friction. Plus licensing considerations. |
| **egui** (Rust) | Same immediate-mode fit with better defaults, and ecosystem alignment (`zmk-protocol`, qmk_xap's backend). Only worth it if learning Rust is itself a goal. |

**The one genuine strike against ImGui: accessibility.** It draws everything itself and exposes
no native accessibility tree, so there is no screen-reader support. If the tool ships to other
people and that matters, it is a real argument for Qt or Tauri.

## Extension-mechanism reference

Summary of [the per-protocol sections](README.md#adding-custom-features):

| | Mechanism | Stock client renders it | Fork needed |
|---|---|---|---|
| VIA tier 1 | custom channels + V3 `menus` | **yes**, declared widgets only | no |
| VIA tier 2 | `raw_hid_receive_kb()` | no | no |
| VIAL | as VIA, plus settings | yes for menus | already forked |
| **XAP** | **`xap.hjson` -> 0x02 KB / 0x03 USER** | no UI, but generated bindings + docs | **no** |
| **ZMK — behaviour** | devicetree `display-name` + C metadata | **yes, automatically** | **no** |
| ZMK — new subsystem | modify `zmk-studio-messages` | n/a | **yes** |
| GP2040-CE | modify the whole stack (add-on + proto + handler + React) | n/a — you ship the UI | n/a |
