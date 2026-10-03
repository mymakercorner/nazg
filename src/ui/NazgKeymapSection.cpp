// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeymapSection.h"

#include <exception>
#include <string>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    KeymapSection::KeymapSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                 const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends)
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

    void KeymapSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, m_Layer, m_Legends);

        for (BoardKey& key : board.keys)
        {
            if (m_Selected && m_Selected->row == key.geometry.row && m_Selected->column == key.geometry.column)
                key.marks |= Mark::Selected;
        }
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
        const Keycode keycode = m_Keyboard.KeycodeFor(key, m_Layer);
        std::string   name    = FormatKeycode(keycode);
        if (const auto* named = std::get_if<NamedKey>(&keycode))
            if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, m_Keyboard.keycodeVersion);
                row != nullptr && row->label[0] != '\0' && name != row->label)
                name += std::string(" -- ") + row->label;

        ImGui::SetTooltip("%s\nrow %d, column %d", name.c_str(), key.row, key.column);

        if (events.clickedKey)
            m_Selected = Cell{ key.row, key.column };
    }

    void KeymapSection::DrawPanel()
    {
        if (!m_Selected)
        {
            ImGui::TextUnformatted("Click a key to change it.");
        }
        else
        {
            ImGui::Text("Layer %d, row %d, column %d: %s", m_Layer, m_Selected->row, m_Selected->column,
                        FormatKeycode(m_Keyboard.keymap.At(m_Layer, m_Selected->row, m_Selected->column)).c_str());

            if (m_IsWriting)
                ImGui::TextUnformatted("writing...");
            else if (!m_Message.empty())
                ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        }

        // Always shown, as the design has it; a keycode clicked with no key selected does nothing.
        ImGui::BeginDisabled(IsBusy());
        const std::optional<Keycode> picked =
            DrawKeycodePicker(m_Picker, m_Keyboard.keycodeVersion, m_Keyboard.keymap.Layers(), m_Legends);
        ImGui::EndDisabled();

        // Never replace a Task still running: destroying it would free a coroutine frame the
        // transport still holds a handle to.
        if (picked && m_Selected && !IsBusy())
            m_Write = WriteKey(m_Layer, *m_Selected, *picked, m_Keyboard.keycodeVersion);
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
            }
            else
            {
                m_Message = "asked for " + FormatKeycode(keycode) + " but the board stored " + FormatKeycode(stored) +
                            " -- a locked Vial board filters some keycodes";
                m_IsWarning = true;
            }
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
