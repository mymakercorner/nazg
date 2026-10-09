// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SectionPlan - the sections an open board has, in the column's order, each with its icon
// (ui-design.md, "The regions", "Nazg's sections, then the board's menus" and "The icon of
// every section"). Decided from what a load read: the definition and the board's report.
//
// Pure: what draws each section is the caller's -- Keymap for one, a placeholder for the
// sections not built yet.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "model/NazgKeyboard.h"
#include "ui/NazgIcons.h"
#include "ui/NazgSection.h"

namespace nazg
{
    enum class SectionKind
    {
        Keymap,
        Layout,
        Macros,
        Lighting,       // VIA's built-in lighting menus, Vial's lighting
        Audio,          // VIA's built-in audio menu
        TapDance,       // Vial's, as the next four
        Combos,
        KeyOverrides,
        AltRepeatKey,
        QmkSettings,
        CustomMenu,     // one of a VIA V3 definition's own menus
    };

    struct PlannedSection
    {
        SectionKind  kind  = SectionKind::Keymap;
        SectionGroup group = SectionGroup::Protocol;
        std::string  name;
        Icon         icon = Icon::None;

        std::vector<std::string> subsections;   // a custom menu's, as its definition labels them
    };

    // Nazg's sections the board has -- Keymap always, Layout, Macros, Lighting, Audio, then Vial's
    // features -- followed by its definition's custom menus, in the definition's order.
    [[nodiscard]] std::vector<PlannedSection> PlanSections(const Keyboard& keyboard);

    // A custom menu's icon from its label: the label table, which holds every label of VIA's
    // registry, then the keyword rule. Icon::None when neither knows it: a monogram is drawn.
    [[nodiscard]] Icon IconOfMenu(std::string_view label);

    // One or two letters for a section with no icon: the initials of its first two words, short
    // words left out -- "Advanced Features" gives "AF" -- or the first two letters of a lone
    // word, the first a capital: "Kn". "?" for a label with no letters.
    [[nodiscard]] std::string MonogramOf(std::string_view label);
}
