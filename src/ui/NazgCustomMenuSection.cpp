// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgCustomMenuSection.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "model/NazgCombo.h"
#include "model/NazgLighting.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgSectionPlan.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        using Type = CustomControl::Type;

        std::string_view NameOf(const Keycode& keycode)
        {
            const NamedKey* named = std::get_if<NamedKey>(&keycode);
            return named ? named->name : std::string_view();
        }

        bool IsNothing(const Keycode& keycode)
        {
            const std::string_view name = NameOf(keycode);
            return name == "KC_NO" || name == "KC_TRNS";
        }

        // QMK's lighting channels, as a custom menu may use them for its own lighting menu: the
        // system and the line a value id is (via-vial-commands.md, "Custom channels").
        std::optional<LightingKind> LightingKindOf(uint8_t channel)
        {
            switch (channel)
            {
                case 1:
                    return LightingKind::Backlight;
                case 2:
                    return LightingKind::Underglow;
                case 3:
                    return LightingKind::RgbMatrix;
                default:
                    return std::nullopt;
            }
        }

        LightingLine LightingLineOf(uint8_t valueId)
        {
            switch (valueId)
            {
                case 1:
                    return LightingLine::Brightness;
                case 2:
                    return LightingLine::Effect;
                case 3:
                    return LightingLine::Speed;
                case 4:
                    return LightingLine::Colour;
                default:
                    return LightingLine::Any;
            }
        }

        ImU32 HueColour(float hue, float saturation = 1.0f)
        {
            float r = 0, g = 0, b = 0;
            ImGui::ColorConvertHSVtoRGB(hue, saturation, 1.0f, r, g, b);
            return ImGui::GetColorU32(ImVec4(r, g, b, 1.0f));
        }

        // A 0-255 slider over a colour band -- the hue circle, or white to `hue` -- as Lighting's.
        // True while it moves; `letGo` when it was let go after a change.
        bool ColourSlider(const char* id, int& value, bool hueTrack, uint8_t hue, bool& letGo)
        {
            const float  width  = ImGui::GetFontSize() * 12.0f;
            const float  height = ImGui::GetFrameHeight();
            const ImVec2 at     = ImGui::GetCursorScreenPos();
            ImDrawList*  draw   = ImGui::GetWindowDrawList();
            const float  inset  = std::floor(height * 0.3f);
            const ImVec2 a(at.x + 2.0f, at.y + inset);
            const ImVec2 b(at.x + width - 2.0f, at.y + height - inset);
            if (hueTrack)
                for (int part = 0; part < 6; ++part)
                {
                    const float x0 = a.x + (b.x - a.x) * part / 6.0f, x1 = a.x + (b.x - a.x) * (part + 1) / 6.0f;
                    const ImU32 c0 = HueColour(part / 6.0f), c1 = HueColour((part + 1) / 6.0f);
                    draw->AddRectFilledMultiColor(ImVec2(x0, a.y), ImVec2(x1, b.y), c0, c1, c1, c0);
                }
            else
            {
                const ImU32 white = HueColour(0.0f, 0.0f), full = HueColour(hue / 255.0f);
                draw->AddRectFilledMultiColor(a, b, white, full, full, white);
            }
            for (ImGuiCol colour : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive })
                ImGui::PushStyleColor(colour, ImVec4(0, 0, 0, 0));
            ImGui::SetNextItemWidth(width);
            const bool moved = ImGui::SliderInt(id, &value, 0, 255, "", ImGuiSliderFlags_AlwaysClamp);
            ImGui::PopStyleColor(3);
            letGo = ImGui::IsItemDeactivatedAfterEdit();
            return moved;
        }

        // The longest of `names` and a combo's arrow: a combo as wide as what it shows.
        float ComboWidth(const std::vector<std::string>& names)
        {
            float longest = 0.0f;
            for (const std::string& name : names)
                longest = std::max(longest, ImGui::CalcTextSize(name.c_str()).x);
            return longest + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight();
        }

        // Two columns of lines once the panel is this wide, in font sizes.
        constexpr float c_TwoColumns = 52.0f;
        constexpr float c_LabelWidth = 14.0f;
    }

    CustomMenuSection::CustomMenuSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                         const LegendSettings& legends, const bool& advancedTools, size_t menu)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends),
          m_AdvancedTools(advancedTools), m_MenuIndex(menu)
    {
        m_Name = Menu().label.empty() ? "(unnamed menu)" : Menu().label;
        for (const CustomMenu& each : m_Keyboard.definition.customMenus)
            for (const CustomSubmenu& submenu : each.submenus)
                for (const CustomControl& control : submenu.controls)
                    if (control.type == Type::Range && control.isBound)
                        m_Ranges.push_back(&control);
    }

    Icon CustomMenuSection::ColumnIcon() const
    {
        return IconOfMenu(Menu().label);
    }

    const CustomMenu& CustomMenuSection::Menu() const
    {
        return m_Keyboard.definition.customMenus[m_MenuIndex];
    }

    bool CustomMenuSection::Holds(const std::vector<std::string>& conditions) const
    {
        return ConditionsHold(conditions, m_Values, m_Keyboard.report.firmwareVersion);
    }

    std::vector<size_t> CustomMenuSection::ShownSubmenus() const
    {
        std::vector<size_t> shown;
        const CustomMenu&   menu = Menu();
        for (size_t index = 0; index < menu.submenus.size(); ++index)
            if (Holds(menu.submenus[index].conditions))
                shown.push_back(index);
        return shown;
    }

    std::vector<const CustomControl*> CustomMenuSection::ShownLines(const CustomSubmenu& submenu) const
    {
        std::vector<const CustomControl*> lines;
        for (const CustomControl& control : submenu.controls)
            if (Holds(control.conditions))
                lines.push_back(&control);
        return lines;
    }

    bool CustomMenuSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    void CustomMenuSection::WhileHidden()
    {
        // What the board's own keys changed meanwhile: read again when shown.
        if (!m_Known.empty())
            m_IsStale = true;
        m_Hovered = nullptr;
    }

    const CustomControl* CustomMenuSection::Binding(const std::string& name) const
    {
        for (const CustomMenu& menu : m_Keyboard.definition.customMenus)
            for (const CustomSubmenu& submenu : menu.submenus)
                for (const CustomControl& control : submenu.controls)
                    if (control.isBound && control.type != Type::Button && control.name == name)
                        return &control;
        return nullptr;
    }

    std::vector<std::string> CustomMenuSection::Needed() const
    {
        std::vector<std::string> names;
        const auto add = [&](const std::string& name)
        {
            if (name != c_FirmwareVersionName && Binding(name) &&
                std::find(names.begin(), names.end(), name) == names.end())
                names.push_back(name);
        };
        const auto addNamed = [&](const std::vector<std::string>& conditions)
        {
            for (const std::string& condition : conditions)
                for (const std::string& name : NamesIn(condition))
                    add(name);
        };

        const CustomMenu& menu = Menu();
        addNamed(menu.conditions);
        for (const CustomSubmenu& submenu : menu.submenus)
            addNamed(submenu.conditions);
        if (m_Submenu < menu.submenus.size())
            for (const CustomControl& control : menu.submenus[m_Submenu].controls)
            {
                addNamed(control.conditions);
                if (control.isBound && control.type != Type::Button)
                    add(control.name);
                for (const RangeConstraint& constraint : control.constraints)
                    add(constraint.reference);
            }
        return names;
    }

    // ------------------------------------------------------------------------------------------
    // The strip and the board.

    Strip CustomMenuSection::DescribeStrip() const
    {
        Strip                     strip;
        const std::vector<size_t> shown = ShownSubmenus();
        if (shown.size() < 2)
            return strip;
        for (size_t index : shown)
        {
            const std::string& label = Menu().submenus[index].label;
            strip.entries.push_back(label.empty() ? "(unnamed)" : label);
            if (index == m_Submenu)
                strip.chosen = strip.entries.size() - 1;
        }
        return strip;
    }

    void CustomMenuSection::OnStripChosen(size_t entry)
    {
        const std::vector<size_t> shown = ShownSubmenus();
        if (entry >= shown.size() || shown[entry] == m_Submenu)
            return;
        m_Submenu  = shown[entry];
        m_Selected = nullptr;
        m_Pushed.clear();
        m_IsStale = true;   // what the board holds now
    }

    void CustomMenuSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
        const CustomMenu& menu = Menu();
        if (m_Submenu >= menu.submenus.size() || m_Known.empty())
            return;
        const CustomSubmenu& submenu = menu.submenus[m_Submenu];

        const std::vector<Words> custom   = CustomKeycodeWordsOf(m_Keyboard);
        const uint8_t            lighting = LightingSystemsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither, lighting, custom,
                                          m_Keyboard.tapDances };
        const auto layerLegend = [&](BoardKey& key, const Keycode& keycode, int layer)
        {
            if (layer == 0)
                return;
            KeycapLegend legend = LegendFor(keycode, context);
            legend.header       = { { "Layer " + std::to_string(layer), "L" + std::to_string(layer) }, CommandCategory::Behaviour };
            key.legends         = legend;
            key.fallthrough     = Fallthrough::None;
        };

        // The submenu's own key, by name: the board's keycode whose name its label holds -- the
        // definition's n-th is QK_KB_0 + n.
        std::vector<Keycode> own;
        if (const std::optional<uint16_t> first = EncodeQmkKeycode(NamedKey{ "QK_KB_0" }, m_Keyboard.keycodeVersion))
            for (size_t index = 0; index < m_Keyboard.definition.customKeycodes.size(); ++index)
            {
                const std::string& name = m_Keyboard.definition.customKeycodes[index].name;
                if (!name.empty() && submenu.label.find(name) != std::string::npos && *first + index <= 0xFFFF)
                    own.push_back(DecodeQmkKeycode(static_cast<uint16_t>(*first + index), m_Keyboard.keycodeVersion));
            }
        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                if (std::find(own.begin(), own.end(), keycode) == own.end())
                    continue;
                key.marks |= Mark::HighlightedSecond;
                layerLegend(key, keycode, layer);
                break;
            }
        }

        // What its lines choose, and the lighting keys of QMK's channels -- a line under the
        // pointer, its own alone.
        const bool underglow = (lighting & LightingSystem::Underglow) != 0;
        for (const CustomControl* control : ShownLines(submenu))
        {
            if (m_Hovered && control != m_Hovered)
                continue;
            if (control->type == Type::Keycode && m_Unanswered.count(control->name) == 0)
            {
                const Keycode keycode = KeycodeOf(*control);
                if (IsNothing(keycode))
                    continue;
                if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, keycode))
                    for (BoardKey& key : board.keys)
                        if (!key.geometry.decal && m_Keyboard.KeycodeFor(key.geometry, *layer) == keycode)
                        {
                            key.marks |= Mark::Highlighted;
                            layerLegend(key, keycode, *layer);
                        }
            }
            else if (const std::optional<LightingKind> kind = LightingKindOf(control->channel); kind && control->isBound)
            {
                const LightingLine line = LightingLineOf(control->valueId);
                for (BoardKey& key : board.keys)
                {
                    if (key.geometry.decal)
                        continue;
                    for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
                    {
                        const Keycode    keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                        const std::string_view name = NameOf(keycode);
                        if (!ChangesLighting(name, *kind, line, underglow) &&
                            !(line == LightingLine::Effect && ChangesLighting(name, *kind, LightingLine::On, underglow)))
                            continue;
                        key.marks |= Mark::Highlighted;
                        layerLegend(key, keycode, layer);
                        break;
                    }
                }
            }
        }
    }

    void CustomMenuSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!m_Selected || !events.hoveredKey || IsBusy())
            return;

        // What the key sends on layer 0, for the selected key line (as Combos takes its inputs).
        const Keycode sent = m_Keyboard.KeycodeFor(board.keys[*events.hoveredKey].geometry, 0);
        if (IsNothing(sent))
            return;
        ImGui::SetTooltip("%s -- click to set %s to it", KeycodeLabel(sent, m_Keyboard.keycodeVersion).c_str(),
                          m_Selected->label.c_str());
        if (events.clickedKey)
            SetKeycode(*m_Selected, sent);
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void CustomMenuSection::DrawPanel()
    {
        m_HoveredNow = nullptr;
        const CustomMenu& menu = Menu();

        // What is not read yet, read before the panel shows; what may have changed, read again
        // behind it.
        std::vector<std::string> missing;
        for (const std::string& name : Needed())
            if (m_Known.count(name) == 0)
                missing.push_back(name);
        if (!missing.empty())
        {
            if (!IsBusy() && m_ReadFailure.empty())
            {
                m_IsStale = false;
                m_Request = Read(Needed());
            }
            if (DrawReading("menu's values", IsBusy(), m_ReadFailure))
            {
                m_ReadFailure.clear();
                m_Request = Read(Needed());
            }
            return;
        }
        if (m_IsStale && !IsBusy() && m_Dirty.empty() && !m_StoreWanted && !m_Button)
        {
            m_IsStale = false;
            m_Request = Read(Needed());
        }

        const std::vector<size_t> shown = ShownSubmenus();
        if (!Holds(menu.conditions) || shown.empty())
        {
            ImGui::TextDisabled("This menu has nothing to show on this board as it is set now.");
            return;
        }
        if (std::find(shown.begin(), shown.end(), m_Submenu) == shown.end())
        {
            m_Submenu  = shown.front();
            m_Selected = nullptr;
            m_IsStale  = true;
            return;
        }

        const CustomSubmenu&                    submenu = menu.submenus[m_Submenu];
        const std::vector<const CustomControl*> lines   = ShownLines(submenu);
        if (m_Selected && std::find(lines.begin(), lines.end(), m_Selected) == lines.end())
            m_Selected = nullptr;

        DrawHeader(submenu);
        ImGui::Separator();
        DrawLines(lines);
        if (m_Selected)
        {
            ImGui::Separator();
            DrawPicker(*m_Selected);
        }

        m_Hovered = m_HoveredNow;

        // One write at a time; a slider moving faster than the board answers sends its latest value.
        if (!IsBusy() && (!m_Dirty.empty() || m_StoreWanted || m_Button))
        {
            PendingWrite write;
            for (const std::string& name : m_Dirty)
                if (const CustomControl* control = Binding(name))
                    write.values.emplace_back(control, m_Values[name]);
            write.button = m_Button;
            write.store  = m_StoreWanted;
            m_Dirty.clear();
            m_StoreWanted = false;
            m_Button      = nullptr;
            m_Request     = Write(std::move(write));
        }
    }

    void CustomMenuSection::DrawHeader(const CustomSubmenu& submenu)
    {
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(submenu.label.empty() ? m_Name.c_str() : submenu.label.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (m_IsWriting || !m_Dirty.empty() || m_StoreWanted)
            ColouredText(PanelColour::Muted, "Writing...");
        else if (!m_ReadFailure.empty())
            ColouredText(PanelColour::Warning, "%s", m_ReadFailure.c_str());
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        else
            ColouredText(PanelColour::Muted, "The board's own menu. Each change is written at once; a slider's is stored "
                                             "when let go.");
    }

    void CustomMenuSection::DrawLines(const std::vector<const CustomControl*>& lines)
    {
        if (lines.empty())
        {
            ImGui::TextDisabled("Nothing to set here as the board is set now.");
            return;
        }

        // Two columns when there is room -- the first half down the left -- giving the picker the
        // height (Rico, 2026-10-11).
        const float  font    = ImGui::GetFontSize();
        const bool   two     = lines.size() > 1 && ImGui::GetContentRegionAvail().x >= c_TwoColumns * font;
        const size_t rows    = two ? (lines.size() + 1) / 2 : lines.size();
        const int    columns = two ? 4 : 2;

        float longest = 0.0f;
        for (const CustomControl* line : lines)
            longest = std::max(longest, ImGui::CalcTextSize(line->label.c_str()).x);
        const float labelWidth = std::min(longest, c_LabelWidth * font);

        if (!ImGui::BeginTable("lines", columns, ImGuiTableFlags_SizingFixedFit))
            return;
        for (int pair = 0; pair < columns / 2; ++pair)
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, labelWidth);
            ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
        }
        for (size_t row = 0; row < rows; ++row)
        {
            ImGui::TableNextRow();
            for (int pair = 0; pair < columns / 2; ++pair)
            {
                const size_t index = row + static_cast<size_t>(pair) * rows;
                if (index >= lines.size())
                    break;
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableSetColumnIndex(pair * 2);
                const float top = ImGui::GetCursorScreenPos().y;
                DrawLine(*lines[index]);

                // The pointer over the line's two cells: the board narrows to it.
                const ImVec2 mouse = ImGui::GetMousePos();
                if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mouse.y >= top &&
                    mouse.y < ImGui::GetItemRectMax().y + ImGui::GetStyle().CellPadding.y &&
                    (ImGui::TableGetHoveredColumn() == pair * 2 || ImGui::TableGetHoveredColumn() == pair * 2 + 1))
                    m_HoveredNow = lines[index];
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    void CustomMenuSection::DrawLine(const CustomControl& control)
    {
        // The label, wrapped in its column -- a labelled button says it on itself.
        const int pairColumn = ImGui::TableGetColumnIndex();
        if (control.type != Type::Button)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(control.label.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TableSetColumnIndex(pairColumn + 1);

        const bool unanswered = control.isBound && m_Unanswered.count(control.name) != 0;
        ImGui::BeginDisabled(unanswered);
        switch (control.type)
        {
            case Type::Range:
                DrawRange(control);
                break;

            case Type::Dropdown:
                DrawDropdown(control);
                break;

            case Type::Toggle:
            {
                bool on = IsToggledOn(control, m_Values[control.name]);
                if (ImGui::Checkbox("##toggle", &on))
                {
                    m_Values[control.name] = on ? control.on : control.off;
                    Change({ control.name }, true);
                }
                break;
            }

            case Type::Keycode:
                DrawKey(control);
                break;

            case Type::Colour:
                DrawColour(control);
                break;

            case Type::Button:
                if (ImGui::Button(control.label.empty() ? "Send" : control.label.c_str()))
                    m_Button = &control;
                break;

            case Type::Label:
                ImGui::AlignTextToFramePadding();
                ImGui::PushTextWrapPos(0.0f);
                if (control.isBound)
                    ImGui::TextUnformatted(TextOf(m_Values[control.name]).c_str());
                else
                    ColouredText(PanelColour::Muted, "%s", control.text.c_str());
                ImGui::PopTextWrapPos();
                break;
        }
        ImGui::EndDisabled();
        if (unanswered)
            ImGui::SetItemTooltip("The board does not answer this value: its firmware may not have it.");
    }

    void CustomMenuSection::DrawRange(const CustomControl& control)
    {
        const auto [lowest, highest] = RangeBounds(control, m_Ranges, m_Values);
        int value = RangeValueOf(control, m_Values[control.name]);

        std::string format = "%d";
        if (!control.unit.empty())
        {
            format += ' ';
            for (const char c : control.unit)
                format += c == '%' ? std::string("%%") : std::string(1, c);
        }
        ImGui::SetNextItemWidth(std::min(ImGui::GetFontSize() * 12.0f, ImGui::GetContentRegionAvail().x));
        const bool moved = ImGui::SliderInt("##range", &value, lowest, highest, format.c_str(), ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Ctrl+click to type a value");
        const bool letGo = ImGui::IsItemDeactivatedAfterEdit();

        if (moved)
        {
            // The sliders tied to it, pushed along with it.
            const std::map<std::string, int> changed = ResolveRange(m_Ranges, m_Values, control.name, value);
            std::vector<std::string>         names;
            for (const auto& [name, number] : changed)
                if (const CustomControl* range = Binding(name))
                {
                    m_Values[name] = RangeBytes(*range, number);
                    names.push_back(name);
                    if (name != control.name)
                        m_Pushed[name] = number;
                }
            if (!names.empty())
                Change(names, false);
        }
        if (letGo)
            Change({}, true);
    }

    void CustomMenuSection::DrawDropdown(const CustomControl& control)
    {
        const std::vector<uint8_t>& bytes = m_Values[control.name];
        const uint8_t               value = bytes.empty() ? 0 : bytes[0];

        std::vector<std::string> names;
        std::string              current = "value " + std::to_string(value);
        for (const auto& [name, option] : control.options)
        {
            names.push_back(name);
            if (option == value)
                current = name;
        }
        names.push_back(current);
        ImGui::SetNextItemWidth(std::min(ComboWidth(names), ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##dropdown", current.c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (size_t index = 0; index < control.options.size(); ++index)
            {
                const auto& [name, option] = control.options[index];
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(name.c_str(), option == value) && option != value)
                {
                    m_Values[control.name] = { option };
                    Change({ control.name }, true);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    Keycode CustomMenuSection::KeycodeOf(const CustomControl& control) const
    {
        const auto found = m_Values.find(control.name);
        if (found == m_Values.end() || found->second.size() < 2)
            return NamedKey{ "KC_NO" };
        const uint16_t raw = static_cast<uint16_t>((found->second[0] << 8) | found->second[1]);
        return DecodeQmkKeycode(raw, m_Keyboard.keycodeVersion);
    }

    void CustomMenuSection::SetKeycode(const CustomControl& control, const Keycode& keycode)
    {
        const std::optional<uint16_t> raw = EncodeQmkKeycode(keycode, m_Keyboard.keycodeVersion);
        if (!raw)
        {
            m_Message   = FormatKeycode(keycode) + " has no number on this board's keycodes";
            m_IsWarning = true;
            return;
        }
        m_Values[control.name] = { static_cast<uint8_t>(*raw >> 8), static_cast<uint8_t>(*raw & 0xFF) };
        Change({ control.name }, true);
    }

    void CustomMenuSection::DrawKey(const CustomControl& control)
    {
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
        const Keycode            keycode  = KeycodeOf(control);
        const bool               selected = m_Selected == &control;

        // The key as the picker draws it; a click selects it, again unselects it.
        const ImVec2 size = TileSize();
        const ImVec2 at   = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##key", size))
            m_Selected = selected ? nullptr : &control;
        KeycodeTile tile = TileOf(keycode, context, selected);
        tile.hovered     = ImGui::IsItemHovered();
        DrawKeycodeTile(tile, FaceBox{ at.x, at.y, at.x + size.x, at.y + size.y });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("%s", KeycodeHoverText(keycode, m_Keyboard.keycodeVersion).c_str());

        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (size.y - ImGui::GetTextLineHeight()) / 2);
        ColouredText(PanelColour::Muted, "%s", IsNothing(keycode) ? (selected ? "pick it below" : "")
                                                                   : KeycodeLabel(keycode, m_Keyboard.keycodeVersion).c_str());
    }

    void CustomMenuSection::DrawColour(const CustomControl& control)
    {
        std::vector<uint8_t>& value = m_Values[control.name];
        value.resize(2, 0);
        int  hue = value[0], saturation = value[1];
        bool letGoHue = false, letGoSaturation = false;

        ImGui::BeginGroup();
        const bool hueMoved = ColourSlider("##hue", hue, true, 0, letGoHue);
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "Hue");
        const bool saturationMoved = ColourSlider("##saturation", saturation, false, static_cast<uint8_t>(hue), letGoSaturation);
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "Saturation");
        ImGui::EndGroup();

        ImGui::SameLine();
        float r = 0, g = 0, b = 0;
        ImGui::ColorConvertHSVtoRGB(hue / 255.0f, saturation / 255.0f, 1.0f, r, g, b);
        ImGui::ColorButton("##colour", ImVec4(r, g, b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                           ImVec2(ImGui::GetFrameHeight() * 1.5f, ImGui::GetFrameHeight()));

        if (hueMoved || saturationMoved)
        {
            value = { static_cast<uint8_t>(hue), static_cast<uint8_t>(saturation) };
            Change({ control.name }, false);
        }
        if (letGoHue || letGoSaturation)
            Change({}, true);
    }

    void CustomMenuSection::DrawPicker(const CustomControl& control)
    {
        ColouredText(PanelColour::Muted, "Keymap's picker sets %s -- or click a key of the board, for what it sends.",
                     control.label.c_str());

        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
        const Keycode             current = KeycodeOf(control);
        const KeycodePickerEvents events  = DrawKeycodePicker(
            m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion,
                        current, &m_Keyboard, {} });
        if (events.picked)
            SetKeycode(control, *events.picked);
    }

    void CustomMenuSection::Change(const std::vector<std::string>& names, bool store)
    {
        m_Dirty.insert(names.begin(), names.end());
        if (store)
            m_StoreWanted = true;
    }

    // ------------------------------------------------------------------------------------------
    // Reading and writing.

    Task<void> CustomMenuSection::Read(std::vector<std::string> names)
    {
        m_ReadFailure.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            for (const std::string& name : names)
            {
                const CustomControl* control = Binding(name);
                if (!control)
                    continue;
                // A value the firmware refuses is said on its line; the others still show.
                try
                {
                    std::vector<uint8_t> value = co_await ReadCustomValue(via, *control);
                    if (m_Dirty.count(name) == 0)   // a change waiting keeps what the user sees
                        m_Values[name] = std::move(value);
                    m_Unanswered.erase(name);
                }
                catch (const ProtocolError&)
                {
                    m_Unanswered.insert(name);
                }
                m_Known.insert(name);
            }
        }
        catch (const std::exception& failure)
        {
            m_ReadFailure = std::string("the values could not be read: ") + failure.what();
        }
        m_Transport.Close(device);
    }

    Task<void> CustomMenuSection::Write(PendingWrite write)
    {
        if (write.store || write.button)
        {
            m_Message.clear();
            m_IsWarning = false;
        }
        m_IsWriting     = true;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            for (const auto& [control, value] : write.values)
            {
                co_await WriteCustomValue(via, *control, value);
                m_Unsaved.insert(control->channel);
            }

            if (write.store)
            {
                for (const uint8_t written : m_Unsaved)
                    co_await via.SaveCustomValues(written);
                m_Unsaved.clear();

                // Read back: what the board kept, said where it is not what was written.
                std::vector<std::string> kept;
                for (const auto& [control, value] : write.values)
                {
                    std::vector<uint8_t> read = co_await ReadCustomValue(via, *control);
                    if (m_Dirty.count(control->name) != 0)
                        continue;   // a newer change waiting is read back after it
                    std::vector<uint8_t> written = value;   // a toggle's states may differ in length
                    written.resize(read.size(), 0);
                    if (control->type != Type::Label && read != written)
                        kept.push_back(control->label);
                    m_Values[control->name] = std::move(read);
                }

                if (!kept.empty())
                {
                    std::vector<std::string> quoted;
                    for (const std::string& label : kept)
                        quoted.push_back("\"" + label + "\"");
                    m_Message   = "stored -- the board kept another value for " + JoinNames(quoted);
                    m_IsWarning = true;
                }
                else if (!m_Pushed.empty())
                {
                    std::vector<std::string> moved;
                    for (const auto& [name, number] : m_Pushed)
                        if (const CustomControl* range = Binding(name))
                            moved.push_back("\"" + range->label + "\" to " + std::to_string(number));
                    m_Message = "stored -- moved with it, as the board's rule keeps them apart: " + JoinNames(moved);
                }
                else
                    m_Message = "stored, read back the same";
                m_Pushed.clear();
            }

            // A button: sent and stored at once, as VIA sends it -- then what it changed, read again.
            if (write.button)
            {
                co_await WriteCustomValue(via, *write.button, write.button->on);
                co_await via.SaveCustomValues(write.button->channel);
                m_Message = "sent: " + write.button->label;
                m_IsStale = true;
            }
        }
        catch (const std::exception& failure)
        {
            // A button that restarts the board -- its bootloader, a factory reset -- leaves no one to
            // answer: said, not hidden.
            m_Message   = write.button ? "sent \"" + write.button->label + "\" -- the board did not answer after it: it may "
                                                                              "have restarted (" + failure.what() + ")"
                                       : std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
        m_IsWriting = false;
    }
}
