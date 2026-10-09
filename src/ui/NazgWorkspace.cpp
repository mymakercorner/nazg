// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgWorkspace.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <optional>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"   // RenderTextEllipsis(), to cut a section's name

#include "ui/NazgBoardView.h"
#include "ui/NazgIcons.h"
#include "ui/NazgSectionPlan.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // Pixels, before DPI scaling.
        constexpr float c_SplitterSize  = 10.0f;    // a splitter's thickness, its grip in the middle
        constexpr float c_GripLength    = 56.0f;
        constexpr float c_GripThickness = 4.0f;
        constexpr float c_PanelMin      = 275.0f;  // the key line, the tabs and three rows of tiles
        constexpr float c_PanelMinWidth = 980.0f;   // so a small board's tabs still fit on one line
        constexpr float c_DefaultShare  = 0.6f;     // of the height under the strip, the first time

        // The section column, as its mockup draws it (ui-design/section-column.html).
        constexpr float c_ColumnMin     = 120.0f;   // the list's narrowest, a name cut with "..."
        constexpr float c_ColumnMax     = 200.0f;
        constexpr float c_FoldBelow     = 100.0f;   // dragged narrower than this: icons only
        constexpr float c_FoldedWidth   = 44.0f;
        constexpr float c_EntryHeight   = 32.0f;
        constexpr float c_EntryPadding  = 9.0f;     // before the icon, between it and the name, after
        constexpr float c_IconSize      = 18.0f;
        constexpr float c_MonogramSize  = 9.0f;
        constexpr float c_HeaderSize    = 13.0f;    // a group's header, smaller than the names

        ImFont* g_IconFont = nullptr;

        // The grip both splitters draw, across the middle of the last item -- the board's
        // splitter lying down, the column's edge standing up: a short rounded bar, always shown,
        // brighter while hovered or dragged.
        void DrawGrip(bool isUpright, bool isLit)
        {
            const float  scale  = ImGui::GetStyle().FontScaleDpi;
            const ImVec2 min    = ImGui::GetItemRectMin();
            const ImVec2 max    = ImGui::GetItemRectMax();
            const ImVec2 middle((min.x + max.x) / 2.0f, (min.y + max.y) / 2.0f);
            const ImVec2 half   = isUpright ? ImVec2(c_GripThickness / 2.0f * scale, c_GripLength / 2.0f * scale)
                                            : ImVec2(c_GripLength / 2.0f * scale, c_GripThickness / 2.0f * scale);

            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(middle.x - half.x, middle.y - half.y),
                                                      ImVec2(middle.x + half.x, middle.y + half.y),
                                                      ImGui::GetColorU32(isLit ? ImGuiCol_SeparatorActive : ImGuiCol_Separator),
                                                      c_GripThickness / 2.0f * scale);
        }

        // The splitter under the board: dragged, it moves the board's height within `total`, the
        // height under the strip, and `across`, the width -- the window never grows for it --
        // from the size the board really took, so dragging past where the board stops growing,
        // or below its legibility floor, moves nothing.
        void DrawSplitter(WorkspaceLayout& layout, const BoardEvents& board, float total, float across)
        {
            const float scale = ImGui::GetStyle().FontScaleDpi;
            const float width = ImGui::GetContentRegionAvail().x;

            ImGui::InvisibleButton("##splitter", ImVec2(width, c_SplitterSize * scale));
            const bool active  = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();
            if (active || hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            DrawGrip(false, active || hovered);

            if (active && ImGui::GetIO().MouseDelta.y != 0.0f && total > 0.0f)
            {
                // The board keeps its proportions: as tall as `across` lets it be wide, about.
                float most = std::max(1.0f, total - (c_SplitterSize + c_PanelMin) * scale);
                if (board.fullWidth > 0.0f)
                    most = std::min(most, std::max(board.height, board.height * across / board.fullWidth));

                layout.boardHeight = std::clamp(board.height + ImGui::GetIO().MouseDelta.y, 1.0f, std::max(1.0f, most)) / scale;
                layout.changed     = true;
            }
        }

        // A section's icon, or a monogram of its name, centred on `centre` in a `box` pixels wide.
        void DrawSectionIcon(ImDrawList& list, const Section& section, ImVec2 centre, float box, ImU32 colour)
        {
            const Icon icon = section.ColumnIcon();
            if (icon != Icon::None && g_IconFont != nullptr)
            {
                // Tabler's em is the icon's box. Centred on its ink, so an icon drawn off-centre in
                // its box -- arrow-bar-to-down -- still sits in the middle of the row.
                ImFontBaked* const       baked = g_IconFont->GetFontBaked(box);
                const ImFontGlyph* const glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(icon));
                if (glyph != nullptr)
                {
                    const std::string text = Utf8Of(icon);
                    const ImVec2      at(std::floor(centre.x - (glyph->X0 + glyph->X1) / 2.0f),
                                         std::floor(centre.y - (glyph->Y0 + glyph->Y1) / 2.0f));
                    list.AddText(g_IconFont, box, at, colour, text.c_str());
                    return;
                }
            }

            // A monogram in a rounded square, about the size of an icon's ink.
            const float       scale = ImGui::GetStyle().FontScaleDpi;
            const float       half  = box * 0.42f;
            const std::string text  = MonogramOf(section.Name());
            list.AddRect(ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half), colour,
                         4.0f * scale, 0, 1.5f * scale);

            ImFont* const font   = ImGui::GetFont();
            const float   size   = c_MonogramSize * scale;
            const ImVec2  extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str());
            list.AddText(font, size, ImVec2(std::floor(centre.x - extent.x / 2.0f), std::floor(centre.y - extent.y / 2.0f)),
                         colour, text.c_str());
        }

        // One row: an icon and a name, or the icon alone when the column is folded -- the name is
        // then shown on hover, as it is whenever it is cut. The section shown has the accent bar.
        bool DrawEntry(const Section& section, bool isActive, bool isFolded)
        {
            const float scale = ImGui::GetStyle().FontScaleDpi;

            // The mockup's quiet tints rather than ImGui's header colours, which are a menu's.
            const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImVec4       hover  = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            hover.w             = 0.07f;
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(accent.x, accent.y, accent.z, 0.20f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, isActive ? ImVec4(accent.x, accent.y, accent.z, 0.26f) : hover);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(accent.x, accent.y, accent.z, 0.30f));
            const bool clicked = ImGui::Selectable("##section", isActive, 0, ImVec2(0.0f, c_EntryHeight * scale));
            ImGui::PopStyleColor(3);

            const bool   hovered = ImGui::IsItemHovered();
            const ImVec2 min     = ImGui::GetItemRectMin();
            const ImVec2 max     = ImGui::GetItemRectMax();
            ImDrawList&  list    = *ImGui::GetWindowDrawList();
            const ImU32  colour  = ImGui::GetColorU32(isActive || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);

            if (isActive)
                list.AddRectFilled(ImVec2(min.x - 4.0f * scale, min.y + 6.0f * scale),
                                   ImVec2(min.x - 1.0f * scale, max.y - 6.0f * scale), ImGui::GetColorU32(accent),
                                   1.5f * scale);

            const float box    = c_IconSize * scale;
            const float middle = (min.y + max.y) / 2.0f;
            const float centre = isFolded ? (min.x + max.x) / 2.0f : min.x + c_EntryPadding * scale + box / 2.0f;
            DrawSectionIcon(list, section, ImVec2(centre, middle), box, colour);

            const std::string name(section.Name());
            bool              isCut = isFolded;
            if (!isFolded)
            {
                const ImVec2 start(centre + box / 2.0f + c_EntryPadding * scale,
                                   std::floor(middle - ImGui::GetTextLineHeight() / 2.0f));
                const float  end    = max.x - c_EntryPadding * scale;
                const ImVec2 extent = ImGui::CalcTextSize(name.c_str());
                isCut               = start.x + extent.x > end;

                ImGui::PushStyleColor(ImGuiCol_Text, colour);
                ImGui::RenderTextEllipsis(&list, start, ImVec2(end, max.y), end, name.c_str(), nullptr, &extent);
                ImGui::PopStyleColor();
            }
            if (isCut)
                ImGui::SetItemTooltip("%s", name.c_str());

            return clicked;
        }

        // The column's edge, between it and the rest: dragged, it sizes the list -- or folds the
        // column to icons once below c_FoldBelow, never a width in between; double-clicked, it
        // folds or unfolds it. `left` is the column's left edge.
        void DrawColumnEdge(WorkspaceLayout& layout, float left, float height)
        {
            const float scale = ImGui::GetStyle().FontScaleDpi;

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::InvisibleButton("##column edge", ImVec2(c_SplitterSize * scale, std::max(1.0f, height)));
            const bool active  = ImGui::IsItemActive();
            const bool hovered = ImGui::IsItemHovered();
            if (active || hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            DrawGrip(true, active || hovered);

            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                layout.isColumnFolded = !layout.isColumnFolded;
                layout.changed        = true;
            }
            else if (active && ImGui::GetIO().MouseDelta.x != 0.0f)
            {
                const float width     = (ImGui::GetIO().MousePos.x - left) / scale;
                layout.isColumnFolded = width < c_FoldBelow;
                if (!layout.isColumnFolded)
                    layout.columnWidth = std::clamp(width, c_ColumnMin, c_ColumnMax);
                layout.changed = true;
            }
        }

        // The sections in their groups: Nazg's first, headed by the protocol's name, then the
        // board's menus, headed "Board" -- headers only when there is more than one group, and
        // none when the column is folded, where the line between the groups stays.
        void DrawColumn(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, std::string_view protocol,
                        WorkspaceLayout& layout)
        {
            const float scale    = ImGui::GetStyle().FontScaleDpi;
            const bool  isFolded = layout.isColumnFolded;
            const float width    = isFolded ? c_FoldedWidth : std::clamp(layout.columnWidth, c_ColumnMin, c_ColumnMax);

            const bool hasGroups = std::any_of(sections.begin(), sections.end(), [&](const auto& section)
                                               { return section->Group() != sections.front()->Group(); });

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * scale, 6.0f * scale));
            ImGui::BeginChild("sections", ImVec2(width * scale, 0.0f),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            const float left = ImGui::GetWindowPos().x;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f * scale));

            for (size_t index = 0; index < sections.size(); ++index)
            {
                const Section& section     = *sections[index];
                const bool     opensGroup  = index == 0 || section.Group() != sections[index - 1]->Group();
                if (opensGroup && index > 0)
                {
                    ImGui::Dummy(ImVec2(0.0f, 3.0f * scale));
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0.0f, 1.0f * scale));
                }
                if (opensGroup && hasGroups && !isFolded)
                {
                    std::string header = section.Group() == SectionGroup::Board   ? "Board"
                                         : section.Group() == SectionGroup::Tools ? "Tools"
                                                                                  : std::string(protocol);
                    std::transform(header.begin(), header.end(), header.begin(),
                                   [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });

                    ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + c_EntryPadding * scale);
                    ImGui::PushFont(nullptr, c_HeaderSize);
                    ImGui::TextDisabled("%s", header.c_str());
                    ImGui::PopFont();
                }

                ImGui::PushID(static_cast<int>(index));
                if (DrawEntry(section, index == active, isFolded))
                    active = index;
                ImGui::PopID();
            }

            ImGui::PopStyleVar();
            ImGui::EndChild();

            DrawColumnEdge(layout, left, ImGui::GetItemRectSize().y);
        }

        void DrawStrip(Section& section)
        {
            const Strip strip = section.DescribeStrip();
            if (strip.entries.empty())
                return;

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(strip.label.c_str());

            const float           minWidth = ImGui::GetFrameHeight() * 1.4f;
            std::optional<size_t> hovered;
            for (size_t index = 0; index < strip.entries.size(); ++index)
            {
                ImGui::SameLine();
                ImGui::PushID(static_cast<int>(index));

                const bool isChosen = index == strip.chosen;
                if (isChosen)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

                const std::string& entry = strip.entries[index];
                const float width = std::max(minWidth, ImGui::CalcTextSize(entry.c_str()).x +
                                                           2 * ImGui::GetStyle().FramePadding.x);
                if (ImGui::Button(entry.c_str(), ImVec2(width, 0.0f)) && !isChosen)
                    section.OnStripChosen(index);
                if (ImGui::IsItemHovered())
                    hovered = index;

                if (isChosen)
                    ImGui::PopStyleColor();
                ImGui::PopID();
            }
            section.OnStripHovered(hovered);
        }
    }

    void SetIconFont(ImFont* font)
    {
        g_IconFont = font;
    }

    HeaderAction DrawHeader(const HeaderView& view)
    {
        HeaderAction action;
        if (!ImGui::BeginMenuBar())
            return action;

        if (view.name.empty())
        {
            ImGui::TextDisabled("No board open");
        }
        else
        {
            // The name is the menu, as in ZMK Studio and VIA: it adds nothing to the first
            // glance, and it is the way back to the list when Nazg opened a lone board itself.
            const bool isOpen = ImGui::BeginMenu((view.name + "##board").c_str());
            if (!isOpen && !view.details.empty())
                ImGui::SetItemTooltip("%s", view.details.c_str());

            if (isOpen)
            {
                if (!view.others.empty())
                {
                    ImGui::SeparatorText("Switch to");
                    for (size_t index = 0; index < view.others.size(); ++index)
                    {
                        ImGui::PushID(static_cast<int>(index));
                        if (ImGui::MenuItem(view.others[index].name.c_str(), view.others[index].protocol.c_str(),
                                            false, !view.isBusy))
                            action.switchTo = index;
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                }

                if (view.isVia)
                {
                    action.changeDefinition = ImGui::MenuItem("Change definition...", nullptr, false, !view.isBusy);
                    action.forgetChoice     = ImGui::MenuItem("Forget choice", nullptr, false, view.hasChoice);
                    ImGui::SetItemTooltip("Nazg asks again the next time this board is opened,\n"
                                          "if more than one definition matches it.");
                }

                // Designer and debugging tools, only when asked for in Settings -- the matrix view
                // then joins the column's Tools group.
                if (view.hasAdvanced && ImGui::BeginMenu("Advanced"))
                {
                    action.exportDefinition = ImGui::MenuItem("Export definition...", nullptr, false, view.canExport);
                    ImGui::SetItemTooltip("For investigation and debugging: save the definition drawing this board\n"
                                          "exactly as Nazg has it.");

                    ImGui::EndMenu();
                }

                ImGui::Separator();
                action.allKeyboards = ImGui::MenuItem("All keyboards", nullptr, false, !view.isBusy);

                ImGui::EndMenu();
            }

            if (!view.protocol.empty())
                ImGui::TextDisabled("%s", view.protocol.c_str());
        }

        // On the right: the lock state on boards that have one, then the settings button.
        const char* settings = "Settings";
        const char* lock     = !view.isLocked ? nullptr : *view.isLocked ? "Locked" : "Unlocked";
        const float spacing  = ImGui::GetStyle().ItemSpacing.x;

        float width = ImGui::CalcTextSize(settings).x + 2 * spacing;
        if (lock != nullptr)
            width += ImGui::CalcTextSize(lock).x + spacing;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));

        // Clicking it is how the board is unlocked, or locked again.
        if (lock != nullptr)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ColourOf(*view.isLocked ? PanelColour::Warning : PanelColour::Muted));
            action.toggleLock = ImGui::MenuItem(lock, nullptr, false, !view.isBusy);
            ImGui::PopStyleColor();

            if (*view.isLocked)
                ImGui::SetItemTooltip("Vial refuses some changes while the board is locked:\n"
                                      "macros, the matrix tester, and QK_BOOT in the keymap.\n"
                                      "Click to unlock it.");
            else
                ImGui::SetItemTooltip("Every change is accepted until the board restarts.\n"
                                      "Click to lock it again.");
        }

        action.settings = ImGui::MenuItem(settings, nullptr, view.isSettingsShown);

        ImGui::EndMenuBar();
        return action;
    }

    void DrawSections(const std::vector<std::unique_ptr<Section>>& sections, size_t& active, const Keyboard& keyboard,
                      std::string_view protocol, WorkspaceLayout& layout)
    {
        if (sections.empty())
            return;
        active = std::min(active, sections.size() - 1);

        // The column: only the sections this board has, and none when there is only one. Its
        // edge takes the place of the spacing after it.
        if (sections.size() > 1)
        {
            DrawColumn(sections, active, protocol, layout);
            ImGui::SameLine(0.0f, 0.0f);
        }

        for (size_t index = 0; index < sections.size(); ++index)
            if (index != active)
                sections[index]->WhileHidden();

        DrawView(*sections[active], keyboard, layout);
    }

    void DrawView(Section& section, const Keyboard& keyboard, WorkspaceLayout& layout)
    {
        ImGui::BeginGroup();
        ImGui::PushID(&section);

        DrawStrip(section);

        // The board takes its height, the panel the rest -- never so little that the panel falls
        // below its minimum, the board giving way then, down to its legibility floor.
        const float scale  = ImGui::GetStyle().FontScaleDpi;
        const float total  = ImGui::GetContentRegionAvail().y;
        const float across = ImGui::GetContentRegionAvail().x;
        if (layout.boardHeight <= 0.0f && total > 0.0f)
        {
            layout.boardHeight = c_DefaultShare * total / scale;
            layout.changed     = true;
        }
        const float boardMax = std::clamp(layout.boardHeight * scale, 0.0f,
                                          std::max(0.0f, total - (c_SplitterSize + c_PanelMin) * scale));

        BoardDescription board = DescribeKeyboard(keyboard);
        section.DescribeBoard(board);
        const BoardEvents events = DrawBoard(board, boardMax);
        section.OnBoardEvents(board, events);

        DrawSplitter(layout, events, total, across);

        // What the window could lose: across, until the board reaches its floor -- the board
        // shrinks with a narrower window once the room around it is gone (Rico, 2026-10-05);
        // down, before either gives way.
        layout.spareWidth  = std::max(0.0f, across - std::max(c_PanelMinWidth * scale, events.floorWidth));
        layout.spareHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y - c_PanelMin * scale);

        // The panel: always there, taking all the board leaves -- the whole width, so the picker
        // has the room the board does not need (Rico, 2026-10-05).
        ImGui::BeginChild("panel", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
        section.DrawPanel();
        ImGui::EndChild();

        ImGui::PopID();
        ImGui::EndGroup();
    }
}
