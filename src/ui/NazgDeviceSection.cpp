// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDeviceSection.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "imgui.h"

#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgLineParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
        }

        void Muted(const char* words)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s", words);
        }
    }

    DeviceSection::DeviceSection(HidTransport& transport, std::string path, const Keyboard& keyboard,
                                 const LegendSettings& legends, const std::optional<vial::UnlockStatus>& lock)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock)
    {
    }

    bool DeviceSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool DeviceSection::TakeBoardChanged()
    {
        return std::exchange(m_Changed, false);
    }

    // A VIAL_INSECURE build reports itself unlocked with no keys to unlock with -- and has no jump.
    bool DeviceSection::CanJump() const
    {
        return m_Keyboard.report.isVial && m_Lock && !(m_Lock->unlocked && m_Lock->unlockKeys.empty());
    }

    bool DeviceSection::IsLocked() const
    {
        return CanJump() && !m_Lock->unlocked;
    }

    int DeviceSection::BootLayer() const
    {
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            for (const via::DefinitionKey& key : m_Keyboard.definition.keys)
                if (key.IsSwitch() && IsNamed(m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer)), "QK_BOOT"))
                    return layer;
        return -1;
    }

    // ------------------------------------------------------------------------------------------
    // The board: layer 0's legends; where the board cannot jump, the keys holding QK_BOOT lit.

    void DeviceSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        const int layer = CanJump() ? -1 : BootLayer();
        if (layer < 0)
            return;
        for (BoardKey& key : board.keys)
            key.marks |= key.geometry.IsSwitch() &&
                                 IsNamed(m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer)), "QK_BOOT")
                             ? Mark::Highlighted
                             : Mark::Dimmed;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void DeviceSection::DrawPanel()
    {
        const BoardReport& report = m_Keyboard.report;

        ImGui::TextUnformatted(m_Keyboard.Name().c_str());
        ImGui::SameLine();
        const std::string protocol = report.isVial ? "Vial " + std::to_string(report.vialProtocol)
                                                   : "VIA " + std::to_string(report.viaProtocol);
        ColouredText(PanelColour::Muted, "%s, keycodes %s", protocol.c_str(),
                     qmk::KeycodeVersionName(m_Keyboard.keycodeVersion));
        ImGui::Spacing();

        ImGui::BeginDisabled(IsBusy());
        if (ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch);

            (void)BeginLine("Firmware");
            DrawBootloaderLine();

            (void)BeginLine("Keymap");
            if (ImGui::Button("Reset keymap..."))
                m_Asked = Action::ResetKeymap;
            Muted("every layer's keys and knobs, as the firmware was built");

            if (report.macroCount > 0)
            {
                (void)BeginLine("Macros");
                if (ImGui::Button("Empty macros..."))
                    m_Asked = Action::EmptyMacros;
                const std::string all = "all " + std::to_string(report.macroCount);
                Muted(all.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        if (!m_Message.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Error, "%s", m_Message.c_str());
            ImGui::PopTextWrapPos();
        }

        DrawQuestion();
    }

    void DeviceSection::DrawBootloaderLine()
    {
        if (CanJump())
        {
            ImGui::BeginDisabled(IsLocked());
            if (ImGui::Button("Enter bootloader..."))
                m_Asked = Action::Bootloader;
            ImGui::EndDisabled();
            if (IsLocked())
                Muted("Vial lets only an unlocked board do this: click Locked in the header.");
            return;
        }

        // Says how instead (Rico, 2026-10-18): the key holding QK_BOOT, else the reset button.
        std::string how = m_Keyboard.report.isVial ? "This Vial firmware was built insecure, without the bootloader command: "
                                                   : "VIA's firmware has no bootloader command: ";
        const int layer = BootLayer();
        if (layer == 0)
            how += "press the lit key, which holds QK_BOOT.";
        else if (layer > 0)
            how += "press the lit key on layer " + std::to_string(layer) + ", which holds QK_BOOT.";
        else
            how += "press the board's reset button, or put QK_BOOT on a key in Keymap.";
        if (m_Keyboard.report.isLeydenJar)
            how += " Or Diagnostics, in Device.";

        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0.0f);
        ColouredText(PanelColour::Muted, "%s", how.c_str());
        ImGui::PopTextWrapPos();
    }

    // In a dialog, saying what goes and what stays (Rico, 2026-10-18: Cancel and the action, no more).
    void DeviceSection::DrawQuestion()
    {
        const std::string name = m_Keyboard.Name();
        std::string       title;
        std::string       says;
        std::string       keep;
        const char*       act = "";
        switch (m_Asked)
        {
        case Action::None:
            return;
        case Action::Bootloader:
            title = "Enter the bootloader of the " + name + "?";
            says  = "The " + name + " restarts in its bootloader, to take a new firmware: it leaves Nazg, and comes back "
                                    "in the list once flashed.";
            act   = "Enter bootloader";
            break;
        case Action::ResetKeymap:
            title = "Reset the keymap of the " + name + "?";
            says  = "Every layer's keys and knobs go back to the keymap its firmware was built with. Macros, lighting and "
                    "layout options stay" + std::string(m_Keyboard.report.isVial ? ", and Vial's features." : ".");
            keep  = "To keep this keymap, Save to file... first: Load from file... brings it back.";
            act   = "Reset keymap";
            break;
        case Action::EmptyMacros:
            title = "Empty the macros of the " + name + "?";
            says  = "All " + std::to_string(m_Keyboard.report.macroCount) +
                   " macros are emptied. Keys that play one stay, and play nothing.";
            keep  = "To keep them, Save to file... first: Load from file... brings them back.";
            act   = "Empty macros";
            break;
        }

        const std::string id = title + "###device";
        if (!ImGui::IsPopupOpen(id.c_str()))
            ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f));
        if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            m_Asked = Action::None;
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(says.c_str());
        if (!keep.empty())
        {
            ImGui::Spacing();
            ColouredText(PanelColour::Muted, "%s", keep.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const float width = ButtonWidth("Cancel") + ButtonWidth(act) + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        const Action asked = m_Asked;
        if (ImGui::Button("Cancel"))
        {
            m_Asked = Action::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const bool loses = asked != Action::Bootloader;
        if (loses)
            ImGui::PushStyleColor(ImGuiCol_Button, ColourOf(PanelColour::Error));
        if (ImGui::Button(act))
        {
            m_Asked = Action::None;
            ImGui::CloseCurrentPopup();
            if (!IsBusy())
                m_Request = Run(asked);
        }
        if (loses)
            ImGui::PopStyleColor();
        ImGui::EndPopup();
    }

    Task<void> DeviceSection::Run(Action action)
    {
        m_Message.clear();
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            via::Protocol    protocol(channel);
            switch (action)
            {
            case Action::Bootloader:
                co_await protocol.JumpToBootloader();
                m_Left = "The " + m_Keyboard.Name() + " left for its bootloader. Flash it, and it comes back in the list.";
                break;
            case Action::ResetKeymap:
                co_await protocol.ResetKeymap();
                m_Changed = true;
                break;
            case Action::EmptyMacros:
                co_await protocol.ResetMacros();
                m_Changed = true;
                break;
            case Action::None:
                break;
            }
        }
        catch (const std::exception& failure)
        {
            m_Message = std::string(action == Action::Bootloader ? "The bootloader could not be entered: "
                                    : action == Action::ResetKeymap ? "The keymap could not be reset: "
                                                                    : "The macros could not be emptied: ") +
                        failure.what();
        }
    }
}
