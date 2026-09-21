# Nazg

**One configurator for VIA, Vial and XAP keyboards — native, no browser, no firmware fork.**

Nazg is a cross-platform desktop client for configuring mechanical keyboards at runtime. It
speaks several configuration protocols through a single descriptor-driven interface, so one
application handles boards that today need three different tools.

*One keyboard configurator to rule them all.*

> **Status: early development.** The protocol survey that informs the design is complete; the
> application itself is just starting. There is nothing to install yet.

## Why

The existing tools each give up something. VIA keeps keyboard definitions in an external
registry and needs a browser. Vial fixes the definition problem by embedding it in the device,
but requires a forked QMK. QMK's own XAP has been in draft since 2021.

The common root cause: the configuration UI and the device's capability description usually
live somewhere other than the device. Nazg is built around the opposite assumption — the
device describes itself, and the client renders whatever it is told.

## Design

```
UI layer            renders from the capability model; knows nothing about protocols
    |
Capability model    "this board has: <typed fields, widgets, groups>"
    |
Protocol layer      VIA  |  Vial  |  (XAP later)
    |
Transport           async request/response + unsolicited messages
```

The capability model is the pivot. VIA V3 `menus`, Vial embedded definitions, custom
descriptors and — later — XAP all feed into it through the same path, which means custom
features are not a special case.

**Stack:** C++ · [Dear ImGui](https://github.com/ocornut/imgui) · [hidapi](https://github.com/libusb/hidapi)

The design rests on a detailed comparison of the existing protocols, kept in
[`docs/research_material/`](docs/research_material/README.md) — transports, throughput,
security, expressiveness and extension mechanisms for each, with the reasoning behind the
choices above.

## Scope

| | Status |
|---|---|
| Vial | Planned first — self-describing devices make the pipeline testable end to end |
| VIA | Planned second — shares most of the Vial backend |
| Custom features | Via device-served descriptors, same path as everything else |
| XAP | Future, once the spec settles |
| ZMK Studio | Future — very low priority, but not ruled out |

Out of scope: ZSA (compile-time model), KMK (Python firmware), and any approach requiring a
forked firmware or an embedded web server on the keyboard.

## Building

All dependencies are git submodules pinned to exact commits, so clone recursively:

```
git clone --recurse-submodules https://github.com/mymakercorner/nazg.git
cd nazg
```

On Windows, generate a Visual Studio 2022 solution in `build_VS2022/`:

```
GenerateBuildForVS2022.bat
```

Then open `build_VS2022\nazg.sln`, or build from the command line:

```
cmake --build build_VS2022 --config Debug
```

If you already cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` first.

Requires CMake 3.21+ and a C++17 compiler. Everything else — SDL3, Dear ImGui, hidapi,
nlohmann/json, minlzma — is built from `external/`, so there is nothing to install.

The window reports which GPU backend SDL selected (D3D12 on Windows, Metal on macOS,
Vulkan on Linux).

### Tests

Tests are registered with CTest and built by default (`-DNAZG_BUILD_TESTS=OFF` to skip):

```
ctest --test-dir build_VS2022 -C Debug --output-on-failure
```

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) — the short version is that
commits need a `Signed-off-by` line (`git commit -s`).

## Licence

[GPL-3.0-or-later](LICENSE). Copyright © 2026 Rico.
