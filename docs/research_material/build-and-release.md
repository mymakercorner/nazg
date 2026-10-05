# Building on GitHub and releasing

*Written 2026-10-05. Nothing of it is set up yet: Nazg has only been built by hand, on Windows
with Visual Studio 2022.*

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

## Signing

Unsigned builds work, with warnings: **Windows SmartScreen** warns on an unsigned
executable, **macOS Gatekeeper** refuses it until the user allows it in System Settings.
Signing needs a Windows code-signing certificate, and for macOS an Apple Developer account
($99 a year) plus notarisation, which the workflow can do with the secrets stored in the
repository. It matters for public releases only, not for CI builds.

## Order

1. The workflow: build and test on the three runners, artifacts only. Fix what Linux and
   macOS find.
2. Releases from tags: zips per platform, VIA's bundle included.
3. Signing, when releases are public.
4. The community repository's own workflow and release, when it exists.
