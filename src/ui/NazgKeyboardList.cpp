// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardList.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiTable, for the height its rows took

#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr uint16_t c_ViaUsagePage = 0xFF60;
        constexpr uint16_t c_ViaUsage     = 0x61;

        // Pixels, before DPI scaling.
        constexpr float c_ListWidth = 900.0f;   // at most: across a wide window, rows read as endless lines
        constexpr int   c_MaxRows   = 24;       // the other interfaces' frame at most, then it scrolls
        constexpr int   c_MinRows   = 4;        // and at least, however short the window

        // The height the other interfaces' rows took, header included, last frame: the frame fits
        // it. Measured rather than worked out, so no row height or border is guessed.
        float g_OthersContentHeight = 0.0f;

        const char* NameOf(const HidDeviceInfo& device)
        {
            return device.product.empty() ? "(unnamed)" : device.product.c_str();
        }

        // A list's frame: rounded and outlined like the panels, so it has a visible top and
        // bottom. `height` 0 fits the frame to its rows.
        bool BeginListFrame(const char* id, float width, float height)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            const ImGuiChildFlags flags = ImGuiChildFlags_Borders | (height > 0.0f ? 0 : ImGuiChildFlags_AutoResizeY);
            const bool            open  = ImGui::BeginChild(id, ImVec2(width, height), flags);
            ImGui::PopStyleVar();
            return open;
        }

        // The other HID interfaces: dimmed, with the columns only this view has, in a frame as tall
        // as its rows up to c_MaxRows -- and never past the window's bottom -- then scrolling under a
        // header row that stays.
        void DrawOtherInterfaces(const std::vector<HidDeviceInfo>& devices, float width)
        {
            const auto others = std::count_if(devices.begin(), devices.end(),
                                              [](const HidDeviceInfo& device) { return !IsViaInterface(device); });

            ImGui::Spacing();
            PushHeaderFont();
            ImGui::Text("Other HID interfaces: %d", static_cast<int>(others));
            ImGui::PopFont();
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "not openable");

            // A row is a line of text and the cells' padding; the first frame estimates with it,
            // every later one uses what the rows really took.
            const float row     = ImGui::GetTextLineHeight() + 2.0f * ImGui::GetStyle().CellPadding.y;
            const float content = g_OthersContentHeight > 0.0f ? g_OthersContentHeight
                                                               : row * static_cast<float>(others + 1);
            const float border  = 2.0f * ImGui::GetStyle().ChildBorderSize;
            const float most    = std::max(std::min(row * (c_MaxRows + 1), ImGui::GetContentRegionAvail().y - border),
                                           row * (c_MinRows + 1));
            const float height  = std::min(content, most) + border;
            if (BeginListFrame("others frame", width, height))
            {
                const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
                if (ImGui::BeginTable("others", 4, flags))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Product", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("VID:PID", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Interface", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableHeadersRow();

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
                        ImGui::TextDisabled("%04X:%04X", device.usagePage, device.usage);
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%d", device.interfaceNumber);
                    }

                    g_OthersContentHeight = ImGui::GetCurrentTable()->InnerWindow->ContentSize.y;
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
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

        // One column, centred in the window: the room left over falls evenly on both sides.
        const float available = ImGui::GetContentRegionAvail().x;
        const float width     = std::min(available, c_ListWidth * ImGui::GetStyle().FontScaleDpi);
        const float indent    = std::floor((available - width) / 2.0f);
        if (indent > 0.0f)
            ImGui::Indent(indent);
        const float right = ImGui::GetCursorPosX() + width;
        const auto  keyboards = std::count_if(devices.begin(), devices.end(), IsViaInterface);

        // The heading, with the count once the list is known, and Refresh at the list's right edge.
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        if (isListing)
            ImGui::TextUnformatted("Keyboards");
        else
            ImGui::Text("Keyboards: %d", static_cast<int>(keyboards));
        ImGui::PopFont();

        const char* refresh = "Refresh";
        ImGui::SameLine(right - ImGui::CalcTextSize(refresh).x - 2 * ImGui::GetStyle().FramePadding.x);
        ImGui::BeginDisabled(!canRefresh);
        action.refresh = ImGui::Button(refresh);
        ImGui::EndDisabled();

        if (isListing)
        {
            ColouredText(PanelColour::Muted, "looking for keyboards...");
        }
        else if (keyboards == 0)
        {
            ImGui::TextUnformatted("No keyboard found.");
        }
        else
        {
            // As tall as its rows: a keyboard or three, usually.
            if (BeginListFrame("keyboards frame", width, 0.0f))
            {
                const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX;
                if (ImGui::BeginTable("keyboards", 3, flags))
                {
                    ImGui::TableSetupColumn("Product", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Protocol", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableHeadersRow();

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
                        ImGui::AlignTextToFramePadding();
                        const std::string& protocol = index < protocols.size() ? protocols[index] : std::string();
                        ColouredText(PanelColour::Muted, "%s", protocol.empty() ? "..." : protocol.c_str());

                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(!canOpen);
                        if (ImGui::Button("Open"))
                            action.open = index;
                        ImGui::EndDisabled();

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Checkbox("Show all HID devices", &showAllHidDevices);
        if (!showAllHidDevices)
            ColouredText(PanelColour::Muted, "Not listed? Turn on Show all HID devices.");
        else
            DrawOtherInterfaces(devices, width);

        if (indent > 0.0f)
            ImGui::Unindent(indent);
        return action;
    }
}
