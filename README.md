<p align="center">
  <img src="resources/icon/png/nazg-256.png" alt="The Nazg icon" width="160">
</p>

<h1 align="center">Nazg</h1>

<p align="center"><b>A configurator for VIA and Vial keyboards.</b><br>
<i>One keyboard configurator to rule them all.</i></p>

<p align="center">
  <img src="docs/user-manual/keymap.png" alt="Nazg editing a VIA keyboard: the board above, the keycode picker below">
</p>

Nazg is a cross-platform and multi-protocol client for configuring mechanical keyboards.  
There is no need to reflash your keyboard: any existing QMK firmware with VIA or Vial enabled works as it is.

> **Status: pre-release.** Nazg is in test builds. Reports of what works and what does not on
> your keyboard are welcome on the [issues page](https://github.com/mymakercorner/nazg/issues).

As of this writing, the following protocols are supported:
- VIA.
- Vial.
- Leyden Jar diagnostics for Model F Labs capacitive keyboards (an extension to the Vial protocol).


Nazg supports the following platforms:
- Windows.
- Linux.
- macOS (untested).
- Web, in Chrome, Edge or Opera.

Prebuilt binaries are available on the **[releases page](https://github.com/mymakercorner/nazg/releases)**.  
The web application is available at **[mymakercorner.com/nazg](https://mymakercorner.com/nazg/)**.  
The user manual is available **[here](docs/user-manual.md)**.


## Why

I use both the VIA and Vial protocols and their clients, both as an ordinary user and as a keyboard PCB designer.  
Although they are great tools overall, there are a few things I do not like about them.  

The most annoying one is that I need two different protocols and apps to configure the keyboards I have on hand.  

VIA and Vial also each have their strengths and weaknesses:
- VIA keyboard definitions are not stored in the firmware:
    - This forces users to hunt for the right keyboard definition on vendor sites when VIA has no official support for it.
    - Vial has no such issue.
- They do not support the same QMK features, and their implementations can differ: Vial supports a wider range of QMK features than VIA.
- The latest VIA protocol allows custom UI elements linked to protocol extensions; Vial has yet to add this incredible feature.
- VIA requires a web browser (its native application is deprecated):
    - Firefox users are left out, and must use Chrome, Edge or Opera.
    - Some people prefer native apps, and users should have the choice.

## What it does

Nazg answers each of those points:
- One application for both VIA and Vial keyboards: plug a keyboard in and Nazg finds which protocol it speaks.
- No more hunting for VIA keyboard definitions:
    - VIA's whole official registry ships with Nazg, so it works offline.
    - About 700 more definitions, published by vendors for keyboards VIA lacks or has older, ship beside it.
    - A definition found elsewhere can still be imported by hand.
- Vial's extra QMK features are there for Vial keyboards: Tap Dance, Combos, Key Overrides, Alt Repeat Key and QMK Settings.
- VIA's custom menus are drawn for keyboards whose definition has them.
- A native application for Windows, Linux and macOS, and the same application in a browser for those who prefer nothing to install.

A few more things come with it:
- Layouts saved to a file in Vial's `.vil` format, for VIA keyboards too; files saved by the VIA app load as well.
- A key tester, reading the switches themselves where the firmware allows it.
- Keyboards plugged and unplugged while Nazg runs are followed, with no refresh button.
- Every QMK keycode, searched or typed by its QMK name (`LT(1,KC_SPC)`, `MT(MOD_LCTL,KC_A)`): no more cryptic hexadecimal values.
- Attention has been given to the readability of the UI, especially the keyboard view:
    - Legends follow your computer's layout, out of 69: an AZERTY board shows AZERTY, with the Shift and AltGr characters too, as on a printed keycap.
    - Two legend styles to choose from in Settings: Cylindrical (top left, mixed case, like GMK keycaps) or Spherical (centred capitals, like SA keycaps).
    - QMK special keys (layers, media, macros, lighting...) carry a coloured header and band naming what kind of key they are.
    - Tap-hold keys show both actions; transparent and empty keys are faint, with a badge.

## Technical choices

The language used for the client is C++.  
I used several external libraries:
- Omar Cornut's incredible [Dear ImGui](https://github.com/ocornut/imgui) library.
- [hidapi](https://github.com/libusb/hidapi) used for RAW HID communications.
- [SDL3](https://github.com/libsdl-org/SDL) used for window management, GPU drawing and file dialogs.
- [nlohmann/json](https://github.com/nlohmann/json) used to read keyboard definitions and settings.
- [minlzma](https://github.com/ionescu007/minlzma) used to decompress the definitions Vial keyboards carry.

They are git submodules in `external/`, pinned to exact commits.

The fonts shipped with Nazg are all under the SIL Open Font License except Tabler Icons (MIT):
- [Arimo](https://github.com/googlefonts/Arimo) used for key legends.
- [Noto Sans Arabic, Math and Symbols 2](https://github.com/notofonts/notofonts.github.io) used for the few legend characters Arimo lacks.
- [Tabler Icons](https://tabler.io/icons) used for the interface icons.

### AI use
I first have to address one pivotal choice that some people may find polarizing: I made extensive use of an AI agent (Claude Code) to develop Nazg.  
In fact I didn't write a single line of code: I only reviewed what Claude was doing, giving feedback and asking for changes where needed.  
Writing such an application by myself would have been technically possible, but would have taken an unreasonable amount of time.  
What the VIA and Vial teams have done so far is no simple task: such applications hide a lot of complexity that makes them difficult to develop and maintain.  
I still wanted as much control as possible over the application's features, look and overall quality.  
Our interactions have been constant throughout development.  

The brainstorming stage settled:
- how to solve the issues mentioned in the 'Why' section.
- what protocols to support.
- what platforms to support.
- what technologies, libraries, languages and compilers to use.

The UI design:
- done in HTML by Claude, knowing the drawing limitations of the UI library we would use (Dear ImGui).
- a lot of back and forth iterations based on my feedback.

The coding phase:
- for example, the decision to use C++ coroutines rather than threads, which made porting to the web much easier.

It has been more a close collaboration between a project leader (me) and a 'know it all' developer (Claude) than a conventional 'make a plan and do it' approach.  
It also taught me many new things along the way.  
Being curious, I constantly asked for explanations of the technical choices made, so much that in the end Claude was directly offering to explain things I probably didn't know about :D

You can have a look at [`docs/research_material/`](docs/research_material/README.md) for a raw record of the results of our interactions.  
Claude's own view of how it went is in [Working with Rico](docs/working-with-rico.md) (yes, I asked him :D).


## Building


```
git clone --recurse-submodules https://github.com/mymakercorner/nazg.git
cd nazg
```
This clones the project with all its submodule dependencies.  
If you already cloned without `--recurse-submodules`, run
`git submodule update --init --recursive` first.

Then fetch the keyboard definitions Nazg ships, VIA's official registry and the community ones (Python 3.8+, nothing to install):
```
python tools/update_via_bundle.py
python tools/update_community_bundle.py
```
They are downloaded once into `build_resources/`, shared by every build, and copied beside the executable.  
Without them Nazg still builds and runs, but VIA keyboards then only get the definitions you import yourself.

### Windows
```
GenerateBuildForVS2022.bat
```
This generates a Visual Studio 2022 solution in `build_VS2022/`.

Then open `build_VS2022\nazg.sln`, or build from the command line:
```
cmake --build build_VS2022 --config Debug
```

### Linux

Install the compiler, Ninja and the libraries SDL needs (Ubuntu or Debian names; CMake 3.21+ and GCC 13 or later):
```
sudo apt-get install build-essential cmake ninja-build pkg-config libudev-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libdrm-dev libgbm-dev \
    libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev \
    libwayland-dev libdecor-0-dev
```
Then:
```
sh GenerateBuildForLinux.sh
cmake --build build_Linux --config Debug
```
This generates a Ninja build in `build_Linux/`, with Debug and Release side by side.  
Linux gives keyboards' raw HID devices to root only: Nazg lists such a board as "not allowed" and its Allow... button writes a udev rule for it.

### macOS

Install Xcode's command line tools, CMake 3.21+ and Ninja (`brew install cmake ninja`), then:
```
cmake -S . -B build_macOS -G "Ninja Multi-Config" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build_macOS --config Debug
```
`python3 tools/package_macos.py` then makes `Nazg.app` from a Release build.

### Emscripten

Install and activate [emsdk](https://emscripten.org/docs/getting_started/downloads.html) at version 6.0.13, on Linux, WSL or macOS:
```
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.13
~/emsdk/emsdk activate 6.0.13
. ~/emsdk/emsdk_env.sh
```
Then, with Ninja installed:
```
sh GenerateBuildForWeb.sh
cmake --build build_Web --config Release
python3 -m http.server -d build_Web/Release 8080
```
Open http://localhost:8080/nazg.html in Chrome, Edge or Opera: the page must be served over HTTP, not opened as a file.

### Tests

Tests are registered with CTest and built by default (`-DNAZG_BUILD_TESTS=OFF` to skip):
```
ctest --test-dir build_VS2022 -C Debug --output-on-failure
```
On Linux and macOS, use `build_Linux` or `build_macOS` instead.  
The web build's tests run under Node:
```
ctest --test-dir build_Web -C Release --output-on-failure
```

## Contributing

Contributions are welcome.  
See [CONTRIBUTING.md](CONTRIBUTING.md) — the short version is that
commits need a `Signed-off-by` line (`git commit -s`).

## Licence

[GPL-3.0-or-later](LICENSE). Copyright © 2026 Rico.


