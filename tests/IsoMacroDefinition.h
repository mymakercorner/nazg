// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// One real board in both forms of a VIA definition: the ISO Macro, 0x4D65:0x1200.
//
// The source is the-via/keyboards' v3/merge/iso_macro.json (GPL-3.0, commit 9e3e9f4);
// the converted form is what VIA's build made of it, served as
// usevia.app/definitions/v3/1298469376.json on 2026-09-24. Both are verbatim, only
// re-wrapped. See docs/research_material/via-registry.md, "Side by side: ISO Macro".
//
// Small, and it has what matters: one layout option whose choice 1 starts with a decal,
// so lining the choices up is wrong unless decals count; and an ISO Enter.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

inline std::vector<uint8_t> IsoMacroBytes(const char* text)
{
    return std::vector<uint8_t>(text, text + std::strlen(text));
}

inline std::vector<uint8_t> IsoMacroSource()
{
    return IsoMacroBytes(R"({
  "name": "ISO Macro",
  "vendorId": "0x4D65",
  "productId": "0x1200",
  "matrix": { "rows": 3, "cols": 3 },
  "keycodes": ["qmk_backlight_keycodes"],
  "menus": ["qmk_backlight"],
  "layouts": {
    "labels": ["Single Encoder"],
    "keymap": [
      [ { "d": true }, "2,1\n\n\n0,1",
        { "x": 0.5, "c": "#8f8f8f" }, "2,1\n\n\n0,0",
        { "x": 0.25, "c": "#cccccc" }, "0,0", "0,1", "0,2",
        { "x": 0.25, "w": 1.25, "h": 2, "w2": 1.5, "h2": 1, "x2": -0.25 }, "2,0" ],
      [ { "c": "#8f8f8f" }, "2,2\n\n\n0,1",
        { "x": 0.5 }, "2,2\n\n\n0,0",
        { "x": 0.5, "c": "#cccccc" }, "1,0", "1,1", "1,2" ]
    ]
  }
})");
}

inline std::vector<uint8_t> IsoMacroConverted()
{
    return IsoMacroBytes(
        R"({"name":"ISO Macro","vendorProductId":1298469376,"firmwareVersion":0,"menus":["qmk_backlight"],)"
        R"("keycodes":["qmk_backlight_keycodes"],"matrix":{"rows":3,"cols":3},"layouts":{"labels":["Single Encoder"],)"
        R"("width":5.75,"height":2,"optionKeys":{"0":{"0":[)"
        R"({"row":2,"col":1,"x":0,"y":0,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"mod"},)"
        R"({"row":2,"col":2,"x":0,"y":1,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"mod"}],"1":[)"
        R"({"row":-1,"col":-1,"x":0,"y":0,"r":0,"rx":-1.5,"ry":0,"d":true,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":2,"col":2,"x":0,"y":1,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"mod"}]}},"keys":[)"
        R"({"row":0,"col":0,"x":1.25,"y":0,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":0,"col":1,"x":2.25,"y":0,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":0,"col":2,"x":3.25,"y":0,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":2,"col":0,"x":4.5,"y":0,"r":0,"rx":-1.5,"ry":0,"d":false,"h":2,"w":1.25,"w2":1.5,"x2":-0.25,"h2":1,"color":"alpha"},)"
        R"({"row":1,"col":0,"x":1.5,"y":1,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":1,"col":1,"x":2.5,"y":1,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"},)"
        R"({"row":1,"col":2,"x":3.5,"y":1,"r":0,"rx":-1.5,"ry":0,"d":false,"h":1,"w":1,"color":"alpha"}]}})");
}
