// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgPermissionDialog.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
        }

        std::string UsbId(uint16_t vendorId, uint16_t productId)
        {
            char text[16];
            std::snprintf(text, sizeof(text), "%04x:%04x", vendorId, productId);
            return text;
        }

        void Wrapped(const std::string& text)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
        }

        void Wrapped(PanelColour colour, const std::string& text)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(colour, "%s", text.c_str());
            ImGui::PopTextWrapPos();
        }
    }

    void PermissionDialog::AskAllow(const HidDeviceInfo& device)
    {
        Ask(Kind::Allow, device);
    }

    void PermissionDialog::AskStop(const HidDeviceInfo& device)
    {
        Ask(Kind::Stop, device);
    }

    void PermissionDialog::Ask(Kind kind, const HidDeviceInfo& device)
    {
        if (IsOpen())
            return;
        m_Kind         = kind;
        m_Name         = device.product.empty() ? "keyboard" : device.product;
        m_VendorId     = device.vendorId;
        m_ProductId    = device.productId;
        m_ShowCommands = false;
        m_IsWaiting    = false;
        m_CannotAsk    = false;
        m_Problem.clear();
    }

    std::optional<PermissionDialog::Outcome> PermissionDialog::Draw()
    {
        if (m_Kind == Kind::None)
            return std::nullopt;

        const bool isAllow = m_Kind == Kind::Allow;
        std::optional<Outcome> outcome;

        // The password window answered: allowed or stopped, closed, or the computer could not ask.
        if (m_IsWaiting)
        {
            const RootCommand::Outcome answer = m_Command.Poll();
            if (answer != RootCommand::Outcome::Running)
                m_IsWaiting = false;
            switch (answer)
            {
            case RootCommand::Outcome::Running:
            case RootCommand::Outcome::Cancelled:   // nothing changed; the dialog stays
                break;
            case RootCommand::Outcome::Done:
                outcome = Outcome{ m_VendorId, m_ProductId,
                                   isAllow ? "The " + m_Name + " is allowed -- Linux remembers it."
                                           : "The " + m_Name + " is no longer allowed." };
                break;
            case RootCommand::Outcome::NotAsked:
                m_CannotAsk    = true;
                m_ShowCommands = true;
                m_Problem      = m_Command.Detail();
                break;
            case RootCommand::Outcome::Failed:
                m_ShowCommands = true;
                m_Problem      = std::string(isAllow ? "Allowing" : "Stopping") + " did not work: " + m_Command.Detail() + ".";
                break;
            }
        }

        const std::string title = (isAllow ? "Allow the " : "Stop allowing the ") + m_Name + "###permission";
        if (!outcome && !ImGui::IsPopupOpen(title.c_str()))
            ImGui::OpenPopup(title.c_str());
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, 0.0f));
        if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            m_Kind = Kind::None;
            return outcome;
        }
        if (outcome)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            m_Kind = Kind::None;
            return outcome;
        }

        if (isAllow)
        {
            Wrapped("Linux keeps a keyboard closed to programs until you allow it. Allowed, Nazg can configure the " +
                    m_Name + ", and Linux remembers it.");
            ImGui::Spacing();
            Wrapped("To stop allowing it later, click Allowed, which appears next to its Open button.");
        }
        else
        {
            Wrapped("Nazg will no longer be able to configure the " + m_Name +
                    ". It still types as ever: only its settings are closed again.");
            ImGui::Spacing();
            Wrapped(PanelColour::Muted, "Every keyboard with the same USB id, " + UsbId(m_VendorId, m_ProductId) +
                                            ", goes with it.");
        }
        ImGui::Spacing();

        if (m_CannotAsk)
            Wrapped(PanelColour::Warning, "Your computer did not ask for the password (" + m_Problem +
                                              "). Run these commands in a terminal -- it asks for your password -- then "
                                              "click " + (isAllow ? "Try again." : "Done."));
        else if (m_IsWaiting)
            Wrapped(PanelColour::Muted, "Waiting for your password, in your computer's own window...");
        else
            Wrapped(PanelColour::Muted, "Your computer asks for your password, once.");
        if (!m_CannotAsk && !m_Problem.empty())
            Wrapped(PanelColour::Error, m_Problem);

        if (m_ShowCommands)
            DrawCommands();
        ImGui::Spacing();

        // Show the commands on the left; Cancel and the action on the right.
        const char* act    = m_CannotAsk ? (isAllow ? "Try again" : "Done") : (isAllow ? "Allow" : "Stop allowing");
        const float right  = ButtonWidth("Cancel") + ButtonWidth(act) + ImGui::GetStyle().ItemSpacing.x;
        bool        cancel = false;
        bool        acted  = false;
        ImGui::BeginDisabled(m_IsWaiting);
        if (!m_CannotAsk)
        {
            // As it was before the click: the click changes it between the push and the pop.
            const bool shown = m_ShowCommands;
            if (shown)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button("Show the commands"))
                m_ShowCommands = !m_ShowCommands;
            if (shown)
                ImGui::PopStyleColor();
            ImGui::SameLine();
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - right));
        cancel = ImGui::Button("Cancel");
        ImGui::SameLine();
        acted = ImGui::Button(act);
        ImGui::EndDisabled();

        if (cancel)
        {
            ImGui::CloseCurrentPopup();
            m_Kind = Kind::None;
        }
        else if (acted && m_CannotAsk)
        {
            // Run in a terminal, or so the user says: the board is asked again.
            outcome = Outcome{ m_VendorId, m_ProductId, std::string() };
            ImGui::CloseCurrentPopup();
            m_Kind = Kind::None;
        }
        else if (acted)
        {
            m_Problem.clear();
            m_Command.Start(isAllow ? AllowScript(m_Name, m_VendorId, m_ProductId) : StopScript(m_VendorId, m_ProductId));
            m_IsWaiting = true;   // polled from the next frame -- a start that failed answers at once
        }
        ImGui::EndPopup();
        return outcome;
    }

    // What runs, as a terminal takes it: one command per line, wrapped, with Copy.
    void PermissionDialog::DrawCommands()
    {
        const bool                     isAllow  = m_Kind == Kind::Allow;
        const std::vector<std::string> commands = isAllow ? AllowCommands(m_Name, m_VendorId, m_ProductId)
                                                          : StopCommands(m_VendorId, m_ProductId);
        ImGui::Spacing();
        if (!m_CannotAsk)
            Wrapped(PanelColour::Muted, std::string(isAllow ? "Allow" : "Stop allowing") +
                                            " runs these, as administrator (sudo), once you give your password. You "
                                            "may also paste them into a terminal yourself.");

        std::string all;
        for (const std::string& command : commands)
            all += command + "\n";

        ImGui::BeginChild("commands", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        for (const std::string& command : commands)
            Wrapped(command);
        ImGui::EndChild();
        if (ImGui::Button("Copy"))
            ImGui::SetClipboardText(all.c_str());
    }
}
