// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSlotParts.h"

#include <algorithm>

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    std::string JoinNames(const std::vector<std::string>& names)
    {
        std::string joined;
        for (size_t index = 0; index < names.size(); ++index)
            joined += (index == 0 ? "" : index + 1 == names.size() ? " and " : ", ") + names[index];
        return joined;
    }

    bool DrawReading(const char* what, bool busy, const std::string& failure)
    {
        if (busy)
        {
            ImGui::TextDisabled("Reading the %s...", what);
            return false;
        }
        if (failure.empty())
            return false;
        ColouredText(PanelColour::Error, "%s", failure.c_str());
        return ImGui::Button("Try again");
    }

    void DrawWriteState(bool busy, const std::string& changed, const std::string& message, bool isWarning)
    {
        if (busy)
            ColouredText(PanelColour::Muted, "Writing...");
        else if (!changed.empty())
            ColouredText(PanelColour::Warning, "Not written yet -- %s changed", changed.c_str());
        else if (!message.empty())
            ColouredText(isWarning ? PanelColour::Warning : PanelColour::Success, "%s", message.c_str());
        else
            ColouredText(PanelColour::Muted, "Changes are written by Save.");
    }

    SlotWrite DrawSaveRevert(bool canSave, bool canRevert)
    {
        const ImGuiStyle& style   = ImGui::GetStyle();
        const float       buttons = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Revert").x +
                                    4 * style.FramePadding.x + style.ItemSpacing.x;
        SlotWrite clicked = SlotWrite::None;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
        ImGui::BeginDisabled(!canSave);
        if (ImGui::Button("Save"))
            clicked = SlotWrite::Save;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!canRevert);
        if (ImGui::Button("Revert"))
            clicked = SlotWrite::Revert;
        ImGui::EndDisabled();
        return clicked;
    }

    void MarkUnlockKeys(BoardDescription& board, const std::optional<VialUnlockStatus>& lock)
    {
        if (!lock || lock->unlocked)
            return;
        for (BoardKey& key : board.keys)
            for (const auto& [row, column] : lock->unlockKeys)
                if (key.geometry.row == row && key.geometry.column == column)
                    key.marks |= Mark::Warning;
    }
}
