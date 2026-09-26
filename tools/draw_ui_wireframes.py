#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
"""Draw the wireframes of docs/research_material/ui-design.md as SVG files.

Every picture is a function below, laid out by hand from a few pieces -- a frame, the header,
the section column, the strip, a 60% board, the keycode picker. Colours are fixed hex values,
so the pictures stay light in a dark theme. Text widths are estimated, not measured: check a
changed picture by eye.

    python tools/draw_ui_wireframes.py             write into docs/research_material/ui-design
    python tools/draw_ui_wireframes.py <folder>    write elsewhere

The SVG files are committed; run this again after changing a picture. Python 3.8 or later,
standard library only.
"""

import argparse
import html
import os

OUT = ""

FONT = "Segoe UI, Helvetica, Arial, sans-serif"
MONO = "Consolas, Menlo, monospace"
C = dict(
    white="#FFFFFF", border="#C9C7BF", hair="#E2E0D8", s1="#F4F3EF", text="#2C2C2A", text2="#5F5E5A",
    muted="#888780", abg="#E6F1FB", atx="#0C447C", abd="#378ADD", wbg="#FAEEDA", wtx="#854F0B",
    wbd="#EF9F27", pbg="#EEEDFE", ptx="#3C3489", pbd="#7F77DD", ok="#3B6D11", bad="#A32D2D",
)


class Svg:
    def __init__(self, w, h):
        self.w, self.h, self.e = w, h, []

    def rect(self, x, y, w, h, fill=C["white"], stroke=C["border"], rx=6, sw=1, dash=None, op=None):
        a = f' stroke-dasharray="{dash}"' if dash else ""
        a += f' opacity="{op}"' if op is not None else ""
        st = f'stroke="{stroke}" stroke-width="{sw}"' if stroke else 'stroke="none"'
        self.e.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}" {st}{a}/>')

    def text(self, x, y, s, size=12, fill=C["text2"], anchor="start", weight=400, op=None, family=FONT):
        a = f' opacity="{op}"' if op is not None else ""
        self.e.append(f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" font-size="{size}" fill="{fill}" '
                      f'text-anchor="{anchor}" font-weight="{weight}"{a}>{html.escape(s)}</text>')

    def line(self, x1, y1, x2, y2, stroke=C["hair"], sw=1):
        self.e.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{sw}"/>')

    def save(self, name):
        body = "\n".join(self.e)
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                    f'viewBox="0 0 {self.w} {self.h}">\n{body}\n</svg>\n')


def tw(s, size=12):
    return len(s) * size * 0.55


ROWS = ["Esc 1 2 3 4 5 6 7 8 9 0 - = Bksp:2", "Tab:1.5 Q W E R T Y U I O P [ ] \\:1.5",
        "Caps:1.75 A S D F G H J K L ; ' Enter:2.25", "Shift:2.25 Z X C V B N M , . / Shift:2.75",
        "Ctrl:1.25 Win:1.25 Alt:1.25 :6.25 Alt:1.25 Fn:1.25 Menu:1.25 Ctrl:1.25"]


def keys():
    out = []
    for y, r in enumerate(ROWS):
        x = 0.0
        for t in r.split(" "):
            label, _, w = t.partition(":")
            w = float(w or 1)
            out.append((x, y, w, label))
            x += w
    return out


def board(s, x0, y0, u, sel=None, hot=(), dim=False, blank=False, shuffle=False):
    ks = keys()
    labels = [k[3] for k in ks]
    if shuffle:
        labels = [ks[(i * 7 + 3) % len(ks)][3] for i in range(len(ks))]
    for i, (x, y, w, _) in enumerate(ks):
        is_hot = i in hot
        fill, stroke, tx, sw = C["s1"], C["border"], C["text2"], 1
        if is_hot:
            fill, stroke, tx = C["wbg"], C["wbd"], C["wtx"]
        if sel == i:
            stroke, tx, sw = C["abd"], C["atx"], 2
        op = 0.45 if dim and not is_hot else None
        s.rect(x0 + x * u + 1, y0 + y * u + 1, w * u - 2, u - 2, fill, stroke, rx=3, sw=sw, op=op)
        size = 11 if u >= 20 else 9
        if not blank and labels[i] and u >= 14 and tw(labels[i], size) * 0.9 <= w * u - 4:
            s.text(x0 + (x + w / 2) * u, y0 + y * u + u / 2 + size * 0.35, labels[i], size, tx, "middle", op=op)
    return 15 * u, 5 * u


def frame(s, x, y, w, h):
    s.rect(x, y, w, h, C["white"], C["border"], rx=12)


def header(s, x, y, w, name, proto="", lock=None, open_menu=False):
    if name:
        nw = tw(name, 13) + 26
        if open_menu:
            s.rect(x + 8, y + 6, nw, 22, C["s1"], C["border"], rx=6)
        s.text(x + 14, y + 22, name + "  ▾", 13, C["text"])
        if proto:
            s.text(x + 14 + nw, y + 22, proto, 12, C["muted"])
    else:
        s.text(x + 14, y + 22, "No board open", 12, C["muted"])
    right = x + w - 14
    s.text(right, y + 22, "⚙", 15, C["text2"], "end")
    if lock:
        s.text(right - 24, y + 22, "Locked" if lock == "on" else "Unlocked", 12,
               C["wtx"] if lock == "on" else C["muted"], "end")
    s.line(x, y + 34, x + w, y + 34)
    return 34


NAV = ["Keymap", "Layout", "Macros", "Tap dance", "Combos", "QMK settings", "Diagnostics"]


def nav(s, x, y, h, on, items=NAV, w=150):
    s.rect(x, y, w, h, C["s1"], None, rx=0)
    s.line(x + w, y, x + w, y + h)
    for i, it in enumerate(items):
        yy = y + 8 + i * 30
        if it == on:
            s.rect(x + 8, yy, w - 16, 26, C["abg"], None, rx=6)
        s.text(x + 18, yy + 17, it, 13, C["atx"] if it == on else C["text2"])
    return w


def strip(s, x, y, items, on, label=""):
    xx = x
    if label:
        s.text(xx, y + 15, label, 12, C["muted"])
        xx += tw(label) + 8
    for i, it in enumerate(items):
        w = tw(it) + 20
        s.rect(xx, y, w, 21, C["abg"] if i == on else C["white"], C["abd"] if i == on else C["hair"], rx=6)
        s.text(xx + w / 2, y + 15, it, 12, C["atx"] if i == on else C["text2"], "middle")
        xx += w + 4
    return 21


def button(s, x, y, label, op=None):
    w = tw(label) + 20
    s.rect(x, y, w, 22, C["white"], C["border"], rx=6, op=op)
    s.text(x + w / 2, y + 15, label, 12, C["text"], "middle", op=op)
    return w


def badge(s, x, y, label):
    w = tw(label, 11) + 16
    s.rect(x, y, w, 18, C["pbg"], None, rx=6)
    s.text(x + w / 2, y + 13, label, 11, C["ptx"], "middle")
    return w


def picker(s, x, y, w):
    s.line(x, y, x + w, y)
    xx = x
    for i, t in enumerate(["Basic", "Layers", "Modifiers", "Media", "Lighting", "Macros"]):
        tw_ = tw(t) + 20
        if xx + tw_ > x + w - 130:
            break
        s.rect(xx, y + 8, tw_, 21, C["abg"] if i == 0 else C["white"], C["abd"] if i == 0 else C["hair"], rx=6)
        s.text(xx + tw_ / 2, y + 23, t, 12, C["atx"] if i == 0 else C["text2"], "middle")
        xx += tw_ + 4
    s.rect(x + w - 120, y + 8, 120, 21, C["white"], C["border"], rx=6)
    s.text(x + w - 110, y + 23, "Search", 12, C["muted"])
    for i, t in enumerate("A B C D E F G H I J K L M N O P".split()):
        s.rect(x + i * 30, y + 38, 26, 22, C["white"], C["border"], rx=4)
        s.text(x + i * 30 + 13, y + 53, t, 11, C["text2"], "middle")
    return 64


def list_row(s, x, y, w, name, proto):
    s.rect(x, y, w, 34, C["white"], C["hair"], rx=8)
    s.text(x + 12, y + 22, name, 13, C["text"])
    bw = tw("Open") + 20
    button(s, x + w - bw - 10, y + 6, "Open")
    badge(s, x + w - bw - 20 - tw(proto, 11) - 16, y + 8, proto)


# --- 1. workspace division, the four apps -------------------------------------------------

def block(s, x, y, w, h, label, kind="n", size=11):
    fill, stroke, tx = {"n": (C["white"], C["hair"], C["text2"]), "b": (C["abg"], C["abd"], C["atx"]),
                        "w": (C["wbg"], C["wbd"], C["wtx"]), "p": (C["pbg"], C["pbd"], C["ptx"]),
                        "d": (C["white"], C["border"], C["muted"])}[kind]
    s.rect(x, y, w, h, fill, stroke, rx=5, dash="4 3" if kind == "d" else None)
    lines = label.split("\n")
    y0 = y + h / 2 - (len(lines) - 1) * (size + 3) / 2 + size * 0.35
    for i, l in enumerate(lines):
        s.text(x + w / 2, y0 + i * (size + 3), l, size, tx, "middle")


def comparison():
    W, H = 330, 230
    s = Svg(2 * W + 20, 2 * (H + 26) + 4)

    def cap(x, y, t):
        s.text(x, y + 14, t, 13, C["text"], weight=600)

    # Vial
    x, y = 0, 0
    cap(x, y, "Vial: tabs per feature")
    y += 22
    frame(s, x, y, W, H)
    block(s, x + 8, y + 8, W - 16, 20, "Device combo, Refresh, menus")
    block(s, x + 8, y + 32, W - 16, 20, "Keymap | Layout | Macros | Tap Dance | Combos | ...")
    block(s, x + 8, y + 56, W - 16, 20, "Layers 0 1 2 3   zoom + -")
    block(s, x + 8, y + 80, W - 16, 80, "Board", "b", 12)
    block(s, x + 8, y + 164, W - 16, H - 172, "Palette tabs: Basic, ISO, Layers, Quantum...")
    # VIA
    x, y = W + 20, 0
    cap(x, y, "VIA: the board stays, the sub-pane changes")
    y += 22
    frame(s, x, y, W, H)
    block(s, x + 8, y + 8, W - 16, 20, "Configure, Key tester, Settings... | board badge")
    block(s, x + 8, y + 32, 70, H - 40, "Keymap\nLayouts\nMacros\nmenus")
    block(s, x + 82, y + 32, W - 90, 110, "Board (2D or 3D)\nLayer 0 1 2 3", "b", 12)
    block(s, x + 82, y + 146, W - 90, H - 154, "Sub-pane: picker,\nmacro editor, sliders")
    # ZMK
    x, y = 0, H + 26
    cap(x, y, "ZMK Studio: one screen")
    y += 22
    frame(s, x, y, W, H)
    block(s, x + 8, y + 8, W - 16, 20, "Logo | device menu | undo redo save discard")
    block(s, x + 8, y + 32, 84, H - 40, "Layout menu\n\nLayers, named\n+ - rename\ndrag")
    block(s, x + 96, y + 32, W - 104, 110, "Board, zoom menu", "b", 12)
    block(s, x + 96, y + 146, W - 104, H - 154, "Binding: behaviour,\nthen typed parameters")
    # Nazg
    x, y = W + 20, H + 26
    cap(x, y, "Nazg")
    y += 22
    frame(s, x, y, W, H)
    block(s, x + 8, y + 8, W - 16, 20, "Board menu (switch, definition) | lock | settings", "w")
    block(s, x + 8, y + 32, 84, H - 40, "Keymap\nLayout\nMacros\n...")
    block(s, x + 96, y + 32, W - 104, 20, "Strip: layers 0 1 2 3", "p")
    block(s, x + 96, y + 56, W - 104, 90, "Board", "b", 12)
    block(s, x + 96, y + 150, W - 104, H - 158, "Panel: keycode picker,\nmacro editor...")
    s.save("workspace-comparison.svg")


# --- 2. sections: the strip belongs to the section ---------------------------------------

def sections():
    W, H = 300, 240
    s = Svg(3 * W + 40, H + 26)
    cases = [
        ("Keymap", ["Keymap", "Layout", "Diagnostics"], "Layer 0  1  2", "Board: legends\nof the layer", "b",
         "Keycode picker"),
        ("Macros", ["Keymap", "Macros", "..."], "M0  M1  M2  ...  M15", "Board: keys using\nthis macro, or folded", "d",
         "Action list of M2"),
        ("Diagnostics", ["Keymap", "Layout", "Diagnostics"], "Key presses  Levels  Device",
         "Board: live levels\nas a heat map", "b", "Thresholds, bins,\nBootloader, Erase EEPROM"),
    ]
    for i, (on, items, st, bd, kind, pn) in enumerate(cases):
        x = i * (W + 20)
        s.text(x, 14, on + (" (Leyden Jar plugin)" if on == "Diagnostics" else ""), 13, C["text"], weight=600)
        y = 22
        frame(s, x, y, W, H)
        block(s, x + 6, y + 6, W - 12, 18, "Model F Labs B104 | lock | settings")
        s.rect(x + 6, y + 28, 84, H - 34, C["s1"], C["hair"], rx=5)
        for j, it in enumerate(items):
            yy = y + 34 + j * 22
            if it == on:
                s.rect(x + 10, yy, 76, 19, C["abg"], None, rx=4)
            s.text(x + 16, yy + 13, it, 11, C["atx"] if it == on else C["text2"])
        block(s, x + 94, y + 28, W - 100, 22, st, "p")
        block(s, x + 94, y + 54, W - 100, 110, bd, kind, 11)
        block(s, x + 94, y + 168, W - 100, H - 174, pn)
    s.save("sections.svg")


# --- 3. the six common screens ------------------------------------------------------------

W = 680


def screen_no_board():
    s = Svg(W, 250)
    frame(s, 0, 0, W, 250)
    header(s, 0, 0, W, "")
    s.text(12, 60, "Keyboards found", 13, C["text"], weight=600)
    for i, (n, p) in enumerate([("Model F Labs B104", "Vial"), ("Aquanaut", "VIA"), ("Phoenix Project No 1", "VIA")]):
        list_row(s, 12, 72 + i * 42, W - 24, n, p)
    s.text(12, 222, "Not listed? Turn on Show all HID devices below the list.", 12, C["muted"])
    s.save("screen-no-board.svg")


def screen_keymap_only():
    h = 34 + 12 + 21 + 8 + 130 + 8 + 64 + 12
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Aquanaut", "VIA")
    y = 46
    strip(s, 12, y, ["0", "1", "2", "3"], 0, "Layer")
    y += 29
    board(s, (W - 390) / 2, y, 26, sel=17)
    y += 138
    picker(s, 12, y, W - 24)
    s.save("screen-keymap-only.svg")


def screen_keymap_sections():
    h = 34 + 12 + 21 + 8 + 120 + 8 + 64 + 12 + 10
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Model F Labs B104", "Vial", "off")
    nav(s, 0.5, 34.5, h - 35, "Keymap")
    x, mw = 162, W - 174
    y = 46
    strip(s, x, y, ["0", "1", "2"], 1, "Layer")
    y += 29
    board(s, x + (mw - 360) / 2, y, 24, sel=31)
    y += 130
    picker(s, x, y, mw)
    s.save("screen-keymap-sections.svg")


def candidate_card(s, x, y, w, name, source):
    s.rect(x, y, w, 150, C["white"], C["hair"], rx=8)
    board(s, x + (w - 165) / 2, y + 10, 11, blank=True)
    s.text(x + 10, y + 86, name, 13, C["text"])
    s.text(x + 10, y + 104, source, 12, C["text2"])
    button(s, x + 10, y + 116, "Use this one")


def screen_choose_definition():
    h = 250
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Phoenix Project No 1", "VIA")
    s.text(12, 60, "Two definitions match this board. Which one draws it?", 13, C["text"])
    cw = (W - 24 - 10) / 2
    candidate_card(s, 12, 72, cw, "Phoenix Project No 1", "Official, from VIA")
    candidate_card(s, 22 + cw, 72, cw, "Phoenix ortho test", "Yours, imported from phoenix.json")
    s.text(12, 240, "Nazg remembers the answer for this board.", 12, C["muted"])
    s.save("screen-choose-definition.svg")


def screen_macros_locked():
    h = 34 + 12 + 21 + 8 + 110 + 8 + 64 + 12 + 20
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Model F Labs B104", "Vial", "on")
    nav(s, 0.5, 34.5, h - 35, "Macros")
    x, mw = 162, W - 174
    y = 46
    strip(s, x, y, ["M0", "M1", "M2", "M3", "M4", "...", "M15"], 2)
    y += 29
    board(s, x + (mw - 330) / 2, y, 22, dim=True, hot=(0, 15))
    y += 120
    s.line(x, y, x + mw, y)
    s.text(x, y + 22, "Macros can't be changed while the board is locked.", 12, C["wtx"])
    s.text(x, y + 40, "Hold the two highlighted keys to unlock.", 12, C["wtx"])
    xx = x
    for t in ["Tap Ctrl+Shift+Esc", "Delay 200 ms", "Text \"taskmgr\"", "+ Add action"]:
        xx += button(s, xx, y + 52, t, op=0.5) + 6
    s.save("screen-macros-locked.svg")


def screen_settings():
    h = 250
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Model F Labs B104", "Vial", "off")
    s.text(12, 58, "← Back to the board", 12, C["text2"])
    s.text(12, 88, "Legends", 12, C["text"], weight=600)
    s.text(12, 110, "Host layout", 12, C["text2"])
    button(s, 90, 96, "French (AZERTY)  ▾")
    s.text(12, 142, "Definitions", 12, C["text"], weight=600)
    s.text(12, 162, "Official: 3513, from VIA", 12, C["muted"])
    s.rect(12, 172, W - 24, 34, C["white"], C["hair"], rx=8)
    s.text(24, 194, "0x21C0:0x9901", 12, C["muted"])
    s.text(130, 194, "Phoenix ortho test", 13, C["text"])
    button(s, W - 170, 178, "Re-import")
    button(s, W - 82, 178, "Remove")
    button(s, 12, 216, "Import a definition...")
    s.save("screen-settings.svg")


# --- 4. many candidates -------------------------------------------------------------------

def screen_many_candidates():
    h = 340
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "handwired", "VIA  0xFEED:0x0000")
    lw = 230
    s.rect(0.5, 34.5, lw, h - 35, C["s1"], None, rx=0)
    s.line(lw, 34, lw, h)
    s.rect(8, 42, lw - 16, 22, C["white"], C["border"], rx=6)
    s.text(18, 57, "Filter by name", 12, C["muted"])
    y = 80
    groups = [("Best matches", [("Handwired 60", "yours, 2026-08-02, 61 keys", True),
                                ("Handwired 60 rev2", "yours, 2026-09-11, 62 keys", False)]),
              ("Others on this id", [("Dactyl 5x6", "yours, 2026-05-14, 64 keys", False),
                                     ("Numpad test", "yours, 2026-03-02, 21 keys", False),
                                     ("Split 36", "yours, 2026-01-20, 36 keys", False),
                                     ("Ortho 4x12", "yours, 2025-11-09, 48 keys", False)])]
    for g, items in groups:
        s.text(12, y, g, 11, C["muted"])
        y += 6
        for n, sub, on in items:
            if on:
                s.rect(8, y, lw - 16, 36, C["abg"], None, rx=6)
            s.text(16, y + 15, n, 13, C["atx"] if on else C["text"])
            s.text(16, y + 30, sub, 11, C["atx"] if on else C["text2"])
            y += 38
        y += 12
    x, mw = lw + 12, W - lw - 24
    s.text(x, 60, "7 definitions match this board. Which one draws it?", 13, C["text"])
    s.text(x, 80, "Your board's layer 0, read through Handwired 60:", 12, C["muted"])
    board(s, x + (mw - 390) / 2, 92, 26)
    button(s, x, 238, "Use this one")
    s.text(x + 112, 253, "Nazg remembers the answer for this board.", 12, C["muted"])
    s.save("screen-many-candidates.svg")


def right_vs_wrong():
    pw = (W - 12) / 2
    s = Svg(W, 146)
    for i, (shuf, msg, col) in enumerate([(False, "Right definition: your keymap reads normally", C["ok"]),
                                          (True, "Wrong matrix: legends land in the wrong places", C["bad"])]):
        x = i * (pw + 12)
        s.rect(x, 0, pw, 146, C["s1"], C["hair"], rx=12)
        board(s, x + (pw - 300) / 2, 12, 20, shuffle=shuf)
        s.text(x + 12, 132, msg, 12, col)
    s.save("preview-right-vs-wrong.svg")


# --- 5. the board menu and the keyboard list ----------------------------------------------

def board_menu():
    h = 290
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Aquanaut", "VIA", open_menu=True)
    s.rect(W - 300, 46, 288, 20, C["s1"], C["hair"], rx=5, op=0.5)
    s.rect(160, 72, 360, 120, C["s1"], C["hair"], rx=5, op=0.5)
    s.rect(12, 200, W - 24, 70, C["s1"], C["hair"], rx=5, op=0.5)
    mx, my, mw = 12, 38, 280
    s.rect(mx, my, mw, 234, C["white"], C["border"], rx=8)
    s.text(mx + 12, my + 18, "Switch to", 11, C["muted"])
    y = my + 24
    for n, p in [("Model F Labs B104", "Vial"), ("Phoenix Project No 1", "VIA")]:
        s.text(mx + 12, y + 18, n, 13, C["text"])
        s.text(mx + mw - 12, y + 18, p, 11, C["muted"], "end")
        y += 28
    s.line(mx + 4, y + 4, mx + mw - 4, y + 4)
    y += 8
    for n in ["Change definition...", "Forget choice"]:
        s.text(mx + 12, y + 18, n, 13, C["text"])
        y += 28
    s.line(mx + 4, y + 4, mx + mw - 4, y + 4)
    y += 8
    s.text(mx + 12, y + 18, "Show matrix...", 13, C["text"])
    y += 28
    s.line(mx + 4, y + 4, mx + mw - 4, y + 4)
    y += 10
    s.rect(mx + 4, y, mw - 8, 26, C["abg"], None, rx=6)
    s.text(mx + 12, y + 18, "All keyboards", 13, C["atx"])
    s.save("board-menu.svg")


# --- 6. the matrix view -------------------------------------------------------------------

# Matrix column of each key of ROWS, per row; None is "the key's index in its row". Row 2
# leaves C12 unused, row 3 C1 (the ISO key's position) and C12, row 4 most of them.
MATRIX_COLS = [None, None, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13], [0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13],
               [0, 1, 2, 6, 10, 11, 12, 13]]


def matrix_view(selected, name):
    u, m = 34, 30
    ks = []
    for i, (x, y, w, label) in enumerate(keys()):
        index = sum(1 for k in ks if k["r"] == y)
        c = MATRIX_COLS[y][index] if MATRIX_COLS[y] else index
        ks.append(dict(x=x, y=y, w=w, l=label or "Space", r=y, c=c))
    rows, cols = 5, 14

    h = 345
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Aquanaut", "VIA")
    strip(s, 12, 46, ["Wiring", "Live test"], 0, "Matrix")
    x0, y0 = (W - (m + 15 * u)) / 2, 75
    sr, sc = selected if selected else (None, None)

    def cx(k):
        return (k["x"] + k["w"] / 2) * u

    def cy(k):
        return (k["y"] + 0.5) * u

    # Keys, then the wiring lines, then the legends, so a line never hides a legend.
    def dimmed(k):
        return 0.3 if selected and k["r"] != sr and k["c"] != sc else None

    for k in ks:
        in_r, in_c = k["r"] == sr, k["c"] == sc
        fill, stroke, sw = C["s1"], C["border"], 1
        if in_r:
            fill, stroke = C["abg"], C["abd"]
        if in_c:
            fill, stroke = C["pbg"], C["pbd"]
        if in_r and in_c:
            stroke, sw = C["text"], 2
        s.rect(x0 + m + k["x"] * u + 1, y0 + m + k["y"] * u + 1, k["w"] * u - 2, u - 2, fill, stroke, rx=4, sw=sw,
               op=dimmed(k))

    def polyline(points, color):
        pts = " ".join(f"{x0 + m + px:.1f},{y0 + m + py:.1f}" for px, py in points)
        s.e.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="2.5" '
                   f'stroke-linejoin="round" opacity="0.7"/>')

    if selected:
        polyline([(cx(k), cy(k)) for k in sorted((k for k in ks if k["r"] == sr), key=lambda k: k["c"])], "#185FA5")
        polyline([(cx(k), cy(k)) for k in sorted((k for k in ks if k["c"] == sc), key=lambda k: k["r"])], "#534AB7")

    for k in ks:
        s.text(x0 + m + cx(k), y0 + m + cy(k) + 4, k["l"], 12, C["text"], "middle", 600 if selected and
               (k["r"] == sr or k["c"] == sc) else 400, op=dimmed(k))

    # Rulers: a label at the average position of its keys, nudged apart so none overlap.
    col_pos = []
    for c in range(cols):
        on = [cx(k) for k in ks if k["c"] == c]
        col_pos.append(sum(on) / len(on))
    order = sorted(range(cols), key=lambda c: col_pos[c])
    for a, b in zip(order, order[1:]):
        col_pos[b] = max(col_pos[b], col_pos[a] + u * 0.95)

    def ruler_label(x, y, w, label, kind, struck):
        if kind == "r":
            s.rect(x, y, w, 22, C["abg"], None, rx=4)
        elif kind == "c":
            s.rect(x, y, w, 22, C["pbg"], None, rx=4)
        colour = {"r": C["atx"], "c": C["ptx"]}.get(kind, C["muted"])
        s.text(x + w / 2, y + 15, label, 11, colour, "middle", 600 if kind else 400, op=0.6 if struck else None)
        if struck:
            s.line(x + w / 2 - 9, y + 11, x + w / 2 + 9, y + 11, C["muted"])

    for r in range(rows):
        on = [cy(k) for k in ks if k["r"] == r]
        struck = sc is not None and r != sr and not any(k["r"] == r and k["c"] == sc for k in ks)
        ruler_label(x0, y0 + m + sum(on) / len(on) - 11, m - 4, f"R{r}", "r" if r == sr else "", struck)
    for c in range(cols):
        struck = sr is not None and c != sc and not any(k["c"] == c and k["r"] == sr for k in ks)
        ruler_label(x0 + m + col_pos[c] - 15, y0 + 2, 30, f"C{c}", "c" if c == sc else "", struck)

    py = y0 + m + 5 * u + 8
    s.line(12, py, W - 12, py)
    if selected:
        k = next(k for k in ks if k["r"] == sr and k["c"] == sc)
        nr = sum(1 for k2 in ks if k2["r"] == sr)
        nc = sum(1 for k2 in ks if k2["c"] == sc)
        status = f"{k['l']} at row {sr}, column {sc}. Row {sr} wires {nr} keys, column {sc} wires {nc}. Pinned."
    else:
        status = "Hover a key, or a row or column label. Click to pin."
    s.text(12, py + 24, status, 12, C["text2"])
    s.text(12, py + 44, "Definition checks: no problem found.", 12, C["muted"])
    button(s, W - 70, py + 10, "Close")
    s.save(name)


# --- 7. the console drawer ----------------------------------------------------------------

def console_drawer(is_open, name):
    h = 432 if is_open else 290
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "Model F Labs B104", "Vial", "off")
    # The Console button: only on a board with a console interface, off until pressed.
    bw = tw("Console") + 20
    bx = W - 110 - bw
    s.rect(bx, 7, bw, 21, C["abg"] if is_open else C["white"], C["abd"] if is_open else C["border"], rx=6)
    s.text(bx + bw / 2, 22, "Console", 12, C["atx"] if is_open else C["text"], "middle")
    nav(s, 0.5, 34.5, (243 if is_open else h) - 35, "Keymap", NAV[:5])
    x, mw = 162, W - 174
    strip(s, x, 46, ["0", "1", "2"], 0, "Layer")
    board(s, x + (mw - 330) / 2, 75, 22)
    block(s, x, 193, mw, 40 if is_open else 80, "Keycode picker")
    if not is_open:
        s.save(name)
        return
    y = 243
    s.rect(0.5, y, W - 1, h - y - 0.5, C["s1"], None, rx=0)
    s.line(0, y, W, y, C["border"])
    s.text(12, y + 20, "Console", 12, C["text"], weight=600)
    s.text(70, y + 20, "FF31:0074, interface 2", 12, C["muted"])
    xx = W - 12
    for label in reversed(["Pause", "Clear", "Copy", "Save..."]):
        xx -= tw(label) + 20
        button(s, xx, y + 6, label)
        xx -= 6
    s.rect(xx - 130, y + 6, 124, 22, C["white"], C["border"], rx=6)
    s.text(xx - 120, y + 21, "Filter", 12, C["muted"])
    s.line(0, y + 34, W, y + 34)
    lines = [("14:02:11.204", "leyden_jar: calibration done, 16 bins", ""),
             ("14:02:11.230", "DAC threshold 0: 142, ref 131", ""),
             ("14:02:15.871", "KL: kc: 0x0014, col: 3, row: 1, pressed: 1, time: 15871", "sel"),
             ("14:02:15.944", "KL: kc: 0x0014, col: 3, row: 1, pressed: 0, time: 15944", "sel"),
             ("14:02:31.002", "board disconnected, waiting for it to come back", "event"),
             ("14:02:34.518", "board back, console reattached", "event"),
             ("14:02:34.610", "leyden_jar: calibration done, 16 bins", "")]
    ly = y + 42
    for stamp, text, kind in lines:
        if kind == "sel":
            s.rect(4, ly, W - 8, 19, C["abg"], None, rx=3)
        s.text(12, ly + 14, stamp, 12, C["muted"], family=MONO)
        s.text(118, ly + 14, text, 12, C["muted"] if kind == "event" else C["text"], family=MONO)
        ly += 20
    s.save(name)


def keyboard_list_all():
    h = 360
    s = Svg(W, h)
    frame(s, 0, 0, W, h)
    header(s, 0, 0, W, "")
    s.text(12, 60, "Keyboards found", 13, C["text"], weight=600)
    button(s, W - 90, 44, "↻ Refresh")
    for i, (n, p) in enumerate([("Model F Labs B104", "Vial"), ("Aquanaut", "VIA"), ("Phoenix Project No 1", "VIA")]):
        list_row(s, 12, 72 + i * 42, W - 24, n, p)
    y = 204
    s.rect(12, y, 28, 16, C["abd"], None, rx=8)
    s.rect(26, y + 3, 10, 10, C["white"], None, rx=5)
    s.text(48, y + 13, "Show all HID devices", 12, C["text2"])
    s.text(W - 12, y + 13, "off at every start", 12, C["muted"], "end")
    s.text(12, y + 40, "Other HID interfaces, not openable", 12, C["muted"])
    y += 48
    for n, v, u, itf in [("Logitech USB Receiver", "046D:C52B", "0001:0002", "if 1"),
                         ("Model F Labs B104", "1209:4704", "0001:0006", "if 0, keyboard"),
                         ("Aquanaut", "FEED:0001", "000C:0001", "if 2, consumer"),
                         ("USB Audio", "0D8C:0014", "000C:0001", "if 3")]:
        s.rect(12, y, W - 24, 24, C["white"], C["border"], rx=6, dash="4 3")
        s.text(24, y + 16, n, 12, C["text2"])
        s.text(330, y + 16, v, 12, C["text2"])
        s.text(420, y + 16, "usage " + u, 12, C["text2"])
        s.text(W - 24, y + 16, itf, 12, C["text2"], "end")
        y += 28
    s.save("keyboard-list-all.svg")


def main():
    global OUT
    default = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", "research_material", "ui-design")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("folder", nargs="?", default=default, help="where to write the SVG files")
    OUT = os.path.normpath(parser.parse_args().folder)
    os.makedirs(OUT, exist_ok=True)

    for draw in (comparison, sections, screen_no_board, screen_keymap_only, screen_keymap_sections,
                 screen_choose_definition, screen_macros_locked, screen_settings, screen_many_candidates,
                 right_vs_wrong, board_menu, keyboard_list_all):
        draw()
    matrix_view(None, "matrix-view.svg")
    matrix_view((3, 5), "matrix-view-selected.svg")
    console_drawer(False, "console-closed.svg")
    console_drawer(True, "console-open.svg")
    print(f"wrote 16 pictures to {OUT}")


if __name__ == "__main__":
    main()
