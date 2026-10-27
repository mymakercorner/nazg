// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeycodePicker.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
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

        // What a tile prints: the keycap's legend, with what only the picker needs. `shiftAlone`: a
        // tile's -- Extra's keycaps print every level of the key, as Basic's do (Rico, 2026-10-27).
        KeycodeTile TileFor(const Keycode& keycode, const LegendContext& context, bool dropHeader, bool selected,
                            bool shiftAlone = true)
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
                shiftAlone && modified != nullptr && (modified->mods == Mod::LeftShift || modified->mods == Mod::RightShift) &&
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
            tile.fill     = KeycapClassOf(keycode, via::DefinitionKey{});
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

        // The full-size keyboard's width, in key units (FullSizeKeyboard()), and the unit Basic draws
        // it at, and Extra its keys: a 1u key the tiles' size at most -- 44 px before their zoom --
        // smaller to fit the panel's width, down to a floor past which the panel scrolls sideways
        // (ui-design.md, "The Keys tab as a keyboard"). Pixels before DPI scaling.
        constexpr float c_KeyboardUnits = 22.5f;
        constexpr float c_DrawnUnitMax  = 44.0f * c_TileZoom;
        constexpr float c_DrawnUnitMin  = 30.0f;

        // Room is kept for a vertical scrollbar whether it shows or not, so the keys keep their size
        // when it comes and goes.
        float DrawnUnit()
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float room  = ImGui::GetContentRegionAvail().x - (ImGui::GetScrollMaxY() > 0.0f ? 0.0f : style.ScrollbarSize);
            const float scale = style.FontScaleDpi;
            return std::clamp(std::floor(room / c_KeyboardUnits), c_DrawnUnitMin * scale, c_DrawnUnitMax * scale);
        }

        // What the board makes of a keycode in the picker: a lighting key that may do nothing on
        // its firmware, and why the caller refuses it, if it does -- either drawn faint.
        struct Availability
        {
            LightingNote               note;
            std::optional<std::string> refused;

            [[nodiscard]] bool IsFaint() const { return note.mayDoNothing || refused.has_value(); }
        };

        Availability AvailabilityOf(const Keycode& keycode, const KeycodePickerInput& input)
        {
            return { input.keyboard != nullptr ? LightingNoteOf(keycode, *input.keyboard) : LightingNote{},
                     input.unavailable ? input.unavailable(keycode) : std::nullopt };
        }

        // What the mouse does with a key of the picker -- a tile or a keycap, the item just drawn
        // over it: hover gives QMK's name, and the board previews what a click would write, the key
        // line's hold or modifiers kept; a click picks it, unless refused. True while it is hovered.
        bool HandleKey(const Keycode& keycode, bool clicked, const Availability& availability, KeycodePickerState& state,
                       const KeycodePickerInput& input, KeycodePickerEvents& events)
        {
            const std::string key      = FormatKeycode(keycode);
            const Keycode     composed = input.current ? ComposeWithKey(keycode, *input.current, input.version) : keycode;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            {
                std::string more = availability.note.text.empty() ? std::string() : "\n" + availability.note.text;
                if (availability.refused)
                    more += "\n" + *availability.refused;
                if (composed == keycode)
                    ImGui::SetTooltip("%s%s", KeycodeHoverText(keycode, input.version).c_str(), more.c_str());
                else
                    ImGui::SetTooltip("%s\n%s, keeping the key's %s%s", FormatKeycode(composed).c_str(), key.c_str(),
                                      std::holds_alternative<ModifiedKey>(composed) ? "modifiers" : "hold", more.c_str());

                if (input.current && state.justPicked != key)
                    events.preview = composed;
            }

            const bool hovered = ImGui::IsItemHovered();
            if (hovered && state.justPicked && *state.justPicked != key)
                state.justPicked.reset();
            if (clicked && !availability.refused)
            {
                events.picked    = keycode;
                events.preview.reset();
                state.justPicked = key;
            }
            return hovered;
        }

        // The groups on one grid of cells: a title box takes two, a key one, and no empty cell
        // parts two groups on a row -- the box does. A group that does not fit the rest of a row
        // starts the next; one longer than a row wraps, its rows starting under its first key,
        // its box spanning them all (Rico, 2026-10-04). The keys are tiles, or keycaps at the Basic
        // keyboard's unit, a unit apart, each inside its cell with its gap all round as on a board
        // (Rico, 2026-10-26, for Extra).
        void DrawGroups(const std::vector<CatalogueGroup>& groups, CatalogueLook look, KeycodePickerState& state,
                        const KeycodePickerInput& input, KeycodePickerEvents& events)
        {
            const bool  keycaps = look == CatalogueLook::Keycaps;
            const float zoom    = ImGui::GetStyle().FontScaleDpi * c_TileZoom;
            const float unit    = keycaps ? DrawnUnit() : 0.0f;

            // A cell is a key and its gaps: a tile's to its right and under it, a keycap's all round.
            const float  itemW  = keycaps ? unit : c_TileWidth * zoom;
            const float  itemH  = keycaps ? unit : c_TileHeight * zoom;
            const float  gapX   = keycaps ? 0.0f : c_TileGap * zoom;
            const float  rowGap = keycaps ? 0.0f : c_RowGap * zoom;
            const float  inset  = keycaps ? c_KeyGap * unit : 0.0f;
            const float  cell   = itemW + gapX;
            const float  rowH   = itemH + rowGap;
            const int    cells  = std::max(c_MinCells, static_cast<int>((ImGui::GetContentRegionAvail().x + gapX) / cell));
            const ImVec2 origin = ImGui::GetCursorScreenPos();

            // Where each key and title box goes, first: keycaps are drawn together, as a board's.
            struct Placed
            {
                const Keycode* keycode;
                ImVec2         p0;
                bool           dropHeader;
            };
            struct Label
            {
                const CatalogueGroup* group;
                ImVec2                p0;
                ImVec2                p1;
            };
            std::vector<Placed> placed;
            std::vector<Label>  labels;

            int row = 0, column = 0;
            for (const CatalogueGroup& group : groups)
            {
                const int count = static_cast<int>(group.keycodes.size());

                int start = column;
                if (column > 0 && start + std::min(c_LabelCells + count, cells) > cells)
                {
                    ++row;
                    start = 0;
                }
                const int first = row;
                int       at    = start + c_LabelCells;

                const bool dropHeader = SharesHeader(group.keycodes, input.context);
                for (const Keycode& keycode : group.keycodes)
                {
                    if (at + 1 > cells)
                    {
                        ++row;
                        at = start + c_LabelCells;
                    }
                    placed.push_back({ &keycode,
                                       ImVec2(origin.x + static_cast<float>(at) * cell, origin.y + static_cast<float>(row) * rowH),
                                       dropHeader });
                    ++at;
                }

                labels.push_back({ &group,
                                   ImVec2(origin.x + static_cast<float>(start) * cell + inset,
                                          origin.y + static_cast<float>(first) * rowH + inset),
                                   ImVec2(origin.x + static_cast<float>(start + c_LabelCells) * cell - gapX - inset,
                                          origin.y + static_cast<float>(row + 1) * rowH - rowGap - inset) });
                column = at;
            }

            // The keycaps, as the board draws them, with the tiles' legends but every level of a key.
            if (keycaps)
            {
                BoardDescription board;
                for (const Placed& each : placed)
                {
                    const Keycode& keycode = *each.keycode;
                    KeycodeTile    tile    = TileFor(keycode, input.context, each.dropHeader, false, false);
                    BoardKey       key;
                    key.geometry.x  = (each.p0.x - origin.x) / unit;
                    key.geometry.y  = (each.p0.y - origin.y) / unit;
                    key.legends     = std::move(tile.legend);
                    key.fill        = tile.fill;
                    key.fallthrough = tile.mark;
                    if (input.current && IsSentBy(keycode, *input.current))
                        key.marks |= Mark::Selected;
                    if (AvailabilityOf(keycode, input).IsFaint())
                        key.marks |= Mark::Dimmed;
                    board.keys.push_back(std::move(key));
                }
                DrawKeycaps(board, origin, unit);
            }

            bool anyHovered = false;
            for (size_t index = 0; index < placed.size(); ++index)
            {
                const Keycode&     keycode = *placed[index].keycode;
                const ImVec2       p0(placed[index].p0.x + inset, placed[index].p0.y + inset);
                const ImVec2       size(itemW - 2 * inset, itemH - 2 * inset);
                const Availability availability = AvailabilityOf(keycode, input);

                ImGui::PushID(static_cast<int>(index));
                ImGui::SetCursorScreenPos(p0);
                const bool clicked = ImGui::InvisibleButton("##key", size);
                if (!keycaps)
                {
                    KeycodeTile tile = TileFor(keycode, input.context, placed[index].dropHeader,
                                               input.current && *input.current == keycode);
                    tile.hovered = ImGui::IsItemHovered() && !availability.refused;
                    tile.isFaint = availability.IsFaint();
                    DrawKeycodeTile(tile, { p0.x, p0.y, p0.x + size.x, p0.y + size.y });
                }
                anyHovered = HandleKey(keycode, clicked, availability, state, input, events) || anyHovered;
                ImGui::PopID();
            }

            for (const Label& label : labels)
                DrawGroupLabel(*label.group, label.p0, label.p1);

            if (!anyHovered)
                state.justPicked.reset();

            // Claim the space drawn into, so the child scrolls.
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(static_cast<float>(cells) * cell - gapX,
                                groups.empty() ? 0.0f : static_cast<float>(row + 1) * rowH - rowGap));
        }

        // The Basic tab: a full-size keyboard in the host layout's shape, its legends by each key's
        // side as the board's are. The key the selected key sends is outlined, its tap under a hold
        // too; each key picks as a tile does.
        void DrawKeyboard(const std::vector<CatalogueKey>& keys, KeycodePickerState& state, const KeycodePickerInput& input,
                          KeycodePickerEvents& events)
        {
            const float  unit   = DrawnUnit();
            const float  gap    = c_KeyGap * unit;
            const ImVec2 origin = ImGui::GetCursorScreenPos();

            BoardDescription board;
            for (const CatalogueKey& key : keys)
            {
                BoardKey described;
                described.geometry = key.geometry;
                board.keys.push_back(std::move(described));
            }

            const LegendContext& context = input.context;
            const float          line    = SideLine(board.keys);
            float                width   = 0.0f;
            float                height  = 0.0f;
            for (size_t index = 0; index < keys.size(); ++index)
            {
                const Keycode&            keycode  = keys[index].keycode;
                const via::DefinitionKey& geometry = keys[index].geometry;
                const LegendContext       sided{ context.layout,   context.names,          SideOf(geometry, line),
                                                 context.lighting, context.customKeycodes, context.tapDances };

                BoardKey& described = board.keys[index];
                described.legends   = LegendFor(keycode, sided);
                described.fill      = KeycapClassOf(keycode, geometry);
                if (input.current && IsSentBy(keycode, *input.current))
                    described.marks |= Mark::Selected;
                if (AvailabilityOf(keycode, input).IsFaint())
                    described.marks |= Mark::Dimmed;

                width  = std::max(width, geometry.x + geometry.width);
                height = std::max(height, geometry.y + geometry.height);
            }
            DrawKeycaps(board, origin, unit);

            // An item over each rectangle of a key's face: ISO's Enter has two.
            bool anyHovered = false;
            for (size_t index = 0; index < keys.size(); ++index)
            {
                const Keycode&            keycode      = keys[index].keycode;
                const via::DefinitionKey& geometry     = keys[index].geometry;
                const Availability        availability = AvailabilityOf(keycode, input);

                const auto face = [&](const char* id, float x, float y, float w, float h)
                {
                    ImGui::SetCursorScreenPos(ImVec2(origin.x + x * unit + gap, origin.y + y * unit + gap));
                    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w * unit - 2 * gap, h * unit - 2 * gap));
                    anyHovered         = HandleKey(keycode, clicked, availability, state, input, events) || anyHovered;
                };

                ImGui::PushID(static_cast<int>(index));
                face("##key", geometry.x, geometry.y, geometry.width, geometry.height);
                if (geometry.HasSecondRectangle())
                    face("##second", geometry.x + geometry.secondX, geometry.y + geometry.secondY, geometry.secondWidth,
                         geometry.secondHeight);
                ImGui::PopID();
            }

            if (!anyHovered)
                state.justPicked.reset();

            // Claim the space drawn into, so the child scrolls.
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(width * unit, height * unit));
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

    std::string KeycodeHoverText(const Keycode& keycode, qmk::KeycodeVersion version)
    {
        std::string text = FormatKeycode(keycode);
        if (const auto* named = std::get_if<NamedKey>(&keycode))
            if (const qmk::KeycodeEntry* row = qmk::FindKeycodeByName(named->name, version);
                row != nullptr && row->label[0] != '\0' && text != row->label)
                text += std::string(" -- ") + row->label;
        return text;
    }

    std::string KeycodeLabel(const Keycode& keycode, qmk::KeycodeVersion version)
    {
        if (const auto* named = std::get_if<NamedKey>(&keycode))
            if (const qmk::KeycodeEntry* row = qmk::FindKeycodeByName(named->name, version); row != nullptr && row->label[0] != '\0')
                return row->label;
        return FormatKeycode(keycode);
    }

    const std::vector<CatalogueTab>& CatalogueOf(KeycodePickerState& state, const Keyboard& keyboard,
                                                 const LegendSettings& legends, bool advancedTools)
    {
        const std::string settings = legends.hostLayout + "/" + std::string(IdOf(legends.modifierNames)) +
                                     (advancedTools ? "/advanced" : "");
        if (state.catalogue.empty() || settings != state.catalogueFor)
        {
            state.catalogue    = BuildKeycodeCatalogue(keyboard, legends, advancedTools);
            state.catalogueFor = settings;
        }
        return state.catalogue;
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

        // The keys, scrolling under the tabs -- sideways too, should the keyboard reach its floor.
        ImGui::BeginChild("##tiles", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
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
                DrawGroups(results, CatalogueLook::Tiles, state, input, events);
        }
        else
        {
            const auto tab = std::find_if(input.tabs.begin(), input.tabs.end(),
                                          [&](const CatalogueTab& each) { return each.id == state.tab; });
            const CatalogueTab& shown = tab != input.tabs.end() ? *tab : input.tabs.front();
            if (shown.look == CatalogueLook::Keyboard)
                DrawKeyboard(shown.keyboard, state, input, events);
            else
                DrawGroups(shown.groups, shown.look, state, input, events);
        }
        ImGui::EndChild();

        return events;
    }
}
