#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Build the bundle of VIA's official keyboard definitions.

The definitions come from github.com/the-via/keyboards, at the commit pinned in
resources/via-keyboards.commit. Every V3 (v3/) and V2 (src/) definition goes into one tar,
named by VIA's id -- vendorId * 65536 + productId, in decimal -- byte for byte as the
repository has it, and the tar is compressed to a single-block XZ stream, which is what
Nazg's minlzma decodes. See docs/research_material/via-registry.md.

    python tools/update_via_bundle.py              build from the pinned commit
    python tools/update_via_bundle.py --update     move the pin to master first
    python tools/update_via_bundle.py --from-dir ../via-keyboards
                                                   build from a local clone, offline

The output, build_resources/via_definitions.tar.xz, is not committed: the build copies it
beside the executable when it is there, and releases ship it. The same commit always gives
the same bytes. Python 3.8 or later, standard library only.
"""

import argparse
import io
import json
import lzma
import re
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

REPOSITORY = "the-via/keyboards"
ROOT = Path(__file__).resolve().parent.parent
PIN = ROOT / "resources" / "via-keyboards.commit"
OUTPUT = ROOT / "build_resources" / "via_definitions.tar.xz"
CACHE = ROOT / "build_resources" / "cache"

# The repository's folder for each definition version, and the bundle's.
FOLDERS = {"v3": "v3", "src": "v2"}


class BundleError(Exception):
    pass


def read_pin():
    lines = [line.strip() for line in PIN.read_text(encoding="utf-8").splitlines()]
    commits = [line for line in lines if line and not line.startswith("#")]
    if len(commits) != 1 or not re.fullmatch(r"[0-9a-f]{40}", commits[0]):
        raise BundleError(f"{PIN} must hold one full commit id")
    return commits[0]


def write_pin(commit):
    text = PIN.read_text(encoding="utf-8")
    with open(PIN, "w", encoding="utf-8", newline="\n") as pin:
        pin.write(re.sub(r"(?m)^[0-9a-f]{40}$", commit, text))


def download(url):
    print(f"downloading {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "nazg-update-via-bundle"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def tarball_commit(data):
    """The commit a GitHub tarball was made from: git archive writes it in the pax header."""
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        commit = archive.pax_headers.get("comment", "")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise BundleError("the tarball does not say which commit it holds")
    return commit


def fetch_commit(commit):
    """The tarball of one commit, downloaded once and then kept in the cache."""
    cached = CACHE / f"keyboards-{commit}.tar.gz"
    if cached.exists():
        data = cached.read_bytes()
    else:
        data = download(f"https://codeload.github.com/{REPOSITORY}/tar.gz/{commit}")
        CACHE.mkdir(parents=True, exist_ok=True)
        cached.write_bytes(data)
    if tarball_commit(data) != commit:
        raise BundleError(f"the tarball for {commit} holds another commit")
    return data


def files_from_tarball(data):
    """(path inside the repository, bytes) for every file, the top folder stripped."""
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive:
            if member.isfile():
                path = member.name.split("/", 1)[1] if "/" in member.name else member.name
                yield path, archive.extractfile(member).read()


def files_from_dir(directory):
    for path in sorted(directory.rglob("*")):
        if path.is_file():
            yield path.relative_to(directory).as_posix(), path.read_bytes()


def usb_id(value, what, path):
    """As VIA and Nazg read it: a string is hex, with or without 0x; a number is itself."""
    if isinstance(value, int) and 0 <= value <= 0xFFFF:
        return value
    if isinstance(value, str) and re.fullmatch(r"\s*(0[xX])?[0-9a-fA-F]{1,4}\s*", value):
        return int(re.sub(r"^0[xX]", "", value.strip()), 16)
    raise BundleError(f"{path}: unreadable {what} {value!r}")


def collect(files):
    """{bundle path: (repository path, bytes, name)}, failing on two files with one id."""
    definitions = {}
    for path, data in files:
        folder = path.split("/", 1)[0]
        if folder not in FOLDERS or not path.endswith(".json"):
            continue
        try:
            document = json.loads(data.decode("utf-8-sig"))
        except (UnicodeDecodeError, json.JSONDecodeError) as failure:
            raise BundleError(f"{path}: not valid JSON: {failure}")
        if not isinstance(document, dict) or "vendorId" not in document or "productId" not in document:
            raise BundleError(f"{path}: not a keyboard definition")

        vendor = usb_id(document["vendorId"], "vendorId", path)
        product = usb_id(document["productId"], "productId", path)
        target = f"{FOLDERS[folder]}/{vendor * 65536 + product}.json"
        if target in definitions:
            raise BundleError(f"{path} and {definitions[target][0]} are both {vendor:04X}:{product:04X}")

        name = document.get("name")
        name = name if isinstance(name, str) else (name or {}).get("options", ["?"])[0]
        definitions[target] = (path, data, name)
    return definitions


def make_bundle(definitions, commit):
    """The tar, compressed. Sorted names and fixed metadata: the same input, the same bytes."""
    manifest = {
        "source": f"https://github.com/{REPOSITORY}",
        "commit": commit,
        "v2": sum(1 for target in definitions if target.startswith("v2/")),
        "v3": sum(1 for target in definitions if target.startswith("v3/")),
        "generator": "tools/update_via_bundle.py",
    }
    entries = {"manifest.json": (json.dumps(manifest, indent=2) + "\n").encode("utf-8")}
    entries.update({target: data for target, (_, data, _) in definitions.items()})

    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name in sorted(entries):
            info = tarfile.TarInfo(name)
            info.size = len(entries[name])
            info.mode = 0o644
            info.mtime = 0
            archive.addfile(info, io.BytesIO(entries[name]))

    # One call compresses into a single block, the only kind minlzma decodes.
    return lzma.compress(buffer.getvalue(), format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC64,
                         preset=9 | lzma.PRESET_EXTREME)


def read_bundle(path):
    """{bundle path: bytes} of an existing bundle, to report what changed."""
    try:
        data = lzma.decompress(path.read_bytes())
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as archive:
            return {member.name: archive.extractfile(member).read() for member in archive if member.isfile()}
    except (OSError, lzma.LZMAError, tarfile.TarError):
        return {}


def report_changes(previous, definitions):
    if not previous:
        return
    current = {target: data for target, (_, data, _) in definitions.items()}
    known = {target for target in previous if target != "manifest.json"}
    added = sorted(set(current) - known)
    removed = sorted(known - set(current))
    changed = sorted(target for target in set(current) & known if current[target] != previous[target])
    print(f"since the previous bundle: {len(added)} added, {len(removed)} removed, {len(changed)} changed")
    for label, targets in (("added", added), ("changed", changed)):
        for target in targets:
            print(f"  {label:8} {target}  {definitions[target][2]}  ({definitions[target][0]})")
    for target in removed:
        print(f"  removed  {target}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--update", action="store_true", help="move the pin to the repository's master first")
    source.add_argument("--from-dir", type=Path, help="read a local clone instead of downloading")
    parser.add_argument("--output", type=Path, default=OUTPUT, help=f"default: {OUTPUT.relative_to(ROOT)}")
    arguments = parser.parse_args()

    try:
        if arguments.from_dir:
            directory = arguments.from_dir.resolve()
            try:
                commit = subprocess.run(["git", "-C", str(directory), "rev-parse", "HEAD"], check=True,
                                        capture_output=True, text=True).stdout.strip()
            except (OSError, subprocess.CalledProcessError):
                commit = "unknown"
            print(f"reading {directory} at {commit}; the pin is left alone")
            files = files_from_dir(directory)
        else:
            if arguments.update:
                data = download(f"https://codeload.github.com/{REPOSITORY}/tar.gz/refs/heads/master")
                commit = tarball_commit(data)
                CACHE.mkdir(parents=True, exist_ok=True)
                (CACHE / f"keyboards-{commit}.tar.gz").write_bytes(data)
                if commit != read_pin():
                    write_pin(commit)
                    print(f"pin moved to {commit}")
                else:
                    print(f"the pin is already master, {commit}")
            commit = read_pin()
            files = files_from_tarball(fetch_commit(commit))

        definitions = collect(files)
        bundle = make_bundle(definitions, commit)

        output = arguments.output.resolve()
        previous = read_bundle(output) if output.exists() else {}
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(bundle)

        v2 = sum(1 for target in definitions if target.startswith("v2/"))
        print(f"{output}: {len(definitions)} definitions ({len(definitions) - v2} V3, {v2} V2), "
              f"{len(bundle) / 1024:.1f} KB, from {REPOSITORY} {commit}")
        report_changes(previous, definitions)
    except BundleError as failure:
        print(f"error: {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
