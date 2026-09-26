// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDefinitionPicker.h"

#include "imgui.h"

#include "ui/NazgBoardView.h"

namespace nazg
{
    std::string DescribeDefinitionSource(const DefinitionCandidate& candidate)
    {
        if (candidate.ref.kind == DefinitionRef::Kind::User)
            return "user definition, imported from " + candidate.origin;
        return "official definition, from VIA";
    }

    DefinitionPickerAction DrawDefinitionPicker(const std::vector<DefinitionCandidate>& candidates,
                                                const std::optional<DefinitionRef>&     current,
                                                const DeviceIdentity&                   device,
                                                bool                                    canCancel)
    {
        DefinitionPickerAction action;

        if (candidates.empty())
        {
            ImGui::TextWrapped("This board (%04X:%04X) is not in VIA's registry, and no user definition matches it. "
                               "Your vendor probably provides a via.json for it -- import it.",
                               device.vendorId, device.productId);
        }
        else if (candidates.size() == 1)
        {
            ImGui::TextWrapped("One definition matches %04X:%04X. If it does not draw this board, import one that "
                               "does. The keymap on the board does not change either way.",
                               device.vendorId, device.productId);
        }
        else
        {
            ImGui::TextWrapped("%zu definitions match %04X:%04X. Which one draws this board? Nazg remembers the "
                               "answer for it. The keymap on the board does not change, whichever you pick.",
                               candidates.size(), device.vendorId, device.productId);
        }

        const float scale = ImGui::GetStyle().FontScaleDpi;

        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const DefinitionCandidate& candidate = candidates[index];
            const bool                 isCurrent = current && *current == candidate.ref;

            ImGui::PushID(static_cast<int>(index));
            ImGui::Separator();

            ImGui::BeginGroup();
            DrawDefinitionPreview(candidate.definition, 320.0f * scale, 110.0f * scale);
            ImGui::EndGroup();

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextUnformatted(candidate.definition.name.empty() ? "(unnamed)" : candidate.definition.name.c_str());
            ImGui::TextDisabled("%s", DescribeDefinitionSource(candidate).c_str());
            if (isCurrent)
                ImGui::TextDisabled("in use now");
            if (ImGui::Button(isCurrent ? "Keep this one" : "Use this one"))
                action.picked = index;
            ImGui::EndGroup();

            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::Button("Import a definition..."))
            action.import = true;
        ImGui::SetItemTooltip("Add a user definition: the .json file that describes your keyboard,\n"
                              "often named via.json, from its vendor or designer.");

        if (canCancel)
        {
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                action.cancel = true;
        }

        return action;
    }
}
