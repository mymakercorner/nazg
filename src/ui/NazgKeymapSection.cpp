// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeymapSection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/qmk/NazgQmkExpression.h"
#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaProtocol.h"
#include "adapters/vial/NazgEncoderMap.h"
#include "adapters/vial/NazgVialProtocol.h"
#include "model/NazgHostKeys.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgKeyLine.h"
#include "ui/NazgKeycodeCatalogue.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    KeymapSection::KeymapSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                 const LegendSettings& legends, const bool& moveToNextKey, const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends),
          m_MoveToNextKey(moveToNextKey), m_AdvancedTools(advancedTools)
    {
    }

    Strip KeymapSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Layer";
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            strip.entries.push_back(std::to_string(layer));
        strip.chosen = m_Layer;
        return strip;
    }

    void KeymapSection::OnStripChosen(size_t entry)
    {
        m_Layer = static_cast<uint8_t>(entry);
    }

    // The board shows the hovered layer as if chosen (Rico, 2026-10-04: simpler than second
    // legends, and nothing to collide); a click still edits the chosen one -- ui-design.md, "The
    // other direction: peek".
    void KeymapSection::OnStripHovered(std::optional<size_t> entry)
    {
        m_Peek.reset();
        if (entry)
            m_Peek = static_cast<uint8_t>(*entry);
    }

    void KeymapSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, m_Peek.value_or(m_Layer), m_Legends);

        // The hover preview: the selected key shows what a click on the tile under the mouse
        // would write, outlined in the highlight's colour rather than the selection's.
        const bool               previewing = m_Preview && !m_Peek;
        const float              line       = previewing ? SideLine(board.keys) : 0.0f;
        const std::vector<Words> custom     = previewing ? CustomKeycodeWordsOf(m_Keyboard) : std::vector<Words>{};

        // The board's order, for "next key": by rows of keys -- centres within a third of a unit
        // of each other's height are one row -- then left to right; a cell once, at its first key.
        std::vector<const via::DefinitionKey*> ordered;
        for (const BoardKey& key : board.keys)
            if (!key.geometry.decal && m_Keyboard.keymap.Contains(m_Layer, key.geometry.row, key.geometry.column))
                ordered.push_back(&key.geometry);
        std::stable_sort(ordered.begin(), ordered.end(), [](const via::DefinitionKey* a, const via::DefinitionKey* b)
        {
            const auto [ax, ay] = KeyCentre(*a);
            const auto [bx, by] = KeyCentre(*b);
            if (std::abs(ay - by) > 1.0f / 3.0f)
                return ay < by;
            return ax < bx;
        });
        m_Order.clear();
        for (const via::DefinitionKey* key : ordered)
        {
            const Cell cell{ static_cast<uint8_t>(key->row), static_cast<uint8_t>(key->column) };
            if (std::none_of(m_Order.begin(), m_Order.end(),
                             [&](const Cell& seen) { return seen.row == cell.row && seen.column == cell.column; }))
                m_Order.push_back(cell);
        }

        const bool flashing = ImGui::GetTime() < m_FlashUntil;

        for (BoardKey& key : board.keys)
        {
            const via::DefinitionKey& geometry = key.geometry;

            // A knob: flashed after a write, outlined while selected -- a Vial knob's direction key
            // when it is the action chosen.
            if (geometry.encoderIndex >= 0)
            {
                if (flashing && m_FlashKnob == geometry.encoderIndex)
                    key.marks |= Mark::Checked;
                if (m_Knob && m_Knob->index == geometry.encoderIndex &&
                    (geometry.encoderDirection < 0 ||
                     (geometry.encoderDirection == 1) == (m_Knob->action == KnobAction::Clockwise)))
                    key.marks |= Mark::Selected;
                if (!geometry.IsSwitch())
                    continue;
            }

            if (flashing && m_Flash && m_Flash->row == geometry.row && m_Flash->column == geometry.column)
                key.marks |= Mark::Checked;

            if (!m_Selected || m_Selected->row != key.geometry.row || m_Selected->column != key.geometry.column)
                continue;

            if (previewing)
            {
                const LegendContext context{ m_Legends.Layout(), m_Legends.modifierNames, SideOf(key.geometry, line),
                                             LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
                key.legends     = LegendFor(*m_Preview, context);
                key.fallthrough = Fallthrough::None;
                key.marks |= Mark::Highlighted;
            }
            else
                key.marks |= Mark::Selected;
        }
    }

    void KeymapSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!events.hoveredKey)
            return;

        const via::DefinitionKey& key = board.keys[*events.hoveredKey].geometry;

        // A knob: what it does each way, and a click selects it -- the direction clicked on Vial,
        // the press on a VIA knob that has one.
        if (key.encoderIndex >= 0)
        {
            const auto turnText = [&](bool clockwise)
            {
                const std::optional<Keycode> turn = m_Keyboard.TurnFor(key.encoderIndex, clockwise, m_Layer);
                return turn ? KeycodeHoverText(*turn, m_Keyboard.keycodeVersion) : std::string("kept by the firmware");
            };
            std::string text = "Knob " + std::to_string(key.encoderIndex) + "\nTurned left: " + turnText(false) +
                               "\nTurned right: " + turnText(true);
            if (key.encoderDirection < 0 && key.IsSwitch())
                text += "\nPressed: " + KeycodeHoverText(m_Keyboard.KeycodeFor(key, m_Layer), m_Keyboard.keycodeVersion);
            ImGui::SetTooltip("%s", text.c_str());

            if (events.clickedKey)
            {
                KnobSelection selection{ key.encoderIndex, KnobAction::Clockwise };
                if (key.encoderDirection >= 0)
                    selection.action = key.encoderDirection == 1 ? KnobAction::Clockwise : KnobAction::CounterClockwise;
                else if (m_Knob && m_Knob->index == key.encoderIndex)
                    selection.action = m_Knob->action;
                else if (key.IsSwitch())
                    selection.action = KnobAction::Press;
                m_Knob = selection;
                m_Selected.reset();
            }
            return;
        }

        // A definition can put a key outside its own matrix -- VIA's registry refuses that, an
        // imported file or a Vial board may not. The keymap has no cell for it: never selected.
        if (!m_Keyboard.keymap.Contains(m_Layer, key.row, key.column))
        {
            ImGui::SetTooltip("row %d, column %d: outside the matrix,\nso it cannot be remapped", key.row, key.column);
            return;
        }

        // QMK's name and label, so a short form on the key may be terse (short-forms.md, rule 10).
        const auto nameOf = [&](const Keycode& keycode) { return KeycodeHoverText(keycode, m_Keyboard.keycodeVersion); };

        // A transparent or KC_NO key wears the keycode below it: hover says which, and from where.
        const Keycode     own      = m_Keyboard.KeycodeFor(key, m_Layer);
        const ResolvedKey resolved = ResolveKey(m_Keyboard, key, m_Layer);
        std::string       text     = FormatKeycode(own);
        if (resolved.fallthrough == Fallthrough::None)
            text = nameOf(own);
        else if (resolved.fallthrough == Fallthrough::Transparent)
            text += " -- falls through to layer " + std::to_string(*resolved.layer) + ": " + nameOf(resolved.keycode);
        else if (std::holds_alternative<NamedKey>(own) && std::get<NamedKey>(own).name == "KC_TRNS")
            text += " -- falls through, and does nothing";
        else
            text += " -- does nothing on this layer";

        if (resolved.fallthrough == Fallthrough::Disabled)
            text += resolved.layer ? "\ndisables layer " + std::to_string(*resolved.layer) + "'s " + nameOf(resolved.keycode)
                                   : std::string("\nnothing below to disable");
        else if (const LightingNote note = LightingNoteOf(resolved.keycode, m_Keyboard); !note.text.empty())
            text += "\n" + note.text;   // a lighting key that may do nothing on this firmware, or drives two systems

        // A tap dance: all four actions -- the key shows the tap and the hold only.
        if (const auto* td = std::get_if<TapDanceKey>(&resolved.keycode); td && td->index < m_Keyboard.tapDances.size())
        {
            static constexpr const char* c_Actions[] = { "Tap", "Hold", "Double tap", "Tap, then hold" };
            const TapDance& dance = m_Keyboard.tapDances[td->index];
            for (DanceAction action : c_DanceActions)
                text += std::string("\n") + c_Actions[static_cast<size_t>(action)] + ": " +
                        (dance[action] ? KeycodeLabel(*dance[action], m_Keyboard.keycodeVersion) : std::string("-"));
            text += "\nTapping term: " + std::to_string(dance.tappingTerm) + " ms";
        }

        ImGui::SetTooltip("%s\nrow %d, column %d", text.c_str(), key.row, key.column);

        if (events.clickedKey)
        {
            m_Selected = Cell{ key.row, key.column };
            m_Knob.reset();
        }
    }

    void KeymapSection::DrawPanel()
    {
        std::optional<Keycode> current;
        if (m_Selected)
            current = m_Keyboard.keymap.At(m_Layer, m_Selected->row, m_Selected->column);
        else if (m_Knob)
            current = KnobKeycode(*m_Knob);

        // Press a key belongs to the key line: gone with it.
        if (!m_Selected)
            m_Listening = false;

        if (m_Knob)
            DrawKnobPanel();
        else if (current)
            DrawKeyLine(*current);
        else
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Click a key to change it.");
        }
        ImGui::Separator();

        // Always shown, as the design has it; a keycode clicked with no key selected does nothing.

        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom };

        ImGui::BeginDisabled(IsBusy());
        const KeycodePickerEvents events =
            DrawKeycodePicker(m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion, current, &m_Keyboard });
        ImGui::EndDisabled();
        m_Preview = events.preview;

        // A pick keeps the key's hold or modifiers; a knob's turn takes the keycode as it is.
        if (events.picked && m_Knob)
            WriteKnob(m_Knob->action == KnobAction::Press && current
                          ? ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion)
                          : *events.picked);
        else if (events.picked && current)
            Write(ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion), true);
    }

    std::optional<KeymapSection::Cell> KeymapSection::PushCellOf(int knob) const
    {
        for (const via::DefinitionKey& key : m_Keyboard.definition.keys)
            if (key.encoderIndex == knob && key.encoderPush && m_Keyboard.keymap.Contains(m_Layer, key.row, key.column))
                return Cell{ key.row, key.column };
        return std::nullopt;
    }

    std::optional<Keycode> KeymapSection::KnobKeycode(const KnobSelection& knob) const
    {
        if (knob.action == KnobAction::Press)
        {
            if (const std::optional<Cell> cell = PushCellOf(knob.index))
                return m_Keyboard.keymap.At(m_Layer, cell->row, cell->column);
            return std::nullopt;
        }
        return m_Keyboard.TurnFor(knob.index, knob.action == KnobAction::Clockwise, m_Layer);
    }

    void KeymapSection::WriteKnob(const Keycode& keycode)
    {
        if (!m_Knob || IsBusy())
            return;

        if (m_Knob->action == KnobAction::Press)
        {
            if (const std::optional<Cell> cell = PushCellOf(m_Knob->index))
                m_Write = WriteKey(m_Layer, *cell, keycode, m_Keyboard.keycodeVersion);
            return;
        }

        if (m_Keyboard.encoderMap.empty())
            return;   // the firmware keeps its turns in its code: the panel says so

        m_Write = WriteTurn(m_Layer, static_cast<uint8_t>(m_Knob->index), m_Knob->action == KnobAction::Clockwise,
                            keycode, m_Keyboard.keycodeVersion, m_Keyboard.report.isVial);
    }

    namespace
    {
        // Pixels, before DPI scaling: a knob's action card, as Tap Dance's.
        constexpr float c_KnobCardWidth = 132.0f;
        constexpr float c_KnobCardGap   = 12.0f;
        constexpr float c_KnobDrawing   = 26.0f;

        // Text centred in [left, left + width), cut to it.
        void CentredLine(ImDrawList* list, float left, float width, float y, ImU32 colour, const std::string& text)
        {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            list->PushClipRect(ImVec2(left, y), ImVec2(left + width, y + ImGui::GetFontSize() * 2), true);
            list->AddText(ImVec2(left + std::max(0.0f, (width - w) / 2), y), colour, text.c_str());
            list->PopClipRect();
        }
    }

    void KeymapSection::DrawKnobPanel()
    {
        const float scale   = ImGui::GetStyle().FontScaleDpi;
        const bool  noMap   = m_Keyboard.encoderMap.empty();
        const bool  isVial  = m_Keyboard.report.isVial;
        const bool  canPush = !isVial && PushCellOf(m_Knob->index).has_value();

        // The line over the cards: which knob, what the last write did, and a map the firmware lacks.
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::Text("Knob %d", m_Knob->index);
        ImGui::PopFont();
        ImGui::SameLine();
        if (noMap)
            ColouredText(PanelColour::Warning, "%s",
                         !isVial && m_Keyboard.report.viaProtocol < 10
                             ? "VIA before protocol 10 has no encoder map: a turn does what the firmware's code says."
                             : "This firmware keeps no encoder map (ENCODER_MAP_ENABLE): a turn does what its code says.");
        else if (m_IsWriting)
            ImGui::TextDisabled("writing...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        else
            ImGui::TextDisabled("Layer %d. Each action is written at once.", m_Layer);
        if (isVial)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Pressing it, if it can be, is a key of its own.");
        }

        struct Card
        {
            KnobAction  action;
            const char* name;
        };
        std::vector<Card> cards{ { KnobAction::CounterClockwise, "Turn left" }, { KnobAction::Clockwise, "Turn right" } };
        if (canPush)
            cards.push_back({ KnobAction::Press, "Press" });
        if (!canPush && m_Knob->action == KnobAction::Press)
            m_Knob->action = KnobAction::Clockwise;

        const ImVec2 tile  = TileSize();
        const float  cardW = std::max(c_KnobCardWidth * scale, tile.x + 16 * scale);
        const float  gap   = c_KnobCardGap * scale;
        const float  pad   = 6 * scale;
        const float  line  = ImGui::GetTextLineHeight();
        const float  cardH = pad + line + 4 * scale + c_KnobDrawing * scale + 4 * scale + tile.y + 4 * scale + line + pad;
        ImDrawList*  list  = ImGui::GetWindowDrawList();
        const ImU32  muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32  text  = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32  ink   = ImGui::GetColorU32(ImGuiCol_Text);
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom };

        ImGui::Dummy(ImVec2(0, gap / 2));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        for (size_t index = 0; index < cards.size(); ++index)
        {
            const Card&  card     = cards[index];
            const ImVec2 p0(origin.x + index * (cardW + gap), origin.y);
            const ImVec2 p1(p0.x + cardW, p0.y + cardH);
            const bool   selected = m_Knob->action == card.action;
            const bool   isTurn   = card.action != KnobAction::Press;

            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos(p0);
            if (ImGui::InvisibleButton("##knobaction", ImVec2(cardW, cardH)))
                m_Knob->action = card.action;
            const bool hovered = ImGui::IsItemHovered();
            if (selected || hovered)
                list->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 7 * scale);
            if (selected)
                list->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_ButtonActive), 7 * scale, 0, std::max(1.0f, 1.5f * scale));

            float y = p0.y + pad;
            PushHeaderFont();
            CentredLine(list, p0.x, cardW, y, text, card.name);
            ImGui::PopFont();
            y += line + 4 * scale;

            // The gesture: the knob as a circle, turned by an arrow round it or pushed by one into it.
            const ImVec2 knob(p0.x + cardW / 2, y + c_KnobDrawing * scale / 2);
            const float  r = c_KnobDrawing * scale * 0.3f;
            list->AddCircle(knob, r, ImGui::GetColorU32(ImGuiCol_Border), 0, 2 * scale);
            if (isTurn)
                DrawTurnArrow(list, knob, r + 3.5f * scale, card.action == KnobAction::Clockwise, ink, 1.6f * scale);
            else
            {
                const float top = knob.y - r - 7 * scale;
                list->AddLine(ImVec2(knob.x, top), ImVec2(knob.x, knob.y - r - 1 * scale), ink, 1.6f * scale);
                list->AddTriangleFilled(ImVec2(knob.x - 4 * scale, knob.y - r - 4 * scale),
                                        ImVec2(knob.x + 4 * scale, knob.y - r - 4 * scale),
                                        ImVec2(knob.x, knob.y - r + 0.5f * scale), ink);
            }
            y += c_KnobDrawing * scale + 4 * scale;

            const FaceBox box{ p0.x + (cardW - tile.x) / 2, y, p0.x + (cardW + tile.x) / 2, y + tile.y };
            std::string   words;
            if (const std::optional<Keycode> keycode = KnobKeycode({ m_Knob->index, card.action }))
            {
                KeycodeTile keyTile = TileOf(*keycode, legends, false);
                keyTile.hovered     = hovered;
                DrawKeycodeTile(keyTile, box);
                if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s", KeycodeHoverText(*keycode, m_Keyboard.keycodeVersion).c_str());
            }
            else
            {
                list->AddRect(ImVec2(box.x0, box.y0), ImVec2(box.x1, box.y1), ImGui::GetColorU32(ImGuiCol_Border), 5 * scale,
                              0, std::max(1.0f, scale));
                CentredLine(list, box.x0, box.x1 - box.x0, (box.y0 + box.y1 - line) / 2, muted, "?");
                words = "kept by the firmware";
            }
            if (!isTurn)
                words = "its own switch";
            y += tile.y + 4 * scale;
            if (!words.empty())
                CentredLine(list, p0.x + 2 * scale, cardW - 4 * scale, y, muted, words);
            ImGui::PopID();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + cardH));
        ImGui::Dummy(ImVec2(cards.size() * (cardW + gap), gap / 2));
    }

    void KeymapSection::Write(const Keycode& keycode, bool advance)
    {
        // Never replace a Task still running: destroying it would free a coroutine frame the
        // transport still holds a handle to.
        if (!m_Selected || IsBusy())
            return;

        m_Write = WriteKey(m_Layer, *m_Selected, keycode, m_Keyboard.keycodeVersion);

        // The next key in the board's order, at once; past the last one the selection clears,
        // as VIA's does, rather than wrapping round to the first, as Vial's.
        if (advance && m_MoveToNextKey)
        {
            const auto here = std::find_if(m_Order.begin(), m_Order.end(), [&](const Cell& cell)
                                           { return cell.row == m_Selected->row && cell.column == m_Selected->column; });
            if (here != m_Order.end() && here + 1 != m_Order.end())
                m_Selected = *(here + 1);
            else
                m_Selected.reset();
        }
    }

    namespace
    {
        // A tap-hold's or a modified key's tap, or a plain key itself: what the composer shapes.
        // Only a basic keycode -- QMK's bottom byte, from KC_A -- can be one.
        std::optional<std::string_view> TapOf(const Keycode& keycode, qmk::KeycodeVersion version)
        {
            if (const auto* modTap = std::get_if<ModTapKey>(&keycode))
                return modTap->key;
            if (const auto* layerTap = std::get_if<LayerTapKey>(&keycode))
                return layerTap->key;
            if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
                return modified->key;
            if (const auto* named = std::get_if<NamedKey>(&keycode))
                if (const qmk::KeycodeEntry* row = qmk::FindKeycodeByName(named->name, version);
                    row != nullptr && row->value >= 0x04 && row->value <= 0xFF)
                    return named->name;
            return std::nullopt;
        }
    }

    void KeymapSection::DrawKeyLine(const Keycode& current)
    {
        const qmk::KeycodeVersion version = m_Keyboard.keycodeVersion;

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Row %d, column %d", m_Selected->row, m_Selected->column);
        ImGui::SameLine();

        // The Any entry: refilled from the key whenever it changes and nobody is typing.
        const std::string formatted = FormatKeycode(current) + "@" + std::to_string(m_Layer) + "," +
                                      std::to_string(m_Selected->row) + "," + std::to_string(m_Selected->column);
        if (formatted != m_ExpressionFor && !m_ExpressionEditing)
        {
            const std::string text = FormatKeycode(current);
            std::snprintf(m_Expression, sizeof(m_Expression), "%s", text.c_str());
            m_ExpressionFor = formatted;
            m_ExpressionError.clear();
        }

        const bool bad = !m_ExpressionError.empty();
        if (bad)
        {
            ImGui::PushStyleColor(ImGuiCol_Border, ColourOf(PanelColour::Error));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(1.0f, ImGui::GetStyle().FontScaleDpi));
        }
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("MT(MOD_LCTL|MOD_LSFT,KC_A)").x + 2 * ImGui::GetStyle().FramePadding.x);
        ImGui::BeginDisabled(IsBusy());
        const bool entered = ImGui::InputText("##expression", m_Expression, sizeof(m_Expression),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::EndDisabled();
        if (bad)
        {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        m_ExpressionEditing = ImGui::IsItemActive();
        if (ImGui::IsItemEdited())
            m_ExpressionError.clear();
        ImGui::SetItemTooltip("Type a keycode as QMK writes it -- LT(1,KC_SPC), MT(MOD_LCTL,KC_A),\n"
                              "C(S(KC_T)), OSM(MOD_LSFT), MO(2) -- and press Enter.");

        // Press a key: the next key pressed on the computer's keyboard is written (ui-design.md, "The
        // Keys tab as a keyboard"). Clicked again, it stops listening.
        ImGui::SameLine();
        ImGui::BeginDisabled(IsBusy());
        if (ToggleButton(m_Listening ? "Press a key now...###presskey" : "Press a key###presskey", m_Listening))
        {
            m_Listening = !m_Listening;
            m_Message.clear();
        }
        ImGui::EndDisabled();
        const std::array<const char*, 4> modifiers = ModifierWords(m_Legends.modifierNames);
        if (m_Listening)
            ImGui::SetItemTooltip("Click again to stop listening.");
        else
            ImGui::SetItemTooltip("Press a key on the computer's keyboard: it is written to this key.\n"
                                  "%s, %s, %s or %s held with it are sent with it.",
                                  modifiers[0], modifiers[1], modifiers[2], modifiers[3]);

        if (entered)
        {
            const std::optional<Keycode> typed = qmk::ParseExpression(m_Expression, version);
            if (!typed)
                m_ExpressionError = "not a keycode Nazg knows";
            else if (!qmk::EncodeKeycode(*typed, version))
                m_ExpressionError = "this board cannot store it";
            else
                Write(*typed, true);
        }

        DrawComposer(current);

        // What the last write did, or why Enter wrote nothing.
        ImGui::SameLine();
        if (bad)
            ColouredText(PanelColour::Error, "%s", m_ExpressionError.c_str());
        else if (m_IsWriting)
            ImGui::TextDisabled("writing...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
    }

    void KeymapSection::DrawComposer(const Keycode& current)
    {
        const qmk::KeycodeVersion             version = m_Keyboard.keycodeVersion;
        const std::optional<std::string_view> tap     = TapOf(current, version);
        if (!tap)
            return;

        const auto* modTap   = std::get_if<ModTapKey>(&current);
        const auto* layerTap = std::get_if<LayerTapKey>(&current);
        const ModifierNames names = m_Legends.modifierNames;

        // The modifiers' words as the board prints them: "Ctrl", "Ctrl Sft", "Hyper".
        const auto wordsOf = [&](uint8_t mods)
        { return LegendFor(OneShotModKey{ mods }, { m_Legends.Layout(), names }).cylindrical.full; };

        ImGui::BeginDisabled(IsBusy());

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("When held");
        ImGui::SameLine();
        if (ToggleButton("Nothing", modTap == nullptr && layerTap == nullptr) && (modTap != nullptr || layerTap != nullptr))
            Write(NamedKey{ *tap });

        ImGui::SameLine();
        const std::string holdMods = modTap != nullptr ? wordsOf(modTap->mods) + " \xE2\x96\xBE###holdmods" : "Modifiers###holdmods";
        if (ToggleButton(holdMods.c_str(), modTap != nullptr))
        {
            if (modTap == nullptr)
                Write(ModTapKey{ Mod::LeftCtrl, *tap });
            OpenPopupUnder("##holdmods");
        }
        if (ImGui::BeginPopup("##holdmods"))
        {
            if (modTap != nullptr)
                if (const std::optional<uint8_t> mods = ModifierChoices(modTap->mods, names, false))
                    Write(ModTapKey{ *mods, modTap->key });
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        const std::string holdLayer = layerTap != nullptr ? "Layer " + std::to_string(layerTap->layer) + " \xE2\x96\xBE###holdlayer"
                                                          : "Layer###holdlayer";
        if (ToggleButton(holdLayer.c_str(), layerTap != nullptr))
        {
            if (layerTap == nullptr)
                Write(LayerTapKey{ static_cast<uint8_t>(m_Keyboard.keymap.Layers() > 1 ? 1 : 0), *tap });
            OpenPopupUnder("##holdlayer");
        }
        if (ImGui::BeginPopup("##holdlayer"))
        {
            // LT reaches the first 16 layers.
            const int layers = std::min<int>(m_Keyboard.keymap.Layers(), 16);
            for (int layer = 0; layer < layers; ++layer)
            {
                if (layer > 0)
                    ImGui::SameLine();
                const std::string label = "L" + std::to_string(layer);
                if (ToggleButton(label.c_str(), layerTap != nullptr && layerTap->layer == layer) && layerTap != nullptr)
                    Write(LayerTapKey{ static_cast<uint8_t>(layer), layerTap->key });
            }
            ImGui::EndPopup();
        }

        // A tap-hold's tap is a plain key, so "Sent with" goes while a hold is set.
        if (modTap == nullptr && layerTap == nullptr)
        {
            ImGui::SameLine();
            Keycode sent = current;
            DrawSentWith(sent, m_Legends, version);
            if (sent != current)
                Write(sent);
        }

        ImGui::EndDisabled();
    }

    bool KeymapSection::IsBusy() const
    {
        return m_Write.IsValid() && !m_Write.IsDone();
    }

    void KeymapSection::WhileHidden()
    {
        m_Listening = false;
        m_WrittenKeyDown.reset();
    }

    // The modifiers are followed whether listening or not, so one held before Press a key was
    // clicked counts too.
    void KeymapSection::OnHostKey(uint32_t usage, bool down)
    {
        if (!down && m_WrittenKeyDown == usage)
            m_WrittenKeyDown.reset();

        const uint32_t code = usage & 0xFFFF;
        if ((usage >> 16) == c_KeyboardPage && code >= 0xE0 && code <= 0xE7)
        {
            // USB numbers the modifiers in QMK's bit order: left Ctrl, Shift, Alt, GUI, then right.
            const auto bit = static_cast<uint8_t>(1u << (code - 0xE0));
            if (down)
            {
                if (m_HeldMods == 0)
                    m_ModifierAlone = true;
                m_HeldMods = static_cast<uint8_t>(m_HeldMods | bit);
                return;
            }

            // Let go with no other key since the first modifier went down: the modifier itself.
            const bool alone = m_ModifierAlone && m_HeldMods == bit;
            m_HeldMods = static_cast<uint8_t>(m_HeldMods & ~bit);
            if (alone && m_Listening)
                WriteHostKey(usage, 0);
            return;
        }

        if (!down)
            return;
        m_ModifierAlone = false;
        m_WrittenKeyDown.reset();
        if (m_Listening)
            WriteHostKey(usage, m_HeldMods);
    }

    void KeymapSection::WriteHostKey(uint32_t usage, uint8_t held)
    {
        const qmk::KeycodeVersion           version = m_Keyboard.keycodeVersion;
        const std::vector<std::string_view> names   = KeycodesSending(usage, version);
        if (names.empty())
        {
            m_Message   = "Nazg has no keycode for that key";
            m_IsWarning = true;
            return;
        }

        // A keycode holds the modifiers of one side: the right ones only with no left one held.
        const auto left  = static_cast<uint8_t>(held & 0x0F);
        const auto right = static_cast<uint8_t>(held >> 4);
        const auto mods  = static_cast<uint8_t>(left != 0 ? (left | right) : right << 4);

        Keycode keycode = NamedKey{ names.front() };
        if (mods != 0)
            if (const Keycode modified = ModifiedKey{ mods, names.front() }; qmk::EncodeKeycode(modified, version))
                keycode = modified;

        // What the key holds already: typed on the board being changed, perhaps, whose keys send what
        // they hold now -- the key it had is then in Basic. Still listening.
        if (m_Selected && m_Keyboard.keymap.At(m_Layer, m_Selected->row, m_Selected->column) == keycode)
        {
            m_Message   = "already " + FormatKeycode(keycode) + " -- typing on this board? Its keys send what they hold now";
            m_IsWarning = true;
            return;
        }

        m_Listening      = false;
        m_WrittenKeyDown = usage;
        Write(keycode, true);
    }

    // Keep the model in step with what the board says it stored.
    Task<void> KeymapSection::WriteKey(uint8_t layer, Cell cell, Keycode keycode, qmk::KeycodeVersion version)
    {
        m_IsWriting = true;
        m_Message.clear();
        m_IsWarning = false;

        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            via::Protocol via(channel);

            const Keycode stored = co_await nazg::via::WriteKeycode(via, layer, cell.row, cell.column, keycode, version);
            m_Keyboard.keymap.Set(layer, cell.row, cell.column, stored);

            // Compared as the values on the wire: two Keycodes can differ as values yet be
            // the same key (a macro by name or by index), and the wire is what counts.
            if (qmk::EncodeKeycode(stored, version) == qmk::EncodeKeycode(keycode, version))
            {
                m_Message = "stored " + FormatKeycode(stored);

                // Once the selection has moved on, the message names the key written.
                if (!m_Selected || m_Selected->row != cell.row || m_Selected->column != cell.column || m_Layer != layer)
                    m_Message += " on row " + std::to_string(cell.row) + ", column " + std::to_string(cell.column);
            }
            else
            {
                m_Message = "asked for " + FormatKeycode(keycode) + " but the board stored " + FormatKeycode(stored) +
                            " -- a locked Vial board filters some keycodes";
                m_IsWarning = true;
            }

            // The key flashes where the write landed, whatever the board stored.
            m_Flash      = cell;
            m_FlashUntil = ImGui::GetTime() + 0.9;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }

        m_IsWriting = false;
    }

    // As WriteKey(), through the encoder map: the turn stored, read back, kept in the model.
    Task<void> KeymapSection::WriteTurn(uint8_t layer, uint8_t knob, bool clockwise, Keycode keycode,
                                        qmk::KeycodeVersion version, bool isVial)
    {
        m_IsWriting = true;
        m_Message.clear();
        m_IsWarning = false;

        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            vial::Protocol protocol(channel);

            const Keycode stored = co_await vial::WriteKnobTurn(protocol, isVial, layer, knob, clockwise, keycode, version);
            if (layer < m_Keyboard.encoderMap.size() && knob < m_Keyboard.encoderMap[layer].size())
                (clockwise ? m_Keyboard.encoderMap[layer][knob].clockwise : m_Keyboard.encoderMap[layer][knob].counterClockwise) =
                    stored;

            if (qmk::EncodeKeycode(stored, version) == qmk::EncodeKeycode(keycode, version))
                m_Message = "stored " + FormatKeycode(stored);
            else
            {
                m_Message = "asked for " + FormatKeycode(keycode) + " but the board stored " + FormatKeycode(stored) +
                            " -- a locked Vial board filters some keycodes";
                m_IsWarning = true;
            }

            m_FlashKnob  = knob;
            m_FlashUntil = ImGui::GetTime() + 0.9;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }

        m_IsWriting = false;
    }
}
