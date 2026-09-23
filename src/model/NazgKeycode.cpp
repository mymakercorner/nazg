// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycode.h"

#include <cstdio>

namespace nazg
{
    namespace
    {
        struct ModName
        {
            uint8_t     bit;
            const char* wrapper;   // LCTL(kc)
            const char* flag;      // MOD_LCTL
        };

        constexpr ModName c_ModNames[] = {
            { Mod::LeftCtrl,   "LCTL", "MOD_LCTL" },
            { Mod::LeftShift,  "LSFT", "MOD_LSFT" },
            { Mod::LeftAlt,    "LALT", "MOD_LALT" },
            { Mod::LeftGui,    "LGUI", "MOD_LGUI" },
            { Mod::RightCtrl,  "RCTL", "MOD_RCTL" },
            { Mod::RightShift, "RSFT", "MOD_RSFT" },
            { Mod::RightAlt,   "RALT", "MOD_RALT" },
            { Mod::RightGui,   "RGUI", "MOD_RGUI" },
        };

        // MOD_LCTL|MOD_LSFT
        std::string FormatModFlags(uint8_t mods)
        {
            std::string text;

            for (const ModName& mod : c_ModNames)
            {
                if ((mods & mod.bit) == 0)
                    continue;

                if (!text.empty())
                    text += '|';
                text += mod.flag;
            }

            return text.empty() ? std::string("0") : text;
        }

        // LCTL(LSFT(KC_A)) -- QMK nests one wrapper per modifier.
        std::string FormatWrapped(uint8_t mods, std::string_view key)
        {
            std::string open;
            std::string close;

            for (const ModName& mod : c_ModNames)
            {
                if ((mods & mod.bit) == 0)
                    continue;

                open += mod.wrapper;
                open += '(';
                close += ')';
            }

            return open + std::string(key) + close;
        }

        const char* LayerOpName(LayerOp op) noexcept
        {
            switch (op)
            {
            case LayerOp::Momentary:         return "MO";
            case LayerOp::Toggle:            return "TG";
            case LayerOp::To:                return "TO";
            case LayerOp::Default:           return "DF";
            case LayerOp::PersistentDefault: return "PDF";
            case LayerOp::OneShot:           return "OSL";
            case LayerOp::TapToggle:         return "TT";
            }

            return "?";
        }

        // One overload per alternative; std::visit picks the right one.
        struct Formatter
        {
            std::string operator()(const NamedKey& k) const { return std::string(k.name); }

            std::string operator()(const ModifiedKey& k) const { return FormatWrapped(k.mods, k.key); }

            std::string operator()(const ModTapKey& k) const
            {
                return "MT(" + FormatModFlags(k.mods) + "," + std::string(k.key) + ")";
            }

            std::string operator()(const LayerTapKey& k) const
            {
                return "LT(" + std::to_string(k.layer) + "," + std::string(k.key) + ")";
            }

            std::string operator()(const LayerKey& k) const
            {
                return std::string(LayerOpName(k.op)) + "(" + std::to_string(k.layer) + ")";
            }

            std::string operator()(const LayerModKey& k) const
            {
                return "LM(" + std::to_string(k.layer) + "," + FormatModFlags(k.mods) + ")";
            }

            std::string operator()(const OneShotModKey& k) const { return "OSM(" + FormatModFlags(k.mods) + ")"; }

            std::string operator()(const SwapHandsTapKey& k) const { return "SH_T(" + std::string(k.key) + ")"; }

            std::string operator()(const TapDanceKey& k) const { return "TD(" + std::to_string(k.index) + ")"; }

            std::string operator()(const MacroKey& k) const { return "MC_" + std::to_string(k.index); }

            std::string operator()(const UnknownKey& k) const
            {
                char text[8];
                std::snprintf(text, sizeof(text), "0x%04X", k.raw);
                return text;
            }
        };
    }

    std::string FormatKeycode(const Keycode& keycode)
    {
        return std::visit(Formatter{}, keycode);
    }
}
