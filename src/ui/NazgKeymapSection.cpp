// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeymapSection.h"

#include <exception>
#include <utility>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/via/NazgViaKeymap.h"
#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    KeymapSection::KeymapSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                 const std::string& hostLayoutId)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_HostLayoutId(hostLayoutId)
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
        const HostLayout& layout = CurrentHostLayout();

        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;

            // The printed-keycap arrangement: the main legend, and above it the Shift
            // character or the hold action.
            const KeycapLegend legend        = LegendFor(m_Keyboard.KeycodeFor(key.geometry, m_Layer), layout);
            key[LegendSlot::MiddleLeft].text = legend.primary;
            key[LegendSlot::TopLeft].text    = legend.secondary;

            if (m_Selected && m_Selected->row == key.geometry.row && m_Selected->column == key.geometry.column)
                key.marks |= Mark::Selected;
        }
    }

    void KeymapSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!events.hoveredKey)
            return;

        const DefinitionKey& key = board.keys[*events.hoveredKey].geometry;
        ImGui::SetTooltip("%s\nrow %d, column %d", FormatKeycode(m_Keyboard.KeycodeFor(key, m_Layer)).c_str(), key.row,
                          key.column);

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
            DrawKeycodePicker(m_Picker, m_Keyboard.keycodeVersion, m_Keyboard.keymap.Layers(), CurrentHostLayout());
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

    const HostLayout& KeymapSection::CurrentHostLayout() const
    {
        // A saved id this build does not know falls back to US rather than failing.
        const HostLayout* found = FindHostLayout(m_HostLayoutId);
        return found != nullptr ? *found : UsHostLayout();
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
