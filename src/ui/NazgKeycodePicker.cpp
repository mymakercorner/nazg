// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycodePicker.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"

namespace nazg
{
    namespace
    {
        bool ContainsNoCase(std::string_view text, std::string_view needle)
        {
            if (needle.empty())
                return true;

            auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };

            return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                               [&](char a, char b) { return lower(a) == lower(b); }) != text.end();
        }

        struct Choice
        {
            Keycode     keycode;
            std::string caption;   // on the button
            std::string tooltip;
        };

        // Every choice for this version, grouped in the order the groups first appear in
        // the table -- which, sorted by value, puts basic keys first.
        std::vector<std::pair<std::string, std::vector<Choice>>> BuildChoices(QmkKeycodeVersion version,
                                                                              uint8_t           layerCount,
                                                                              const HostLayout& layout)
        {
            std::vector<std::pair<std::string, std::vector<Choice>>> groups;

            // Layer keys are parameterised, so the table does not list them.
            std::vector<Choice> layers;
            const LayerOp ops[] = { LayerOp::Momentary, LayerOp::Toggle, LayerOp::To,
                                    LayerOp::TapToggle, LayerOp::OneShot, LayerOp::Default };
            for (LayerOp op : ops)
                for (uint8_t layer = 0; layer < layerCount; ++layer)
                {
                    const Keycode keycode = LayerKey{ op, layer };
                    const std::string text = FormatKeycode(keycode);
                    layers.push_back({ keycode, text, text });
                }
            groups.emplace_back("layers", std::move(layers));

            for (const QmkKeycode& row : QmkKeycodeTable())
            {
                if (!row.ExistsIn(version))
                    continue;

                auto group = std::find_if(groups.begin(), groups.end(),
                                          [&](const auto& entry) { return entry.first == row.group; });
                if (group == groups.end())
                {
                    groups.emplace_back(row.group, std::vector<Choice>{});
                    group = groups.end() - 1;
                }

                // A macro is a macro by its index, as the codec decodes it.
                const Keycode keycode = std::string_view(row.group) == "macro"
                                            ? Keycode{ MacroKey{ static_cast<uint8_t>(row.value & 0x7F) } }
                                            : Keycode{ NamedKey{ row.name } };

                const KeycapLegend legend  = LegendFor(keycode, layout);
                std::string        caption = legend.primary.empty() ? std::string(row.name) : legend.primary;
                if (!legend.secondary.empty())
                    caption = legend.secondary + " " + caption;

                std::string tooltip = row.name;
                if (row.label[0] != '\0')
                    tooltip += std::string("\n") + row.label;

                group->second.push_back({ keycode, std::move(caption), std::move(tooltip) });
            }

            return groups;
        }
    }

    std::optional<Keycode> DrawKeycodePicker(KeycodePickerState& state,
                                             QmkKeycodeVersion   version,
                                             uint8_t             layerCount,
                                             const HostLayout&   layout)
    {
        std::optional<Keycode> picked;

        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        ImGui::InputTextWithHint("##filter", "filter: name or label", state.filter, sizeof(state.filter));

        const auto groups = BuildChoices(version, layerCount, layout);

        const float buttonWidth = ImGui::GetFontSize() * 6.5f;
        const float right       = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;

        for (const auto& [name, choices] : groups)
        {
            std::vector<const Choice*> shown;
            for (const Choice& choice : choices)
                if (ContainsNoCase(choice.tooltip, state.filter) || ContainsNoCase(choice.caption, state.filter))
                    shown.push_back(&choice);

            if (shown.empty())
                continue;

            const std::string header = name + " (" + std::to_string(shown.size()) + ")";
            if (!ImGui::CollapsingHeader(header.c_str(), state.filter[0] != '\0' ? ImGuiTreeNodeFlags_DefaultOpen : 0))
                continue;

            ImGui::PushID(name.c_str());
            for (size_t i = 0; i < shown.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Button(shown[i]->caption.c_str(), ImVec2(buttonWidth, 0)))
                    picked = shown[i]->keycode;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", shown[i]->tooltip.c_str());
                ImGui::PopID();

                // Flow the buttons into rows that fit the window.
                const float nextRight = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + buttonWidth;
                if (i + 1 < shown.size() && nextRight < right)
                    ImGui::SameLine();
            }
            ImGui::PopID();
        }

        return picked;
    }
}
