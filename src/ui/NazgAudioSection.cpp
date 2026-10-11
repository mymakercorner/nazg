// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgAudioSection.h"

#include <exception>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "imgui.h"

#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // A named key's name, or empty.
        std::string_view NameOf(const Keycode& keycode)
        {
            const NamedKey* named = std::get_if<NamedKey>(&keycode);
            return named ? named->name : std::string_view();
        }

        // Whether the key changes `setting` from the keyboard -- either, without one. The clicks'
        // pitch, the voice and music mode are the keyboard's alone, out of the wire's reach: not lit.
        bool ChangesAudio(std::string_view name, std::optional<AudioSetting> setting)
        {
            const bool on     = name == "AU_TOGG" || name == "AU_ON" || name == "AU_OFF";
            const bool clicky = name == "CK_TOGG" || name == "CK_ON" || name == "CK_OFF";
            if (!setting)
                return on || clicky;
            return *setting == AudioSetting::On ? on : clicky;
        }

        // A line of the table: its name in the first column, its controls in the second. The top is
        // kept to tell, once drawn, whether the pointer is over it.
        float BeginLine(const char* name)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float top = ImGui::GetCursorScreenPos().y;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name);
            ImGui::TableSetColumnIndex(1);
            return top;
        }

        bool IsOverLine(float top)
        {
            if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                return false;
            const float y = ImGui::GetMousePos().y;
            return y >= top && y < ImGui::GetItemRectMax().y + ImGui::GetStyle().CellPadding.y;
        }

        void Muted(const char* words)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s", words);
        }
    }

    AudioSection::AudioSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends)
    {
    }

    bool AudioSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    void AudioSection::WhileHidden()
    {
        // What the audio keys changed meanwhile: read again when shown.
        if (m_IsLoaded)
            m_IsStale = true;
        m_Hovered.reset();
    }

    bool AudioSection::AnyKeyChanges() const
    {
        for (const DefinitionKey& key : m_Keyboard.definition.keys)
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
                if (ChangesAudio(NameOf(m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer))), std::nullopt))
                    return true;
        return false;
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    void AudioSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
        if (m_NoAudio)
            return;

        // The keys that change the audio -- or, under the pointer, one line of it -- on the first
        // layer that has them, that layer's legend and "L1" when it is not the base.
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                if (!ChangesAudio(NameOf(keycode), m_Hovered))
                    continue;
                key.marks |= Mark::Highlighted;
                if (layer != 0)
                {
                    KeycapLegend legend = LegendFor(keycode, context);
                    legend.header       = { { "Layer " + std::to_string(layer), "L" + std::to_string(layer) },
                                            CommandCategory::Behaviour };
                    key.legends         = legend;
                    key.fallthrough     = Fallthrough::None;
                }
                break;
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void AudioSection::DrawPanel()
    {
        m_HoveredNow.reset();

        if (!m_IsLoaded && !m_NoAudio)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("audio", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }

        if (m_IsStale && !IsBusy() && !m_Pending)
        {
            m_IsStale = false;
            m_Request = Load();
        }

        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted("Audio");
        ImGui::PopFont();
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "the keyboard's speaker");
        ImGui::SameLine();
        if (IsBusy() || m_Pending)
            ColouredText(PanelColour::Muted, m_IsLoaded ? "Writing..." : "Reading...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        ImGui::Separator();

        if (m_NoAudio)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Warning, "This firmware is built without audio");
            ImGui::SameLine(0.0f, 0.0f);
            ColouredText(PanelColour::Muted, " -- the definition offers it, the board answers it has none. Its maker's "
                                             "firmware with audio built in brings it.");
            ImGui::PopTextWrapPos();
            return;
        }

        DrawLines();
        DrawSays();

        m_Hovered = m_HoveredNow;

        // One write at a time.
        if (m_Pending && !IsBusy())
        {
            m_Request = Write(*m_Pending);
            m_Pending.reset();
        }
    }

    void AudioSection::DrawLines()
    {
        if (!ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch);

        const auto hover = [this](float top, AudioSetting setting)
        {
            if (IsOverLine(top))
                m_HoveredNow = setting;
        };

        float top = BeginLine("Audio");
        if (ImGui::Checkbox("##on", &m_Values.on))
            m_Pending = PendingWrite{ AudioSetting::On, m_Values.on };
        Muted("the board's speaker: its songs, the clicks and music mode");
        hover(top, AudioSetting::On);

        // Greyed while audio is off, as Lighting greys a system's lines (Rico, 2026-10-11).
        ImGui::BeginDisabled(!m_Values.on);
        top = BeginLine("Clicky");
        if (ImGui::Checkbox("##clicky", &m_Values.clicky))
            m_Pending = PendingWrite{ AudioSetting::Clicky, m_Values.clicky };
        Muted(m_Values.on ? "a click with each key press" : "heard with Audio on");
        hover(top, AudioSetting::Clicky);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button("Play a sound"))
            m_Pending = PendingWrite{ std::nullopt, false };
        Muted(m_Values.on ? "the song the board plays when VIA picks it -- its lights blink once" : "with Audio on");
        ImGui::EndDisabled();

        ImGui::EndTable();
    }

    void AudioSection::DrawSays()
    {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        if (AnyKeyChanges())
            ColouredText(PanelColour::Muted, "The keys lit change the audio from the keyboard too -- a line under the "
                                             "pointer, the keys that change it. Nazg reads it again each time you come "
                                             "back here.");
        else
            ColouredText(PanelColour::Muted, "No key on this keymap changes the audio -- Keymap's picker has them, in its "
                                             "Special tab.");
        ImGui::PopTextWrapPos();
    }

    // ------------------------------------------------------------------------------------------
    // Reading and writing.

    Task<void> AudioSection::Load()
    {
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);
            m_Values   = co_await ReadAudio(via);
            m_IsLoaded = true;
        }
        catch (const ProtocolError&)
        {
            m_NoAudio = true;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the audio could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    Task<void> AudioSection::Write(PendingWrite write)
    {
        m_Message.clear();
        m_IsWarning = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            if (!write.setting)
            {
                try
                {
                    co_await PlayIndication(via);
                    m_Message = "sent -- the keyboard plays its song";
                }
                catch (const ProtocolError&)
                {
                    m_Message   = "this firmware plays nothing on request";
                    m_IsWarning = true;
                }
            }
            else
            {
                co_await WriteAudio(via, *write.setting, write.value);
                const AudioValues read = co_await ReadAudio(via);
                const bool kept = *write.setting == AudioSetting::On ? read.on : read.clicky;
                // A newer change waiting keeps what the user sees; it is read back after it.
                if (!m_Pending)
                    m_Values = read;
                if (kept != write.value)
                {
                    m_Message   = std::string("the board kept it ") + (kept ? "on" : "off");
                    m_IsWarning = true;
                }
                else
                    m_Message = "stored, read back the same";
            }
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }
}
