// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardList.h"

#include <algorithm>

#include "imgui.h"

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr uint16_t c_ViaUsagePage = 0xFF60;
        constexpr uint16_t c_ViaUsage     = 0x61;

        const char* NameOf(const HidDeviceInfo& device)
        {
            return device.product.empty() ? "(unnamed)" : device.product.c_str();
        }

        // The other HID interfaces: dimmed, with the columns only this view has.
        void DrawOtherInterfaces(const std::vector<HidDeviceInfo>& devices)
        {
            ImGui::Spacing();
            ColouredText(PanelColour::Muted, "Other HID interfaces, not openable");

            const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
            if (!ImGui::BeginTable("others", 4, flags))
                return;

            ImGui::TableSetupColumn("Product", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("VID:PID", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Interface", ImGuiTableColumnFlags_WidthFixed);

            for (const HidDeviceInfo& device : devices)
            {
                if (IsViaInterface(device))
                    continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", NameOf(device));
                if (ImGui::IsItemHovered() && !device.manufacturer.empty())
                    ImGui::SetTooltip("%s", device.manufacturer.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%04X:%04X", device.vendorId, device.productId);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("usage %04X:%04X", device.usagePage, device.usage);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("if %d", device.interfaceNumber);
            }

            ImGui::EndTable();
        }
    }

    bool IsViaInterface(const HidDeviceInfo& device) noexcept
    {
        return device.usagePage == c_ViaUsagePage && device.usage == c_ViaUsage;
    }

    KeyboardListAction DrawKeyboardList(const std::vector<HidDeviceInfo>& devices,
                                        const std::vector<std::string>&   protocols,
                                        bool&                             showAllHidDevices,
                                        bool                              isListing,
                                        bool                              canRefresh,
                                        bool                              canOpen)
    {
        KeyboardListAction action;

        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Keyboards found");

        const char* refresh = "Refresh";
        ImGui::SameLine(right - ImGui::CalcTextSize(refresh).x - 2 * ImGui::GetStyle().FramePadding.x);
        ImGui::BeginDisabled(!canRefresh);
        action.refresh = ImGui::Button(refresh);
        ImGui::EndDisabled();

        const auto keyboards = std::count_if(devices.begin(), devices.end(), IsViaInterface);

        if (isListing)
        {
            ColouredText(PanelColour::Muted, "looking for keyboards...");
        }
        else if (keyboards == 0)
        {
            ImGui::TextUnformatted("No keyboard found.");
        }
        else if (ImGui::BeginTable("keyboards", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
        {
            ImGui::TableSetupColumn("Product", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Open", ImGuiTableColumnFlags_WidthFixed);

            for (size_t index = 0; index < devices.size(); ++index)
            {
                const HidDeviceInfo& device = devices[index];
                if (!IsViaInterface(device))
                    continue;

                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(NameOf(device));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%04X:%04X\n%s", device.manufacturer.c_str(), device.vendorId,
                                      device.productId, device.path.c_str());

                ImGui::TableNextColumn();
                const std::string& protocol = index < protocols.size() ? protocols[index] : std::string();
                ColouredText(PanelColour::Muted, "%s", protocol.empty() ? "..." : protocol.c_str());

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!canOpen);
                if (ImGui::SmallButton("Open"))
                    action.open = index;
                ImGui::EndDisabled();

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Checkbox("Show all HID devices", &showAllHidDevices);
        if (!showAllHidDevices)
            ColouredText(PanelColour::Muted, "Not listed? Turn on Show all HID devices.");
        else
            DrawOtherInterfaces(devices);

        return action;
    }
}
