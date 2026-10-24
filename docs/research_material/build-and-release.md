# Building on GitHub and releasing

*Written 2026-10-05. Built by hand on Windows with Visual Studio 2022, and on Linux with GCC in
WSL (2026-10-18, "Linux: first build, in WSL"), its AppImage in an AlmaLinux 8 container ("The
AppImage"). GitHub Actions builds Windows, Linux and macOS since 2026-10-24 ("Windows on GitHub,
first run").*

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

### Windows on GitHub, first run (2026-10-24)

`.github/workflows/build.yml`, on every push and pull request and by hand (Actions tab, *Run
workflow*): one job per configuration, **Debug and Release side by side**, each fetching both
bundles -- so `via_bundle_contents` and `community_bundle_contents` run -- then configuring,
building and running `ctest`. Release then runs `tools/package_windows.py` and keeps the testers'
zip as the run's artifact, `nazg-windows-x64`. A newer push to the same branch cancels the older
run. **A push changing only notes runs nothing** (`paths-ignore`: `docs/**`, `**.md`) -- the
user manual among them, though the zip carries it; a tag always builds, GitHub applying no path
filter to tags.

- **The runner is `windows-2022`**: `windows-2025` carries Visual Studio 2026 only, and the
  `Visual Studio 17 2022` generator finds nothing there. When GitHub retires `windows-2022`, the
  choice is the one CLAUDE.md defers: VS2026, on the runner and on Rico's machine.
- **First green run**: about six minutes per job, most of it SDL; **49 of 49 tests pass** in both
  configurations, MSVC 14.44.
- The actions are `checkout@v5`, `upload-artifact@v6` and `download-artifact@v7`, all on
  Node 24; the versions before them were forced from Node 20 with a warning.

**Linux joined the same day**: a job on `ubuntu-24.04` -- the Ubuntu of Rico's WSL, GCC 13, the
compiler whose crash on a braced list in a `co_await` the code avoids -- Debug and Release,
configured by `GenerateBuildForLinux.sh` itself. Its packages: SDL's `docs/README-linux.md` list
without audio, plus `libwayland-dev`, `libdecor-0-dev` and `libudev-dev`. **49 of 49 tests pass
in both**, about four minutes a job. No package kept: the AppImage comes from the container, a
job of its own with the releases. The log's two kinds of warning are known and left: GCC's
`-Wstringop-overflow` false positive in `NazgLighting.cpp` ("The AppImage"), and
`wayland-scanner` finding SDL's protocol XML off its DTD.

**macOS joined too, and passed at once**: a job on `macos-15`, Apple Silicon, AppleClang 17 --
the first compiler of Apple's Nazg has met -- Debug and Release, `Ninja Multi-Config` into
`build_macOS/` (Ninja is on the runner already), `-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`. No
generate script: nobody builds Nazg on a Mac by hand yet. **49 of 49 tests pass in both**, about three minutes a job, no compiler warning,
nothing skipped. GitHub warns that arm64 macOS runners may queue longer. Not proven by it: the
window, Metal, hidapi's IOKit backend on a real board, and whether macOS asks for Input
Monitoring -- a colleague of Rico's with an Apple Silicon Mac will try the `.app`, which comes
with the releases.

## Releases

### Making one (since 2026-10-24)

A release is built on GitHub from a tag, by the same workflow, `.github/workflows/build.yml`:

1. Set `NAZG_VERSION` in `CMakeLists.txt` (`0.1.0-test.4`), commit, push.
2. Tag that commit `v<NAZG_VERSION>` and push the tag: `git tag v0.1.0-test.4`, then
   `git push origin v0.1.0-test.4`.
3. The workflow builds and tests the three systems as on any push, packaging the Windows zip and
   its manual page and the macOS app; the **appimage** job builds the AppImage in its container,
   tests included; then **Draft release** checks that the tag names `NAZG_VERSION`, and makes a
   **draft** release of that tag with the four files -- a pre-release when the version has a
   label. About seven minutes.
4. Rico writes the notes on the draft (Releases page, or a draft handed to Claude) and publishes.
   Nobody sees a draft before.

**First made so: `0.1.0-test.4`** (2026-10-24), the first with the macOS app -- all eight jobs
green in about six minutes, the draft's notes then set with `gh release edit --notes-file`, and
published on Rico's word. A draft's page lives at `releases/tag/untagged-<hash>` until published.

*Run workflow* on the Actions tab does all of it but the release -- a rehearsal, its packages
kept as the run's artifacts for a tester. The packages a release carries: `nazg-<v>-windows-x64.zip`,
`nazg-<v>-manual.html`, `nazg-<v>-linux-x86_64.AppImage`, `nazg-<v>-macos-arm64.zip`. Building
them by hand still works -- the three `tools/package_*.py` -- but nothing needs it.

**The macOS app** (`tools/package_macos.py`, first made 2026-10-24, released in `0.1.0-test.4`,
never yet opened on a Mac):
`Nazg.app` in a zip made by `ditto`, 6.2 MB. `Info.plist` from `resources/package/macos/`, its
version and minimum macOS (11.0, the first on Apple Silicon: `CMAKE_OSX_DEPLOYMENT_TARGET`)
filled in; the icon, fonts, both bundles and the licences in `Contents/Resources/`, where
`SDL_GetBasePath()` points; **SDL3 and hidapi as libraries in `Contents/Frameworks/`**, the
binary's build rpaths replaced by `@executable_path/../Frameworks`. Not linked statically: SDL
carries its own copy of hidapi, whose four `hid_darwin_*` functions then clash with hidapi's --
Linux's static build has no such clash. Every binary may ask only for `/usr/lib` and
`/System/Library`, checked. **Signed ad hoc**, the bundle sealed as a whole: no developer's
signature, so Gatekeeper refuses it once, but not "damaged". The tester's way past it, for the
release notes: open it, close the refusal, then System Settings, Privacy & Security, *Open
Anyway*. `Main.cpp` gives the window no PNG icon on macOS, where SDL would put it in the Dock
over the app's `.icns`.

### What a release is

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
| **tar.gz** | The Windows zip's twin; nothing installed; enough for testers | The udev rule is the user's to install (see below) |
| **AppImage** | One file; what VIA and Vial ship -- VIA a .deb beside it, no .rpm (its releases 2.0.5-3.0.0, the last desktop one, 2023); Vial only that (0.7.5) | Needs FUSE -- the older runtime wants `libfuse2`, which Ubuntu 22.04+ no longer installs, so the current static runtime; cannot install a udev rule |
| **.deb and .rpm** (fpm or nfpm, from the same binary) | Install a desktop entry, an icon -- and could install a udev rule | Two more assets per release |
| **Flatpak / Flathub** | Found in software centres; its runtime removes the glibc question | Flathub builds from source on its own manifest and review; `--device=all`; the rule is still needed on the host |

Snap fits badly: its hidraw access is connected device by device. **The order** -- *decided
2026-10-18 (Rico)*: the **AppImage first, for testers** -- the format VIA and Vial users already
know -- built ("The AppImage" below); .deb / .rpm later, once wanted, when they can also install
the polkit action giving pkexec's window Nazg's own sentence; Flathub later still, if asked for.

### Permissions: the udev rule

Linux gives `/dev/hidraw*` to root only, so on a stock distribution Nazg cannot open any
keyboard -- VIA, Vial and Chrome's WebHID all ask their users for a udev rule. Vial's empty
keyboard list links to its manual page (`main_window.py`); VIA's desktop wrapper carries a
udev prompt (via-registry.md, "The native application").

What every such rule needs: a name sorting **before `73-seat-late.rules`** (`60-nazg-...`),
so that `TAG+="uaccess"` hands the device to whoever is logged in at the screen -- no group,
no logging out -- then `udevadm control --reload-rules` and
`udevadm trigger --subsystem-match=hidraw` for the boards already plugged in. uaccess needs
systemd-logind or elogind, which nearly every desktop distribution has; the others need a
group-based rule (`MODE="0660", GROUP=...`, the group differing by distribution).

**How wide** -- *decided 2026-10-18 (Rico): one board at a time*, by its VID:PID, written by
Nazg itself (below):

| Rule | For | Against |
|---|---|---|
| Every hidraw device | One rule ever; every VIA and Vial board | Opens every HID device to any program the user runs |
| Vial's serial marker, `ATTRS{serial}=="*vial:f64c2b3c*"` | Tight | Vial only; VIA boards carry no marker |
| One line per VID:PID | Tight; as wide as Vial's own rule (the whole board, its typing interface too) | A line per board -- which only Nazg itself can write without guessing, see below |
| A helper udev runs on each hidraw device, keeping the `0xFF60` interfaces | Tight, both protocols, every board | A program of Nazg's installed at a fixed path; most to build |

Narrowing one board's line to its raw HID interface alone is possible, but takes chained
rules: the vendor and product sit on the USB device, the interface number on its interface,
and one udev rule matches `ATTRS` of a single parent.

### Nazg allowing the boards itself

*Proposed 2026-10-13; designed and built 2026-10-18 from the mockup
[linux-permission.html](ui-design/linux-permission.html), Rico's choices and what was tried in
ui-design.md, "A board Linux does not allow".* On Windows and macOS, and on a Linux whose rules
already allow the boards, nothing of this appears.

1. **Noticing.** Listing needs no permission -- hidapi reads each interface's usage page from
   sysfs, readable by anyone -- so the board and its `0xFF60` interface are listed; only
   opening fails, with `EACCES`. **Today the probe calls that "no answer"** (`Main.cpp`,
   the probing loop), which reads as "no VIA in this firmware". The fix stands alone: when
   `hid_open_path` fails on Linux, `ExecuteOpen` checks `access(path, R_OK | W_OK)`, raises a
   permission error of its own, and the list says **"not allowed"**.
2. **Saying it, on the board's row.** "not allowed", and **Allow...** in place of *Open*: a
   dialog for that board alone, with **Show the commands** -- exactly what it runs. A board Nazg allowed
   shows **Allowed** beside *Open*, which opens **Stop allowing** for that board alone.
3. **Doing it** with **`pkexec`**, the desktop's way to run one command as root: the password
   window is drawn by the desktop's **polkit agent** (GNOME's, KDE's...), and **Nazg never sees
   the password**. One `pkexec /bin/sh -c '...'`, so one prompt: the file written with its
   text inside the command (no temporary file to swap), or deleted, then the reload and the
   trigger. Then Nazg asks the board again itself. Still refused: "unplug the keyboard and plug
   it back in" -- a board that did not answer is asked again once it is back (ui-design.md,
   "Hotplug"). Window closed by the user (exit code 126): nothing changes. No `pkexec`, or no
   agent running (a bare window manager, a Flatpak, WSL) -- which Nazg cannot tell beforehand,
   so it tries: the dialog shows the commands instead. `--disable-internal-agent`, or pkexec
   would ask in the terminal Nazg was started from, where nobody looks. pkexec waits on the
   user, so it never blocks the frame loop: started with `posix_spawnp`, asked each frame with
   `waitpid(WNOHANG)`, ended if Nazg quits first (`permission/NazgBoardPermission.*`). The code
   is POSIX, Linux only, no SDL -- `Main.cpp` keeps its role. A missing pkexec can come back as
   `EACCES`, not `ENOENT`, when a folder on the PATH refuses to be searched -- WSL puts
   Windows' folders there -- so both say "pkexec is not installed".
4. **What it writes: one file per board** (Rico, 2026-10-18 -- one shared file, "delete its
   two lines" to take a board back, confused him: "what lines?"). Nazg knows the VID:PID of each
   board it was refused, which a package installed beforehand cannot. Allowing creates
   `/etc/udev/rules.d/60-nazg-<vid>-<pid>.rules`, stopping deletes it: nothing is ever edited,
   and the file being there is how Nazg knows the board is its own -- a board allowed any other
   way shows no *Allowed*, since Nazg cannot take back what it did not write:

   ```
   # Written by Nazg: lets whoever is logged in configure the Keychron K10 HE (3434:0ea7).
   KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3434", ATTRS{idProduct}=="0ea7", TAG+="uaccess"
   ```

   Boards sharing a VID:PID -- the F77 and the B104, both `1209:4704` -- are allowed and stopped
   together, and the dialogs say so.
5. **The window's sentence.** pkexec alone says "Authentication is needed to run `/bin/sh' as
   the super user", all an AppImage or a tar.gz can get. A .deb or .rpm installs a **polkit
   action** in `/usr/share/polkit-1/actions/` -- root only, so only an installer can -- which
   gives the window Nazg's own sentence and names the one program it may run, tighter than any
   `/bin/sh` command.

This works from the tar.gz, the AppImage and the .deb / .rpm alike, so the packages need not
install a rule of their own; a Flatpak gets the commands only.

**Open**: whether *Stop allowing* takes effect at once or only after a replug -- uaccess's
access list may stay on the entry until it is made again; WSL cannot tell, having no seat.
**Testing** the happy path of uaccess and pkexec needs a real Linux desktop with a board at
hand -- a live USB stick -- which WSL is not.

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

### The AppImage (2026-10-18)

*First released with `0.1.0-test.3`, beside the Windows zip (GitHub pre-release, 2026-10-18).*

**`python3 tools/package_linux.py`**, run in WSL's Ubuntu from the Windows checkout, writes
`dist/nazg-<NAZG_VERSION>-linux-x86_64.AppImage`; `--test` builds the tests too and runs them in
the container. It needs **podman** in Ubuntu -- installed once, `sudo apt-get install podman`;
rootless, so the build's files are Rico's -- and both bundles in `build_resources/`. The first run
pulls the image and makes the container (a few minutes), and a clean build takes about three;
after that podman's cache answers, and a run with little changed takes seconds.

- **The build machine** is `tools/package_linux.Containerfile`: `manylinux_2_28`, pinned by tag
  and digest, plus `gcc-toolset-13`, `ninja-build` and SDL's headers -- X11, Wayland, EGL, Vulkan,
  D-Bus, libudev; no audio. **libdecor is built from source there** (0.2.5, pinned by commit):
  AlmaLinux 8 and EPEL 8 lack it, and without it SDL has no window frame on a Wayland desktop that
  draws none for its programs -- GNOME. SDL loads the system's libdecor when it runs, and SDL 3.4
  chooses Wayland only where the compositor has `fifo-v1` (recent GNOME and KDE), XWayland
  elsewhere -- WSLg among them.
- **The build**: `build_manylinux/`, single-config Ninja, Release, `-DBUILD_SHARED_LIBS=OFF` --
  SDL3 and hidapi linked in, nothing beside the binary to find -- `-DHIDAPI_WITH_LIBUSB=OFF`,
  `-static-libstdc++ -static-libgcc -s`. The script then reads the binary: it may ask only for
  glibc (no newer than 2.28) and libudev, and stops otherwise. The first build: **glibc 2.27**,
  needing `libc libm libdl libpthread ld-linux libudev`, **all 49 tests passing** in the container.
  GCC 13 at `-O3` warns once, `-Wstringop-overflow` on a `push_back` in `NazgLighting.cpp`: its
  known false positive, the code left as it is.
- **The package**: appimagetool 1.9.1 and the static runtime of `type2-runtime` 20251108, pinned
  by URL and SHA-256 in the script and cached in `build_resources/cache/appimage/`; appimagetool
  runs extracted (`--appimage-extract-and-run`), so it needs no FUSE either. Inside: the program
  folder as in the Windows zip -- `nazg`, `fonts/`, `icon/`, both bundles -- in `usr/lib/nazg/`,
  `AppRun` a symbolic link to it (SDL's base path follows `/proc/self/exe`, the real file);
  `nazg.desktop` and the icons at the root and in `usr/share/` (resources/icon/README.md);
  `LICENSE` and the libraries' licences in `usr/share/doc/nazg/`. **6.9 MB** -- the definitions
  compress well in its zstd squashfs. No README beside it: the release page says what
  `resources/package/README.txt` says for Windows.
- **Seen working in WSL**: the AppImage starts from `/mnt/c` -- WSL's Ubuntu has `fuse3` and
  `/dev/fuse` -- through XWayland, SDL_GPU on Vulkan (llvmpipe); the F77 lent by usbipd, refused,
  shown **"not allowed" with Allow...** (Rico).
- **Seen working on a tester's Linux desktop** (reported 2026-10-24; KDE, the distribution not
  known), a Keychron on VIA: the AppImage started with nothing to install, a few keys changed, the
  key test used -- reading the switches, Keychron building every board with `VIA_INSECURE`, so the
  key test on the computer's keys is still unseen on Linux. His board was **already allowed**
  -- most likely a udev rule left by an earlier VIA or Vial setup, whose instructions have users
  write one -- so Allow..., pkexec and uaccess were not met: they wait for a tester whose board
  Linux refuses. Not yet on an old distribution (glibc 2.28).
- `Main.cpp` asks SDL_GPU for its debug mode in every build, so Vulkan says "Validation layers
  not found"; where they are installed (a Vulkan developer's machine), the release would run
  them.

### The user manual (2026-10-19)

`docs/user-manual.md` is the manual's source, read on GitHub as it is; its screenshots are in
`docs/user-manual/`, taken from Rico's boards (Concordia, F77, Phoenix). A release carries it as
**one HTML page** (Rico, 2026-10-19): it opens in any browser, the screenshots embedded, the
contents' links working, light or dark as the system is, a click on a screenshot showing it at full
size. Plain Markdown opens in Notepad on Windows, its images as text; a PDF needs pandoc and
LaTeX or a headless browser, and its page width shrinks the 1600 px screenshots.

- **`python tools/build_manual.py`** writes `dist/nazg-<NAZG_VERSION>-manual.html` (4.3 MB);
  `tools/package_windows.py` calls it and puts the page in the zip as `nazg-manual.html`. It needs
  Python's **`markdown`** package (`pip install markdown`; 3.11.1 the first) -- the one tool that is
  not standard library only.
- **The release attaches the page as an asset**: the AppImage is one file and carries nothing
  beside it.
- **Python-Markdown is stricter than GitHub**: a list needs a blank line before it, and an image
  inside a list item four spaces of indent -- the manual is written so both read it alike. The
  build stops on an in-page link that reaches no heading.

## Signing

Unsigned builds work, with warnings: **Windows SmartScreen** warns on an unsigned
executable, **macOS Gatekeeper** refuses it until the user allows it in System Settings.
Signing needs a Windows code-signing certificate, and for macOS an Apple Developer account
($99 a year) plus notarisation, which the workflow can do with the secrets stored in the
repository. It matters for public releases only, not for CI builds.

## Order

1. The workflow: build and test on the three runners, artifacts only. Fix what Linux and
   macOS find. *Done 2026-10-24.*
2. Releases from tags: a package per platform, both bundles included; for Linux, the AppImage
   built in the AlmaLinux 8 container -- `tools/package_linux.py` runs as it is in a job, podman
   being on GitHub's Ubuntu runners ("The AppImage"). *Done 2026-10-24* ("Making one").
3. Signing, when releases are public; with them, Linux's .deb / .rpm if wanted.
4. The community repository's own workflow and release, when it exists.
