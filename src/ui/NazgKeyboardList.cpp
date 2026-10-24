// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyboardList.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <initializer_list>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiTable, for the height its rows took

#include "ui/NazgIcons.h"
#include "ui/NazgTheme.h"
#include "ui/NazgWorkspace.h"

namespace nazg
{
    namespace
    {
        constexpr uint16_t c_ViaUsagePage = 0xFF60;
        constexpr uint16_t c_ViaUsage     = 0x61;

        // Pixels, before DPI scaling.
        constexpr float c_ListWidth    = 900.0f;   // at most: across a wide window, rows read as endless lines
        constexpr float c_MessageWidth = 560.0f;   // a message alone on the screen, at most
        constexpr int   c_MaxRows   = 24;       // the other interfaces' frame at most, then it scrolls
        constexpr int   c_MinRows   = 4;        // and at least, however short the window

        // The height the other interfaces' rows took, header included, last frame: the frame fits
        // it. Measured rather than worked out, so no row height or border is guessed.
        float g_OthersContentHeight = 0.0f;

        const char* NameOf(const HidDeviceInfo& device)
        {
            return device.product.empty() ? "(unnamed)" : device.product.c_str();
        }

        // A board Nazg allowed: "Allowed" after a shield, quiet -- muted, no frame until hovered --
        // so the row still reads as its name and Open. Without the icon font, the word alone.
        bool AllowedButton()
        {
            const char*       word   = "Allowed";
            const std::string shield = Utf8Of(Icon::ShieldCheck);
            ImFont* const     icons  = IconFont();
            const float       size   = ImGui::GetFontSize();
            const ImVec2      pad    = ImGui::GetStyle().FramePadding;
            const float       gap    = ImGui::GetStyle().ItemInnerSpacing.x;
            const float       icon   = icons != nullptr ? icons->CalcTextSizeA(size, FLT_MAX, 0.0f, shield.c_str()).x : 0.0f;
            const float       text   = ImGui::CalcTextSize(word).x;
            const float       width  = pad.x * 2.0f + (icon > 0.0f ? icon + gap : 0.0f) + text;

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            const bool clicked = ImGui::Button("##allowed", ImVec2(width, ImGui::GetFrameHeight()));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to stop allowing it");

            ImDrawList* const list   = ImGui::GetWindowDrawList();
            const ImU32       colour = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
            ImVec2            at(ImGui::GetItemRectMin().x + pad.x, ImGui::GetItemRectMin().y + pad.y);
            if (icon > 0.0f)
            {
                list->AddText(icons, size, at, colour, shield.c_str());
                at.x += icon + gap;
            }
            list->AddText(at, colour, word);
            return clicked;
        }

        // A short message alone on the screen, in a narrow column centred in the window and a little
        // way down: a heading, its paragraphs wrapped, and one button under them, centred. True when
        // the button was clicked.
        bool CentredMessage(const char* heading, std::initializer_list<const char*> paragraphs, const char* button)
        {
            const float available = ImGui::GetContentRegionAvail().x;
            const float width     = std::min(available, c_MessageWidth * ImGui::GetStyle().FontScaleDpi);
            const float indent    = std::floor((available - width) / 2.0f);
            const float start     = ImGui::GetCursorPosX();

            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight() * 2.0f));
            PushHeaderFont();
            ImGui::SetCursorPosX(start + std::floor((available - ImGui::CalcTextSize(heading).x) / 2.0f));
            ImGui::TextUnformatted(heading);
            ImGui::PopFont();
            ImGui::Spacing();

            ImGui::PushTextWrapPos(start + indent + width);
            for (const char* paragraph : paragraphs)
            {
                ImGui::SetCursorPosX(start + indent);
                ImGui::TextWrapped("%s", paragraph);
                ImGui::Spacing();
            }
            ImGui::PopTextWrapPos();

            const ImGuiStyle& style = ImGui::GetStyle();
            const float       wide  = ImGui::CalcTextSize(button).x + style.FramePadding.x * 4.0f;
            ImGui::SetCursorPosX(start + std::floor((available - wide) / 2.0f));
            return ImGui::Button(button, ImVec2(wide, ImGui::GetFrameHeight() * 1.25f));
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
                                        const std::vector<bool>&          allowedByNazg,
                                        bool&                             showAllHidDevices,
                                        bool                              isListing,
                                        bool                              canOpen,
                                        bool                              chosenOnly)
    {
        KeyboardListAction action;
        const auto         keyboards = std::count_if(devices.begin(), devices.end(), IsViaInterface);

        // A browser's first visit: nothing chosen yet, so no list -- what to do instead.
        if (chosenOnly && !isListing && keyboards == 0)
        {
            action.add = CentredMessage("Add your keyboard",
                                        { "A web page may only use a keyboard you choose. Click below, and your browser lists "
                                          "the keyboards Nazg can configure -- VIA and Vial ones. Pick yours and click Connect.",
                                          "Once added, it is there each time you come back. Not in the browser's list? It may "
                                          "not run VIA or Vial firmware." },
                                        "Add a keyboard...");
            return action;
        }

        // One column, centred in the window: the room left over falls evenly on both sides.
        const float available = ImGui::GetContentRegionAvail().x;
        const float width     = std::min(available, c_ListWidth * ImGui::GetStyle().FontScaleDpi);
        const float indent    = std::floor((available - width) / 2.0f);
        if (indent > 0.0f)
            ImGui::Indent(indent);

        // The heading, with the count once the list is known.
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        if (isListing)
            ImGui::TextUnformatted("Keyboards");
        else
            ImGui::Text("Keyboards: %d", static_cast<int>(keyboards));
        ImGui::PopFont();

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
                        const std::string& protocol   = index < protocols.size() ? protocols[index] : std::string();
                        const bool         notAllowed = protocol == c_NotAllowed;
                        if (notAllowed)
                        {
                            ColouredText(PanelColour::Warning, "%s", protocol.c_str());
                            if (ImGui::IsItemHovered())
                                ImGui::SetTooltip("Linux does not let Nazg use this keyboard yet.");
                        }
                        else
                        {
                            ColouredText(PanelColour::Muted, "%s", protocol.empty() ? "..." : protocol.c_str());
                        }

                        // Allow... in Open's place; or Allowed before Open, on a board Nazg allowed.
                        ImGui::TableNextColumn();
                        if (notAllowed)
                        {
                            if (ImGui::Button("Allow..."))
                                action.allow = index;
                        }
                        else
                        {
                            if (index < allowedByNazg.size() && allowedByNazg[index])
                            {
                                if (AllowedButton())
                                    action.stopAllowing = index;
                                ImGui::SameLine();
                            }
                            ImGui::BeginDisabled(!canOpen);
                            if (ImGui::Button("Open"))
                                action.open = index;
                            ImGui::EndDisabled();
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        if (chosenOnly)
        {
            // Only the chosen boards could be shown: no Show all HID devices.
            action.add = ImGui::Button("Add a keyboard...");
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
            ImGui::PushStyleColor(ImGuiCol_Text, ColourOf(PanelColour::Muted));
            ImGui::TextWrapped("Nazg sees only the keyboards you add, each once: your browser remembers them for this site. "
                               "To remove one, use the browser's site settings (the icon left of the address).");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        else
        {
            ImGui::Checkbox("Show all HID devices", &showAllHidDevices);
            if (!showAllHidDevices)
                ColouredText(PanelColour::Muted, "Not listed? Turn on Show all HID devices.");
            else
                DrawOtherInterfaces(devices, width);
        }

        if (indent > 0.0f)
            ImGui::Unindent(indent);
        return action;
    }

    bool DrawNoWebHid()
    {
        return CentredMessage("This browser cannot reach keyboards",
                              { "Nazg in a browser needs WebHID, which only Chrome, Edge and Opera have, on a computer -- "
                                "Firefox and Safari do not, nor does any phone.",
                                "Open this page in one of them, or download Nazg for Windows, macOS or Linux." },
                              "Download Nazg");
    }
}
