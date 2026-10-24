#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Package a macOS build of Nazg as an app testers unzip and open.

From a Release build in build_macOS/ it makes Nazg.app (resources/icon/README.md, "Where each
platform takes it"): Info.plist from resources/package/macos/ with the version and the minimum
macOS filled in, the executable in Contents/MacOS/, and in Contents/Resources/ -- where
SDL_GetBasePath() points inside a bundle -- the icon, fonts/, both definition bundles and the
licences. SDL3 and hidapi are libraries of their own, as Windows' DLLs are: linked statically,
SDL's copy of hidapi clashes with hidapi's macOS functions. The binary finds them through its
rpath, so they go in Contents/Frameworks/ and the rpath is set to that; past them, binary and
libraries may ask only for macOS's own libraries and frameworks, checked here. The app is signed
ad hoc, as a whole: not a developer's signature, so Gatekeeper asks the tester to allow it once,
but a sealed bundle, which macOS would otherwise call damaged.

    cmake -S . -B build_macOS -G "Ninja Multi-Config" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
    cmake --build build_macOS --config Release --target nazg
    python3 tools/package_macos.py

writes dist/nazg-<version>-macos-<architectures>.zip, the version being NAZG_VERSION in
CMakeLists.txt. Runs on macOS (the CI's runner: build-and-release.md); both bundles must be
there: tools/update_via_bundle.py and tools/update_community_bundle.py fetch them. Python 3.8 or
later, standard library only.
"""

import argparse
import io
import json
import lzma
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build_macOS" / "Release"
CACHE = ROOT / "build_macOS" / "CMakeCache.txt"
DIST = ROOT / "dist"
BUNDLES = ["via_definitions.tar.xz", "community_definitions.tar.xz"]

# What the binary may ask of the system: macOS's own libraries and frameworks, nothing installed.
SYSTEM = ("/usr/lib/", "/System/Library/")

# The libraries Nazg links, and the licence file each one ships: as tools/package_windows.py.
LICENCES = {
    "SDL3.txt": "external/SDL/LICENSE.txt",
    "hidapi.txt": "external/hidapi/LICENSE-bsd.txt",
    "Dear-ImGui.txt": "external/imgui/LICENSE.txt",
    "nlohmann-json.txt": "external/json/LICENSE.MIT",
    "minlzma.txt": "external/minlzma/LICENSE",
}


def version():
    found = re.search(r'set\(NAZG_VERSION "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not found:
        sys.exit("CMakeLists.txt sets no NAZG_VERSION")
    return found.group(1)


def deployment_target():
    """The oldest macOS the build was made for, from its CMake cache: Info.plist says the same."""
    found = re.search(r"^CMAKE_OSX_DEPLOYMENT_TARGET:STRING=(\S+)$", CACHE.read_text(encoding="utf-8"), re.M)
    if not found:
        sys.exit(f"{CACHE} sets no CMAKE_OSX_DEPLOYMENT_TARGET -- configure with -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0")
    return found.group(1)


def manifest(bundle):
    """A bundle's manifest.json, out of its tar."""
    with tarfile.open(fileobj=io.BytesIO(lzma.decompress(bundle.read_bytes())), mode="r:") as archive:
        return json.load(archive.extractfile("manifest.json"))


def run(*command):
    return subprocess.run(list(command), capture_output=True, text=True, check=True).stdout


def linked(binary):
    """The libraries a binary asks for, its own install name left out."""
    lines = run("otool", "-L", str(binary)).splitlines()[1:]
    names = [line.split(" (")[0].strip() for line in lines]
    own = run("otool", "-D", str(binary)).splitlines()[1:]
    return [name for name in names if name not in own]


def libraries(binary):
    """The build's own libraries the binary finds through its rpath (SDL3, hidapi), from the build
    directory; anything else must be the system's."""
    found = []
    for name in linked(binary):
        if name.startswith(SYSTEM):
            continue
        if not name.startswith("@rpath/"):
            sys.exit(f"{binary} needs {name}: neither the system's nor the build's")
        file = name[len("@rpath/"):]
        matches = [path for path in (ROOT / "build_macOS").rglob(file) if "Release" in path.parts]
        if not matches:
            sys.exit(f"{binary} needs {name}: not in build_macOS/'s Release")
        found.append(matches[0])
    for library in found:
        foreign = [name for name in linked(library) if not name.startswith(SYSTEM)]
        if foreign:
            sys.exit(f"{library.name} needs {', '.join(foreign)}")
    return found


def rpaths(binary):
    """LC_RPATH entries: the build directories CMake wrote in."""
    return re.findall(r"^\s*path (\S+) \(offset", run("otool", "-l", str(binary)), re.M)


def app(folder, label, minimum, shipped):
    """Nazg.app's contents."""
    contents = folder / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    (contents / "Frameworks").mkdir()
    resources = contents / "Resources"
    (resources / "licences").mkdir(parents=True)

    plist = ((ROOT / "resources" / "package" / "macos" / "Info.plist").read_text(encoding="utf-8")
             .replace("@NAZG_VERSION@", label)
             .replace("@CMAKE_OSX_DEPLOYMENT_TARGET@", minimum))
    (contents / "Info.plist").write_text(plist, encoding="utf-8")
    (contents / "PkgInfo").write_text("APPL????", encoding="ascii")

    binary = contents / "MacOS" / "nazg"
    shutil.copy2(BUILD / "nazg", binary)
    # The build's rpaths out, Frameworks/ in: the libraries are found inside the app, never in
    # a build directory that happens to exist.
    for path in rpaths(binary):
        run("install_name_tool", "-delete_rpath", path, str(binary))
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(binary))
    for library in shipped:
        # copyfile, not copy2: CMake's library files are links to the versioned one.
        shutil.copyfile(library, contents / "Frameworks" / library.name)

    shutil.copy2(ROOT / "resources" / "icon" / "nazg.icns", resources / "nazg.icns")
    shutil.copytree(BUILD / "fonts", resources / "fonts")
    for name in BUNDLES:
        shutil.copy2(ROOT / "build_resources" / name, resources / name)
    shutil.copy2(ROOT / "LICENSE", resources / "LICENSE")
    for name, source in LICENCES.items():
        shutil.copy2(ROOT / source, resources / "licences" / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.parse_args()

    if sys.platform != "darwin":
        sys.exit("runs on macOS")
    if not (BUILD / "nazg").exists() or not (BUILD / "fonts").is_dir():
        sys.exit(f"no Release build in {BUILD} -- build it first, as this script's help says")
    missing = [name for name in BUNDLES if not (ROOT / "build_resources" / name).exists()]
    if missing:
        sys.exit(f"not in build_resources/: {', '.join(missing)} -- fetch both bundles first")

    label = version()
    minimum = deployment_target()
    shipped = libraries(BUILD / "nazg")
    architectures = run("lipo", "-archs", str(BUILD / "nazg")).split()

    DIST.mkdir(exist_ok=True)
    output = DIST / f"nazg-{label}-macos-{'-'.join(sorted(architectures))}.zip"
    output.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch) / "Nazg.app"
        app(folder, label, minimum, shipped)
        # Ad hoc, the bundle sealed as a whole; then checked as macOS will.
        run("codesign", "--force", "--deep", "--sign", "-", str(folder))
        run("codesign", "--verify", "--deep", "--strict", str(folder))
        # ditto, as Finder zips: it keeps the bundle's links and attributes, the signature intact.
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(folder), str(output))

    official = manifest(ROOT / "build_resources" / "via_definitions.tar.xz")
    community = manifest(ROOT / "build_resources" / "community_definitions.tar.xz")
    print(f"{output}: {output.stat().st_size / 1024 / 1024:.1f} MB -- {' '.join(architectures)}, macOS {minimum}+, "
          f"with {' '.join(library.name for library in shipped)}; "
          f"VIA {official['commit'][:7]}, community {community['release']}")


if __name__ == "__main__":
    main()
