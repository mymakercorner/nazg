#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Build the user manual as one HTML file, for the release.

docs/user-manual.md is the source, read on GitHub as it is. A release carries it as a single
page that opens in any browser with nothing beside it: the screenshots of docs/user-manual/
embedded, the styles inline, light or dark as the system is. A click on a screenshot shows it
at full size.

    python tools/build_manual.py

writes dist/nazg-<version>-manual.html, the version being NAZG_VERSION in CMakeLists.txt --
the GitHub release's asset, since the AppImage is one file and carries nothing beside it.
tools/package_windows.py puts the same page in the zip. Python 3.8 or later, with the
`markdown` package (pip install markdown).
"""

import argparse
import base64
import html
import re
import sys
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("the markdown package is missing: pip install markdown")

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "docs" / "user-manual.md"
DIST = ROOT / "dist"

TYPES = {".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml"}

STYLE = """
:root { --text: #1f2328; --muted: #59636e; --page: #ffffff; --line: #d1d9e0; --code: #f6f8fa;
        --link: #0969da; --shade: rgba(0, 0, 0, 0.75); }
@media (prefers-color-scheme: dark) {
  :root { --text: #e6edf3; --muted: #9198a1; --page: #0d1117; --line: #3d444d; --code: #151b23;
          --link: #4493f8; --shade: rgba(0, 0, 0, 0.85); }
}
html { background: var(--page); }
body { color: var(--text); background: var(--page); margin: 0 auto; max-width: 1000px;
       padding: 24px 16px 64px; font: 16px/1.55 -apple-system, "Segoe UI", "Noto Sans", Helvetica,
       Arial, sans-serif; }
h1, h2, h3 { line-height: 1.25; margin: 1.6em 0 0.6em; }
h1 { font-size: 2em; margin-top: 0; }
h2 { font-size: 1.5em; padding-bottom: 0.3em; border-bottom: 1px solid var(--line); }
h3 { font-size: 1.2em; }
a { color: var(--link); text-decoration: none; }
a:hover { text-decoration: underline; }
code { background: var(--code); border-radius: 4px; padding: 0.1em 0.35em; font-size: 0.9em;
       font-family: Consolas, "Liberation Mono", Menlo, monospace; }
pre { background: var(--code); border-radius: 6px; padding: 12px 16px; overflow-x: auto; }
pre code { padding: 0; background: none; }
table { border-collapse: collapse; margin: 1em 0; }
th, td { border: 1px solid var(--line); padding: 6px 12px; text-align: left; vertical-align: top; }
hr { border: 0; border-top: 1px solid var(--line); margin: 2em 0; }
img { max-width: 100%; height: auto; border: 1px solid var(--line); border-radius: 6px;
      cursor: zoom-in; display: block; margin: 0.8em 0; }
#zoom { display: none; position: fixed; inset: 0; background: var(--shade); overflow: auto;
        cursor: zoom-out; z-index: 1; }
#zoom.shown { display: block; }
#zoom img { max-width: none; margin: 24px auto; cursor: zoom-out; border: 0; }
"""

SCRIPT = """
const zoom = document.getElementById('zoom');
document.querySelectorAll('main img').forEach(img => img.addEventListener('click', () => {
  zoom.replaceChildren(Object.assign(new Image(), { src: img.src, alt: img.alt }));
  zoom.classList.add('shown');
}));
zoom.addEventListener('click', () => zoom.classList.remove('shown'));
document.addEventListener('keydown', e => { if (e.key === 'Escape') zoom.classList.remove('shown'); });
"""


def version():
    found = re.search(r'set\(NAZG_VERSION "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not found:
        sys.exit("CMakeLists.txt sets no NAZG_VERSION")
    return found.group(1)


def embed(match):
    """An <img> whose src is a file beside the manual, made a data URI."""
    source = html.unescape(match.group(2))
    if re.match(r"[a-z]+:", source):
        return match.group(0)
    path = (SOURCE.parent / source).resolve()
    kind = TYPES.get(path.suffix.lower())
    if kind is None or not path.exists():
        sys.exit(f"{SOURCE.name}: cannot embed {source}")
    data = base64.b64encode(path.read_bytes()).decode("ascii")
    return f'{match.group(1)}data:{kind};base64,{data}{match.group(3)}'


def build(label):
    """The manual as one HTML page."""
    text = SOURCE.read_text(encoding="utf-8")
    body = markdown.markdown(text, extensions=["tables", "toc", "fenced_code", "sane_lists"])
    body = re.sub(r'(<img [^>]*src=")([^"]+)(")', embed, body)

    # Every in-page link must land on a heading: a renamed section would otherwise break it silently.
    ids = set(re.findall(r'id="([^"]+)"', body))
    broken = sorted(set(re.findall(r'href="#([^"]+)"', body)) - ids)
    if broken:
        sys.exit(f"{SOURCE.name}: links to no heading: {', '.join(broken)}")

    return (f'<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>Nazg {html.escape(label)} user manual</title>\n<style>{STYLE}</style>\n</head>\n'
            f'<body>\n<main>\n{body}\n</main>\n<div id="zoom"></div>\n<script>{SCRIPT}</script>\n'
            f'</body>\n</html>\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--output", type=Path, help="where to write it (default: dist/nazg-<version>-manual.html)")
    arguments = parser.parse_args()

    label = version()
    output = arguments.output or DIST / f"nazg-{label}-manual.html"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build(label), encoding="utf-8")
    print(f"{output}: {output.stat().st_size / 1024 / 1024:.1f} MB")


if __name__ == "__main__":
    main()
