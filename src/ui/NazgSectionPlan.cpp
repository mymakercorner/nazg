// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSectionPlan.h"

#include <algorithm>

namespace nazg
{
    namespace
    {
        // The menus VIA builds in that are a lighting section, and its audio one (VIA V3's
        // `menus` ids); and the lighting presets of VIA V2 and Vial that name a system.
        constexpr std::string_view c_LightingMenus[] = { "qmk_backlight", "qmk_rgblight", "qmk_backlight_rgblight",
                                                         "qmk_rgb_matrix", "qmk_led_matrix" };
        constexpr std::string_view c_AudioMenu       = "qmk_audio";

        bool HasLighting(const KeyboardDefinition& definition)
        {
            if (!definition.lighting.empty() && definition.lighting != "none")
                return true;
            return std::any_of(definition.menuIds.begin(), definition.menuIds.end(), [](const std::string& id)
                               { return std::find(std::begin(c_LightingMenus), std::end(c_LightingMenus), id) !=
                                        std::end(c_LightingMenus); });
        }

        char Lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        // A word's characters: ASCII letters and digits, and every byte of a UTF-8 sequence --
        // a label in another script still has words.
        bool IsWordByte(char c)
        {
            const auto byte = static_cast<unsigned char>(c);
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || byte >= 0x80;
        }

        std::vector<std::string> WordsOf(std::string_view label)
        {
            std::vector<std::string> words;
            std::string              word;
            for (const char c : label)
            {
                if (IsWordByte(c))
                {
                    word += c;
                }
                else if (!word.empty())
                {
                    words.push_back(word);
                    word.clear();
                }
            }
            if (!word.empty())
                words.push_back(std::move(word));
            return words;
        }

        // The first character of `word`, whole: one byte, or a UTF-8 sequence.
        std::string FirstCharacter(std::string_view word)
        {
            size_t length = 1;
            while (length < word.size() && (static_cast<unsigned char>(word[length]) & 0xC0) == 0x80)
                ++length;
            return std::string(word.substr(0, length));
        }

        std::string Upper(std::string text)
        {
            for (char& c : text)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return text;
        }
    }

    std::vector<PlannedSection> PlanSections(const Keyboard& keyboard)
    {
        const KeyboardDefinition& definition = keyboard.definition;
        const BoardReport&        report     = keyboard.report;

        std::vector<PlannedSection> sections;
        const auto add = [&](SectionKind kind, const char* name, Icon icon)
        { sections.push_back({ kind, SectionGroup::Protocol, name, icon, {} }); };

        add(SectionKind::Keymap, "Keymap", Icon::Keyboard);
        if (!definition.layoutLabels.empty())
            add(SectionKind::Layout, "Layout", Icon::Layout);
        if (report.macroCount > 0)
            add(SectionKind::Macros, "Macros", Icon::PlayerPlay);
        if (HasLighting(definition))
            add(SectionKind::Lighting, "Lighting", Icon::Bulb);
        if (std::find(definition.menuIds.begin(), definition.menuIds.end(), c_AudioMenu) != definition.menuIds.end())
            add(SectionKind::Audio, "Audio", Icon::Volume);

        if (report.tapDanceCount > 0)
            add(SectionKind::TapDance, "Tap Dance", Icon::HandClick);
        if (report.comboCount > 0)
            add(SectionKind::Combos, "Combos", Icon::ArrowsJoin);
        if (report.keyOverrideCount > 0)
            add(SectionKind::KeyOverrides, "Key Overrides", Icon::Replace);
        if (report.altRepeatKeyCount > 0)
            add(SectionKind::AltRepeatKey, "Alt Repeat Key", Icon::Repeat);
        if (report.hasQmkSettings)
            add(SectionKind::QmkSettings, "QMK Settings", Icon::AdjustmentsHorizontal);

        for (const KeyboardDefinition::CustomMenu& menu : definition.customMenus)
            sections.push_back({ SectionKind::CustomMenu, SectionGroup::Board, menu.label, IconOfMenu(menu.label),
                                 menu.sections });

        return sections;
    }

    Icon IconOfMenu(std::string_view label)
    {
        // Every custom menu label in VIA's V3 definitions, with how many use it (2026-10-09).
        const struct
        {
            std::string_view label;
            Icon             icon;
        } c_Labels[] = {
            { "lighting", Icon::Bulb },                         // 214
            { "tap dance", Icon::HandClick },                   // 63, cipulot's own, as the next
            { "combos", Icon::ArrowsJoin },                     // 63
            { "qmk settings", Icon::AdjustmentsHorizontal },    // 63
            { "switch configuration", Icon::ArrowBarToDown },   // 55
            { "dks", Icon::Gauge },                             // 51
            { "controller", Icon::DeviceGamepad2 },             // 51
            { "system", Icon::Settings },                       // 51
            { "socd", Icon::ArrowsLeftRight },                  // 50
            { "indicators", Icon::CircleDot },                  // 22
            { "board system", Icon::Settings },                 // 13
            { "advanced features", Icon::Sparkles },            // 12
            { "custom features", Icon::Components },            // 3
            { "rgb_matrix", Icon::Bulb },                       // 3
            { "pmk custom settings", Icon::AdjustmentsHorizontal },   // 2
            { "haptic feedback", Icon::DeviceMobileVibration },       // 2
            { "knob", Icon::RotateClockwise },                        // 1
        };

        std::string lower(label);
        std::transform(lower.begin(), lower.end(), lower.begin(), Lower);
        for (const auto& known : c_Labels)
            if (known.label == lower)
                return known.icon;

        // A label the table does not know, by keyword, first match wins (ui-design.md, "A custom
        // menu's icon"). `joined` has no spaces or punctuation, for "tap dance", "tap-dance".
        const std::vector<std::string> words = WordsOf(lower);
        std::string                    joined;
        for (const std::string& word : words)
            joined += word;

        const auto has      = [&](std::string_view part) { return joined.find(part) != std::string::npos; };
        const auto hasWord  = [&](std::string_view whole)
        { return std::find(words.begin(), words.end(), whole) != words.end(); };
        const auto hasStart = [&](std::string_view start)
        { return std::any_of(words.begin(), words.end(), [&](const std::string& word) { return word.rfind(start, 0) == 0; }); };

        if (has("tapdance"))
            return Icon::HandClick;
        if (has("combo"))
            return Icon::ArrowsJoin;
        if (has("override"))
            return Icon::Replace;
        if (has("socd"))
            return Icon::ArrowsLeftRight;
        if (hasWord("dks"))
            return Icon::Gauge;
        if (has("indicator"))
            return Icon::CircleDot;
        if (has("light") || has("rgb") || has("backlight") || hasStart("led"))
            return Icon::Bulb;
        if (has("audio") || has("sound"))
            return Icon::Volume;
        if (has("haptic"))
            return Icon::DeviceMobileVibration;
        if (has("display") || has("oled") || has("lcd") || has("screen"))
            return Icon::DeviceDesktop;
        if (has("switch") || has("actuation"))
            return Icon::ArrowBarToDown;
        if (has("controller") || has("gamepad") || has("joystick"))
            return Icon::DeviceGamepad2;
        if (has("knob") || has("encoder") || has("dial"))
            return Icon::RotateClockwise;
        if (has("setting"))
            return Icon::AdjustmentsHorizontal;
        if (has("system"))
            return Icon::Settings;
        return Icon::None;
    }

    std::string MonogramOf(std::string_view label)
    {
        std::vector<std::string> words = WordsOf(label);
        words.erase(std::remove_if(words.begin(), words.end(),
                                   [](std::string word)
                                   {
                                       std::transform(word.begin(), word.end(), word.begin(), Lower);
                                       return word == "and" || word == "of" || word == "the";
                                   }),
                    words.end());

        if (words.empty())
            return "?";
        if (words.size() > 1)
            return Upper(FirstCharacter(words[0]) + FirstCharacter(words[1]));

        // A lone word keeps its case after the first letter: "Knob" gives "Kn".
        const std::string first = FirstCharacter(words[0]);
        return Upper(first) + FirstCharacter(std::string_view(words[0]).substr(first.size()));
    }
}
