#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Package a macOS build of Nazg as an app testers unzip and open.

From a Release build in build_macOS/ -- SDL3 and hidapi linked in (-DBUILD_SHARED_LIBS=OFF), so
the binary asks only for macOS's own libraries and frameworks, checked here -- it makes Nazg.app
(resources/icon/README.md, "Where each platform takes it"): Info.plist from
resources/package/macos/ with the version and the minimum macOS filled in, the executable in
Contents/MacOS/, and in Contents/Resources/ -- where SDL_GetBasePath() points inside a bundle --
the icon, fonts/, both definition bundles and the licences. The app is signed ad hoc, as a whole:
not a developer's signature, so Gatekeeper asks the tester to allow it once, but a sealed bundle,
which macOS would otherwise call damaged.

    cmake -S . -B build_macOS -G "Ninja Multi-Config" -DBUILD_SHARED_LIBS=OFF -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
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


def check_binary(binary):
    """Nothing but the system's libraries, and the architectures built: the zip's name says them."""
    linked = [line.split(" (")[0].strip() for line in run("otool", "-L", str(binary)).splitlines()[1:]]
    foreign = [path for path in linked if not path.startswith(SYSTEM)]
    if foreign:
        sys.exit(f"{binary} needs {', '.join(foreign)}: not linked in -- configure with -DBUILD_SHARED_LIBS=OFF")
    return run("lipo", "-archs", str(binary)).split()


def app(folder, label, minimum):
    """Nazg.app's contents."""
    contents = folder / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    resources = contents / "Resources"
    (resources / "licences").mkdir(parents=True)

    plist = ((ROOT / "resources" / "package" / "macos" / "Info.plist").read_text(encoding="utf-8")
             .replace("@NAZG_VERSION@", label)
             .replace("@CMAKE_OSX_DEPLOYMENT_TARGET@", minimum))
    (contents / "Info.plist").write_text(plist, encoding="utf-8")
    (contents / "PkgInfo").write_text("APPL????", encoding="ascii")

    shutil.copy2(BUILD / "nazg", contents / "MacOS" / "nazg")
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
    architectures = check_binary(BUILD / "nazg")

    DIST.mkdir(exist_ok=True)
    output = DIST / f"nazg-{label}-macos-{'-'.join(sorted(architectures))}.zip"
    output.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch) / "Nazg.app"
        app(folder, label, minimum)
        # Ad hoc, the bundle sealed as a whole; then checked as macOS will.
        run("codesign", "--force", "--deep", "--sign", "-", str(folder))
        run("codesign", "--verify", "--deep", "--strict", str(folder))
        # ditto, as Finder zips: it keeps the bundle's links and attributes, the signature intact.
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(folder), str(output))

    official = manifest(ROOT / "build_resources" / "via_definitions.tar.xz")
    community = manifest(ROOT / "build_resources" / "community_definitions.tar.xz")
    print(f"{output}: {output.stat().st_size / 1024 / 1024:.1f} MB -- {' '.join(architectures)}, macOS {minimum}+; "
          f"VIA {official['commit'][:7]}, community {community['release']}")


if __name__ == "__main__":
    main()
