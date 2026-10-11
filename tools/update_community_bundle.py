#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Fetch the bundle of community keyboard definitions.

The community definitions -- VIA definitions vendors publish for boards VIA's registry lacks
or has older -- live in github.com/mymakercorner/nazg-community-definitions, which publishes
them as one asset of each release: community_definitions.tar.xz, built there by
tools/build_bundle.py. Nazg pins a release, not a commit, by its tag and the asset's SHA-256,
in resources/nazg-community-definitions.release: removing a file from that repository for good
rewrites its history, which would break a pinned commit, never a published asset. See
docs/research_material/via-registry.md, "Decisions to take".

    python tools/update_community_bundle.py                    fetch the pinned release
    python tools/update_community_bundle.py --update <tag>     pin another release first
    python tools/update_community_bundle.py --from-file <path> take a bundle built locally,
                                                               checked against the pin

--update with --from-file pins the local file under <tag>: for a release about to be
published with that very file. The output, build_resources/community_definitions.tar.xz, is not
committed: the build copies it beside the executable, and releases ship it. Python 3.8 or
later, standard library only.
"""

import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

REPOSITORY = "mymakercorner/nazg-community-definitions"
ASSET = "community_definitions.tar.xz"
ROOT = Path(__file__).resolve().parent.parent
PIN = ROOT / "resources" / "nazg-community-definitions.release"
OUTPUT = ROOT / "build_resources" / ASSET

PIN_HEADER = """\
# The release of github.com/mymakercorner/nazg-community-definitions whose bundle of community
# definitions Nazg ships: its tag, and the SHA-256 of its community_definitions.tar.xz. Moved by
# `python tools/update_community_bundle.py --update <tag>`; see that script.
"""


def read_pin():
    fields = {}
    for line in PIN.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            key, _, value = line.partition(" ")
            fields[key] = value.strip()
    if "tag" not in fields or "sha256" not in fields:
        sys.exit(f"{PIN}: needs a tag and a sha256 line")
    return fields["tag"], fields["sha256"]


def write_pin(tag, digest):
    PIN.write_text(PIN_HEADER + f"tag {tag}\nsha256 {digest}\n", encoding="utf-8")


def download(tag):
    url = f"https://github.com/{REPOSITORY}/releases/download/{tag}/{ASSET}"
    print(f"downloading {url}")
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--update", metavar="TAG", help="pin this release first")
    parser.add_argument("--from-file", type=Path, help="a bundle built locally, instead of downloading")
    arguments = parser.parse_args()

    if arguments.update:
        tag = arguments.update
        data = arguments.from_file.read_bytes() if arguments.from_file else download(tag)
        digest = hashlib.sha256(data).hexdigest()
        write_pin(tag, digest)
        print(f"pinned {tag}, sha256 {digest}")
    else:
        tag, digest = read_pin()
        data = arguments.from_file.read_bytes() if arguments.from_file else download(tag)
        if hashlib.sha256(data).hexdigest() != digest:
            sys.exit(f"the bundle is not release {tag}'s: its SHA-256 differs from {PIN.name}'s")

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_bytes(data)
    print(f"{OUTPUT}: release {tag}, {len(data) / 1024:.0f} KB")


if __name__ == "__main__":
    main()
