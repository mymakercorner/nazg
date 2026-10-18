#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Build Nazg's Linux release and package it as an AppImage testers download and run.

One binary for most distributions (build-and-release.md, "Linux: one build for most
distributions"): built Release in AlmaLinux 8's container (tools/package_linux.Containerfile,
glibc 2.28, GCC 13), with the C++ runtime, SDL3 and hidapi linked in, so the binary needs only
glibc and libudev -- checked here before packaging. The build has its own directory,
build_manylinux/, beside the toolchains' own.

The AppImage holds the program folder as Windows' zip does -- nazg, fonts/, icon/, both
definition bundles -- in usr/lib/nazg/, started by AppRun; the desktop entry and the icons
(resources/package/linux/, resources/icon/); Nazg's licence and the libraries'. It carries the
current static runtime, so a distribution without libfuse2 runs it. appimagetool and that runtime
are downloaded once, pinned by release and SHA-256 below.

    python3 tools/package_linux.py            build in the container, then package
    python3 tools/package_linux.py --test     build the tests too and run them in the container

writes dist/nazg-<version>-linux-x86_64.AppImage, the version being NAZG_VERSION in
CMakeLists.txt. Runs on Linux (WSL's Ubuntu will do) with podman; both bundles must be there:
tools/update_via_bundle.py and tools/update_community_bundle.py fetch them. Python 3.8 or later,
standard library only.
"""

import argparse
import hashlib
import io
import json
import lzma
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build_manylinux"
DIST = ROOT / "dist"
CACHE = ROOT / "build_resources" / "cache" / "appimage"
BUNDLES = ["via_definitions.tar.xz", "community_definitions.tar.xz"]

IMAGE = "nazg-manylinux"
CONTAINERFILE = ROOT / "tools" / "package_linux.Containerfile"

# Linked in, so the binary asks for glibc and libudev only -- glibc no newer than the container's.
GLIBC_FLOOR = (2, 28)
NEEDED = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
          "ld-linux-x86-64.so.2", "libudev.so.1"}

# The AppImage tools: appimagetool, and the static runtime it puts at the head of the file.
TOOLS = {
    "appimagetool-x86_64.AppImage": (
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage",
        "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"),
    "runtime-x86_64": (
        "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64",
        "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"),
}

ICON_SIZES = [16, 22, 24, 32, 48, 64, 128, 256]

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


def manifest(bundle):
    """A bundle's manifest.json, out of its tar."""
    with tarfile.open(fileobj=io.BytesIO(lzma.decompress(bundle.read_bytes())), mode="r:") as archive:
        return json.load(archive.extractfile("manifest.json"))


def in_container(command, capture=False):
    """A shell command in the build container, the repository mounted as /nazg."""
    result = subprocess.run(["podman", "run", "--rm", "-v", f"{ROOT}:/nazg", "-w", "/nazg", IMAGE,
                             "bash", "-c", command], capture_output=capture, text=True)
    if result.returncode != 0:
        sys.exit(f"failed in the container: {command}" + (f"\n{result.stderr}" if capture else ""))
    return result.stdout


def build(test):
    """Nazg Release in the container: the C++ runtime, SDL3 and hidapi (hidraw only) linked in."""
    # The image, from the Containerfile: made once, then podman's cache answers.
    if subprocess.run(["podman", "build", "-t", IMAGE, "-f", str(CONTAINERFILE),
                       str(CONTAINERFILE.parent)]).returncode != 0:
        sys.exit(f"the container image was not built: {CONTAINERFILE.name}")
    configure = " ".join([
        "cmake -S . -B build_manylinux -G Ninja -DCMAKE_BUILD_TYPE=Release",
        "-DHIDAPI_WITH_LIBUSB=OFF -DBUILD_SHARED_LIBS=OFF",
        f"-DNAZG_BUILD_TESTS={'ON' if test else 'OFF'}",
        "'-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc -s'"])
    steps = ["source /opt/rh/gcc-toolset-13/enable", configure,
             "cmake --build build_manylinux" + ("" if test else " --target nazg")]
    if test:
        steps.append("ctest --test-dir build_manylinux --output-on-failure")
    in_container(" && ".join(steps))


def check_binary():
    """What the binary asks of the system: glibc no newer than the floor, and nothing beyond NEEDED."""
    dynamic = in_container("readelf -d build_manylinux/nazg && objdump -T build_manylinux/nazg", capture=True)
    needed = set(re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dynamic))
    if needed - NEEDED:
        sys.exit(f"build_manylinux/nazg needs {', '.join(sorted(needed - NEEDED))}: not linked in")
    glibc = max(tuple(int(part) for part in found.split("."))
                for found in re.findall(r"GLIBC_(\d+(?:\.\d+)+)", dynamic))
    if glibc > GLIBC_FLOOR:
        sys.exit(f"build_manylinux/nazg needs glibc {'.'.join(map(str, glibc))}, newer than the container's")
    return sorted(needed), glibc


def tool(name):
    """One of TOOLS, downloaded once into the cache and checked against its SHA-256."""
    url, sha256 = TOOLS[name]
    path = CACHE / name
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != sha256:
        print(f"downloading {url}", flush=True)
        with urllib.request.urlopen(url) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != sha256:
            sys.exit(f"{url}: not the SHA-256 pinned in tools/package_linux.py")
        CACHE.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    path.chmod(0o755)
    return path


def app_dir(folder):
    """The AppImage's contents: the program folder in usr/lib/nazg/, the desktop entry, the icons."""
    program = folder / "usr" / "lib" / "nazg"
    program.mkdir(parents=True)
    shutil.copy2(BUILD / "nazg", program / "nazg")
    for name in BUNDLES:
        shutil.copy2(ROOT / "build_resources" / name, program / name)
    shutil.copytree(BUILD / "fonts", program / "fonts")
    shutil.copytree(BUILD / "icon", program / "icon")
    # A symbolic link: SDL finds the program folder from /proc/self/exe, which resolves it.
    (folder / "AppRun").symlink_to("usr/lib/nazg/nazg")

    entry = ROOT / "resources" / "package" / "linux" / "nazg.desktop"
    shutil.copy2(entry, folder / "nazg.desktop")
    (folder / "usr" / "share" / "applications").mkdir(parents=True)
    shutil.copy2(entry, folder / "usr" / "share" / "applications" / "nazg.desktop")

    icons = ROOT / "resources" / "icon"
    shutil.copy2(icons / "png" / "nazg-256.png", folder / "nazg.png")
    (folder / ".DirIcon").symlink_to("nazg.png")
    for size in ICON_SIZES:
        target = folder / "usr" / "share" / "icons" / "hicolor" / f"{size}x{size}" / "apps"
        target.mkdir(parents=True)
        shutil.copy2(icons / "png" / f"nazg-{size}.png", target / "nazg.png")
    scalable = folder / "usr" / "share" / "icons" / "hicolor" / "scalable" / "apps"
    scalable.mkdir(parents=True)
    shutil.copy2(icons / "nazg.svg", scalable / "nazg.svg")

    doc = folder / "usr" / "share" / "doc" / "nazg"
    (doc / "licences").mkdir(parents=True)
    shutil.copy2(ROOT / "LICENSE", doc / "LICENSE")
    for name, source in LICENCES.items():
        shutil.copy2(ROOT / source, doc / "licences" / name)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--test", action="store_true", help="build the tests too and run them in the container")
    arguments = parser.parse_args()

    if not sys.platform.startswith("linux"):
        sys.exit("runs on Linux -- on Windows, in WSL's Ubuntu")
    if not shutil.which("podman"):
        sys.exit("podman is not installed -- build-and-release.md, \"The AppImage\"")
    missing = [name for name in BUNDLES if not (ROOT / "build_resources" / name).exists()]
    if missing:
        sys.exit(f"not in build_resources/: {', '.join(missing)} -- fetch both bundles first")

    label = version()
    build(arguments.test)
    needed, glibc = check_binary()

    appimagetool = tool("appimagetool-x86_64.AppImage")
    runtime = tool("runtime-x86_64")
    DIST.mkdir(exist_ok=True)
    output = DIST / f"nazg-{label}-linux-x86_64.AppImage"
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch) / "Nazg.AppDir"
        app_dir(folder)
        # Extracted and run, so appimagetool itself needs no FUSE; no AppStream file to check.
        subprocess.run([str(appimagetool), "--appimage-extract-and-run", "--no-appstream",
                        "--runtime-file", str(runtime), str(folder), str(output)],
                       env=dict(os.environ, ARCH="x86_64", VERSION=label), check=True)

    official = manifest(ROOT / "build_resources" / "via_definitions.tar.xz")
    community = manifest(ROOT / "build_resources" / "community_definitions.tar.xz")
    print(f"{output}: {output.stat().st_size / 1024 / 1024:.1f} MB -- glibc {'.'.join(map(str, glibc))}+, "
          f"needs {' '.join(needed)}; VIA {official['commit'][:7]}, community {community['release']}")


if __name__ == "__main__":
    main()
