// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycodePicker.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodes.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // Between tiles, and between rows of them, in pixels before DPI scaling: a cell is a
        // tile and its gap, and a group's title box takes two (ui-design.md, "The tiles").
        constexpr float c_TileGap   = 4.0f;
        constexpr float c_RowGap    = 6.0f;
        constexpr int   c_LabelCells = 2;
        constexpr int   c_MinCells   = 6;

        // The search box takes what the tabs leave, from 100 px up to its hint's width.
        constexpr float c_SearchMin = 100.0f;
        constexpr float c_SearchMax = 220.0f;
        constexpr char  c_SearchHint[] = "Search: name, QMK name or label";

        bool IsNamed(const Keycode& keycode, std::string_view name)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named != nullptr && named->name == name;
        }

        // The header every keycode of a group shares -- Media, Hold, MIDI -- which its title says,
        // so the tiles leave it out.
        bool SharesHeader(const std::vector<Keycode>& keycodes, const LegendContext& context)
        {
            if (keycodes.size() < 2)
                return false;

            std::string shared;
            for (const Keycode& keycode : keycodes)
            {
                const KeycapLegend legend = LegendFor(keycode, context);
                const std::string& header = !legend.hold.IsEmpty() ? legend.hold.words.full : legend.header.words.full;
                if (header.empty() || (!shared.empty() && header != shared))
                    return false;
                shared = header;
            }
            return true;
        }

        // What a tile prints: the keycap's legend, with what only the picker needs.
        KeycodeTile TileFor(const Keycode& keycode, const LegendContext& context, bool dropHeader, bool selected)
        {
            KeycodeTile  tile;
            KeycapLegend legend = LegendFor(keycode, context);
            tile.band = legend.Band();

            // A key blank on the board says what it is here.
            if (legend.placement == PlacementClass::Blank)
            {
                legend.placement   = PlacementClass::Modifier;
                legend.cylindrical = { IsNamed(keycode, "KC_SPC") ? "Space" : "None", "" };
            }

            // Shift alone with a character key: its Shift character alone, as VIA's tiles -- the
            // board keeps "Shift+" over the pair, one rule for every modifier (short-forms.md, rule 7).
            if (const auto* modified = std::get_if<ModifiedKey>(&keycode);
                modified != nullptr && (modified->mods == Mod::LeftShift || modified->mods == Mod::RightShift) &&
                !legend.shifted.empty())
            {
                legend.plain = legend.shifted;
                legend.shifted.clear();
                legend.header = {};
            }

            if (dropHeader)
            {
                legend.header = {};
                legend.hold   = {};
            }

            tile.legend   = std::move(legend);
            tile.fill     = KeycapClassOf(keycode, DefinitionKey{});
            tile.mark     = IsNamed(keycode, "KC_TRNS") ? Fallthrough::Transparent
                          : IsNamed(keycode, "KC_NO")   ? Fallthrough::Disabled
                                                        : Fallthrough::None;
            tile.selected = selected;
            return tile;
        }

        // The title in its box, its words wrapped and cut to it; the rest of it on hover.
        void DrawGroupLabel(const CatalogueGroup& group, ImVec2 p0, ImVec2 p1)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float scale    = ImGui::GetStyle().FontScaleDpi;
            const float rounding = 5.0f * scale;

            drawList->AddRectFilled(p0, p1, BoardColours::GroupLabel(), rounding);
            drawList->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Border), rounding);

            ImFont*      font  = ImGui::GetFont();
            const float  size  = ImGui::GetFontSize() * 0.92f;
            const float  pad   = 8.0f * scale;
            const float  wrap  = std::max(1.0f, p1.x - p0.x - 2 * pad);
            const ImVec2 text  = font->CalcTextSizeA(size, FLT_MAX, wrap, group.title.c_str());
            const ImVec2 at(p0.x + pad, p0.y + std::max(0.0f, (p1.y - p0.y - text.y) / 2.0f));
            const ImVec4 clip(p0.x + 1, p0.y + 1, p1.x - 1, p1.y - 1);
            drawList->AddText(font, size, at, BoardColours::GroupLabelText(), group.title.c_str(), nullptr, wrap, &clip);

            if (!group.more.empty() && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p0, p1))
                ImGui::SetTooltip("%s -- %s", group.title.c_str(), group.more.c_str());
        }

        // The groups on one grid of cells: a title box takes two, a tile one, and no empty cell
        // parts two groups on a row -- the box does. A group that does not fit the rest of a row
        // starts the next; one longer than a row wraps, its rows starting under its first tile,
        // its box spanning them all (Rico, 2026-10-04).
        void DrawGroups(const std::vector<CatalogueGroup>& groups, KeycodePickerState& state,
                        const KeycodePickerInput& input, KeycodePickerEvents& events)
        {
            const float scale  = ImGui::GetStyle().FontScaleDpi * c_TileZoom;
            const float tileW  = c_TileWidth * scale;
            const float tileH  = c_TileHeight * scale;
            const float gapX   = c_TileGap * scale;
            const float cell   = tileW + gapX;
            const float rowH   = tileH + c_RowGap * scale;
            const int   cells  = std::max(c_MinCells, static_cast<int>((ImGui::GetContentRegionAvail().x + gapX) / cell));
            const ImVec2 origin = ImGui::GetCursorScreenPos();

            bool anyHovered = false;
            int  row = 0, column = 0;
            for (size_t index = 0; index < groups.size(); ++index)
            {
                const CatalogueGroup& group = groups[index];
                const int             count = static_cast<int>(group.keycodes.size());

                int start = column;
                if (column > 0 && start + std::min(c_LabelCells + count, cells) > cells)
                {
                    ++row;
                    start = 0;
                }
                const int first = row;
                int       at    = start + c_LabelCells;

                const bool dropHeader = SharesHeader(group.keycodes, input.context);

                ImGui::PushID(static_cast<int>(index));
                for (int tileIndex = 0; tileIndex < count; ++tileIndex)
                {
                    if (at + 1 > cells)
                    {
                        ++row;
                        at = start + c_LabelCells;
                    }

                    const Keycode& keycode = group.keycodes[static_cast<size_t>(tileIndex)];
                    const ImVec2   p0(origin.x + static_cast<float>(at) * cell, origin.y + static_cast<float>(row) * rowH);

                    ImGui::PushID(tileIndex);
                    ImGui::SetCursorScreenPos(p0);
                    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
                    const bool hovered = ImGui::IsItemHovered();
                    const std::string key = FormatKeycode(keycode);

                    const bool selected = input.current && *input.current == keycode;
                    KeycodeTile        tile = TileFor(keycode, input.context, dropHeader, selected);
                    const LightingNote note = input.keyboard != nullptr ? LightingNoteOf(keycode, *input.keyboard)
                                                                        : LightingNote{};
                    const std::optional<std::string> refused = input.unavailable ? input.unavailable(keycode) : std::nullopt;
                    tile.hovered = hovered && !refused;
                    tile.isFaint = note.mayDoNothing || refused.has_value();
                    DrawKeycodeTile(tile, { p0.x, p0.y, p0.x + tileW, p0.y + tileH });

                    // What a click writes, the key line's hold or modifiers kept.
                    const Keycode composed =
                        input.current ? ComposeWithKey(keycode, *input.current, input.version) : keycode;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    {
                        std::string more = note.text.empty() ? std::string() : "\n" + note.text;
                        if (refused)
                            more += "\n" + *refused;
                        if (composed == keycode)
                            ImGui::SetTooltip("%s%s", KeycodeHoverText(keycode, input.version).c_str(), more.c_str());
                        else
                            ImGui::SetTooltip("%s\n%s, keeping the key's %s%s", FormatKeycode(composed).c_str(),
                                              key.c_str(),
                                              std::holds_alternative<ModifiedKey>(composed) ? "modifiers" : "hold",
                                              more.c_str());

                        if (input.current && state.justPicked != key)
                            events.preview = composed;
                    }
                    if (hovered)
                    {
                        anyHovered = true;
                        if (state.justPicked && *state.justPicked != key)
                            state.justPicked.reset();
                    }
                    if (clicked && !refused)
                    {
                        events.picked    = keycode;
                        events.preview.reset();
                        state.justPicked = key;
                    }
                    ImGui::PopID();
                    ++at;
                }
                ImGui::PopID();

                const ImVec2 labelP0(origin.x + static_cast<float>(start) * cell, origin.y + static_cast<float>(first) * rowH);
                const ImVec2 labelP1(labelP0.x + c_LabelCells * cell - gapX,
                                     origin.y + static_cast<float>(row + 1) * rowH - c_RowGap * scale);
                DrawGroupLabel(group, labelP0, labelP1);

                column = at;
            }

            if (!anyHovered)
                state.justPicked.reset();

            // Claim the space drawn into, so the child scrolls.
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(static_cast<float>(cells) * cell - gapX,
                                groups.empty() ? 0.0f : static_cast<float>(row + 1) * rowH - c_RowGap * scale));
        }

        // A search through every tab: a group per tab with what matches, titled with its name.
        std::vector<CatalogueGroup> SearchResults(const KeycodePickerInput& input, std::string_view query)
        {
            std::vector<CatalogueGroup> results;
            for (const CatalogueTab& tab : input.tabs)
            {
                CatalogueGroup found{ std::string(tab.name), "", {} };
                for (const CatalogueGroup& group : tab.groups)
                    for (const Keycode& keycode : group.keycodes)
                        if (MatchesSearch(SearchTextOf(keycode, input.version, input.context), query))
                            found.keycodes.push_back(keycode);
                if (!found.keycodes.empty())
                    results.push_back(std::move(found));
            }
            return results;
        }
    }

    std::string KeycodeHoverText(const Keycode& keycode, QmkKeycodeVersion version)
    {
        std::string text = FormatKeycode(keycode);
        if (const auto* named = std::get_if<NamedKey>(&keycode))
            if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, version);
                row != nullptr && row->label[0] != '\0' && text != row->label)
                text += std::string(" -- ") + row->label;
        return text;
    }

    KeycodeTile TileOf(const Keycode& keycode, const LegendContext& context, bool selected)
    {
        return TileFor(keycode, context, false, selected);
    }

    ImVec2 TileSize()
    {
        const float scale = ImGui::GetStyle().FontScaleDpi * c_TileZoom;
        return ImVec2(c_TileWidth * scale, c_TileHeight * scale);
    }

    KeycodePickerEvents DrawKeycodePicker(KeycodePickerState& state, const KeycodePickerInput& input)
    {
        KeycodePickerEvents events;
        if (input.tabs.empty())
            return events;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float       scale = style.FontScaleDpi;

        // The tabs, then the search box on their line, taking what they leave.
        const float avail   = ImGui::GetContentRegionAvail().x;
        const float searchW = std::clamp(avail * 0.22f, c_SearchMin * scale, c_SearchMax * scale);

        ImGui::BeginChild("##tabbar", ImVec2(avail - searchW - style.ItemSpacing.x, ImGui::GetFrameHeight() + 2 * scale),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyShrink))
        {
            const bool known = std::any_of(input.tabs.begin(), input.tabs.end(),
                                           [&](const CatalogueTab& tab) { return tab.id == state.tab; });
            std::string chosen = state.tab;
            for (const CatalogueTab& tab : input.tabs)
            {
                // Room for the dot before the name; the id stays the tab's whatever its name.
                const std::string label = std::string(tab.category != CommandCategory::None ? "    " : "") +
                                          std::string(tab.name) + "###" + std::string(tab.id);
                const ImGuiTabItemFlags flags =
                    (known && state.restoreTab && tab.id == state.tab) ? ImGuiTabItemFlags_SetSelected : 0;

                const bool open = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
                if (tab.category != CommandCategory::None)
                {
                    const ImVec2 min    = ImGui::GetItemRectMin();
                    const float  radius = 3.5f * scale;
                    const ImVec2 centre(min.x + style.FramePadding.x + radius, (min.y + ImGui::GetItemRectMax().y) / 2.0f);
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        centre, radius,
                        BoardColours::Category(tab.category, CategoryUse::Band, KeyFill::Modifier, 0.0f)
                            .value_or(ImGui::GetColorU32(ImGuiCol_Text)));
                }
                if (open)
                {
                    chosen = std::string(tab.id);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();

            // A tab clicked ends a search. The frame the remembered tab is restored, the bar may
            // still report its first tab: that is not a click.
            if (!state.restoreTab || !known)
            {
                if (chosen != state.tab)
                    state.search[0] = '\0';
                state.tab = chosen;
            }
            state.restoreTab = false;
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::SetNextItemWidth(searchW);
        const bool  fits = ImGui::CalcTextSize(c_SearchHint).x + 2 * style.FramePadding.x <= searchW;
        ImGui::InputTextWithHint("##search", fits ? c_SearchHint : "Search", state.search, sizeof(state.search));

        // The tiles, scrolling under the tabs.
        ImGui::BeginChild("##tiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        if (state.search[0] != '\0')
        {
            if (state.searched != state.search || state.searchedTabs != &input.tabs)
            {
                state.results      = SearchResults(input, state.search);
                state.searched     = state.search;
                state.searchedTabs = &input.tabs;
            }
            const std::vector<CatalogueGroup>& results = state.results;
            if (results.empty())
                ImGui::TextDisabled("Nothing matches. Type a QMK expression in the key line for anything not listed.");
            else
                DrawGroups(results, state, input, events);
        }
        else
        {
            const auto tab = std::find_if(input.tabs.begin(), input.tabs.end(),
                                          [&](const CatalogueTab& each) { return each.id == state.tab; });
            DrawGroups(tab != input.tabs.end() ? tab->groups : input.tabs.front().groups, state, input, events);
        }
        ImGui::EndChild();

        return events;
    }
}
