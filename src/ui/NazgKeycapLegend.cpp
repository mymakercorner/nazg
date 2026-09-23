// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycapLegend.h"

#include <variant>

#include "adapters/qmk/NazgQmkKeycodes.h"

namespace nazg
{
    namespace
    {
        // US: what each layout-dependent position types. From QMK's keymap extras
        // (keycodes_us_0.0.1) for the Shift column; the plain column is QMK's own label.
        // KC_NUHS and KC_NUBS type what KC_BSLS does -- operating systems treat the ISO
        // hash key as backslash, and a US layout gives the extra ISO key the same pair.
        constexpr HostLegend c_UsLegends[] = {
            { "KC_A", "A", "" }, { "KC_B", "B", "" }, { "KC_C", "C", "" }, { "KC_D", "D", "" },
            { "KC_E", "E", "" }, { "KC_F", "F", "" }, { "KC_G", "G", "" }, { "KC_H", "H", "" },
            { "KC_I", "I", "" }, { "KC_J", "J", "" }, { "KC_K", "K", "" }, { "KC_L", "L", "" },
            { "KC_M", "M", "" }, { "KC_N", "N", "" }, { "KC_O", "O", "" }, { "KC_P", "P", "" },
            { "KC_Q", "Q", "" }, { "KC_R", "R", "" }, { "KC_S", "S", "" }, { "KC_T", "T", "" },
            { "KC_U", "U", "" }, { "KC_V", "V", "" }, { "KC_W", "W", "" }, { "KC_X", "X", "" },
            { "KC_Y", "Y", "" }, { "KC_Z", "Z", "" },

            { "KC_1", "1", "!" }, { "KC_2", "2", "@" }, { "KC_3", "3", "#" }, { "KC_4", "4", "$" },
            { "KC_5", "5", "%" }, { "KC_6", "6", "^" }, { "KC_7", "7", "&" }, { "KC_8", "8", "*" },
            { "KC_9", "9", "(" }, { "KC_0", "0", ")" },

            { "KC_GRV",  "`", "~" }, { "KC_MINS", "-", "_" }, { "KC_EQL",  "=", "+" },
            { "KC_LBRC", "[", "{" }, { "KC_RBRC", "]", "}" }, { "KC_BSLS", R"(\)", "|" },
            { "KC_NUHS", R"(\)", "|" }, { "KC_SCLN", ";", ":" }, { "KC_QUOT", "'", "\"" },
            { "KC_COMM", ",", "<" }, { "KC_DOT",  ".", ">" }, { "KC_SLSH", "/", "?" },
            { "KC_NUBS", R"(\)", "|" },
        };

        constexpr HostLayout c_UsLayout = { "US", c_UsLegends };

        // "LCTL+LSFT" -- the short QMK wrapper names, joined.
        std::string ModNames(uint8_t mods)
        {
            static constexpr struct { uint8_t bit; const char* name; } c_Names[] = {
                { Mod::LeftCtrl, "LCTL" },  { Mod::LeftShift, "LSFT" },  { Mod::LeftAlt, "LALT" },
                { Mod::LeftGui, "LGUI" },   { Mod::RightCtrl, "RCTL" },  { Mod::RightShift, "RSFT" },
                { Mod::RightAlt, "RALT" },  { Mod::RightGui, "RGUI" },
            };

            std::string text;
            for (const auto& mod : c_Names)
            {
                if ((mods & mod.bit) == 0)
                    continue;
                if (!text.empty())
                    text += '+';
                text += mod.name;
            }
            return text;
        }

        // The legend of one named key on its own.
        KeycapLegend NamedLegend(std::string_view name, const HostLayout& layout)
        {
            if (name == "KC_NO")
                return {};
            if (name == "KC_TRNS")
                return { "Trans", "" };

            if (const HostLegend* legend = layout.Find(name))
                return { std::string(legend->plain), std::string(legend->shifted) };

            // Names are the newest QMK spelling, so the latest table always has them --
            // except keycodes QMK has since removed, which fall back to the name itself.
            const QmkKeycode* keycode = FindQmkKeycodeByName(name, c_LatestQmkKeycodeVersion);
            if (keycode != nullptr && keycode->label[0] != '\0')
                return { keycode->label, "" };

            return { std::string(name), "" };
        }

        struct Legender
        {
            const HostLayout& layout;

            KeycapLegend operator()(const NamedKey& k) const { return NamedLegend(k.name, layout); }

            KeycapLegend operator()(const ModifiedKey& k) const
            {
                // LSFT(KC_1) is simply "!" -- the character Shift gives on this host.
                const bool shiftOnly = k.mods == Mod::LeftShift || k.mods == Mod::RightShift;
                if (const HostLegend* legend = layout.Find(k.key); shiftOnly && legend != nullptr &&
                                                                  !legend->shifted.empty())
                    return { std::string(legend->shifted), "" };

                return { NamedLegend(k.key, layout).primary, ModNames(k.mods) };
            }

            KeycapLegend operator()(const ModTapKey& k) const
            {
                return { NamedLegend(k.key, layout).primary, "MT " + ModNames(k.mods) };
            }

            KeycapLegend operator()(const LayerTapKey& k) const
            {
                return { NamedLegend(k.key, layout).primary, "LT " + std::to_string(k.layer) };
            }

            KeycapLegend operator()(const SwapHandsTapKey& k) const
            {
                return { NamedLegend(k.key, layout).primary, "SH_T" };
            }

            // Everything else is best shown in QMK's own notation: MO(1), TD(3), MC_5.
            template <typename T>
            KeycapLegend operator()(const T& k) const
            {
                return { FormatKeycode(k), "" };
            }
        };
    }

    const HostLegend* HostLayout::Find(std::string_view key) const noexcept
    {
        for (const HostLegend& legend : legends)
            if (legend.key == key)
                return &legend;

        return nullptr;
    }

    const HostLayout& UsHostLayout() noexcept
    {
        return c_UsLayout;
    }

    KeycapLegend LegendFor(const Keycode& keycode, const HostLayout& layout)
    {
        return std::visit(Legender{ layout }, keycode);
    }
}
