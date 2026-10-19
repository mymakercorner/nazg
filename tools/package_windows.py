#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Package a Windows build of Nazg as a zip testers unpack and run.

From a Release build in build_VS2022/, it gathers what nazg.exe needs beside it -- SDL3's and
hidapi's DLLs (minlzma is linked in), fonts/, both definition bundles -- plus the Visual C++
runtime DLLs the build links (MSVCP140, VCRUNTIME140, VCRUNTIME140_1; the rest of the C
runtime ships with Windows 10 and later), Microsoft's app-local deployment, so nothing has to
be installed: started from the package, nazg.exe loads them from its own folder (checked
2026-10-11). Then a README for testers (resources/package/README.txt), Nazg's LICENSE, the
licences of the libraries it links, and the user manual as one HTML page (tools/build_manual.py),
which it also writes beside the zip for the release.

    cmake --build build_VS2022 --config Release --target nazg
    python tools/package_windows.py

writes dist/nazg-<version>-windows-x64.zip and dist/nazg-<version>-manual.html, the version
being NAZG_VERSION in CMakeLists.txt. Both bundles must be there: tools/update_via_bundle.py and
tools/update_community_bundle.py fetch them. Python 3.8 or later, with the `markdown` package
(pip install markdown) for the manual.
"""

import argparse
import io
import json
import lzma
import re
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

import build_manual

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build_VS2022" / "Release"
DIST = ROOT / "dist"

BESIDE = ["nazg.exe", "SDL3.dll", "hidapi.dll",
          "via_definitions.tar.xz", "community_definitions.tar.xz"]
RUNTIME = ["msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"]

# The libraries Nazg links, and the licence file each one ships: the folder they go in.
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


def runtime_folder():
    """The newest Visual C++ runtime redistributable, through vswhere."""
    vswhere = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    if not vswhere.exists():
        sys.exit("vswhere.exe not found: is Visual Studio installed?")
    installation = subprocess.run([str(vswhere), "-latest", "-property", "installationPath"], capture_output=True,
                                  text=True, check=True).stdout.strip()
    folders = sorted(Path(installation).glob("VC/Redist/MSVC/*/x64/Microsoft.VC14*.CRT"))
    if not folders:
        sys.exit(f"no Visual C++ runtime redistributable under {installation}")
    return folders[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.parse_args()

    missing = [name for name in BESIDE if not (BUILD / name).exists()]
    if missing:
        sys.exit(f"not in {BUILD}: {', '.join(missing)} -- build Release, and fetch both bundles first")

    label = version()
    official = manifest(BUILD / "via_definitions.tar.xz")
    community = manifest(BUILD / "community_definitions.tar.xz")
    readme = ((ROOT / "resources" / "package" / "README.txt").read_text(encoding="utf-8")
              .replace("@VERSION@", label)
              .replace("@OFFICIAL@", str(official["v2"] + official["v3"]))
              .replace("@COMMUNITY@", str(community["definitions"])))

    top = f"nazg-{label}"
    DIST.mkdir(exist_ok=True)
    output = DIST / f"{top}-windows-x64.zip"
    runtime = runtime_folder()
    manual = build_manual.build(label)
    (DIST / f"{top}-manual.html").write_text(manual, encoding="utf-8")
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in BESIDE:
            archive.write(BUILD / name, f"{top}/{name}")
        for name in RUNTIME:
            archive.write(runtime / name, f"{top}/{name}")
        for font in sorted((BUILD / "fonts").iterdir()):
            archive.write(font, f"{top}/fonts/{font.name}")
        for name, source in LICENCES.items():
            archive.write(ROOT / source, f"{top}/licences/{name}")
        archive.write(ROOT / "LICENSE", f"{top}/LICENSE.txt")
        # CRLF, so Notepad on an old Windows shows the lines.
        archive.writestr(f"{top}/README.txt", readme.replace("\r\n", "\n").replace("\n", "\r\n"))
        archive.writestr(f"{top}/nazg-manual.html", manual)

    print(f"{output}: {output.stat().st_size / 1024 / 1024:.1f} MB -- VIA {official['commit'][:7]}, "
          f"community {community['release']}, runtime from {runtime.parent.parent.name}")


if __name__ == "__main__":
    main()
