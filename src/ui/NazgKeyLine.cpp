// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyLine.h"

#include <array>
#include <string>
#include <string_view>
#include <variant>

#include "imgui.h"

namespace nazg
{
    namespace
    {
        // The four modifiers by the host's names, each as a bit on either side.
        struct Modifier
        {
            uint8_t     left;
            uint8_t     right;
            const char* name;
        };

        std::array<Modifier, 4> ModifiersFor(ModifierNames names)
        {
            const std::array<const char*, 4> words = ModifierWords(names);
            return { { { Mod::LeftCtrl, Mod::RightCtrl, words[0] },
                       { Mod::LeftShift, Mod::RightShift, words[1] },
                       { Mod::LeftAlt, Mod::RightAlt, words[2] },
                       { Mod::LeftGui, Mod::RightGui, words[3] } } };
        }

        bool IsRight(uint8_t mods) { return (mods & 0xF0) != 0; }
    }

    std::array<const char*, 4> ModifierWords(ModifierNames names)
    {
        const bool mac = names == ModifierNames::Mac;
        return { "Ctrl", "Shift", mac ? "Option" : "Alt", mac ? "Cmd" : names == ModifierNames::Linux ? "Super" : "Win" };
    }

    bool ToggleButton(const char* label, bool on, bool enabled)
    {
        ImGui::BeginDisabled(!enabled);
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool clicked = ImGui::Button(label);
        if (on)
            ImGui::PopStyleColor();
        ImGui::EndDisabled();
        return clicked;
    }

    void OpenPopupUnder(const char* popup)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::OpenPopup(popup);
    }

    std::optional<uint8_t> ModifierChoices(uint8_t mods, ModifierNames names, bool none)
    {
        std::optional<uint8_t> changed;
        const bool             right = IsRight(mods);

        if (none)
        {
            if (ToggleButton("None", mods == 0) && mods != 0)
                changed = uint8_t{ 0 };
        }

        const std::array<Modifier, 4> modifiers = ModifiersFor(names);
        for (size_t index = 0; index < modifiers.size(); ++index)
        {
            const Modifier& modifier = modifiers[index];
            if (none || index > 0)
                ImGui::SameLine();
            const uint8_t bit = right ? modifier.right : modifier.left;
            if (ToggleButton(modifier.name, (mods & bit) != 0))
            {
                const uint8_t next = static_cast<uint8_t>(mods ^ bit);
                if (next != 0 || none)
                    changed = next;
            }
        }

        // QMK holds one side only: the side moves every modifier set.
        if (ToggleButton("Left", mods != 0 && !right, mods != 0) && right)
            changed = static_cast<uint8_t>(mods >> 4);
        ImGui::SameLine();
        if (ToggleButton("Right", mods != 0 && right, mods != 0) && !right)
            changed = static_cast<uint8_t>(mods << 4);
        return changed;
    }

    bool DrawSentWith(Keycode& key, const LegendSettings& legends, QmkKeycodeVersion version, const char* words)
    {
        // The basic key under the modifiers: only QMK's bottom byte, from KC_A, can be sent with any.
        const auto*      modified = std::get_if<ModifiedKey>(&key);
        std::string_view base;
        if (modified != nullptr)
            base = modified->key;
        else if (const auto* named = std::get_if<NamedKey>(&key))
            if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, version);
                row != nullptr && row->value >= 0x04 && row->value <= 0xFF)
                base = named->name;
        if (base.empty())
            return false;

        // The modifiers' words as the board prints them: "Ctrl", "Ctrl Sft", "Hyper".
        const uint8_t     mods  = modified != nullptr ? modified->mods : 0;
        const std::string modWords = mods != 0 ? LegendFor(OneShotModKey{ mods }, { legends.Layout(), legends.modifierNames }).cylindrical.full
                                               : std::string("nothing");

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", words);
        ImGui::SameLine();
        if (ToggleButton((modWords + " \xE2\x96\xBE###sentwith").c_str(), mods != 0))
            OpenPopupUnder("##sentwith");
        if (ImGui::BeginPopup("##sentwith"))
        {
            if (const std::optional<uint8_t> chosen = ModifierChoices(mods, legends.modifierNames, true))
                key = *chosen == 0 ? Keycode{ NamedKey{ base } } : Keycode{ ModifiedKey{ *chosen, base } };
            ImGui::EndPopup();
        }
        return true;
    }
}
