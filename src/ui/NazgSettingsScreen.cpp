// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSettingsScreen.h"

#include <span>
#include <string>

#include "imgui.h"

#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        void DrawHostLayout(LegendSettings& legends, SettingsAction& action)
        {
            const HostLayout& current = legends.Layout();

            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            if (ImGui::BeginCombo("Host layout", std::string(current.name).c_str()))
            {
                for (const HostLayout& choice : HostLayouts())
                {
                    const bool isCurrent = &choice == &current;
                    if (ImGui::Selectable(std::string(choice.name).c_str(), isCurrent) && !isCurrent)
                    {
                        legends.hostLayout    = choice.id;
                        action.legendsChanged = true;
                    }
                    if (isCurrent)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("The layout your computer types with: legends show what each key\n"
                                  "types there. The keymap on the board does not change.");
        }

        // A combo over a setting's choices, by their names; true when one was picked.
        template <typename Choice>
        bool DrawChoice(const char* label, std::span<const Choice> choices, Choice& current)
        {
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            if (ImGui::BeginCombo(label, std::string(NameOf(current)).c_str()))
            {
                for (const Choice choice : choices)
                {
                    const bool isCurrent = choice == current;
                    if (ImGui::Selectable(std::string(NameOf(choice)).c_str(), isCurrent) && !isCurrent)
                    {
                        current = choice;
                        changed = true;
                    }
                    if (isCurrent)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void DrawLegendFamily(BoardStyle& style, SettingsAction& action)
        {
            action.appearanceChanged |= DrawChoice("Legend style", LegendFamilies(), style.legends);
            ImGui::SetItemTooltip("Cylindrical: as GMK keycaps print them -- top left, mixed case.\n"
                                  "Spherical: as SA keycaps print them -- centred, capitals.");
        }

        void DrawModifierNames(LegendSettings& legends, SettingsAction& action)
        {
            action.legendsChanged |= DrawChoice("Modifier names", AllModifierNames(), legends.modifierNames);
            ImGui::SetItemTooltip("What the modifiers are called on your computer:\n"
                                  "Windows: Win, Alt, Alt Gr.  Mac: Cmd, Option.  Linux: Super, Alt, Alt Gr.");
        }

        void DrawAppearance(BoardStyle& style, SettingsAction& action)
        {
            ImGui::SeparatorText("Appearance");
            action.appearanceChanged |= DrawChoice("Theme", Themes(), style.theme);
            action.appearanceChanged |= DrawChoice("Keycaps", KeycapStyles(), style.keycaps);
            ImGui::SetItemTooltip("Outlined: a thin border around each key.\n"
                                  "Bottom lip: a darker strip under each key, as if seen slightly from above.");
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

    SettingsAction DrawSettings(const SettingsView& view, LegendSettings& legends, BoardStyle& style,
                                bool& moveToNextKey, bool& advancedTools)
    {
        SettingsAction action;

        action.back = ImGui::Button(view.backLabel);

        DrawAppearance(style, action);

        ImGui::SeparatorText("Legends");
        DrawLegendFamily(style, action);
        DrawHostLayout(legends, action);
        DrawModifierNames(legends, action);

        // Off by default (Rico, 2026-10-04): the writes are live and there is no undo, so a
        // second click meant to correct a pick would land, written, on the key after.
        ImGui::SeparatorText("Keymap");
        action.keymapChanged = ImGui::Checkbox("Move to the next key after a pick", &moveToNextKey);
        ImGui::SetItemTooltip("Fills a row in one click a key, as Vial does: after a keycode is picked or\n"
                              "typed, the next key -- top to bottom, then left to right -- is selected.\n"
                              "Every pick is written at once, and a pick cannot be undone.");

        ImGui::SeparatorText("Definitions");
        DrawOfficial(view.official);
        DrawUserDefinitions(view, action);

        ImGui::SeparatorText("Advanced");
        action.advancedToolsChanged = ImGui::Checkbox("Advanced tools", &advancedTools);
        ImGui::SetItemTooltip("For keyboard designers and debugging: adds an Advanced submenu to the\n"
                              "board menu, with the matrix view and Export definition.");

        ImGui::SeparatorText("About");
        ImGui::TextUnformatted("Nazg -- one keyboard configurator to rule them all.");
        for (const std::string& line : view.about)
            ColouredText(PanelColour::Muted, "%s", line.c_str());

        return action;
    }
}
