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
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeycapLegend.h"
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
        std::vector<const DefinitionKey*> ordered;
        for (const BoardKey& key : board.keys)
            if (!key.geometry.decal && m_Keyboard.keymap.Contains(m_Layer, key.geometry.row, key.geometry.column))
                ordered.push_back(&key.geometry);
        std::stable_sort(ordered.begin(), ordered.end(), [](const DefinitionKey* a, const DefinitionKey* b)
        {
            const auto [ax, ay] = KeyCentre(*a);
            const auto [bx, by] = KeyCentre(*b);
            if (std::abs(ay - by) > 1.0f / 3.0f)
                return ay < by;
            return ax < bx;
        });
        m_Order.clear();
        for (const DefinitionKey* key : ordered)
        {
            const Cell cell{ static_cast<uint8_t>(key->row), static_cast<uint8_t>(key->column) };
            if (std::none_of(m_Order.begin(), m_Order.end(),
                             [&](const Cell& seen) { return seen.row == cell.row && seen.column == cell.column; }))
                m_Order.push_back(cell);
        }

        const bool flashing = m_Flash && ImGui::GetTime() < m_FlashUntil;

        for (BoardKey& key : board.keys)
        {
            if (flashing && m_Flash->row == key.geometry.row && m_Flash->column == key.geometry.column)
                key.marks |= Mark::Checked;

            if (!m_Selected || m_Selected->row != key.geometry.row || m_Selected->column != key.geometry.column)
                continue;

            if (previewing)
            {
                const LegendContext context{ m_Legends.Layout(), m_Legends.modifierNames, SideOf(key.geometry, line),
                                             LightingSystemsOf(m_Keyboard), custom };
                key.legends     = LegendFor(*m_Preview, context);
                key.fallthrough = Fallthrough::None;
                key.marks |= Mark::Highlighted;
            }
            else
                key.marks |= Mark::Selected;
        }
    }

    const std::vector<CatalogueTab>& KeymapSection::Catalogue()
    {
        const std::string settings = m_Legends.hostLayout + "/" + std::string(IdOf(m_Legends.modifierNames)) +
                                     (m_AdvancedTools ? "/advanced" : "");
        if (m_Catalogue.empty() || settings != m_CatalogueFor)
        {
            m_Catalogue    = BuildKeycodeCatalogue(m_Keyboard, m_Legends, m_AdvancedTools);
            m_CatalogueFor = settings;
        }
        return m_Catalogue;
    }

    void KeymapSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!events.hoveredKey)
            return;

        const DefinitionKey& key = board.keys[*events.hoveredKey].geometry;

        // A definition can put a key outside its own matrix -- VIA's registry refuses that, an
        // imported file or a Vial board may not. The keymap has no cell for it: never selected.
        if (!m_Keyboard.keymap.Contains(m_Layer, key.row, key.column))
        {
            ImGui::SetTooltip("row %d, column %d: outside the matrix,\nso it cannot be remapped", key.row, key.column);
            return;
        }

        // QMK's name and label, so a short form on the key may be terse (short-forms.md, rule 10).
        const auto nameOf = [&](const Keycode& keycode)
        {
            std::string name = FormatKeycode(keycode);
            if (const auto* named = std::get_if<NamedKey>(&keycode))
                if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, m_Keyboard.keycodeVersion);
                    row != nullptr && row->label[0] != '\0' && name != row->label)
                    name += std::string(" -- ") + row->label;
            return name;
        };

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

        ImGui::SetTooltip("%s\nrow %d, column %d", text.c_str(), key.row, key.column);

        if (events.clickedKey)
            m_Selected = Cell{ key.row, key.column };
    }

    void KeymapSection::DrawPanel()
    {
        std::optional<Keycode> current;
        if (m_Selected)
            current = m_Keyboard.keymap.At(m_Layer, m_Selected->row, m_Selected->column);

        if (current)
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
            DrawKeycodePicker(m_Picker, { Catalogue(), context, m_Keyboard.keycodeVersion, current, &m_Keyboard });
        ImGui::EndDisabled();
        m_Preview = events.preview;

        // A pick keeps the key's hold or modifiers.
        if (events.picked && current)
            Write(ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion), true);
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
        std::optional<std::string_view> TapOf(const Keycode& keycode, QmkKeycodeVersion version)
        {
            if (const auto* modTap = std::get_if<ModTapKey>(&keycode))
                return modTap->key;
            if (const auto* layerTap = std::get_if<LayerTapKey>(&keycode))
                return layerTap->key;
            if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
                return modified->key;
            if (const auto* named = std::get_if<NamedKey>(&keycode))
                if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, version);
                    row != nullptr && row->value >= 0x04 && row->value <= 0xFF)
                    return named->name;
            return std::nullopt;
        }

        // The four modifiers by the host's names, each as a bit on either side.
        struct Modifier
        {
            uint8_t     left;
            uint8_t     right;
            const char* name;
        };

        std::array<Modifier, 4> ModifiersFor(ModifierNames names)
        {
            const bool mac = names == ModifierNames::Mac;
            return { { { Mod::LeftCtrl, Mod::RightCtrl, "Ctrl" },
                       { Mod::LeftShift, Mod::RightShift, "Shift" },
                       { Mod::LeftAlt, Mod::RightAlt, mac ? "Option" : "Alt" },
                       { Mod::LeftGui, Mod::RightGui, mac ? "Cmd" : names == ModifierNames::Linux ? "Super" : "Win" } } };
        }

        bool IsRight(uint8_t mods) { return (mods & 0xF0) != 0; }

        // A toggle in a popup or on the line: on, it looks pressed.
        bool Toggle(const char* label, bool on, bool enabled = true)
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

        // Its popup opens under the button just drawn.
        void OpenUnder(const char* popup)
        {
            ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
            ImGui::OpenPopup(popup);
        }

        // The modifier toggles and the side, editing `mods` -- one at least stays on unless `none`
        // is offered. Returns the new set when something was clicked.
        std::optional<uint8_t> ModifierChoices(uint8_t mods, ModifierNames names, bool none)
        {
            std::optional<uint8_t> changed;
            const bool             right = IsRight(mods);

            if (none)
            {
                if (Toggle("None", mods == 0) && mods != 0)
                    changed = uint8_t{ 0 };
            }

            const std::array<Modifier, 4> modifiers = ModifiersFor(names);
            for (size_t index = 0; index < modifiers.size(); ++index)
            {
                const Modifier& modifier = modifiers[index];
                if (none || index > 0)
                    ImGui::SameLine();
                const uint8_t bit = right ? modifier.right : modifier.left;
                if (Toggle(modifier.name, (mods & bit) != 0))
                {
                    const uint8_t next = static_cast<uint8_t>(mods ^ bit);
                    if (next != 0 || none)
                        changed = next;
                }
            }

            // QMK holds one side only: the side moves every modifier set.
            if (Toggle("Left", mods != 0 && !right, mods != 0) && right)
                changed = static_cast<uint8_t>(mods >> 4);
            ImGui::SameLine();
            if (Toggle("Right", mods != 0 && right, mods != 0) && !right)
                changed = static_cast<uint8_t>(mods << 4);
            return changed;
        }
    }

    void KeymapSection::DrawKeyLine(const Keycode& current)
    {
        const QmkKeycodeVersion version = m_Keyboard.keycodeVersion;

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

        if (entered)
        {
            const std::optional<Keycode> typed = ParseQmkExpression(m_Expression, version);
            if (!typed)
                m_ExpressionError = "not a keycode Nazg knows";
            else if (!EncodeQmkKeycode(*typed, version))
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
        const QmkKeycodeVersion               version = m_Keyboard.keycodeVersion;
        const std::optional<std::string_view> tap     = TapOf(current, version);
        if (!tap)
            return;

        const auto* modTap   = std::get_if<ModTapKey>(&current);
        const auto* layerTap = std::get_if<LayerTapKey>(&current);
        const auto* modified = std::get_if<ModifiedKey>(&current);
        const ModifierNames names = m_Legends.modifierNames;

        // The modifiers' words as the board prints them: "Ctrl", "Ctrl Sft", "Hyper".
        const auto wordsOf = [&](uint8_t mods)
        { return LegendFor(OneShotModKey{ mods }, { m_Legends.Layout(), names }).cylindrical.full; };

        ImGui::BeginDisabled(IsBusy());

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("When held");
        ImGui::SameLine();
        if (Toggle("Nothing", modTap == nullptr && layerTap == nullptr) && (modTap != nullptr || layerTap != nullptr))
            Write(NamedKey{ *tap });

        ImGui::SameLine();
        const std::string holdMods = modTap != nullptr ? wordsOf(modTap->mods) + " \xE2\x96\xBE###holdmods" : "Modifiers###holdmods";
        if (Toggle(holdMods.c_str(), modTap != nullptr))
        {
            if (modTap == nullptr)
                Write(ModTapKey{ Mod::LeftCtrl, *tap });
            OpenUnder("##holdmods");
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
        if (Toggle(holdLayer.c_str(), layerTap != nullptr))
        {
            if (layerTap == nullptr)
                Write(LayerTapKey{ static_cast<uint8_t>(m_Keyboard.keymap.Layers() > 1 ? 1 : 0), *tap });
            OpenUnder("##holdlayer");
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
                if (Toggle(label.c_str(), layerTap != nullptr && layerTap->layer == layer) && layerTap != nullptr)
                    Write(LayerTapKey{ static_cast<uint8_t>(layer), layerTap->key });
            }
            ImGui::EndPopup();
        }

        // A tap-hold's tap is a plain key, so "Sent with" goes while a hold is set.
        if (modTap == nullptr && layerTap == nullptr)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Sent with");
            ImGui::SameLine();
            const std::string sent = (modified != nullptr ? wordsOf(modified->mods) : std::string("nothing")) +
                                     " \xE2\x96\xBE###sentwith";
            if (Toggle(sent.c_str(), modified != nullptr))
                OpenUnder("##sentwith");
            if (ImGui::BeginPopup("##sentwith"))
            {
                // None first: back to the plain key in one click (Rico, 2026-10-04).
                if (const std::optional<uint8_t> mods = ModifierChoices(modified != nullptr ? modified->mods : 0, names, true))
                {
                    if (*mods == 0)
                        Write(NamedKey{ *tap });
                    else
                        Write(ModifiedKey{ *mods, *tap });
                }
                ImGui::EndPopup();
            }
        }

        ImGui::EndDisabled();
    }

    bool KeymapSection::IsBusy() const
    {
        return m_Write.IsValid() && !m_Write.IsDone();
    }

    // Keep the model in step with what the board says it stored.
    Task<void> KeymapSection::WriteKey(uint8_t layer, Cell cell, Keycode keycode, QmkKeycodeVersion version)
    {
        m_IsWriting = true;
        m_Message.clear();
        m_IsWarning = false;

        DeviceId device = c_InvalidDevice;

        try
        {
            device = co_await m_Transport.Open(m_Path);

            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            const Keycode stored = co_await nazg::WriteKeycode(via, layer, cell.row, cell.column, keycode, version);
            m_Keyboard.keymap.Set(layer, cell.row, cell.column, stored);

            // Compared as the values on the wire: two Keycodes can differ as values yet be
            // the same key (a macro by name or by index), and the wire is what counts.
            if (EncodeQmkKeycode(stored, version) == EncodeQmkKeycode(keycode, version))
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

        // Outside the catch: closing an id that never opened does nothing.
        m_Transport.Close(device);
        m_IsWriting = false;
    }
}
