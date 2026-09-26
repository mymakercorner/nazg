// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSettingsScreen.h"

#include "imgui.h"

#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        void DrawHostLayout(std::string& hostLayoutId, SettingsAction& action)
        {
            ImGui::SeparatorText("Legends");

            // A saved id this build does not know falls back to US rather than failing.
            const HostLayout* found   = FindHostLayout(hostLayoutId);
            const HostLayout& current = found != nullptr ? *found : UsHostLayout();

            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            if (ImGui::BeginCombo("Host layout", std::string(current.name).c_str()))
            {
                for (const HostLayout& choice : HostLayouts())
                {
                    const bool isCurrent = &choice == &current;
                    if (ImGui::Selectable(std::string(choice.name).c_str(), isCurrent) && !isCurrent)
                    {
                        hostLayoutId             = choice.id;
                        action.hostLayoutChanged = true;
                    }
                    if (isCurrent)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("The layout your computer types with: legends show what each key\n"
                                  "types there. The keymap on the board does not change.");
        }

        void DrawOfficial(const OfficialDefinitionsInfo& official)
        {
            if (!official.found)
                ColouredText(PanelColour::Warning, "Official: not found");
            else if (official.count >= 0)
                ImGui::Text("Official: %d, from VIA", official.count);
            else
                ImGui::TextUnformatted("Official: from VIA");

            if (ImGui::IsItemHovered())
            {
                if (official.commit.empty())
                    ImGui::SetTooltip("%s", official.path.c_str());
                else
                    ImGui::SetTooltip("%s\nthe-via/keyboards at %s", official.path.c_str(), official.commit.c_str());
            }
        }

        void DrawUserDefinitions(const SettingsView& view, SettingsAction& action)
        {
            if (view.library == nullptr)
            {
                ColouredText(PanelColour::Error, "User definitions are unavailable: %s", view.libraryError.c_str());
                return;
            }

            ImGui::Text("Yours: %zu", view.library->Entries().size());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", view.libraryFolder.c_str());

            if (!view.library->Entries().empty() &&
                ImGui::BeginTable("definitions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
            {
                ImGui::TableSetupColumn("VID:PID", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed);

                for (const LibraryEntry& entry : view.library->Entries())
                {
                    ImGui::PushID(static_cast<int>(entry.id));
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ColouredText(PanelColour::Muted, "%04X:%04X", entry.vendorId, entry.productId);

                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(entry.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("imported from %s\nfirst imported on %s", entry.origin.c_str(),
                                          entry.added.c_str());

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Re-import"))
                        action.reimport = entry.id;
                    ImGui::SetItemTooltip("Read %s again, after editing it.\nThe version it replaces is kept.",
                                          entry.origin.c_str());

                    if (entry.previousRevision != 0)
                    {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Restore previous"))
                            action.restore = entry.id;
                        ImGui::SetItemTooltip("Go back to the version before the last re-import.\n"
                                              "The current one becomes the previous, so this can be undone.");
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove"))
                        action.remove = entry.id;

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            action.import = ImGui::Button("Import a definition...");
            ImGui::SetItemTooltip("Add a user definition: the .json file that describes your keyboard,\n"
                                  "often named via.json, from its vendor or designer.");

            if (view.libraryMessages != nullptr)
                for (const std::string& message : *view.libraryMessages)
                    ColouredText(PanelColour::Muted, "%s", message.c_str());
        }
    }

    SettingsAction DrawSettings(const SettingsView& view, std::string& hostLayoutId, bool& showDemoWindow)
    {
        SettingsAction action;

        action.back = ImGui::Button(view.backLabel);

        DrawHostLayout(hostLayoutId, action);

        ImGui::SeparatorText("Definitions");
        DrawOfficial(view.official);
        DrawUserDefinitions(view, action);

        ImGui::SeparatorText("About");
        ImGui::TextUnformatted("Nazg -- one keyboard configurator to rule them all.");
        for (const std::string& line : view.about)
            ColouredText(PanelColour::Muted, "%s", line.c_str());
        ImGui::Checkbox("Dear ImGui demo window", &showDemoWindow);

        return action;
    }
}
