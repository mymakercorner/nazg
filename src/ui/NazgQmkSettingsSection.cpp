// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgQmkSettingsSection.h"

#include <algorithm>
#include <array>
#include <exception>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/vial/NazgVialProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr uint32_t c_FlowTapLongest = 500;   // the firmware stops there (action_tapping.c)

        // A named key's name, or empty.
        std::string_view NameOf(const Keycode& keycode)
        {
            const NamedKey* named = std::get_if<NamedKey>(&keycode);
            return named ? named->name : std::string_view();
        }

        bool StartsWith(std::string_view text, std::string_view start)
        {
            return text.substr(0, start.size()) == start;
        }

        // KC_A..KC_Z, KC_1..KC_0, and the symbols QMK's Auto Shift calls special.
        bool IsLetter(std::string_view name) { return name.size() == 4 && StartsWith(name, "KC_") && name[3] >= 'A' && name[3] <= 'Z'; }
        bool IsNumber(std::string_view name) { return name.size() == 4 && StartsWith(name, "KC_") && name[3] >= '0' && name[3] <= '9'; }
        bool IsSymbol(std::string_view name)
        {
            constexpr std::array<std::string_view, 12> c_Symbols{ "KC_MINS", "KC_EQL", "KC_LBRC", "KC_RBRC", "KC_BSLS", "KC_NUBS",
                                                                  "KC_SCLN", "KC_QUOT", "KC_GRV", "KC_COMM", "KC_DOT", "KC_SLSH" };
            return std::find(c_Symbols.begin(), c_Symbols.end(), name) != c_Symbols.end();
        }

        // QMK's Magic keycodes: keys that change qsid 21 too, and store it.
        bool IsMagicKey(std::string_view name)
        {
            for (std::string_view start : { "NK_", "CL_", "AG_", "GU_", "GE_", "BS_", "CG_", "EC_" })
                if (StartsWith(name, start))
                    return true;
            return false;
        }

        // A line's top and bottom, kept while it is drawn, to tell whether the pointer is over it.
        struct LineSpan
        {
            float top    = 0.0f;
            float bottom = 0.0f;

            void Grow() { bottom = std::max(bottom, ImGui::GetItemRectMax().y); }
        };

        LineSpan BeginLine()
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float top = ImGui::GetCursorScreenPos().y;
            return { top, top };
        }

        // What a line does, beside it, muted and wrapped.
        void Beside(LineSpan& span, const std::string& what)
        {
            ImGui::TableSetColumnIndex(1);
            if (what.empty())
                return;
            ImGui::AlignTextToFramePadding();
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "%s", what.c_str());
            ImGui::PopTextWrapPos();
            span.Grow();
        }

        bool IsOver(const LineSpan& span)
        {
            if (!ImGui::IsWindowHovered())
                return false;
            const ImVec2 mouse = ImGui::GetMousePos();
            return mouse.y >= span.top && mouse.y < span.bottom + ImGui::GetStyle().CellPadding.y;
        }

        // Every setting the firmware lists that Nazg knows, each at its own width.
        Task<QmkSettings> ReadSettings(vial::Protocol& vial)
        {
            QmkSettings settings;
            for (uint16_t qsid : co_await vial.ListQmkSettings())
                if (const size_t width = QmkSettingWidth(qsid); width != 0)
                    settings[qsid] = co_await vial.GetQmkSetting(qsid, width);
            co_return settings;
        }

        const char* GroupName(int group)
        {
            constexpr std::array<const char*, 8> c_Names{ "Tap-Hold", "Auto Shift", "One Shot Keys", "Combos",
                                                          "Grave Escape", "Magic", "Mouse keys", "Tap delays" };
            return c_Names[group];
        }

        const char* GroupWords(int group)
        {
            constexpr std::array<const char*, 8> c_Words{
                "keys doing one thing tapped, another held: mod-taps, layer-taps",
                "a key held a little longer is sent shifted",
                "a modifier or layer tapped once applies to the next key",
                "keys pressed together send another",
                "one key for both: Esc, or ` with Shift or GUI held",
                "swaps the firmware makes on every layer -- the keymap stays as it is",
                "keys moving the cursor and the wheel",
                "how long a tap the board sends by itself is held: a tap-hold key's, a tap dance's, a macro's",
            };
            return c_Words[group];
        }
    }

    QmkSettingsSection::QmkSettingsSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                           const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends)
    {
    }

    bool QmkSettingsSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    uint32_t QmkSettingsSection::Value(uint16_t qsid) const
    {
        const auto found = m_Settings.find(qsid);
        return found != m_Settings.end() ? found->second : 0;
    }

    std::vector<QmkSettingsSection::Group> QmkSettingsSection::Groups() const
    {
        using namespace QmkSetting;
        std::vector<Group> groups;
        if (Has(TappingTerm) || Has(Tapping) || Has(PermissiveHold) || Has(TappingToggle))
            groups.push_back(Group::TapHold);
        if (Has(AutoShift) || Has(AutoShiftTimeout))
            groups.push_back(Group::AutoShift);
        if (Has(OneShotTapToggle) || Has(OneShotTimeout))
            groups.push_back(Group::OneShot);
        if (Has(ComboTerm))
            groups.push_back(Group::Combos);
        if (Has(GraveEscape))
            groups.push_back(Group::GraveEscape);
        if (Has(Magic))
            groups.push_back(Group::Magic);
        if (Has(MouseDelay))
            groups.push_back(Group::Mouse);
        if (Has(TapCodeDelay) || Has(TapHoldCapsDelay))
            groups.push_back(Group::TapDelays);
        return groups;
    }

    QmkSettingsSection::Concern QmkSettingsSection::ConcernOf(Group group)
    {
        switch (group)
        {
        case Group::TapHold:     return Concern::TapHold;
        case Group::AutoShift:   return Concern::AutoShift;
        case Group::OneShot:     return Concern::OneShot;
        case Group::GraveEscape: return Concern::GraveEscape;
        case Group::Magic:       return Concern::Magic;
        case Group::Mouse:       return Concern::Mouse;
        case Group::TapDelays:   return Concern::TapHold;
        default:                 return Concern::None;
        }
    }

    std::function<bool(const Keycode&)> QmkSettingsSection::TestOf(Concern concern) const
    {
        switch (concern)
        {
        case Concern::TapHold:
            return [](const Keycode& keycode)
            {
                return std::holds_alternative<ModTapKey>(keycode) || std::holds_alternative<LayerTapKey>(keycode) ||
                       std::holds_alternative<SwapHandsTapKey>(keycode);
            };
        case Concern::TapToggle:
            return [](const Keycode& keycode)
            {
                const LayerKey* layer = std::get_if<LayerKey>(&keycode);
                return layer && layer->op == LayerOp::TapToggle;
            };
        case Concern::OneShot:
            return [](const Keycode& keycode)
            {
                const LayerKey* layer = std::get_if<LayerKey>(&keycode);
                return std::holds_alternative<OneShotModKey>(keycode) || (layer && layer->op == LayerOp::OneShot);
            };
        case Concern::GraveEscape:
            return [](const Keycode& keycode) { return NameOf(keycode) == "QK_GESC"; };
        case Concern::Mouse:
            return [](const Keycode& keycode) { return StartsWith(NameOf(keycode), "MS_"); };
        case Concern::CapsLock:
            return [](const Keycode& keycode)
            {
                const ModTapKey*   modTap   = std::get_if<ModTapKey>(&keycode);
                const LayerTapKey* layerTap = std::get_if<LayerTapKey>(&keycode);
                return NameOf(keycode) == "KC_CAPS" || (modTap && modTap->key == "KC_CAPS") ||
                       (layerTap && layerTap->key == "KC_CAPS");
            };
        case Concern::AutoShift:
        {
            using namespace QmkSetting;
            const bool letters = !Bit(AutoShift, AutoShiftBit::NoAlpha);
            const bool numbers = !Bit(AutoShift, AutoShiftBit::NoNumeric);
            const bool symbols = !Bit(AutoShift, AutoShiftBit::NoSpecial);
            return [=](const Keycode& keycode)
            {
                const std::string_view name = NameOf(keycode);
                return (letters && IsLetter(name)) || (numbers && IsNumber(name)) || (symbols && IsSymbol(name));
            };
        }
        case Concern::Magic:
        {
            // Under the pointer, one line: what its swap changes, or for N-key rollover the keys
            // switching it; otherwise what the swaps on change.
            if (m_HoveredMagicBit && *m_HoveredMagicBit == MagicBit::Nkro)
                return [](const Keycode& keycode) { return StartsWith(NameOf(keycode), "NK_"); };
            const uint32_t magic = m_HoveredMagicBit ? 1u << *m_HoveredMagicBit : Value(QmkSetting::Magic);
            return [magic](const Keycode& keycode)
            {
                const std::string_view name = NameOf(keycode);
                return !name.empty() && MagicKeycode(name, magic).has_value();
            };
        }
        default:
            return [](const Keycode&) { return false; };
        }
    }

    bool QmkSettingsSection::AnyKey(const std::function<bool(const Keycode&)>& test) const
    {
        for (const via::DefinitionKey& key : m_Keyboard.definition.keys)
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
                if (test(m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer))))
                    return true;
        return false;
    }

    bool QmkSettingsSection::IsIdle(Group group) const
    {
        switch (group)
        {
        case Group::TapHold:     return !AnyKey(TestOf(Concern::TapHold)) && !AnyKey(TestOf(Concern::TapToggle));
        case Group::AutoShift:   return !Bit(QmkSetting::AutoShift, AutoShiftBit::Enable);
        case Group::OneShot:     return !AnyKey(TestOf(Concern::OneShot));
        case Group::Combos:      return m_Keyboard.report.comboCount == 0;
        case Group::GraveEscape: return !AnyKey(TestOf(Concern::GraveEscape));
        case Group::Mouse:       return !AnyKey(TestOf(Concern::Mouse));
        default:                 return false;
        }
    }

    // ------------------------------------------------------------------------------------------
    // The strip and the board.

    Strip QmkSettingsSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Group";
        if (!m_IsLoaded)
            return strip;
        const std::vector<Group> groups = Groups();
        for (size_t index = 0; index < groups.size(); ++index)
        {
            strip.entries.push_back(GroupName(static_cast<int>(groups[index])));
            if (groups[index] == m_Group)
                strip.chosen = index;
        }
        return strip;
    }

    void QmkSettingsSection::OnStripChosen(size_t entry)
    {
        const std::vector<Group> groups = Groups();
        if (entry < groups.size())
            m_Group = groups[entry];
        m_ConfirmReset = false;
    }

    void QmkSettingsSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
        if (!m_IsLoaded)
            return;

        const Concern concern = m_Hovered != Concern::None ? m_Hovered : ConcernOf(m_Group);
        if (concern == Concern::None)
            return;

        // Chordal Hold: every key by QMK's guess of its hand, the left lit one way, the right the
        // other, a key of either hand not lit.
        if (concern == Concern::Hands)
        {
            std::vector<via::DefinitionKey> keys;
            std::vector<size_t>             indices;
            for (size_t index = 0; index < board.keys.size(); ++index)
                if (!board.keys[index].geometry.decal)
                {
                    keys.push_back(board.keys[index].geometry);
                    indices.push_back(index);
                }
            const std::vector<char> hands = GuessHands(keys);
            for (size_t at = 0; at < indices.size(); ++at)
                board.keys[indices[at]].marks |= hands[at] == 'L' ? Mark::Highlighted : hands[at] == 'R' ? Mark::HighlightedSecond : 0;
            return;
        }

        // The keys concerned, on the first layer that has them -- that layer's legend and "L1" when
        // it is not the base -- and for Magic a tag under each saying what it sends now.
        const auto               test   = TestOf(concern);
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
        const uint32_t magic = m_HoveredMagicBit ? 1u << *m_HoveredMagicBit : Value(QmkSetting::Magic);
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            BoardKey& key = board.keys[index];
            if (key.geometry.decal)
                continue;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                if (!test(keycode))
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
                if (concern == Concern::Magic)
                    if (const auto sent = MagicKeycode(NameOf(keycode), magic))
                    {
                        const std::string words = *sent == "KC_NO" ? std::string("nothing")
                                                                   : TagText(LegendFor(NamedKey{ *sent }, context), std::string(*sent));
                        board.tags.push_back({ { index }, "\xE2\x86\x92 " + words });
                    }
                break;
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void QmkSettingsSection::DrawPanel()
    {
        m_HoveredNow = Concern::None;
        m_HoveredMagicBitNow.reset();

        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("QMK settings", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }

        // The combo term is the Combos section's too: whichever wrote last.
        if (m_Keyboard.comboTerm && Has(QmkSetting::ComboTerm))
            m_Settings[QmkSetting::ComboTerm] = *m_Keyboard.comboTerm;

        const std::vector<Group> groups = Groups();
        if (groups.empty())
        {
            ImGui::TextDisabled("This firmware lists no QMK setting Nazg knows.");
            return;
        }
        if (std::find(groups.begin(), groups.end(), m_Group) == groups.end())
            m_Group = groups.front();

        DrawGroupLine(m_Group);
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawGroup(m_Group);
        ImGui::EndDisabled();
        DrawSays(m_Group);

        m_Hovered         = m_HoveredNow;
        m_HoveredMagicBit = m_HoveredMagicBitNow;
    }

    void QmkSettingsSection::DrawGroupLine(Group group)
    {
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(GroupName(static_cast<int>(group)));
        ImGui::PopFont();
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "%s", GroupWords(static_cast<int>(group)));
        ImGui::SameLine();
        if (IsBusy())
            ColouredText(PanelColour::Muted, "Writing...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        else
            ColouredText(PanelColour::Muted, "Each change is written at once, and read back.");

        ImGui::SameLine();
        const float width = ImGui::CalcTextSize("Reset all...").x + 2 * ImGui::GetStyle().FramePadding.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
        ImGui::BeginDisabled(IsBusy());
        if (ImGui::Button("Reset all..."))
            m_ConfirmReset = true;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Every QMK setting back to the firmware's own defaults");

        if (m_ConfirmReset)
        {
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Warning, "Every QMK setting back to this firmware's defaults -- Magic and N-key rollover "
                                               "too. Nazg cannot know the defaults before: they are read back after.");
            ImGui::SameLine();
            // A write started since "Reset all..." was clicked must end first: its Task still runs.
            ImGui::BeginDisabled(IsBusy());
            if (ImGui::Button("Reset all"))
            {
                m_ConfirmReset = false;
                m_Request      = ResetAll();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                m_ConfirmReset = false;
        }
    }

    void QmkSettingsSection::DrawGroup(Group group)
    {
        using namespace QmkSetting;
        if (group == Group::Mouse)
        {
            MouseLines();
            return;
        }
        if (group == Group::Magic)
        {
            MagicLines();
            return;
        }

        if (!ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("setting", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
        switch (group)
        {
        case Group::TapHold:
            if (Has(TappingTerm))
                NumberLine(TappingTerm, "Tapping Term", "ms", 10000, "held longer than this, a tap-hold key holds",
                           Concern::TapHold);
            if (Has(Tapping) || Has(PermissiveHold) || Has(HoldOnOtherKey))
                HoldLine();
            if (Has(ChordalHold))
                BitLine(ChordalHold, 0, false, "Chordal Hold", "a key of the same hand pressed with it makes it a tap: only "
                        "the other hand can make it hold early", Concern::Hands);
            if (Has(RetroTapping))
                BitLine(RetroTapping, 0, false, "Retro Tapping", "held past the term and released with no other key "
                        "pressed, the tap is sent anyway", Concern::TapHold);
            if (Has(Tapping))
            {
                BitLine(Tapping, TappingBit::RetroTapping, false, "Retro Tapping", "held past the term and released with "
                        "no other key pressed, the tap is sent anyway", Concern::TapHold);
                BitLine(Tapping, TappingBit::NoQuickTap, false, "Tapping Force Hold", "tapped, then held again within the "
                        "term: on, it holds; off, the tap repeats", Concern::TapHold);
            }
            if (Has(QuickTapTerm))
                NumberLine(QuickTapTerm, "Quick Tap Term", "ms", 10000, "tapped, then held again within this, the tap "
                           "repeats instead of holding; 0: never", Concern::TapHold);
            if (Has(FlowTap))
                NumberLine(FlowTap, "Flow Tap", "ms", c_FlowTapLongest, "pressed this soon after a letter, space or , . ; / "
                           "-- typing fast -- a tap-hold key taps at once; 0: off", Concern::TapHold);
            if (Has(TappingToggle))
                NumberLine(TappingToggle, "Tapping Toggle", "taps", 100, "a TT layer key tapped this many times keeps its "
                           "layer on", Concern::TapToggle);
            break;
        case Group::AutoShift:
            if (Has(AutoShift))
                BitLine(AutoShift, AutoShiftBit::Enable, false, "Enable", "", Concern::AutoShift);
            if (Has(AutoShiftTimeout))
                NumberLine(AutoShiftTimeout, "Timeout", "ms", 1000, "held longer than this, the key is sent shifted",
                           Concern::AutoShift);
            if (Has(AutoShift))
            {
                BitLine(AutoShift, AutoShiftBit::NoAlpha, false, "Do not Auto Shift alpha characters", "", Concern::AutoShift);
                BitLine(AutoShift, AutoShiftBit::NoNumeric, false, "Do not Auto Shift numeric keys", "", Concern::AutoShift);
                BitLine(AutoShift, AutoShiftBit::NoSpecial, false, "Do not Auto Shift special keys", "- = [ ] \\ ; ' ` , . /",
                        Concern::AutoShift);
                BitLine(AutoShift, AutoShiftBit::Modifiers, false, "Enable for modifiers", "otherwise a key pressed with Ctrl, "
                        "Alt or GUI held is sent as it is", Concern::AutoShift);
                BitLine(AutoShift, AutoShiftBit::Repeat, false, "Enable keyrepeat", "held on, the key repeats", Concern::AutoShift);
                BitLine(AutoShift, AutoShiftBit::NoAutoRepeat, false, "Disable keyrepeat when timeout is exceeded",
                        "but not once it is shifted", Concern::AutoShift, Bit(AutoShift, AutoShiftBit::Repeat));
            }
            break;
        case Group::OneShot:
            if (Has(OneShotTimeout))
                NumberLine(OneShotTimeout, "One Shot Timeout", "ms", 60000, "unused this long, a one-shot key lets go; "
                           "0: never", Concern::OneShot);
            if (Has(OneShotTapToggle))
                NumberLine(OneShotTapToggle, "One Shot Tap Toggle", "taps", 50, "tapped this many times, it stays on until "
                           "tapped again; 0 or 1: never", Concern::OneShot);
            break;
        case Group::Combos:
            NumberLine(ComboTerm, "Combo Term", "ms", 10000, "each key of a combo within this of the one before -- the "
                       "term the Combos section shows", Concern::None);
            break;
        case Group::GraveEscape:
            BitLine(GraveEscape, 3, false, "Always send Escape if Shift is pressed", "", Concern::GraveEscape);
            BitLine(GraveEscape, 2, false, "Always send Escape if GUI is pressed", "", Concern::GraveEscape);
            BitLine(GraveEscape, 1, false, "Always send Escape if Control is pressed", "Ctrl+Shift+Esc on Windows",
                    Concern::GraveEscape);
            BitLine(GraveEscape, 0, false, "Always send Escape if Alt is pressed", "Cmd+Opt+Esc on macOS", Concern::GraveEscape);
            break;
        case Group::TapDelays:
            if (Has(TapCodeDelay))
                NumberLine(TapCodeDelay, "Tap Code Delay", "ms", 1000, "between the press and the release of a tap the board "
                           "sends", Concern::TapHold);
            if (Has(TapHoldCapsDelay))
                NumberLine(TapHoldCapsDelay, "Tap Hold Caps Delay", "ms", 1000, "the same for Caps Lock -- macOS ignores a "
                           "short one", Concern::CapsLock);
            break;
        default:
            break;
        }
        ImGui::EndTable();
    }

    void QmkSettingsSection::Hovering(Concern concern, std::optional<int> magicBit)
    {
        m_HoveredNow         = concern;
        m_HoveredMagicBitNow = magicBit;
    }

    void QmkSettingsSection::NumberBox(uint16_t qsid, uint32_t most, const std::string& name)
    {
        ImGui::PushID(qsid);
        const auto typing = m_Typing.find(qsid);
        int        value  = static_cast<int>(typing != m_Typing.end() ? typing->second : Value(qsid));
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000").x + 2 * ImGui::GetStyle().FramePadding.x);
        if (ImGui::InputInt("##number", &value, 0, 0))
            m_Typing[qsid] = static_cast<uint32_t>(std::clamp<int>(value, 0, static_cast<int>(most)));
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            if (const auto edited = m_Typing.find(qsid); edited != m_Typing.end())
            {
                uint32_t wanted = edited->second;
                m_Typing.erase(edited);
                // The firmware keeps the mouse keys' starting delays in tens of ms.
                if (qsid == QmkSetting::MouseDelay || qsid == QmkSetting::WheelDelay)
                    wanted = (wanted + 5) / 10 * 10;
                if (wanted != Value(qsid) && !IsBusy())
                    m_Request = Write({ { qsid, wanted } }, name);
            }
        }
        else if (!ImGui::IsItemActive())
            m_Typing.erase(qsid);
        ImGui::PopID();
    }

    void QmkSettingsSection::NumberLine(uint16_t qsid, const char* name, const char* unit, uint32_t most, const char* what,
                                        Concern concern)
    {
        LineSpan span = BeginLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::SameLine();
        NumberBox(qsid, std::max(most, Value(qsid)), name);
        span.Grow();
        ImGui::SameLine();
        ImGui::TextUnformatted(unit);
        Beside(span, what);
        if (IsOver(span))
            Hovering(concern);
    }

    void QmkSettingsSection::BitLine(uint16_t qsid, int bit, bool inverted, const char* name, const char* what,
                                     Concern concern, bool enabled)
    {
        LineSpan span = BeginLine();
        ImGui::PushID(qsid * 32 + bit);
        bool on = Bit(qsid, bit) != inverted;
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Checkbox(name, &on) && !IsBusy())
            m_Request = Write({ { qsid, WithBit(Value(qsid), bit, on != inverted) } }, name);
        ImGui::EndDisabled();
        ImGui::PopID();
        span.Grow();
        Beside(span, what);
        if (IsOver(span))
            Hovering(concern);
    }

    // QMK's two settings, Permissive Hold and Hold On Other Key Press, as the one choice of three
    // they are (ui-design.md): both on, the second decides first.
    void QmkSettingsSection::HoldLine()
    {
        struct Way
        {
            HoldDecision decision;
            const char*  name;
            const char*  what;
        };
        constexpr std::array<Way, 3> c_Ways{ {
            { HoldDecision::WaitForTerm, "Tapping Term only", "another key pressed while a tap-hold key is down changes "
                                                              "nothing: it holds only once the term has passed" },
            { HoldDecision::Permissive, "Permissive Hold", "another key pressed and released while a tap-hold key is down "
                                                           "makes it hold" },
            { HoldDecision::OtherKeyPress, "Hold On Other Key Press", "another key pressed while a tap-hold key is down "
                                                                      "makes it hold, at that press" },
        } };
        const HoldDecision chosen = HoldDecisionOf(m_Settings);
        const Way*         way    = &c_Ways[0];
        for (const Way& each : c_Ways)
            if (each.decision == chosen)
                way = &each;

        LineSpan span = BeginLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Hold decided by");
        ImGui::SameLine();
        float longest = 0.0f;
        for (const Way& each : c_Ways)
            longest = std::max(longest, ImGui::CalcTextSize(each.name).x);
        ImGui::SetNextItemWidth(longest + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##hold", way->name))
        {
            for (const Way& each : c_Ways)
            {
                if (ImGui::Selectable(each.name, each.decision == chosen) && each.decision != chosen && !IsBusy())
                    m_Request = Write(HoldDecisionWrites(m_Settings, each.decision), each.name);
                ImGui::SetItemTooltip("%s", each.what);
            }
            ImGui::EndCombo();
        }
        span.Grow();
        std::string what = way->what;
        if (Has(QmkSetting::Tapping))
            what += " -- this firmware stores Hold On Other Key Press turned round, as \"Ignore Mod Tap Interrupt\"";
        Beside(span, what);
        if (IsOver(span))
            Hovering(Concern::TapHold);
    }

    void QmkSettingsSection::MagicLines()
    {
        struct Swap
        {
            int         bit;
            const char* name;
        };
        // vial-gui's names; N-key rollover apart, last.
        constexpr std::array<Swap, 9> c_Swaps{ {
            { MagicBit::SwapControlCapsLock, "Swap Caps Lock and Left Control" },
            { MagicBit::CapsLockToControl, "Treat Caps Lock as Control" },
            { MagicBit::SwapLeftAltGui, "Swap Left Alt and GUI" },
            { MagicBit::SwapRightAltGui, "Swap Right Alt and GUI" },
            { MagicBit::SwapLeftControlGui, "Swap Left Control and GUI" },
            { MagicBit::SwapRightControlGui, "Swap Right Control and GUI" },
            { MagicBit::NoGui, "Disable the GUI keys" },
            { MagicBit::SwapGraveEscape, "Swap ` and Escape" },
            { MagicBit::SwapBackslashBksp, "Swap \\ and Backspace" },
        } };

        const auto box = [this](int bit, const char* name)
        {
            const uint32_t magic = Value(QmkSetting::Magic);
            bool           on    = ((magic >> bit) & 1) != 0;
            ImGui::PushID(bit);
            if (ImGui::Checkbox(name, &on) && !IsBusy())
                m_Request = Write({ { QmkSetting::Magic, WithBit(magic, bit, on) } }, name);
            ImGui::PopID();
            if (ImGui::IsItemHovered())
                Hovering(Concern::Magic, bit);
        };

        if (ImGui::BeginTable("swaps", 2, ImGuiTableFlags_SizingStretchSame))
        {
            const size_t half = (c_Swaps.size() + 1) / 2;
            for (size_t row = 0; row < half; ++row)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                box(c_Swaps[row].bit, c_Swaps[row].name);
                if (row + half < c_Swaps.size())
                {
                    ImGui::TableSetColumnIndex(1);
                    box(c_Swaps[row + half].bit, c_Swaps[row + half].name);
                }
            }
            ImGui::EndTable();
        }
        box(MagicBit::Nkro, "Enable N-key rollover");
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "every key held is reported, rather than six at most");
    }

    void QmkSettingsSection::MouseLines()
    {
        using namespace QmkSetting;
        struct Row
        {
            const char* name;
            const char* unit;
            uint16_t    cursor;
            uint16_t    wheel;   // 0: none
        };
        constexpr std::array<Row, 5> c_Rows{ {
            { "Delay", "ms", MouseDelay, WheelDelay },
            { "Interval", "ms", MouseInterval, WheelInterval },
            { "Move Delta", "px", MouseMoveDelta, 0 },
            { "Max Speed", "steps", MouseMaxSpeed, WheelMaxSpeed },
            { "Time To Max", "moves", MouseTimeToMax, WheelTimeToMax },
        } };
        constexpr std::array<const char*, 5> c_What{ "before the first move -- in tens of ms", "between two moves",
                                                     "one move of the cursor", "the fastest, in steps -- the cursor's of "
                                                     "Move Delta", "moves until the fastest" };

        if (!ImGui::BeginTable("mouse", 4, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cursor", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Wheel", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ColouredText(PanelColour::Muted, "Cursor");
        ImGui::TableSetColumnIndex(2);
        ColouredText(PanelColour::Muted, "Wheel");
        for (size_t index = 0; index < c_Rows.size(); ++index)
        {
            const Row& row  = c_Rows[index];
            LineSpan   span = BeginLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(row.name);
            for (int column = 1; column <= 2; ++column)
            {
                const uint16_t qsid = column == 1 ? row.cursor : row.wheel;
                ImGui::TableSetColumnIndex(column);
                if (qsid == 0 || !Has(qsid))
                    continue;
                NumberBox(qsid, 10000, std::string(column == 1 ? "Mouse Key " : "Mouse Wheel ") + row.name);
                span.Grow();
                ImGui::SameLine();
                ImGui::TextUnformatted(row.unit);
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s", c_What[index]);
            span.Grow();
            if (IsOver(span))
                Hovering(Concern::Mouse);
        }
        ImGui::EndTable();
    }

    // What is wrong with the group's settings, or worth knowing -- what Vial never says.
    void QmkSettingsSection::DrawSays(Group group)
    {
        using namespace QmkSetting;
        std::vector<std::pair<bool, std::string>> says;   // warning, words
        const auto none = [&](const char* what)
        {
            says.push_back({ false, std::string("No key on this board ") + what + ": these settings change nothing until one "
                                    "does." });
        };
        switch (group)
        {
        case Group::TapHold:
            if (IsIdle(group))
                none("is a tap-hold key");
            if (m_Keyboard.report.tapDanceCount == 0 && Has(TappingTerm))
                says.push_back({ true, "This firmware was built without tap dance, and vial-qmk then takes the tapping term "
                                       "from its build: Tapping Term changes nothing." });
            if (Has(QuickTapTerm) && Value(QuickTapTerm) > Value(TappingTerm))
                says.push_back({ false, "A Quick Tap Term above the Tapping Term counts as the Tapping Term, " +
                                            std::to_string(Value(TappingTerm)) + " ms." });
            if (Has(FlowTap) && Value(FlowTap) > c_FlowTapLongest)
                says.push_back({ true, "The firmware stops Flow Tap at 500 ms." });
            if (Has(Tapping))
                says.push_back({ false, "This firmware predates Chordal Hold, Flow Tap and a Quick Tap Term of its own: "
                                        "vial-qmk added them in 2025-06." });
            if (m_Keyboard.report.tapDanceCount != 0)
                says.push_back({ false, "A tap dance uses its own term, set in Tap Dance." });
            break;
        case Group::AutoShift:
            if (IsIdle(group))
                says.push_back({ false, "Off: nothing else here applies." });
            if (AnyKey([](const Keycode& keycode) { return StartsWith(NameOf(keycode), "AS_"); }))
                says.push_back({ false, "The board's Auto Shift keys switch it for the moment, while it is enabled here." });
            break;
        case Group::OneShot:
            if (IsIdle(group))
                none("is a one-shot key");
            break;
        case Group::Combos:
            if (IsIdle(group))
                says.push_back({ false, "This board has no combo slots." });
            else
                says.push_back({ false, "One term for every combo." });
            break;
        case Group::GraveEscape:
            if (IsIdle(group))
                none("is Grave Escape");
            if (Bit(GraveEscape, 3) && Bit(GraveEscape, 2))
                says.push_back({ true, "With Shift and GUI both, ` is never sent: Grave Escape is only Esc." });
            break;
        case Group::Magic:
            if (AnyKey([](const Keycode& keycode) { return IsMagicKey(NameOf(keycode)); }))
                says.push_back({ false, "Keys on this board change these too, and store them: what is shown is what the "
                                        "board said when this section was first opened." });
            says.push_back({ false, "N-key rollover works only in a firmware built with it; the board cannot say." });
            break;
        case Group::Mouse:
            if (IsIdle(group))
                none("moves the mouse");
            break;
        default:
            break;
        }

        if (says.empty())
            return;
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        for (const auto& [warning, words] : says)
            ColouredText(warning ? PanelColour::Warning : PanelColour::Muted, "%s", words.c_str());
        ImGui::PopTextWrapPos();
    }

    // ------------------------------------------------------------------------------------------
    // Reading and writing.

    Task<void> QmkSettingsSection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            vial::Protocol   vial(channel);

            m_Settings = co_await ReadSettings(vial);
            if (Has(QmkSetting::ComboTerm))
                m_Keyboard.comboTerm = static_cast<uint16_t>(Value(QmkSetting::ComboTerm));
            m_IsLoaded = true;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the QMK settings could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    Task<void> QmkSettingsSection::Write(QmkSettings writes, std::string what)
    {
        m_Message.clear();
        m_IsWarning = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            vial::Protocol   vial(channel);

            bool kept = true;
            for (const auto& [qsid, value] : writes)
            {
                co_await vial.SetQmkSetting(qsid, value);
                const uint32_t stored = co_await vial.GetQmkSetting(qsid, QmkSettingWidth(qsid));
                m_Settings[qsid]      = stored;
                if (qsid == QmkSetting::ComboTerm)
                    m_Keyboard.comboTerm = static_cast<uint16_t>(stored);
                kept = kept && stored == value;
            }
            if (kept)
                m_Message = "stored " + what + ", read back the same";
            else
            {
                m_Message   = "the board kept another value for " + what;
                m_IsWarning = true;
            }
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    Task<void> QmkSettingsSection::ResetAll()
    {
        m_Message.clear();
        m_IsWarning = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            vial::Protocol   vial(channel);

            co_await vial.ResetQmkSettings();
            m_Settings = co_await ReadSettings(vial);
            if (Has(QmkSetting::ComboTerm))
                m_Keyboard.comboTerm = static_cast<uint16_t>(Value(QmkSetting::ComboTerm));
            m_Message = "reset, and read back: these are the firmware's defaults";
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("reset failed: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }
}
