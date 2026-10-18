# Building on GitHub and releasing

*Written 2026-10-05. Nothing of it is set up yet: Nazg has only been built by hand, on Windows
with Visual Studio 2022, and on Linux with GCC in WSL (2026-10-18, "Linux: first build, in WSL").*

## Automatic builds: GitHub Actions

GitHub can build Nazg for Windows, Linux and macOS on every push, on machines it provides --
**free for a public repository** on its standard runners. One file does it:
`.github/workflows/build.yml`, a job run on three runner images:

| Runner | Has | Needs |
|---|---|---|
| `windows-latest` | Visual Studio 2022, CMake, Python | nothing |
| `ubuntu-latest` | GCC, CMake, Python | SDL3's X11 and Wayland development headers; `libudev-dev` for hidapi's hidraw backend (`libusb-1.0-0-dev` for its libusb one) -- one `apt-get install` line. Vulkan, SDL_GPU's backend there, is loaded at run time |
| `macos-latest` | Xcode's Clang, Metal, IOKit (hidapi's backend), CMake, Python | nothing; Apple Silicon -- a universal binary is `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` |

Each run:

1. checks out the repository **with its submodules** (`submodules: recursive`) -- SDL, Dear
   ImGui, hidapi, nlohmann/json, minlzma, pinned as always;
2. installs the packages above (Linux only);
3. configures and builds with CMake -- the Visual Studio generator on Windows, called directly
   rather than through `GenerateBuildForVS2022.bat`, in a build directory of its own, as the
   one-directory-per-toolchain rule wants;
4. runs `ctest`. The protocol tests use scripted bytes, so no keyboard is needed;
   `via_bundle_contents` runs if the job first runs `tools/update_via_bundle.py` (Python is
   on every runner), and is skipped otherwise;
5. uploads the build as an artifact of the run.

**Expect fixes on the first Linux and macOS builds.** "Proven on Windows, Linux and macOS" is
true of the Leyden Jar tool's CMake layout, not of Nazg's code: it has only met MSVC. GCC and
Clang will warn where MSVC did not, and MSVC-only options such as `/utf-8` need their
counterparts or a guard. Finding those is the point of the job.

## Releases

A **release** is a git tag with a page on GitHub; files attached to it are **release assets**
(up to 2 GB each), downloadable from a fixed address with no API and no rate limit:

```
https://github.com/<owner>/<repo>/releases/download/<tag>/<file>
```

Three ways to make one: the repository's Releases page (draft, pick or create a tag, drop the
files, publish); the `gh` CLI (`gh release create <tag> <files> --notes ...` -- not installed
on Rico's machine yet); or, best, the workflow itself -- on a pushed tag, the same job builds
all three platforms and attaches a zip of each, with `via_definitions.tar.xz` beside the
executable (releases ship the bundle). An asset can be deleted (`gh release delete-asset`) or
replaced under the same name -- which is why anything pinning one checks a SHA-256.

**The community definitions use this** (via-registry.md, "Decisions to take"): their
repository publishes its bundle as a release asset, built by its own workflow when a tag is
pushed -- validator first -- and Nazg pins it in a small file:

```
resources/community-definitions.release
  tag     2026.10.05
  sha256  ...
```

A tool like `tools/update_via_bundle.py` downloads `releases/download/<tag>/<file>`, checks
the hash and writes it to `build_resources/`. A takedown is a new release and the old asset
deleted; a checkout pinned to it then fails to download, and the tool says to move the pin.

## Linux: one build for most distributions

*Added 2026-10-13. A plan, nothing built; the decisions it leaves open are Rico's, marked below.*

### One binary

The **glibc of the build machine is the floor**: a binary built against glibc 2.35 (Ubuntu
22.04) does not start on RHEL / Rocky 9 (2.34) or Debian 11 (2.31). So the release build runs
in an old distribution's container, while the CI job's tests can stay on `ubuntu-latest`:

- **AlmaLinux 8, glibc 2.28** -- the `manylinux_2_28` image Python wheels are built in, usable
  as a GitHub Actions container. It covers Debian 10+, Ubuntu 20.04+, RHEL / Rocky 8+, Fedora
  and Arch. Nazg is C++20 with coroutines, so GCC 10 or later: `gcc-toolset-13` there.
- **The C++ runtime linked statically** (`-static-libstdc++ -static-libgcc`): what is left is
  glibc and libudev, which every distribution has (systemd's, or eudev's where there is no
  systemd).
- **SDL3 from the submodule** loads X11, Wayland, libdecor and Vulkan when it runs, not when it
  is built: their development headers in the container compile the backends in, and the
  binary does not depend on them. `libSDL3.so.0` ships beside `nazg` with RPATH `$ORIGIN`, or
  SDL3 is linked statically.
- **hidapi with hidraw only** (`-DHIDAPI_WITH_LIBUSB=OFF`): the libusb backend would detach the
  keyboard's kernel driver, and adds a dependency for nothing.
- **arm64** is one more job, on `ubuntu-24.04-arm` with the aarch64 image.

**Vulkan is the real risk, not the packaging.** SDL_GPU is Vulkan on Linux, with no OpenGL
fallback of its own. Any recent Mesa or NVIDIA driver has it, and Mesa's lavapipe gives a
software Vulkan in most virtual machines; a very old GPU, or a VM without Mesa's Vulkan
drivers, fails. The answer would be the `imgui_impl_opengl3` fallback CLAUDE.md names, chosen
in `Main.cpp` when no GPU device can be created -- only once a tester meets it.

Vial does the same: its AppImage is built in an Ubuntu 18.04 container
(`vial-gui/util/linux-builder/Dockerfile`, glibc 2.27), packed with pkg2appimage and fpm.

### Formats

| Format | For | Against |
|---|---|---|
| **tar.gz** (a `tools/package_linux.py` beside `package_windows.py`) | The Windows zip's twin; nothing installed; enough for testers | The udev rule is the user's to install (see below) |
| **AppImage** | One file; what VIA and Vial ship | Needs FUSE -- the older runtime wants `libfuse2`, which Ubuntu 22.04+ no longer installs, so the current static runtime; cannot install a udev rule |
| **.deb and .rpm** (fpm or nfpm, from the same binary) | Install a desktop entry, an icon -- and could install a udev rule | Two more assets per release |
| **Flatpak / Flathub** | Found in software centres; its runtime removes the glibc question | Flathub builds from source on its own manifest and review; `--device=all`; the rule is still needed on the host |

Snap fits badly: its hidraw access is connected device by device. A likely order: tar.gz for
testers first; AppImage and .deb / .rpm with the public release; Flathub later, if asked for.

### Permissions: the udev rule

Linux gives `/dev/hidraw*` to root only, so on a stock distribution Nazg cannot open any
keyboard -- VIA, Vial and Chrome's WebHID all ask their users for a udev rule. Vial's empty
keyboard list links to its manual page (`main_window.py`); VIA's desktop wrapper carries a
udev prompt (via-registry.md, "The native application").

What every such rule needs: a name sorting **before `73-seat-late.rules`** (`60-nazg.rules`),
so that `TAG+="uaccess"` hands the device to whoever is logged in at the screen -- no group,
no logging out -- then `udevadm control --reload-rules` and
`udevadm trigger --subsystem-match=hidraw` for the boards already plugged in. uaccess needs
systemd-logind or elogind, which nearly every desktop distribution has; the others need a
group-based rule (`MODE="0660", GROUP=...`, the group differing by distribution).

**How wide** -- *Rico's decision, open*:

| Rule | For | Against |
|---|---|---|
| Every hidraw device | One rule ever; every VIA and Vial board | Opens every HID device to any program the user runs |
| Vial's serial marker, `ATTRS{serial}=="*vial:f64c2b3c*"` | Tight | Vial only; VIA boards carry no marker |
| One line per VID:PID | Tight; as wide as Vial's own rule (the whole board, its typing interface too) | A line per board -- which only Nazg itself can write without guessing, see below |
| A helper udev runs on each hidraw device, keeping the `0xFF60` interfaces | Tight, both protocols, every board | A program of Nazg's installed at a fixed path; most to build |

Narrowing one board's line to its raw HID interface alone is possible, but takes chained
rules: the vendor and product sit on the USB device, the interface number on its interface,
and one udev rule matches `ATTRS` of a single parent.

### Proposed: Nazg allowing the boards itself

*Proposed 2026-10-13; not decided, not drawn. The notice is UI, so a mockup comes first and
the screen then goes into ui-design.md.* On Windows and macOS, and on a Linux whose rules
already allow the boards, nothing of this appears.

1. **Noticing.** Listing needs no permission -- hidapi reads each interface's usage page from
   sysfs, readable by anyone -- so the board and its `0xFF60` interface are listed; only
   opening fails, with `EACCES`. **Today the probe calls that "no answer"** (`Main.cpp`,
   the probing loop), which reads as "no VIA in this firmware". The fix stands alone: when
   `hid_open_path` fails on Linux, `ExecuteOpen` checks `access(path, R_OK | W_OK)`, raises a
   permission error of its own, and the list says **"not allowed"**.
2. **Saying it.** With at least one board not allowed, one notice above the list: Linux does
   not let Nazg open these keyboards yet; Nazg can add the rule, and the system will ask for
   the password. *Allow these keyboards...*, and *Show the commands instead* -- the rule and
   the `sudo` lines with Copy, for those who prefer it and for systems where step 3 cannot run.
3. **Installing it** with **`pkexec`**, the desktop's way to run one command as root: the
   password dialog is the desktop's (GNOME, KDE...), and **Nazg never sees the password**.
   One `pkexec /bin/sh -c '...'`, so one prompt: the rule written with its text inside the
   command (no temporary file to swap), then the reload and the trigger. Then Nazg asks the
   refused boards again itself. Still refused: "unplug the keyboard and plug it back in" --
   a board that did not answer is asked again once it is back (ui-design.md, "Hotplug").
   Dialog dismissed (exit code 126): nothing changes. No
   `pkexec`, or no polkit agent running (a bare window manager, a Flatpak): *Show the
   commands*. pkexec waits on the user, so it runs off the frame loop like a board request.
   The code is POSIX (`posix_spawn`, `access`), Linux only, no SDL -- `Main.cpp` keeps its
   role.
4. **What it writes**, if one line per board is chosen: Nazg knows the VID:PID of each board
   it was refused, which a package installed beforehand cannot. A new board is one more
   password and one more line; Nazg reads the file (anyone can) and writes it back with the
   line added:

   ```
   # Written by Nazg. One line per keyboard allowed; delete a line to take the permission back.
   # Keychron K10 HE, added 2026-10-13
   KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3434", ATTRS{idProduct}=="0ea7", TAG+="uaccess"
   ```

   Taking a permission back is deleting its line, as the header says; a Settings list of the
   boards allowed could come later, if someone asks.

This works from the tar.gz, the AppImage and the .deb / .rpm alike, so the packages need not
install a rule of their own; a Flatpak gets the commands only.

**Open** -- *Rico's decisions*: the rule's width; when the "not allowed" fix of step 1 goes in
(it can now be tried in WSL, below). **Testing** the happy path of uaccess and pkexec needs a
real Linux desktop with a board at hand -- a live USB stick -- which WSL is not.

### First build, in WSL (2026-10-18)

WSL 2 with Ubuntu 24.04 on Rico's PC, a board lent to it with **usbipd-win** (`usbipd bind`
once, from an administrator *Windows* terminal; `usbipd attach --wsl --busid <id>`, no
administrator). While attached the board is gone from Windows; a detach, a replug or
`wsl --shutdown` gives it back -- so never the board being typed on.

- **`GenerateBuildForLinux.sh`**: `Ninja Multi-Config` into `build_Linux/`, so Debug and Release
  sit side by side as in Visual Studio and the same `--config Debug` / `-C Debug` commands work;
  `-DHIDAPI_WITH_LIBUSB=OFF`. Built from the Windows checkout through `/mnt/c`: slow -- the
  configure 3 minutes, nearly all waiting on files -- but one checkout. The packages: SDL's
  `docs/README-linux.md` list without audio, plus `libudev-dev`, `libdecor-0-dev`,
  `vulkan-tools` and `zenity`.
- **GCC 13 needed two changes**: it crashed (an internal compiler error) on a braced
  `initializer_list` written inside a `co_await` -- now a named variable first -- and warned on
  `std::filesystem::u8path`, deprecated in C++20 and replaced by `PathFromUtf8`. The other files
  compiled clean. **All 48 tests pass**, both bundles' included.
- **It runs under WSLg** on Wayland, SDL_GPU on Vulkan -- **llvmpipe only** in this WSL (software,
  no GPU ICD), slower but enough. The data folder is `~/.local/share/mymakercorner/Nazg/`. The
  window frame and its generic icon are WSLg's (Remote Desktop draws it), so the icon can only be
  judged on a real desktop.
- **The F77**: its four interfaces appear as `hidraw0-3`, all `root` only. Nazg lists it, the
  open fails with `EACCES` -- hidapi's error reads "Permission denied" -- and the list says
  **"no answer"**, the gap of step 1 above, seen for real. A rule for its VID:PID
  (`60-nazg.rules`, `TAG+="uaccess", GROUP="plugdev", MODE="0660"`), reloaded and triggered,
  made them `root plugdev 0660`; Vial, the unlock, the keymap and Diagnostics then work (Rico).
  **uaccess does nothing in WSL** -- the session has no seat -- so the group did the work there.
- **The key test of a locked board gets no keys in WSL**, unlocked it does: locked, it shows the
  keys the computer receives, and WSLg's display takes its input from Windows over Remote
  Desktop, never from Linux's input devices -- while Windows no longer sees the board lent to
  Linux. A WSL limit; a real desktop reads the board's input devices, which Linux creates
  (`event0-4` for the F77).

## Signing

Unsigned builds work, with warnings: **Windows SmartScreen** warns on an unsigned
executable, **macOS Gatekeeper** refuses it until the user allows it in System Settings.
Signing needs a Windows code-signing certificate, and for macOS an Apple Developer account
($99 a year) plus notarisation, which the workflow can do with the secrets stored in the
repository. It matters for public releases only, not for CI builds.

## Order

1. The workflow: build and test on the three runners, artifacts only. Fix what Linux and
   macOS find.
2. Releases from tags: zips per platform, VIA's bundle included; for Linux, a tar.gz built in
   the AlmaLinux 8 container ("Linux: one build for most distributions").
3. Signing, when releases are public; with them, Linux's AppImage and .deb / .rpm.
4. The community repository's own workflow and release, when it exists.
