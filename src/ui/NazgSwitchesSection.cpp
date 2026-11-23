// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSwitchesSection.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <exception>
#include <iterator>
#include <utility>

#include "imgui.h"

#include "adapters/NazgProtocolError.h"
#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "model/NazgHostKeys.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr double c_PollInterval  = 0.020;   // seconds: the switches, as the matrix view reads them
        constexpr double c_InUseInterval = 1.0;     // the profile in use, switched from the keyboard

        // Without a curve to go by, the source's full travel: 4.0 mm.
        constexpr uint8_t c_SourceFullTravel = 40;

        // The sensitivities' range, 0.1 to 2.0 mm (ui-design.md, "The Switches section": the firmware
        // takes up to 3.9, the Launcher's own range is unknown).
        constexpr uint8_t c_SensitivityMax = 20;

        // A regular key releases 0.3 mm above where it pressed (action_regular_trigger.c): fixed.
        constexpr uint8_t c_ReleaseGap = 3;

        // Readings at the firmware's clamp in a row before full travel counts as measured.
        constexpr int c_ClampReadingsToMeasure = 3;

        // How long what was written stays at the strip's end, in seconds.
        constexpr double c_MessageTime = 5.0;

        // Rapid Trigger's mark on a key: released and pressed again as it goes up and down, anywhere on
        // its way. Not RT, which is the gamepad's right trigger (Rico, 2026-11-23).
        constexpr const char* c_RapidTriggerMark = "\xE2\x87\x85";

        // The Launcher's names for One Key Multiple Commands' points, in keychron::DksPoint's order.
        constexpr const char* c_DksPointNames[] = { "Starting Trigger", "Bottom In", "Bottom Out", "Bounce Back" };

        // The Launcher's names where the board's name field is empty -- every profile read so far.
        constexpr const char* c_LauncherNames[] = { "Default", "Gaming", "Gamepad" };

        enum Field
        {
            Actuation = 0,
            Press     = 1,
            Release   = 2,
        };

        // "1.7", from 0.1 mm.
        std::string Millimetres(int tenths)
        {
            return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
        }

        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
        }

        // The travel read, in mm with two decimals, from the firmware's 1/60 mm -- "1.38" where the
        // 0.1 mm field floors to "1.3", which made a 0.3 mm release gap look like 0.4 (Rico,
        // 2026-11-08). Never past full travel: the 1/60 field pins beyond it at the bottom.
        std::string FineMillimetres(const keychron::RealtimeTravel& travel, uint8_t full)
        {
            const int sixtieths = std::min<int>(travel.travelSixtieths, full * 6);
            const int hundredths = (sixtieths * 100 + 30) / 60;
            const int rest       = hundredths % 100;
            return std::to_string(hundredths / 100) + "." + (rest < 10 ? "0" : "") + std::to_string(rest);
        }

        // The travel bar's parts, in font sizes: its scale's labels, the bar, then the gap before the
        // words at its marks.
        constexpr float c_BarScale = 2.0f;
        constexpr float c_BarWidth = 1.6f;
        constexpr float c_BarGap   = 0.5f;

        // The bar with the widest words its marks can carry.
        float TravelBarWidth()
        {
            const float unit = ImGui::GetFontSize();
            return unit * (c_BarScale + c_BarWidth + c_BarGap) +
                   std::max(ImGui::CalcTextSize("pressed 0.0").x, ImGui::CalcTextSize("released above 0.0").x);
        }

        // The raw values' table: its widest name, its widest number, a cell's padding each.
        float RawTableWidth()
        {
            return ImGui::CalcTextSize("At the bottom").x + ImGui::CalcTextSize("00000").x +
                   ImGui::GetStyle().CellPadding.x * 4;
        }

        uint8_t ReleasePoint(uint8_t actuation)
        {
            return actuation > c_ReleaseGap ? static_cast<uint8_t>(actuation - c_ReleaseGap) : uint8_t{ 0 };
        }
    }

    SwitchesSection::SwitchesSection(HidTransport& transport, std::string path, const Keyboard& keyboard,
                                     const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends)
    {
    }

    bool SwitchesSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool SwitchesSection::IsPolling() const
    {
        return IsBusy() && m_RequestIsPoll;
    }

    bool SwitchesSection::HasGamepadKeys() const
    {
        const keychron::Profile* profile = Shown();
        return profile && std::any_of(profile->keys.begin(), profile->keys.end(), [](const keychron::KeyConfig& key)
                                      { return key.advancedMode == keychron::AdvancedMode::Gamepad; });
    }

    bool SwitchesSection::WantsGameController() const
    {
        return m_Testing && m_Link == Link::Open && HasGamepadKeys();
    }

    void SwitchesSection::OnGameController(const GameControllerReading& reading)
    {
        m_Controller = reading;
        if (!reading.isSeen)
        {
            m_Levels.reset();
            return;
        }
        m_Levels = keychron::GamepadLevelsOf(reading);
        m_Trail.Add(*m_Levels);
    }

    void SwitchesSection::WhileHidden()
    {
        m_HiddenThisFrame = true;
        m_ResetAsked      = false;
        m_Testing         = false;
        m_PickCommand.reset();
        m_GamepadOpen = false;
        m_HoveredControl.reset();
        m_HoveredKey.reset();
        m_AddingPair = false;
        m_PairFirst.reset();
        m_Levels.reset();
        m_DraggedField.reset();
        if (IsBusy())
            return;

        // A change asked before leaving is still written: nothing closes the board while it is busy.
        if (m_Link == Link::Open && !m_PendingChanges.empty())
        {
            PendingChange change = std::move(m_PendingChanges.front());
            m_PendingChanges.erase(m_PendingChanges.begin());
            m_RequestIsPoll = false;
            m_Request       = Write(std::move(change));
            return;
        }

        // Read again when shown: the Launcher, or the keyboard, may change it meanwhile.
        m_Channel.reset();
        m_Link = Link::Closed;
        m_PendingSelect.reset();
        m_PendingReset.reset();
        m_PendingMode.reset();
    }

    void SwitchesSection::Say(std::string message, bool isWarning)
    {
        m_Message   = std::move(message);
        m_IsWarning = isWarning;
        m_MessageAt = ImGui::GetTime();
    }

    void SwitchesSection::Fail(std::string message)
    {
        m_Channel.reset();
        m_Link      = Link::Failed;
        m_Message   = std::move(message);
        m_IsWarning = true;
    }

    // ------------------------------------------------------------------------------------------
    // The keys.

    void SwitchesSection::FindKeys(const BoardDescription& board)
    {
        m_KeysSelection = m_Keyboard.layoutSelection;
        m_KeysNameLayer = m_NameLayer;

        std::vector<Key> keys;
        m_BoardKeys.assign(board.keys.size(), std::nullopt);
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            const via::DefinitionKey& geometry = board.keys[index].geometry;
            if (!geometry.IsSwitch() || geometry.row >= m_Keyboard.definition.matrixRows ||
                geometry.column >= m_Keyboard.definition.matrixColumns)
                continue;

            const keychron::KeyPosition position{ geometry.row, geometry.column };
            const auto same = std::find_if(keys.begin(), keys.end(), [&](const Key& key) { return key.position == position; });
            if (same != keys.end())
            {
                m_BoardKeys[index] = static_cast<size_t>(same - keys.begin());
                continue;
            }

            Key key;
            key.position = position;
            key.geometry = geometry;
            if (const auto* legend = std::get_if<KeycapLegend>(&board.keys[index].legends))
                key.name = CaptionOf(*legend);
            if (key.name.empty())
                key.name = "R" + std::to_string(position.row) + " C" + std::to_string(position.column);
            m_BoardKeys[index] = keys.size();
            keys.push_back(std::move(key));
        }

        // A selection survives only the same keys.
        if (keys.size() != m_Keys.size())
        {
            m_Selected.assign(keys.size(), false);
            m_WasPressed.assign(keys.size(), false);
            m_Followed.reset();
        }
        m_Keys = std::move(keys);
    }

    std::optional<size_t> SwitchesSection::KeyAt(keychron::KeyPosition position) const
    {
        for (size_t key = 0; key < m_Keys.size(); ++key)
            if (m_Keys[key].position == position)
                return key;
        return std::nullopt;
    }

    // The selection; with none, every key but the advanced ones.
    std::vector<size_t> SwitchesSection::Targets() const
    {
        std::vector<size_t> targets;
        for (size_t key = 0; key < m_Selected.size(); ++key)
            if (m_Selected[key])
                targets.push_back(key);
        if (targets.empty())
            for (size_t key = 0; key < m_Keys.size(); ++key)
                if (!Shown() || ConfigOf(key).advancedMode == keychron::AdvancedMode::None)
                    targets.push_back(key);
        return targets;
    }

    const keychron::Profile* SwitchesSection::Shown() const
    {
        return m_ShownProfile < m_Profiles.size() ? &m_Profiles[m_ShownProfile] : nullptr;
    }

    keychron::KeyConfig SwitchesSection::ConfigOf(size_t key) const
    {
        return keychron::EffectiveConfig(*Shown(), m_Keys[key].position, m_Keyboard.definition.matrixColumns);
    }

    std::string SwitchesSection::ProfileName(size_t profile) const
    {
        if (profile < m_Profiles.size() && !m_Profiles[profile].name.empty())
            return m_Profiles[profile].name;
        if (profile < std::size(c_LauncherNames))
            return c_LauncherNames[profile];
        return "Profile " + std::to_string(profile + 1);
    }

    // ------------------------------------------------------------------------------------------
    // What a key does, in a word, a mark and a sentence.

    SwitchesSection::Mode SwitchesSection::ModeOf(const keychron::KeyConfig& config)
    {
        switch (config.advancedMode)
        {
        case keychron::AdvancedMode::None:
            return config.mode == keychron::KeyMode::RapidTrigger ? Mode::RapidTrigger : Mode::Regular;
        case keychron::AdvancedMode::Dks: return Mode::Dks;
        case keychron::AdvancedMode::Toggle: return Mode::LongPress;
        case keychron::AdvancedMode::Gamepad: return Mode::Gamepad;
        }
        return Mode::Unknown;
    }

    // Whether its Actuation Distance counts: not One Key Multiple Commands, which has points of its
    // own, nor a gamepad's half-axis, which follows the travel.
    bool SwitchesSection::UsesActuation(const keychron::KeyConfig& config)
    {
        switch (ModeOf(config))
        {
        case Mode::Regular:
        case Mode::RapidTrigger:
        case Mode::LongPress: return true;
        case Mode::Gamepad: return keychron::IsGamepadButton(config.advancedData);
        case Mode::Dks:
        case Mode::Unknown: break;
        }
        return false;
    }

    // Two keys doing the same: the mode, the Actuation Distance where it counts, and the two distances
    // only in Rapid Trigger -- a Regular key's do nothing.
    bool SwitchesSection::DoSame(const keychron::KeyConfig& a, const keychron::KeyConfig& b)
    {
        if (ModeOf(a) != ModeOf(b) || a.advancedMode != b.advancedMode)
            return false;
        if (UsesActuation(a) && a.actuationPoint != b.actuationPoint)
            return false;
        if (a.advancedMode != keychron::AdvancedMode::None && a.advancedData != b.advancedData)
            return false;
        return ModeOf(a) != Mode::RapidTrigger ||
               (a.pressSensitivity == b.pressSensitivity && a.releaseSensitivity == b.releaseSensitivity);
    }

    // "Regular, 1.7 mm", "Rapid Trigger, 1.7 mm, Trigger 0.4, Reset 0.4", "Gamepad Analog, A button".
    std::string SwitchesSection::Briefly(const keychron::KeyConfig& config) const
    {
        const std::string distance = Millimetres(config.actuationPoint) + " mm";
        switch (ModeOf(config))
        {
        case Mode::Regular: return "Regular, " + distance;
        case Mode::RapidTrigger:
            return "Rapid Trigger, " + distance + ", Trigger " + Millimetres(config.pressSensitivity) + ", Reset " +
                   Millimetres(config.releaseSensitivity);
        case Mode::Dks: return "One Key Multiple Commands";
        case Mode::LongPress: return "Long-Press Switch, " + distance;
        case Mode::Gamepad: return "Gamepad Analog, " + keychron::GamepadControlWords(config.advancedData);
        case Mode::Unknown: break;
        }
        return "a mode Nazg does not know (" + std::to_string(static_cast<unsigned>(config.advancedMode)) + ")";
    }

    std::string SwitchesSection::Says(const keychron::KeyConfig& config) const
    {
        const std::string pressed  = Millimetres(config.actuationPoint);
        const std::string released = Millimetres(ReleasePoint(config.actuationPoint));
        switch (ModeOf(config))
        {
        case Mode::Regular:
            return "Pressed when it goes down past " + pressed + " mm, released when it comes back above " + released +
                   " mm.";
        case Mode::RapidTrigger:
            // The key's movement, step by step; the sliders above name the settings (Rico, 2026-11-08:
            // the first wording, naming them in the sentence, was unclear).
            return "Pressed at " + pressed + " mm. From there, at any depth: released as soon as it comes up " +
                   Millimetres(config.releaseSensitivity) + " mm, pressed again as soon as it goes down " +
                   Millimetres(config.pressSensitivity) + " mm. Back above " + released + " mm, it starts over.";
        case Mode::LongPress:
            return "Pressed once, the key stays down; pressed again, it is let go. It sends what the Keymap says.";
        case Mode::Gamepad:
            return "Plays " + keychron::GamepadControlWords(config.advancedData) +
                   (keychron::IsGamepadButton(config.advancedData) ? ", pressed at " + pressed + " mm."
                                                                   : ", by how far the key goes down.");
        case Mode::Dks:
        {
            const keychron::Profile* profile = Shown();
            if (!profile || config.advancedData >= profile->dksSlots.size())
                return "One Key Multiple Commands, from a slot the board does not have.";
            std::string lines;
            for (size_t point = 0; point < keychron::c_DksPoints; ++point)
                lines += (point ? "\n" : "") + DksPointSays(profile->dksSlots[config.advancedData], point);
            return lines;
        }
        case Mode::Unknown: break;
        }
        return "A mode Nazg does not know (" + std::to_string(static_cast<unsigned>(config.advancedMode)) +
               "), set with Keychron's Launcher: its Turbocharged or PDO, perhaps. Nazg leaves it as it is unless "
               "another mode is chosen here.";
    }

    // On the key, in its Actuation Distance's place: empty where that shows.
    std::string SwitchesSection::MarkOf(const keychron::KeyConfig& config) const
    {
        switch (ModeOf(config))
        {
        case Mode::Regular:
        case Mode::RapidTrigger: break;
        case Mode::Dks: return "DKS";
        case Mode::LongPress: return "Long";
        case Mode::Gamepad: return keychron::GamepadControlName(config.advancedData);
        case Mode::Unknown: return "?";
        }
        return {};
    }

    std::string SwitchesSection::KeycodeName(uint16_t keycode) const
    {
        const LegendContext context{ m_Legends.Layout(), m_Legends.modifierNames };
        const std::string   name =
            CaptionOf(LegendFor(qmk::DecodeKeycode(keycode, m_Keyboard.keycodeVersion), context));
        return name.empty() ? "keycode " + std::to_string(keycode) : name;
    }

    // "Bottom In, 2.2 mm: Shift pressed".
    std::string SwitchesSection::DksPointSays(const keychron::DksSlot& slot, size_t point) const
    {
        std::string text = std::string(c_DksPointNames[point]) + ", " +
                           Millimetres(keychron::DksDepth(slot, static_cast<keychron::DksPoint>(point))) + " mm: ";
        std::string done;
        for (size_t command = 0; command < keychron::c_DksCommands; ++command)
        {
            if (!slot.keycodes[command])
                continue;
            for (const keychron::DksBar bar : keychron::DksBarsOf(keychron::DksActionsOf(slot, command)))
            {
                const char* what = bar.from == point && bar.to == point ? " tapped"
                                   : bar.from == point                  ? " pressed"
                                   : bar.to == point                    ? " let go"
                                                                        : nullptr;
                if (what)
                    done += (done.empty() ? "" : ", ") + KeycodeName(slot.keycodes[command]) + what;
            }
        }
        return text + (done.empty() ? "nothing" : done);
    }

    // ------------------------------------------------------------------------------------------
    // The strip: the profiles. Picking one makes the board use it.

    Strip SwitchesSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Profile";
        for (size_t profile = 0; profile < m_Profiles.size(); ++profile)
            strip.entries.push_back(ProfileName(profile));
        strip.chosen = m_ShownProfile;
        if (m_Profiles.empty())
            return strip;

        // At its end what is written, else the full travel every slider is bounded by; and Reset.
        const bool writing = !m_PendingChanges.empty() || (IsBusy() && !m_RequestIsPoll);
        if (m_PendingSelect || writing)
        {
            strip.note = m_PendingSelect ? "Switching..." : "Writing...";
        }
        else if (!m_Message.empty() && (m_IsWarning || ImGui::GetTime() - m_MessageAt < c_MessageTime))
        {
            strip.note          = m_Message;
            strip.noteIsNews    = !m_IsWarning;
            strip.noteIsWarning = m_IsWarning;
        }
        else
        {
            strip.note = "Full travel " + Millimetres(m_FullTravel ? m_FullTravel : c_SourceFullTravel) + " mm " +
                         (m_TravelMeasured ? "(measured)" : "(estimated)");
            strip.noteTip = m_TravelMeasured
                                ? "Read from a key pressed fully down.\n\nEvery key is calibrated to this travel, whatever "
                                  "its switch: with a longer switch fitted, an Actuation Distance is the same share of its "
                                  "travel, so a little deeper in fact."
                                : "Estimated from the board's gamepad curve: no command reports it. A key pressed fully down "
                                  "while testing measures it.\n\nEvery key is calibrated to this travel, whatever its "
                                  "switch: with a longer switch fitted, an Actuation Distance is the same share of its "
                                  "travel, so a little deeper in fact.";
        }
        strip.action         = "Reset profile...";
        strip.actionDisabled = IsBusy() && !m_RequestIsPoll;
        return strip;
    }

    void SwitchesSection::OnStripAction()
    {
        m_ResetAsked = true;
    }

    void SwitchesSection::OnStripChosen(size_t entry)
    {
        if (entry >= m_Profiles.size() || entry == m_ShownProfile)
            return;
        m_ShownProfile  = entry;
        m_PendingSelect = static_cast<uint8_t>(entry);
        m_GamepadOpen   = false;
        m_PickCommand.reset();
        m_Trail.Clear();
        ForgetPair();
        m_Message.clear();
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    void SwitchesSection::DescribeBoard(BoardDescription& board)
    {
        if (!std::exchange(m_HiddenThisFrame, false))
            Step();

        // The names first, as the keycaps read on the board's default layer, then every switch's
        // Actuation Distance under it, and Rapid Trigger's mark -- or an advanced mode's, a gamepad
        // control's on a badge.
        DescribeLegends(board, m_Keyboard, m_NameLayer, m_Legends, SpaceBar::Named);
        if (m_KeysSelection != m_Keyboard.layoutSelection || m_KeysNameLayer != m_NameLayer ||
            m_BoardKeys.size() != board.keys.size())
            FindKeys(board);

        const keychron::Profile* profile = Shown();
        const uint8_t            full    = m_FullTravel ? m_FullTravel : c_SourceFullTravel;

        // SOCD: each key's partners, by the pairs of the profile; the pair shown, while the pairs are.
        std::vector<std::vector<keychron::KeyPosition>> partners(m_Keys.size());
        if (profile)
            for (const uint8_t slot : keychron::SocdSlotsInUse(*profile))
            {
                const keychron::KeyPosition ends[2] = { keychron::FirstKeyOf(profile->socdSlots[slot]),
                                                        keychron::SecondKeyOf(profile->socdSlots[slot]) };
                for (size_t end = 0; end < 2; ++end)
                    if (const std::optional<size_t> key = KeyAt(ends[end]))
                        partners[*key].push_back(ends[1 - end]);
            }
        const std::optional<uint8_t> pairSlot = m_ShowPairs && !m_AddingPair && !m_PairFirst ? ShownPair() : std::nullopt;
        const keychron::SocdSlot     pair     = pairSlot ? profile->socdSlots[*pairSlot] : keychron::SocdSlot{};

        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            if (!m_BoardKeys[index])
                continue;
            const size_t key       = *m_BoardKeys[index];
            BoardKey&    described = board.keys[index];

            described.legends                = SlotLegends{};
            described.fallthrough            = Fallthrough::None;
            described.knob.reset();
            described[LegendSlot::TopLeft]   = { m_Keys[key].name, LegendRole::Label };
            if (profile)
            {
                const keychron::KeyConfig config = ConfigOf(key);
                const Mode                mode   = ModeOf(config);
                if (const std::string mark = MarkOf(config); !mark.empty())
                    described[LegendSlot::BottomLeft] = { mark, LegendRole::Label, CommandCategory::Behaviour,
                                                          mode == Mode::Gamepad };
                else
                    described[LegendSlot::BottomLeft] = { Millimetres(config.actuationPoint), LegendRole::Bound };
                if (mode == Mode::RapidTrigger)
                    described[LegendSlot::BottomRight] = { c_RapidTriggerMark, LegendRole::Label,
                                                           CommandCategory::Behaviour };
            }

            if (!partners[key].empty())
            {
                // The partner under the name, a wire joining them.
                std::string names;
                for (const keychron::KeyPosition partner : partners[key])
                    names += (names.empty() ? "" : " ") + NameAt(partner);
                described[LegendSlot::TopLeft].wired  = true;
                described[LegendSlot::MiddleLeft]     = { names, LegendRole::Label, CommandCategory::Behaviour };
            }
            if (m_HoveredKey && *m_HoveredKey != key && *m_HoveredKey < m_Keys.size() &&
                std::find(partners[key].begin(), partners[key].end(), m_Keys[*m_HoveredKey].position) != partners[key].end())
                described.marks |= Mark::Highlighted;

            const size_t end = !pairSlot                                                  ? 2u
                               : m_Keys[key].position == keychron::FirstKeyOf(pair)  ? 0u
                               : m_Keys[key].position == keychron::SecondKeyOf(pair) ? 1u
                                                                                     : 2u;
            if (m_ShowPairs ? end < 2 || m_PairFirst == key : key < m_Selected.size() && m_Selected[key])
                described.marks |= Mark::Selected;
            if (m_Testing && m_ShowPairs && end < 2 && m_PairTravel[end])
            {
                described.fill = KeyFill::Heat;
                described.heat = std::min(1.0f, static_cast<float>(m_PairTravel[end]->travelSixtieths) / (full * 6.0f));
                if (end == 0 ? m_PairSent.first : m_PairSent.second)
                    described.marks |= Mark::Pressed;
            }
            if (profile)
            {
                const keychron::KeyConfig config = ConfigOf(key);
                if (ModeOf(config) == Mode::Gamepad && config.advancedData < keychron::c_GamepadControlCount)
                {
                    if (m_HoveredControl == config.advancedData)
                        described.marks |= Mark::Highlighted;
                    if (m_Levels && (*m_Levels)[config.advancedData] > 0.02f)
                    {
                        described.fill = KeyFill::Heat;
                        described.heat = std::min(1.0f, (*m_Levels)[config.advancedData]);
                    }
                }
            }
            if (m_Testing && !pairSlot && m_Followed == key && m_Travel && !(m_Levels && profile && ModeOf(ConfigOf(key)) == Mode::Gamepad))
            {
                described.fill = KeyFill::Heat;
                described.heat = std::min(1.0f, static_cast<float>(m_Travel->travelSixtieths) / (full * 6.0f));
                if (m_Travel->pressed)
                    described.marks |= Mark::Pressed;
            }
        }
    }

    void SwitchesSection::OnBoardEvents(const BoardDescription& /*board*/, const BoardEvents& events)
    {
        m_HoveredKey = events.hoveredKey && *events.hoveredKey < m_BoardKeys.size() ? m_BoardKeys[*events.hoveredKey]
                                                                                    : std::nullopt;

        // A key's settings in full on hover: the key holds one value, the tooltip all of them.
        if (events.hoveredKey && *events.hoveredKey < m_BoardKeys.size() && m_BoardKeys[*events.hoveredKey] && Shown())
        {
            const size_t              key    = *m_BoardKeys[*events.hoveredKey];
            const keychron::KeyConfig config = ConfigOf(key);
            std::string tip = m_Keys[key].name + "\n";
            switch (ModeOf(config))
            {
            case Mode::Regular: tip += "Regular\nActuation Distance " + Millimetres(config.actuationPoint) + " mm"; break;
            case Mode::RapidTrigger:
                tip += "Rapid Trigger\nActuation Distance " + Millimetres(config.actuationPoint) +
                       " mm\n(Press) Trigger Distance " + Millimetres(config.pressSensitivity) +
                       " mm\n(Release) Reset Distance " + Millimetres(config.releaseSensitivity) + " mm";
                break;
            case Mode::Dks: tip += "One Key Multiple Commands\n" + Says(config); break;
            case Mode::LongPress:
                tip += "Long-Press Switch\nActuation Distance " + Millimetres(config.actuationPoint) + " mm";
                break;
            case Mode::Gamepad:
                tip += "Gamepad Analog\n" + keychron::GamepadControlWords(config.advancedData);
                if (UsesActuation(config))
                    tip += "\nActuation Distance " + Millimetres(config.actuationPoint) + " mm";
                break;
            case Mode::Unknown: tip += Briefly(config); break;
            }
            for (const uint8_t slot : keychron::SocdSlotsInUse(*Shown()))
            {
                const keychron::SocdSlot&   pair   = Shown()->socdSlots[slot];
                const keychron::KeyPosition first  = keychron::FirstKeyOf(pair);
                const keychron::KeyPosition second = keychron::SecondKeyOf(pair);
                if (m_Keys[key].position == first || m_Keys[key].position == second)
                    tip += "\nSOCD with " + NameAt(m_Keys[key].position == first ? second : first) + ": " +
                           SocdBriefly(PairView(pair));
            }
            ImGui::SetTooltip("%s", tip.c_str());
        }

        if (!events.clickedKey || *events.clickedKey >= m_BoardKeys.size() || !m_BoardKeys[*events.clickedKey])
            return;
        const size_t key = *m_BoardKeys[*events.clickedKey];
        if (m_ShowPairs)
        {
            if (!m_Testing)
                PairKeyChosen(key);
        }
        else if (m_PickCommand)
            PickFromBoard(key);
        else if (key < m_Selected.size())
            m_Selected[key] = !m_Selected[key];
    }

    // A key pressed on the board: followed while testing, else selected or unselected, as a click.
    void SwitchesSection::PressSeen(size_t key)
    {
        if (IsTestingPair())
            return;
        if (m_ShowPairs && !m_Testing)
        {
            PairKeyChosen(key);
            return;
        }
        if (m_Testing)
        {
            if (m_Followed != key)
            {
                m_Travel.reset();
                m_ClampReadings = 0;
                m_Player.Reset();
            }
            m_Followed = key;
            return;
        }
        if (m_PickCommand)
        {
            PickFromBoard(key);
            return;
        }
        if (key < m_Selected.size())
            m_Selected[key] = !m_Selected[key];
    }

    // ------------------------------------------------------------------------------------------
    // What goes to the board next: one request at a time, from the frame.

    void SwitchesSection::Step()
    {
        if (IsBusy() || m_Link == Link::Failed)
            return;

        m_RequestIsPoll = false;
        if (m_Link == Link::Closed)
        {
            m_Request = Open();
            return;
        }
        if (m_PendingSelect)
        {
            m_Request = Select(*std::exchange(m_PendingSelect, std::nullopt));
            return;
        }
        if (m_PendingMode)
        {
            const ModeChange change = *std::exchange(m_PendingMode, std::nullopt);
            m_Request               = WriteGamepadMode(change.mask, change.value);
            return;
        }
        if (m_PendingReset)
        {
            m_Request = Reset(*std::exchange(m_PendingReset, std::nullopt));
            return;
        }
        if (!m_PendingChanges.empty() && !m_DraggedField)
        {
            PendingChange change = std::move(m_PendingChanges.front());
            m_PendingChanges.erase(m_PendingChanges.begin());
            m_Request = Write(std::move(change));
            return;
        }

        const double now = ImGui::GetTime();
        if (now - m_LastPoll < c_PollInterval)
            return;
        const bool readInUse = now - m_LastInUseRead >= c_InUseInterval;
        m_LastPoll           = now;
        if (readInUse)
            m_LastInUseRead = now;
        m_RequestIsPoll = true;
        m_Request       = Poll(readInUse);
    }

    // A change on the keys the editor acts on, planned against the profile shown and applied to it at
    // once -- what the user sees -- then written in turn. The board's answer replaces it.
    void SwitchesSection::Ask(keychron::TravelChange change)
    {
        if (m_ShownProfile >= m_Profiles.size())
            return;

        std::vector<keychron::KeyPosition> keys;
        for (const size_t key : Targets())
            keys.push_back(m_Keys[key].position);

        keychron::Profile&  profile = m_Profiles[m_ShownProfile];
        const uint8_t       columns = m_Keyboard.definition.matrixColumns;
        PendingChange       pending{ static_cast<uint8_t>(m_ShownProfile), {},
                                     keychron::PlanTravelWrites(profile, columns, keys, change), {} };
        for (const keychron::TravelWrite& write : pending.writes)
            keychron::ApplyTravelWrite(profile, columns, write);
        m_PendingChanges.push_back(std::move(pending));
        m_Message.clear();
    }

    // The targets' mode: the advanced fields first, then -- for Regular and Rapid Trigger -- the mode
    // under them, which the firmware keeps apart.
    void SwitchesSection::AskMode(Mode mode)
    {
        if (m_ShownProfile >= m_Profiles.size())
            return;

        const std::vector<size_t> targets = Targets();
        keychron::Profile&        profile = m_Profiles[m_ShownProfile];
        const uint8_t             columns = m_Keyboard.definition.matrixColumns;

        // Gamepad Analog needs a control: the controls first, written once one is chosen.
        m_GamepadOpen = mode == Mode::Gamepad;
        if (m_GamepadOpen)
        {
            m_GamepadOpenFor = m_Selected;
            return;
        }

        // One Key Multiple Commands starts with the key itself, held, as the keymap has it on the
        // layer the board types with (Rico, 2026-11-23).
        if (mode == Mode::Dks)
        {
            const std::optional<size_t>  key  = OneSelected();
            const std::optional<uint8_t> slot = keychron::FreeDksSlot(profile);
            if (!key)
                return;
            if (!slot)
            {
                Say("All " + std::to_string(profile.dksSlots.size()) + " are in use: set another key to Regular first", true);
                return;
            }
            const ResolvedKey resolved = ResolveKey(m_Keyboard, m_Keys[*key].geometry, m_NameLayer);
            AskDks(*key, *slot,
                   keychron::NewDksSlot(qmk::EncodeKeycode(resolved.keycode, m_Keyboard.keycodeVersion).value_or(uint16_t{ 0 }),
                                        m_FullTravel));
            return;
        }

        PendingChange             pending{ static_cast<uint8_t>(m_ShownProfile), {}, {}, {} };
        const bool                basic = mode == Mode::Regular || mode == Mode::RapidTrigger;

        for (const size_t key : targets)
        {
            const keychron::KeyConfig config = ConfigOf(key);
            if (basic ? config.advancedMode == keychron::AdvancedMode::None : ModeOf(config) == mode)
                continue;
            keychron::AdvancedWrite write;
            write.key  = m_Keys[key].position;
            write.mode = mode == Mode::LongPress ? keychron::AdvancedMode::Toggle : keychron::AdvancedMode::None;
            pending.advanced.push_back(write);
        }
        for (const keychron::AdvancedWrite& write : pending.advanced)
            keychron::ApplyAdvancedWrite(profile, columns, write);

        if (basic)
        {
            std::vector<keychron::KeyPosition> keys;
            for (const size_t key : targets)
                keys.push_back(m_Keys[key].position);
            keychron::TravelChange change;
            change.mode    = mode == Mode::Regular ? keychron::KeyMode::Regular : keychron::KeyMode::RapidTrigger;
            pending.writes = keychron::PlanTravelWrites(profile, columns, keys, change);
            for (const keychron::TravelWrite& write : pending.writes)
                keychron::ApplyTravelWrite(profile, columns, write);
        }
        if (pending.advanced.empty() && pending.writes.empty())
            return;
        m_PendingChanges.push_back(std::move(pending));
        m_Message.clear();
    }

    void SwitchesSection::AskDks(size_t key, uint8_t slot, const keychron::DksSlot& settings)
    {
        if (m_ShownProfile >= m_Profiles.size())
            return;
        keychron::AdvancedWrite write;
        write.key  = m_Keys[key].position;
        write.mode = keychron::AdvancedMode::Dks;
        write.data = slot;
        write.dks  = settings;
        keychron::ApplyAdvancedWrite(m_Profiles[m_ShownProfile], m_Keyboard.definition.matrixColumns, write);
        m_PendingChanges.push_back({ static_cast<uint8_t>(m_ShownProfile), { write }, {}, {} });
        m_Message.clear();
    }

    // What the command being chosen sends from now on. One just added is held all along; a key replaced
    // keeps what its command does.
    void SwitchesSection::PickForCommand(uint16_t keycode)
    {
        const std::optional<size_t> key = OneSelected();
        if (!key || !m_PickCommand || *m_PickCommand >= keychron::c_DksCommands || ModeOf(ConfigOf(*key)) != Mode::Dks)
            return;
        const uint8_t slot = ConfigOf(*key).advancedData;
        if (slot >= Shown()->dksSlots.size())
            return;

        keychron::DksSlot changed = Shown()->dksSlots[slot];
        const size_t      command = *m_PickCommand;
        if (keycode == 0)
            keychron::SetDksActions(changed, command, {});
        else if (!changed.keycodes[command])
            keychron::SetDksActions(changed, command,
                                    keychron::DksActionsFrom({ { 0, static_cast<uint8_t>(keychron::c_DksPoints - 1) } }));
        changed.keycodes[command] = keycode;
        m_PickCommand.reset();
        if (!(changed == Shown()->dksSlots[slot]))
            AskDks(*key, slot, changed);
    }

    // The key clicked or pressed: what the keymap has there, on the layer the names are read from.
    void SwitchesSection::PickFromBoard(size_t key)
    {
        const ResolvedKey resolved = ResolveKey(m_Keyboard, m_Keys[key].geometry, m_NameLayer);
        const uint16_t    keycode  = qmk::EncodeKeycode(resolved.keycode, m_Keyboard.keycodeVersion).value_or(uint16_t{ 0 });
        if (!keychron::IsDksKeycode(keycode))
        {
            Say(m_Keys[key].name + " sends no ordinary key: choose another", true);
            return;
        }
        PickForCommand(keycode);
    }

    // A control clicked: what the one key selected plays from now on, or -- with none -- the keys that
    // play it, selected.
    void SwitchesSection::ChooseControl(uint8_t control)
    {
        if (m_ShownProfile >= m_Profiles.size() || !keychron::IsGamepadControl(control))
            return;
        const bool none = std::none_of(m_Selected.begin(), m_Selected.end(), [](bool selected) { return selected; });
        if (none)
        {
            for (size_t key = 0; key < m_Keys.size(); ++key)
            {
                const keychron::KeyConfig config = ConfigOf(key);
                if (ModeOf(config) == Mode::Gamepad && config.advancedData == control)
                    m_Selected[key] = true;
            }
            return;
        }
        const std::optional<size_t> key = OneSelected();
        if (!key)
        {
            Say("Select one key to choose what it plays", true);
            return;
        }
        keychron::AdvancedWrite write;
        write.key  = m_Keys[*key].position;
        write.mode = keychron::AdvancedMode::Gamepad;
        write.data = control;
        if (keychron::Holds(m_Profiles[m_ShownProfile], m_Keyboard.definition.matrixColumns, write))
            return;
        keychron::ApplyAdvancedWrite(m_Profiles[m_ShownProfile], m_Keyboard.definition.matrixColumns, write);
        m_PendingChanges.push_back({ static_cast<uint8_t>(m_ShownProfile), { write }, {}, {} });
        m_Message.clear();
    }

    // ------------------------------------------------------------------------------------------
    // SOCD: the pairs.

    std::optional<uint8_t> SwitchesSection::ShownPair() const
    {
        const keychron::Profile* profile = Shown();
        if (!profile)
            return std::nullopt;
        const std::vector<uint8_t> used = keychron::SocdSlotsInUse(*profile);
        if (used.empty())
            return std::nullopt;
        return m_Pair && std::find(used.begin(), used.end(), *m_Pair) != used.end() ? *m_Pair : used.front();
    }

    std::string SwitchesSection::NameAt(keychron::KeyPosition position) const
    {
        if (const std::optional<size_t> key = KeyAt(position))
            return m_Keys[*key].name;
        return "R" + std::to_string(position.row) + " C" + std::to_string(position.column);
    }

    SocdPairView SwitchesSection::PairView(const keychron::SocdSlot& pair) const
    {
        return { NameAt(keychron::FirstKeyOf(pair)), NameAt(keychron::SecondKeyOf(pair)), pair.type,
                 m_FullTravel ? m_FullTravel : c_SourceFullTravel };
    }

    // What keeps the pair from doing anything: a key the firmware leaves out of SOCD.
    std::string SwitchesSection::PairWarning(const keychron::SocdSlot& pair) const
    {
        for (const keychron::KeyPosition position : { keychron::FirstKeyOf(pair), keychron::SecondKeyOf(pair) })
        {
            const std::optional<size_t> key = KeyAt(position);
            if (!key)
                return NameAt(position) + " is no key of the layout shown: this pair does nothing.";
            const Mode mode = ModeOf(ConfigOf(*key));
            if (mode == Mode::Dks)
                return m_Keys[*key].name + " is One Key Multiple Commands: the board leaves it out of SOCD, so this pair does "
                                           "nothing.";
            if (mode == Mode::Gamepad && m_GamepadMode && !(*m_GamepadMode & keychron::GamepadMode::Typing))
                return m_Keys[*key].name + " plays the gamepad only, with keyboard keys off: this pair does nothing.";
        }
        return {};
    }

    void SwitchesSection::AskPair(const keychron::SocdWrite& write)
    {
        if (m_ShownProfile >= m_Profiles.size())
            return;
        keychron::ApplySocdWrite(m_Profiles[m_ShownProfile], write);
        m_PendingChanges.push_back({ static_cast<uint8_t>(m_ShownProfile), {}, {}, { write } });
        m_Message.clear();
    }

    // A key clicked or pressed while the pairs show: the first or the other key of a new pair, or a pair it
    // is in, shown; one already shown starts a new pair with it. A new pair is Last Key Priority, written at
    // once (Rico, 2026-11-23).
    void SwitchesSection::PairKeyChosen(size_t key)
    {
        const keychron::Profile* profile = Shown();
        if (!profile)
            return;

        if (m_PairFirst)
        {
            const size_t first = *m_PairFirst;
            m_PairFirst.reset();
            m_AddingPair = false;
            if (first == key || first >= m_Keys.size())
                return;
            const keychron::KeyPosition a = m_Keys[first].position, b = m_Keys[key].position;
            if (const std::optional<uint8_t> known = keychron::SocdSlotPairing(*profile, a, b))
            {
                m_Pair = known;
                return;
            }
            const std::optional<uint8_t> slot = keychron::FreeSocdSlot(*profile);
            if (!slot)
            {
                Say("The board holds " + std::to_string(profile->socdSlots.size()) + " pairs per profile", true);
                return;
            }
            if (!keychron::FitsSocdSlot(a) || !keychron::FitsSocdSlot(b))
            {
                Say("This board's firmware cannot pair " + m_Keys[first].name + " and " + m_Keys[key].name, true);
                return;
            }
            keychron::SocdWrite write;
            write.slot = *slot;
            write.pair = keychron::MakeSocdSlot(a, b, keychron::SocdType::LastWins);
            AskPair(write);
            m_Pair = slot;
            return;
        }

        if (!m_AddingPair)
        {
            const std::optional<uint8_t> shown = ShownPair();
            for (const uint8_t slot : keychron::SocdSlotsInUse(*profile))
            {
                const keychron::SocdSlot& pair = profile->socdSlots[slot];
                if (slot != shown && (keychron::FirstKeyOf(pair) == m_Keys[key].position ||
                                      keychron::SecondKeyOf(pair) == m_Keys[key].position))
                {
                    m_Pair = slot;
                    return;
                }
            }
        }
        if (!keychron::FreeSocdSlot(*profile))
        {
            Say("The board holds " + std::to_string(profile->socdSlots.size()) + " pairs per profile", true);
            return;
        }
        m_PairFirst = key;
    }

    void SwitchesSection::ForgetPair()
    {
        m_Pair.reset();
        m_AddingPair = false;
        m_PairFirst.reset();
        m_PairTrail.Clear();
        m_PairPlayer.Reset();
        m_PairSent = {};
        std::fill(std::begin(m_PairTravel), std::end(m_PairTravel), std::nullopt);
        std::fill(std::begin(m_HostDown), std::end(m_HostDown), false);
    }

    // To the pairs, at the one the keys selected are in; and back to the keys, the pair's two selected, so
    // that a mode button clicked to go back changes nothing.
    void SwitchesSection::ShowPairs()
    {
        const keychron::Profile* profile = Shown();
        std::vector<size_t>      selected;
        for (size_t key = 0; key < m_Selected.size(); ++key)
            if (m_Selected[key])
                selected.push_back(key);

        ForgetPair();
        m_ShowPairs   = true;
        m_Testing     = false;
        m_GamepadOpen = false;
        m_PickCommand.reset();
        if (!profile || selected.empty() || selected.size() > 2)
            return;
        if (selected.size() == 2)
            m_Pair = keychron::SocdSlotPairing(*profile, m_Keys[selected[0]].position, m_Keys[selected[1]].position);
        else
            for (const uint8_t slot : keychron::SocdSlotsInUse(*profile))
                if (const keychron::SocdSlot& pair = profile->socdSlots[slot];
                    !m_Pair && (keychron::FirstKeyOf(pair) == m_Keys[selected[0]].position ||
                                keychron::SecondKeyOf(pair) == m_Keys[selected[0]].position))
                    m_Pair = slot;
    }

    void SwitchesSection::ShowKeys()
    {
        if (const std::optional<uint8_t> slot = ShownPair())
        {
            const keychron::SocdSlot& pair = Shown()->socdSlots[*slot];
            m_Selected.assign(m_Keys.size(), false);
            for (const keychron::KeyPosition position : { keychron::FirstKeyOf(pair), keychron::SecondKeyOf(pair) })
                if (const std::optional<size_t> key = KeyAt(position))
                    m_Selected[*key] = true;
        }
        ForgetPair();
        m_ShowPairs = false;
        m_Testing   = false;
    }

    // The pairs, each with an x to remove it, and Add pair. What a pair does is said on hover, its cards
    // saying it once it is chosen (Rico, 2026-11-23).
    void SwitchesSection::DrawPairsLine()
    {
        const keychron::Profile& profile = *Shown();
        const ImGuiStyle&        style   = ImGui::GetStyle();
        const bool               adding  = m_AddingPair || m_PairFirst;
        const float              right   = ImGui::GetContentRegionMax().x;
        const std::optional<uint8_t> shown = ShownPair();
        std::optional<uint8_t>   removed;

        const auto placed = [&](float width, bool isFirst)
        {
            if (isFirst)
                return;
            ImGui::SameLine();
            if (ImGui::GetCursorPosX() + width > right)
                ImGui::NewLine();
        };
        bool isFirst = true;
        for (const uint8_t slot : keychron::SocdSlotsInUse(profile))
        {
            const SocdPairView view  = PairView(profile.socdSlots[slot]);
            placed(SocdPairItemWidth(view.first, view.second, true), isFirst);
            isFirst = false;

            ImGui::PushID(slot);
            const SocdPairItemEvents item = DrawSocdPairItem(view.first, view.second, !adding && slot == shown, true);
            if (item.clicked)
            {
                m_AddingPair = false;
                m_PairFirst.reset();
                if (shown != slot)
                    ForgetPair();
                m_Pair = slot;
            }
            if (item.hovered)
                ImGui::SetTooltip("%s", SocdBriefly(view).c_str());
            if (item.removed)
                removed = slot;
            ImGui::PopID();
        }

        const bool isFull = !keychron::FreeSocdSlot(profile);
        placed(ButtonWidth("+ Add pair"), isFirst);
        if (adding)
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        ImGui::BeginDisabled(isFull || m_Testing);
        if (ImGui::Button("+ Add pair"))
        {
            m_AddingPair = !adding;
            m_PairFirst.reset();
        }
        ImGui::EndDisabled();
        if (adding)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            if (isFull)
                ImGui::SetTooltip("The board holds %zu pairs per profile.", profile.socdSlots.size());
            else
                ImGui::SetTooltip("%s", m_Testing ? "Stop the test first." : "Or click two keys of the board.");
        }

        if (removed)
        {
            // The one before it is shown next, if it was the one shown.
            const std::vector<uint8_t> used = keychron::SocdSlotsInUse(profile);
            const auto                 at   = std::find(used.begin(), used.end(), *removed);
            const std::optional<uint8_t> next = shown != removed        ? shown
                                                : at != used.begin()    ? std::optional<uint8_t>(*std::prev(at))
                                                                        : std::nullopt;
            keychron::SocdWrite write;
            write.slot = *removed;
            AskPair(write);
            if (shown == removed)
                ForgetPair();
            m_Pair = next;
        }
    }

    void SwitchesSection::DrawPairs()
    {
        if (m_AddingPair || m_PairFirst)
        {
            const bool hasFirst = m_PairFirst && *m_PairFirst < m_Keys.size();
            (void)DrawSocdPairItem(hasFirst ? m_Keys[*m_PairFirst].name : std::string(), std::string(), false, false);
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_AddingPair = false;
                m_PairFirst.reset();
                return;
            }
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "Click the %s key on the board%s.", m_PairFirst ? "other" : "first",
                         m_MatrixRefused ? "" : ", or press it");
            ImGui::PopTextWrapPos();
            return;
        }

        const std::optional<uint8_t> slot = ShownPair();
        if (!slot)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "No pair on this profile. Add one, or click two keys of the board -- A and D, for "
                                             "example -- to choose what is sent while both are held down.");
            ImGui::PopTextWrapPos();
            return;
        }

        const keychron::SocdSlot pair = Shown()->socdSlots[*slot];
        if (const std::optional<keychron::SocdType> type = DrawSocdEditor(PairView(pair), PairWarning(pair)))
        {
            keychron::SocdWrite write;
            write.slot      = *slot;
            write.pair      = pair;
            write.pair.type = *type;
            AskPair(write);
        }
    }

    // ------------------------------------------------------------------------------------------
    // SOCD: the pair tested.

    bool SwitchesSection::IsTestingPair() const
    {
        return m_Testing && m_ShowPairs && m_Link == Link::Open && ShownPair().has_value();
    }

    bool SwitchesSection::WantsComputerKeys() const
    {
        return IsTestingPair();
    }

    // The keycode a key sends on the layer the names are read from, by the name the computer's key is
    // matched to; none when the computer would report nothing of it.
    std::optional<std::string_view> SwitchesSection::NameSent(keychron::KeyPosition position) const
    {
        const std::optional<size_t> key = KeyAt(position);
        if (!key)
            return std::nullopt;
        return TestedName(ResolveKey(m_Keyboard, m_Keys[*key].geometry, m_NameLayer).keycode, m_Keyboard.keycodeVersion);
    }

    void SwitchesSection::OnHostKey(uint32_t usage, bool down)
    {
        if (!IsTestingPair())
            return;
        const keychron::SocdSlot&           pair  = Shown()->socdSlots[*ShownPair()];
        const std::vector<std::string_view> names = KeycodesSending(usage, m_Keyboard.keycodeVersion);
        const keychron::KeyPosition         ends[2] = { keychron::FirstKeyOf(pair), keychron::SecondKeyOf(pair) };
        for (size_t end = 0; end < 2; ++end)
            if (const std::optional<std::string_view> sent = NameSent(ends[end]);
                sent && std::find(names.begin(), names.end(), *sent) != names.end())
                m_HostDown[end] = down;
    }

    // Both keys of the pair, read. What is sent is what the computer receives, where it tells the two
    // apart; else the pair played from their travels, as the firmware plays it.
    void SwitchesSection::PairRead(const keychron::SocdSlot& pair, const keychron::RealtimeTravel& first,
                                   const keychron::RealtimeTravel& second)
    {
        const keychron::SocdPlayer::Sent played = m_PairPlayer.Step(pair.type, { first.travelSixtieths, first.pressed },
                                                                    { second.travelSixtieths, second.pressed });
        const std::optional<std::string_view> firstSent  = NameSent(keychron::FirstKeyOf(pair));
        const std::optional<std::string_view> secondSent = NameSent(keychron::SecondKeyOf(pair));
        if (firstSent && secondSent && *firstSent != *secondSent)
            m_PairSent = { first.pressed && m_HostDown[0], second.pressed && m_HostDown[1] };
        else
            m_PairSent = played;

        m_PairTravel[0] = first;
        m_PairTravel[1] = second;
        const float full = (m_FullTravel ? m_FullTravel : c_SourceFullTravel) * 6.0f;
        m_PairTrail.Add({ ImGui::GetTime(), static_cast<float>(first.travelSixtieths) / full,
                          static_cast<float>(second.travelSixtieths) / full, m_PairSent.first, m_PairSent.second });
    }

    void SwitchesSection::DrawPairTest()
    {
        const uint8_t full = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
        SocdTestInput input;
        input.pair         = PairView(Shown()->socdSlots[*ShownPair()]);
        input.firstTravel  = m_PairTravel[0] ? FineMillimetres(*m_PairTravel[0], full) + " mm" : "";
        input.secondTravel = m_PairTravel[1] ? FineMillimetres(*m_PairTravel[1], full) + " mm" : "";
        input.firstSent    = m_PairSent.first;
        input.secondSent   = m_PairSent.second;
        // Held back only by the other: a key alone is on its way to the computer.
        const bool both      = m_PairTravel[0] && m_PairTravel[1] && m_PairTravel[0]->pressed && m_PairTravel[1]->pressed;
        input.firstHeldBack  = both && !m_PairSent.first;
        input.secondHeldBack = both && !m_PairSent.second;
        input.trail          = &m_PairTrail;
        DrawSocdTest(input);
    }

    std::optional<size_t> SwitchesSection::OneSelected() const
    {
        std::optional<size_t> one;
        for (size_t key = 0; key < m_Selected.size(); ++key)
            if (m_Selected[key])
            {
                if (one)
                    return std::nullopt;
                one = key;
            }
        return one;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void SwitchesSection::DrawPanel()
    {
        if (m_Link == Link::Failed)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Warning, "%s", m_Message.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("Try again"))
            {
                m_Message.clear();
                m_Link = Link::Closed;
            }
            return;
        }
        if (m_Profiles.empty())
        {
            (void)DrawReading("switch settings", true, std::string());
            return;
        }

        DrawHead();
        ImGui::Separator();
        if (m_ShowPairs)
        {
            DrawPairsLine();
            ImGui::Separator();
        }

        // The Test column as wide as what it holds -- the bar and its words, the raw values beside them
        // -- so nothing in it collides (Rico, 2026-11-08), and never narrower than its sentence needs.
        const ImGuiStyle& style    = ImGui::GetStyle();
        const float       header   = ImGui::CalcTextSize("Test").x + ImGui::CalcTextSize("Raw values").x +
                                     ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ButtonWidth("Stop") +
                                     style.ItemSpacing.x * 3;
        float             contents = TravelBarWidth();
        if (m_Testing && m_Raw)
            contents += style.ItemSpacing.x + RawTableWidth();
        // Wider where the editor leaves room (Rico, 2026-11-23): what a key's commands sent on one line.
        const float room      = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 46.0f;
        if (m_Testing && HasGamepadKeys())
            contents = std::max(contents, GamepadTestWidth());
        const float testWidth = std::max({ ImGui::GetFontSize() * 17.0f, header, contents,
                                           std::min(room, ImGui::GetFontSize() * 30.0f) });
        // As tall as the panel at least, so that the line between the two reaches its bottom.
        const ImVec2 wholePanel(0.0f, std::max(0.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::BeginTable("switches", 2, ImGuiTableFlags_BordersInnerV, wholePanel))
        {
            ImGui::TableSetupColumn("editor", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("test", ImGuiTableColumnFlags_WidthFixed, testWidth);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawEditor();
            ImGui::TableSetColumnIndex(1);
            DrawTest();
            ImGui::EndTable();
        }

        DrawResetQuestion();
    }

    // What is edited, its mode, Select All and Clear: one line (Rico, 2026-11-23, the lower part as
    // short as it can be). The profile is the strip's.
    void SwitchesSection::DrawHead()
    {
        const std::vector<size_t> targets  = Targets();
        const size_t              selected = static_cast<size_t>(std::count(m_Selected.begin(), m_Selected.end(), true));
        const ImGuiStyle&         style    = ImGui::GetStyle();

        // The modes the targets share, and what each button can do.
        std::optional<Mode> mode;
        bool                mixed = targets.empty();
        for (const size_t key : targets)
        {
            const Mode own = ModeOf(ConfigOf(key));
            if (mode && *mode != own)
                mixed = true;
            mode = own;
        }

        struct Choice
        {
            Mode        mode;
            const char* label;
            const char* tip;
        };
        static constexpr Choice c_Choices[] = {
            { Mode::Regular, "Regular", "" },
            { Mode::RapidTrigger, "Rapid Trigger", "" },
            { Mode::Dks, "One Key Multiple Commands",
              "Up to four keys sent by one, at four points of its travel." },
            { Mode::LongPress, "Long-Press Switch", "One press holds the key down, the next lets it go." },
            { Mode::Gamepad, "Gamepad Analog", "Plays a game controller." },
        };

        // The controls opened with nothing chosen yet belong to the selection they were opened on.
        if (m_GamepadOpen && m_GamepadOpenFor != m_Selected)
            m_GamepadOpen = false;

        float modesWidth = 0.0f;
        for (const Choice& choice : c_Choices)
            modesWidth += ButtonWidth(choice.label);
        // SOCD ends the line of modes, apart from them: it is not one key's. No number of pairs on it, which
        // read as a name (Rico, 2026-11-23): the keys' marks say there are some.
        const std::vector<uint8_t> pairs    = keychron::SocdSlotsInUse(*Shown());
        const size_t               slots    = Shown()->socdSlots.size();
        const float                socdWide = slots ? ButtonWidth("SOCD") + style.ItemSpacing.x * 2 : 0.0f;
        const float endWidth = ButtonWidth("Select All") + ButtonWidth("Clear") + style.ItemSpacing.x;
        const float right    = ImGui::GetContentRegionMax().x;
        const float modesX   = std::max(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 4.0f,
                                        right - endWidth - style.ItemSpacing.x * 2 - socdWide - modesWidth);

        // What: the key, or how many -- all of it on hover.
        std::string what, all;
        std::vector<std::string> names;
        for (const size_t key : targets)
            names.push_back(m_Keys[key].name);
        if (m_ShowPairs)
        {
            what = std::to_string(pairs.size()) + (pairs.size() == 1 ? " pair of " : " pairs of ") + std::to_string(slots);
            all  = "Each profile has its own " + std::to_string(slots) +
                  ". Keychron's Launcher makes 5 at most; the board takes " + std::to_string(slots) + ".";
        }
        else if (selected == 1)
        {
            what = names.front();
        }
        else if (selected > 1)
        {
            what = std::to_string(selected) + " keys";
            std::string list;
            for (const std::string& name : names)
                list += (list.empty() ? "" : " ") + name;
            all = list;
            what += "  " + list;
        }
        else
        {
            const size_t apart = m_Keys.size() - targets.size();
            what = apart ? std::to_string(targets.size()) + " keys, not the " + std::to_string(apart) + " advanced"
                         : "All " + std::to_string(m_Keys.size()) + " keys";
            all  = apart ? "Every key but those with an advanced mode: select them to change them." : "";
        }
        ImGui::AlignTextToFramePadding();
        const ImVec2 at    = ImGui::GetCursorScreenPos();
        const float  room  = modesX - ImGui::GetCursorPosX() - style.ItemSpacing.x;
        const ImVec2 size  = ImGui::CalcTextSize(what.c_str());
        ImGui::PushClipRect(at, ImVec2(at.x + room, at.y + ImGui::GetFrameHeight()), true);
        ImGui::TextUnformatted(what.c_str());
        ImGui::PopClipRect();
        if (ImGui::IsItemHovered() && (size.x > room || !all.empty()))
            ImGui::SetTooltip("%s", all.empty() ? what.c_str() : all.c_str());

        // The modes, side by side, the targets' lit.
        ImGui::SameLine();
        ImGui::SetCursorPosX(modesX);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
        for (size_t index = 0; index < std::size(c_Choices); ++index)
        {
            const Choice& choice = c_Choices[index];
            if (index)
                ImGui::SameLine();
            // From the pairs, every mode goes back to the keys, and changes nothing.
            if (m_ShowPairs)
            {
                if (ImGui::Button(choice.label))
                    ShowKeys();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Back to the keys: changes nothing.");
                continue;
            }
            const bool        isCurrent = m_GamepadOpen ? choice.mode == Mode::Gamepad : !mixed && mode == choice.mode;
            const bool        needsOne  = (choice.mode == Mode::Dks && selected != 1) ||
                                  (choice.mode == Mode::Gamepad && selected != 1 && !(selected == 0 && HasGamepadKeys()));
            const bool        needs     = needsOne || (choice.mode == Mode::LongPress && selected == 0);
            const std::string tip       = needsOne && !isCurrent ? std::string("Select one key: each has its own.\n\n") + choice.tip
                                          : needs && !isCurrent  ? std::string("Select the keys first.\n\n") + choice.tip
                                                                 : std::string(choice.tip);
            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
            ImGui::BeginDisabled(!isCurrent && (needs || (targets.empty() && choice.mode != Mode::Gamepad)));
            if (ImGui::Button(choice.label) && !isCurrent)
                AskMode(choice.mode);
            ImGui::EndDisabled();
            if (isCurrent)
                ImGui::PopStyleColor();
            if (!tip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::PopStyleVar();

        if (slots)
        {
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 2);
            const bool isOn = m_ShowPairs;
            if (isOn)
                ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
            if (ImGui::Button("SOCD") && !isOn)
                ShowPairs();
            if (isOn)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Two keys, and what is sent while both are down.");
        }
        if (m_ShowPairs)
            return;

        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - endWidth));
        ImGui::BeginDisabled(selected == m_Keys.size());
        if (ImGui::Button("Select All"))
            m_Selected.assign(m_Keys.size(), true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(selected == 0);
        if (ImGui::Button("Clear"))
            m_Selected.assign(m_Keys.size(), false);
        ImGui::EndDisabled();
    }

    void SwitchesSection::DrawEditor()
    {
        const std::vector<size_t> targets  = Targets();
        const size_t              selected = static_cast<size_t>(std::count(m_Selected.begin(), m_Selected.end(), true));
        m_HoveredControl.reset();
        if (m_ShowPairs)
        {
            m_PickCommand.reset();
            DrawPairs();
            return;
        }
        if (m_GamepadOpen || (selected > 0 && std::all_of(targets.begin(), targets.end(), [&](size_t key)
                                                          { return ModeOf(ConfigOf(key)) == Mode::Gamepad; })))
        {
            m_PickCommand.reset();
            DrawGamepad(targets);
            return;
        }
        if (targets.empty())
        {
            ColouredText(PanelColour::Muted, "Every key has an advanced mode. Select some to change them.");
            return;
        }

        // The mode they share, and whether they do the same.
        const keychron::KeyConfig first = ConfigOf(targets.front());
        bool                      sameMode = true, same = true;
        for (const size_t key : targets)
        {
            const keychron::KeyConfig config = ConfigOf(key);
            sameMode                         = sameMode && ModeOf(config) == ModeOf(first);
            same                             = same && DoSame(config, first);
        }
        // One key's commands have a drawing of their own.
        if (const std::optional<size_t> one = OneSelected(); one && ModeOf(first) == Mode::Dks)
        {
            DrawDks(*one);
            return;
        }
        m_PickCommand.reset();
        DrawDistances(targets, sameMode ? ModeOf(first) : Mode::Unknown);

        ImGui::PushTextWrapPos(0.0f);
        if (same && ModeOf(first) != Mode::Dks)
            ColouredText(PanelColour::Muted, "%s", Says(first).c_str());
        else if (sameMode && ModeOf(first) == Mode::Dks)
            ColouredText(PanelColour::Muted, "Select one key to see what it does.");
        else
            DrawDifferences(targets);
        if (selected == 0)
            ColouredText(PanelColour::Muted, "%s",
                         m_MatrixRefused ? "Click keys to select them -- this firmware does not report its switches, so "
                                           "pressing them selects nothing."
                                         : "Click keys to select them, or press them on the keyboard.");
        ImGui::PopTextWrapPos();
    }

    void SwitchesSection::DrawDks(size_t key)
    {
        const keychron::Profile&  profile = m_Profiles[m_ShownProfile];
        const uint8_t             slot    = ConfigOf(key).advancedData;
        if (slot >= profile.dksSlots.size())
        {
            ColouredText(PanelColour::Warning, "This key's commands are in slot %u, which the board does not have.", slot);
            return;
        }

        const keychron::DksSlot settings = profile.dksSlots[slot];
        DksEditorInput          input{ settings };
        input.fullTravel  = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
        input.keycodeName = [this](uint16_t keycode) { return KeycodeName(keycode); };
        input.slotsUsed   = keychron::DksSlotsInUse(profile);
        input.slotsCount  = profile.dksSlots.size();
        input.player      = m_Testing && m_Followed == key ? &m_Player : nullptr;

        input.picking        = m_PickCommand;
        input.pickByPressing = !m_Testing && !m_MatrixRefused;

        const DksEditorEvents events = DrawDksEditor(m_DksState, input);
        if (events.changed)
            AskDks(key, slot, *events.changed);
        if (events.pick)
            m_PickCommand = events.pick;
        if (events.more)
            m_OpenPicker = true;
        if (events.remove)
            PickForCommand(0);
        if (events.cancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
            m_PickCommand.reset();
        DrawDksPicker(settings);
    }

    void SwitchesSection::DrawGamepad(const std::vector<size_t>& targets)
    {
        DrawGamepadSettings();

        const std::optional<size_t> one      = OneSelected();
        const size_t                selected = static_cast<size_t>(std::count(m_Selected.begin(), m_Selected.end(), true));

        GamepadViewInput input;
        for (size_t key = 0; key < m_Keys.size(); ++key)
        {
            const keychron::KeyConfig config = ConfigOf(key);
            if (ModeOf(config) == Mode::Gamepad && config.advancedData < input.keys.size())
            {
                std::string& names = input.keys[config.advancedData];
                names += (names.empty() ? "" : ", ") + m_Keys[key].name;
            }
        }
        std::optional<keychron::KeyConfig> config;
        if (one && ModeOf(ConfigOf(*one)) == Mode::Gamepad)
            config = ConfigOf(*one);
        if (config)
            input.selected = config->advancedData;
        input.levels = m_Levels ? &*m_Levels : nullptr;

        // As large as the panel allows, room kept for the line under it and a button's slider.
        const ImVec2 room  = ImGui::GetContentRegionAvail();
        const float  under = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetFrameHeightWithSpacing() +
                            ImGui::GetStyle().ItemSpacing.y * 2;
        input.scale        = GamepadViewScale(room.x, room.y - under);

        const GamepadViewEvents events = DrawGamepadView(input);
        m_HoveredControl               = events.hovered;
        if (events.clicked)
            ChooseControl(*events.clicked);

        ImGui::PushTextWrapPos(0.0f);
        if (selected == 0)
            ColouredText(PanelColour::Muted, "Click a control to select the key that plays it, or a key on the board to give "
                                             "it a control.");
        else if (!one)
            ColouredText(PanelColour::Muted, "Select one key to choose what it plays.");
        else if (!config)
            ColouredText(PanelColour::Muted, "Click the control it plays.");
        else if (UsesActuation(*config))
            ColouredText(PanelColour::Muted, "Plays %s.", keychron::GamepadControlWords(config->advancedData).c_str());
        else
            ColouredText(PanelColour::Muted, "Plays %s, by how far the key goes down%s.",
                         keychron::GamepadControlWords(config->advancedData).c_str(),
                         config->advancedData == keychron::c_GamepadLeftTrigger ||
                                 config->advancedData == keychron::c_GamepadRightTrigger
                             ? ""
                             : ", through the gamepad curve");
        ImGui::PopTextWrapPos();
        if (selected > 0 && (config || !one))
            DrawDistances(targets, Mode::Gamepad);
    }

    // The gamepad's settings, the whole board's: one line.
    void SwitchesSection::DrawGamepadSettings()
    {
        if (!m_GamepadMode)
            return;
        const uint8_t mode   = *m_GamepadMode;
        const auto    change = [&](uint8_t bit, bool set)
        {
            m_PendingMode = ModeChange{ bit, set ? bit : uint8_t{ 0 } };
            m_GamepadMode = static_cast<uint8_t>(set ? mode | bit : mode & ~bit);
            m_Message.clear();
        };
        const auto tip = [](const char* words)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", words);
        };

        bool typing = (mode & keychron::GamepadMode::Typing) != 0;
        if (ImGui::Checkbox("Keyboard keys enabled", &typing))
            change(keychron::GamepadMode::Typing, typing);
        tip("On: a gamepad key also types its key. Off: it types nothing.\n\nThe whole board, every profile.");

        const bool recognised = (mode & keychron::GamepadMode::RecognitionOff) == 0;
        if (m_HasRecognition)
        {
            ImGui::SameLine();
            bool on = recognised;
            if (ImGui::Checkbox("Controller Recognition Feature", &on))
                change(keychron::GamepadMode::RecognitionOff, !on);
            tip("Off: the computer does not see a controller, and the gamepad keys play nothing.\n\nTakes effect when the "
                "board is next plugged in. The whole board, every profile.");
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!recognised);
        const bool xbox = (mode & keychron::GamepadMode::XInput) != 0;
        if (ImGui::RadioButton("Standard", !xbox) && xbox)
            change(keychron::GamepadMode::XInput, false);
        tip("Standard Controller: a plain joystick, for any program that reads one.\n\nTakes effect when the board is next "
            "plugged in.");
        ImGui::SameLine();
        if (ImGui::RadioButton("Xbox", xbox) && !xbox)
            change(keychron::GamepadMode::XInput, true);
        tip("Xbox Controller: as games expect.\n\nTakes effect when the board is next plugged in.");
        ImGui::EndDisabled();
    }

    // A command's key that the board does not have, from the picker's Basic tab in a popup: the only keys
    // that work there.
    void SwitchesSection::DrawDksPicker(const keychron::DksSlot& settings)
    {
        if (std::exchange(m_OpenPicker, false))
            ImGui::OpenPopup("##dkspicker");

        // Over the board and the panel, as wide as the picker draws its keyboard: the lower part keeps its
        // height.
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 size(std::min(ImGui::GetFontSize() * 84.0f, viewport->WorkSize.x * 0.95f),
                          std::min(ImGui::GetFontSize() * 30.0f, viewport->WorkSize.y * 0.9f));
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x / 2, viewport->WorkPos.y + viewport->WorkSize.y / 2),
                                ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        if (!ImGui::BeginPopup("##dkspicker"))
            return;
        if (!m_PickCommand || *m_PickCommand >= keychron::c_DksCommands)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const size_t command = *m_PickCommand;
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::Text("Command %zu", command + 1);
        ImGui::PopFont();
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "Ordinary keys and modifiers only.");
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - ButtonWidth("Close")));
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();

        // The Basic tab alone, built again only when the whole catalogue is.
        const std::vector<CatalogueTab>& tabs = CatalogueOf(m_Picker, m_Keyboard, m_Legends, false);
        if (m_BasicTab.empty() || m_BasicTabFor != m_Picker.catalogueFor)
        {
            m_BasicTabFor = m_Picker.catalogueFor;
            m_BasicTab.clear();
            for (const CatalogueTab& tab : tabs)
                if (tab.id == "basic")
                    m_BasicTab.push_back(tab);
        }

        const LegendContext    context{ m_Legends.Layout(), m_Legends.modifierNames };
        std::optional<Keycode> current;
        if (settings.keycodes[command])
            current = qmk::DecodeKeycode(settings.keycodes[command], m_Keyboard.keycodeVersion);
        const auto codeOf = [&](const Keycode& keycode)
        { return qmk::EncodeKeycode(keycode, m_Keyboard.keycodeVersion).value_or(uint16_t{ 0 }); };
        const KeycodePickerEvents events = DrawKeycodePicker(
            m_Picker, { m_BasicTab, context, m_Keyboard.keycodeVersion, current, &m_Keyboard,
                        [&](const Keycode& keycode) -> std::optional<std::string>
                        {
                            if (keychron::IsDksKeycode(codeOf(keycode)))
                                return std::nullopt;
                            return "One Key Multiple Commands sends ordinary keys and modifiers only.";
                        } });
        if (events.picked && keychron::IsDksKeycode(codeOf(*events.picked)))
        {
            PickForCommand(codeOf(*events.picked));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // The sliders the mode has, each at the targets' value or mixed. Unknown: theirs differ.
    void SwitchesSection::DrawDistances(const std::vector<size_t>& targets, Mode mode)
    {
        const bool actuation = std::all_of(targets.begin(), targets.end(),
                                           [&](size_t key) { return UsesActuation(ConfigOf(key)); });
        if (!actuation)
            return;

        const auto common = [&](auto field) -> std::optional<uint8_t>
        {
            std::optional<uint8_t> value;
            for (const size_t key : targets)
            {
                const uint8_t own = static_cast<uint8_t>(field(ConfigOf(key)));
                if (value && *value != own)
                    return std::nullopt;
                value = own;
            }
            return value;
        };

        const uint8_t full = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
        if (!ImGui::BeginTable("settings", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);
        DrawDistance("Actuation Distance", "##actuation", "How far the key goes down before it counts as pressed.",
                     common([](const keychron::KeyConfig& config) { return config.actuationPoint; }), full,
                     Field::Actuation);
        if (mode == Mode::RapidTrigger)
        {
            const uint8_t most = std::min(c_SensitivityMax, full);
            DrawDistance("(Press) Trigger Distance", "##press", "Going down by this much presses it again.",
                         common([](const keychron::KeyConfig& config) { return config.pressSensitivity; }), most,
                         Field::Press);
            DrawDistance("(Release) Reset Distance", "##release", "Coming up by this much releases it.",
                         common([](const keychron::KeyConfig& config) { return config.releaseSensitivity; }), most,
                         Field::Release);
        }
        ImGui::EndTable();
    }

    // What the keys differ in, by what they do: a Regular key's Trigger and Reset Distances do nothing,
    // so they set no key apart -- Gaming's keys, alike on the board, differed only there (Rico,
    // 2026-11-08).
    void SwitchesSection::DrawDifferences(const std::vector<size_t>& targets)
    {
        const size_t selected = static_cast<size_t>(std::count(m_Selected.begin(), m_Selected.end(), true));

        const keychron::KeyConfig          first = ConfigOf(targets.front());
        std::optional<keychron::KeyConfig> firstActuated, firstRapid;
        bool                               mode = false, actuation = false, press = false, release = false;
        for (const size_t key : targets)
        {
            const keychron::KeyConfig config = ConfigOf(key);
            mode                             = mode || ModeOf(config) != ModeOf(first) ||
                   (config.advancedMode != keychron::AdvancedMode::None && config.advancedData != first.advancedData);
            if (UsesActuation(config))
            {
                if (!firstActuated)
                    firstActuated = config;
                actuation = actuation || config.actuationPoint != firstActuated->actuationPoint;
            }
            if (ModeOf(config) != Mode::RapidTrigger)
                continue;
            if (!firstRapid)
                firstRapid = config;
            press   = press || config.pressSensitivity != firstRapid->pressSensitivity;
            release = release || config.releaseSensitivity != firstRapid->releaseSensitivity;
        }
        std::vector<std::string> differ;
        if (mode)
            differ.push_back("Mode");
        if (actuation)
            differ.push_back("Actuation Distance");
        if (press)
            differ.push_back("(Press) Trigger Distance");
        if (release)
            differ.push_back("(Release) Reset Distance");

        const std::string which = selected == 0 ? std::to_string(targets.size()) + " keys"
                                                : "These " + std::to_string(targets.size()) + " keys";
        ColouredText(PanelColour::Muted, "%s", (which + " differ in their " + JoinNames(differ) + ".").c_str());

        // The keys apart from what most do, named when few, and a button selecting them -- one of
        // 104 is hard to find on the board (Rico, 2026-11-08).
        std::vector<std::pair<keychron::KeyConfig, std::vector<size_t>>> groups;
        for (const size_t key : targets)
        {
            const keychron::KeyConfig config = ConfigOf(key);
            const auto group = std::find_if(groups.begin(), groups.end(),
                                            [&](const auto& known) { return DoSame(known.first, config); });
            if (group != groups.end())
                group->second.push_back(key);
            else
                groups.push_back({ config, { key } });
        }
        const auto most = std::max_element(groups.begin(), groups.end(), [](const auto& a, const auto& b)
                                           { return a.second.size() < b.second.size(); });
        std::vector<size_t> apart;
        for (const size_t key : targets)
            if (std::find(most->second.begin(), most->second.end(), key) == most->second.end())
                apart.push_back(key);

        std::string named;
        if (apart.size() <= 5)
        {
            std::vector<std::string> names;
            for (const size_t key : apart)
                names.push_back(m_Keys[key].name);
            named = JoinNames(names) + (apart.size() == 1 ? " is" : " are");
        }
        else
        {
            named = std::to_string(apart.size()) + " keys are";
        }
        ImGui::AlignTextToFramePadding();
        ColouredText(PanelColour::Muted, "Most are %s; %s not.", Briefly(most->first).c_str(), named.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(apart.size() == 1 ? "Select it" : "Select them"))
        {
            m_Selected.assign(m_Keys.size(), false);
            for (const size_t key : apart)
                m_Selected[key] = true;
        }
        ColouredText(PanelColour::Muted, "%s", differ.size() == 1
                                                   ? "Setting it sets it on all of them."
                                                   : "Setting one sets that one on all of them; the rest stays as each key "
                                                     "has it.");
    }

    // A distance as a slider from 0.1 mm: shown while it moves, written when let go.
    void SwitchesSection::DrawDistance(const char* label, const char* id, const char* tip, std::optional<uint8_t> value,
                                       uint8_t maximum, int field)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        ImGui::TableSetColumnIndex(1);

        const bool dragging = m_DraggedField == field;
        int        number   = dragging ? m_DraggedValue : (value ? *value : (1 + maximum) / 2);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
        ImGui::SliderInt(id, &number, 1, maximum, "", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemActive())
        {
            m_DraggedField = field;
            m_DraggedValue = number;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            keychron::TravelChange change;
            const uint8_t          chosen = static_cast<uint8_t>(number);
            if (field == Field::Actuation)
                change.actuationPoint = chosen;
            else if (field == Field::Press)
                change.pressSensitivity = chosen;
            else
                change.releaseSensitivity = chosen;
            m_DraggedField.reset();
            Ask(change);
        }
        else if (dragging && !ImGui::IsItemActive())
        {
            m_DraggedField.reset();
        }

        ImGui::SameLine();
        if (dragging || value)
            ImGui::Text("%s mm", Millimetres(dragging ? m_DraggedValue : *value).c_str());
        else
            ColouredText(PanelColour::Muted, "mixed");
    }

    void SwitchesSection::DrawTest()
    {
        const bool onPair = m_ShowPairs && ShownPair();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Test");
        if (!m_Testing)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - ButtonWidth("Test keys")));
            if (ImGui::Button("Test keys"))
            {
                m_Testing = true;
                m_AddingPair = false;
                m_PairFirst.reset();
                const std::optional<uint8_t> pair = ShownPair();
                ForgetPair();
                m_Pair = pair;
                m_Player.Reset();
                m_Trail.Clear();
                m_Travel.reset();
                m_ClampReadings = 0;
                m_Followed.reset();
                for (size_t key = 0; key < m_Selected.size() && !m_Followed; ++key)
                    if (m_Selected[key])
                        m_Followed = key;
            }
            ImGui::PushTextWrapPos(0.0f);
            if (onPair)
            {
                ColouredText(PanelColour::Muted, "Press the pair's keys on the board to see how far they go, and which the "
                                                 "computer receives. Meanwhile keys do nothing else in Nazg.");
                ImGui::PopTextWrapPos();
                return;
            }
            ColouredText(PanelColour::Muted, "Press keys on the board to see how far they go, and where they are pressed "
                                             "and released with these settings. While testing, pressing a key does not "
                                             "select it.%s", HasGamepadKeys() ? " The gamepad shows here too." : "");
            ImGui::PopTextWrapPos();
            return;
        }

        ImGui::SameLine();
        const float right = (onPair ? 0.0f
                                    : ImGui::CalcTextSize("Raw values").x + ImGui::GetFrameHeight() +
                                          ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x) +
                            ButtonWidth("Stop");
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - right));
        if (!onPair)
        {
            ImGui::Checkbox("Raw values", &m_Raw);
            ImGui::SameLine();
        }
        if (ImGui::Button("Stop"))
        {
            m_Testing = false;
            m_Followed.reset();
            m_Travel.reset();
            m_Levels.reset();
            m_PairTrail.Clear();
            m_PairSent = {};
            std::fill(std::begin(m_PairTravel), std::end(m_PairTravel), std::nullopt);
            return;
        }
        if (onPair)
        {
            DrawPairTest();
            return;
        }

        // What the computer receives, under the key followed, on a profile with gamepad keys.
        const auto gamepad = [&]
        {
            if (!HasGamepadKeys())
                return;
            GamepadTestInput input;
            input.isSeen = m_Controller.isSeen;
            input.isXbox = m_Controller.isXbox;
            input.others = m_Controller.others;
            input.levels = m_Levels ? &*m_Levels : nullptr;
            input.trail  = &m_Trail;
            input.whyNotSeen =
                m_GamepadMode && (*m_GamepadMode & keychron::GamepadMode::RecognitionOff)
                    ? "Controller Recognition Feature is off."
                    : "It is set to be seen: plug it in again.";
            const auto playsTrigger = [&](uint8_t control)
            {
                return std::any_of(Shown()->keys.begin(), Shown()->keys.end(), [&](const keychron::KeyConfig& key)
                                   { return key.advancedMode == keychron::AdvancedMode::Gamepad && key.advancedData == control; });
            };
            input.sharesTriggers =
                playsTrigger(keychron::c_GamepadLeftTrigger) && playsTrigger(keychron::c_GamepadRightTrigger);
            ImGui::Separator();
            DrawGamepadTest(input);
        };

        if (!m_Followed)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "%s", m_MatrixRefused
                                                       ? "This firmware does not report its switches: select a key, then "
                                                         "click Test keys."
                                                       : "Press a key on the board.");
            ImGui::PopTextWrapPos();
            gamepad();
            return;
        }

        const keychron::KeyConfig config = ConfigOf(*m_Followed);
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(m_Keys[*m_Followed].name.c_str());
        ImGui::PopFont();
        if (m_Travel)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s mm",
                         FineMillimetres(*m_Travel, m_FullTravel ? m_FullTravel : c_SourceFullTravel).c_str());
            if (m_Travel->pressed)
            {
                ImGui::SameLine();
                ColouredText(PanelColour::Success, "pressed");
            }
        }

        const keychron::RealtimeTravel travel = m_Travel.value_or(keychron::RealtimeTravel{});
        ImGui::BeginGroup();
        if (ModeOf(config) == Mode::Dks)
        {
            // What its slot sends, played from the travel as the firmware plays it.
            std::string sent;
            if (config.advancedData < Shown()->dksSlots.size())
                for (const keychron::DksPlayer::Sent& one : m_Player.SentSince())
                    sent += (sent.empty() ? "" : ", ") +
                            KeycodeName(Shown()->dksSlots[config.advancedData].keycodes[one.command]) +
                            (one.down ? " down" : " up");
            ImGui::PushTextWrapPos(m_Raw ? ImGui::GetCursorPosX() + TravelBarWidth() : 0.0f);
            ColouredText(PanelColour::Muted, "Sent");
            ImGui::TextUnformatted(sent.empty() ? "nothing yet" : sent.c_str());
            ImGui::PopTextWrapPos();
        }
        else
        {
            if (ModeOf(config) == Mode::LongPress && *m_Followed < m_WasPressed.size())
                ColouredText(m_WasPressed[*m_Followed] ? PanelColour::Success : PanelColour::Muted, "%s",
                             m_WasPressed[*m_Followed] ? "held down" : "let go");
            // With the gamepad under it, the bar lies down: the column stays as tall.
            if (HasGamepadKeys() && !m_Raw)
            {
                const uint8_t full = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
                ImGui::ProgressBar(std::min(1.0f, static_cast<float>(travel.travelSixtieths) / (full * 6.0f)),
                                   ImVec2(-FLT_MIN, ImGui::GetFontSize() * 0.6f), "");
            }
            else
            {
                DrawTravelBar(travel, config);
            }
        }
        ImGui::EndGroup();
        if (!m_Raw)
        {
            gamepad();
            return;
        }

        ImGui::SameLine();
        if (ImGui::BeginTable("raw", 2, ImGuiTableFlags_SizingFixedFit))
        {
            const auto row = [](const char* name, const std::string& value, const char* tip)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ColouredText(PanelColour::Muted, "%s", name);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(value.c_str());
            };
            const bool known = m_Travel.has_value();
            row("Sensor now", known ? std::to_string(travel.rawAdc) : "-",
                "The raw ADC reading: it falls as the magnet comes closer.");
            row("At rest", known ? std::to_string(travel.restValue) : "-",
                "Calibrated: taken at every power-up, and kept up to date while the board is typed on.");
            row("At the bottom", known ? std::to_string(travel.bottomValue) : "-",
                "Calibrated: what this key reads pressed fully down.");
            char scale[32] = "-";
            if (m_Calibration && m_CalibrationOf == m_Followed)
                std::snprintf(scale, sizeof(scale), "%.2f", m_Calibration->scaleFactor);
            row("Scale factor", scale, "Turns this key's readings into the board's travel, from its calibration.");
            ImGui::EndTable();
        }
        gamepad();
    }

    // The key's travel top to bottom -- rest at the top -- with where it presses and releases.
    void SwitchesSection::DrawTravelBar(const keychron::RealtimeTravel& travel, const keychron::KeyConfig& config)
    {
        const float   unit   = ImGui::GetFontSize();
        const float   height = unit * 7.5f;
        const float   bar    = unit * c_BarWidth;
        const float   scale  = unit * c_BarScale;
        const uint8_t full   = m_FullTravel ? m_FullTravel : c_SourceFullTravel;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(TravelBarWidth(), height + unit * 0.5f));
        ImDrawList* list = ImGui::GetWindowDrawList();

        const float top = origin.y + unit * 0.25f;
        const auto  y   = [&](int tenths) { return top + height * static_cast<float>(tenths) / full; };
        const float x0  = origin.x + scale;

        const ImU32 muted  = ImGui::GetColorU32(ColourOf(PanelColour::Muted));
        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        for (int tenths = 0; tenths <= full; tenths += 5)
        {
            const std::string label = Millimetres(tenths);
            const ImVec2      size  = ImGui::CalcTextSize(label.c_str());
            list->AddText(ImVec2(x0 - unit * 0.4f - size.x, y(tenths) - size.y * 0.5f), muted, label.c_str());
        }
        list->AddRect(ImVec2(x0, y(0)), ImVec2(x0 + bar, y(full)), ImGui::GetColorU32(ImGuiCol_Border), 3.0f);
        const float depth = std::min(static_cast<float>(travel.travelSixtieths) / 6.0f, static_cast<float>(full));
        if (depth > 0.0f)
            list->AddRectFilled(ImVec2(x0 + 1, y(0) + 1), ImVec2(x0 + bar - 1, top + height * depth / full),
                                ImGui::GetColorU32(ImGuiCol_CheckMark, travel.pressed ? 0.6f : 0.3f), 2.0f);

        const auto mark = [&](int tenths, ImU32 colour, const std::string& words, float nudge)
        {
            list->AddLine(ImVec2(x0 - 2, y(tenths)), ImVec2(x0 + bar + 4, y(tenths)), colour, 2.0f);
            list->AddText(ImVec2(x0 + bar + unit * c_BarGap, y(tenths) - unit * 0.5f + nudge), colour, words.c_str());
        };
        // 0.3 mm apart is less than a line on the bar: the words are pushed apart, the lines stay true.
        const int   pressed  = config.actuationPoint;
        const int   released = ReleasePoint(config.actuationPoint);
        const float apart    = y(pressed) - y(released);
        const float push     = apart < unit * 1.1f ? (unit * 1.1f - apart) / 2.0f : 0.0f;
        if (!UsesActuation(config))
            return;
        mark(pressed, accent, "pressed " + Millimetres(pressed), push);
        mark(released, muted, "released above " + Millimetres(released), -push);
    }

    void SwitchesSection::DrawResetQuestion()
    {
        if (!m_ResetAsked)
            return;

        const std::string id = "Reset " + ProfileName(m_ShownProfile) + "?###switchesreset";
        if (!ImGui::IsPopupOpen(id.c_str()))
            ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f));
        if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            m_ResetAsked = false;
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        // Each profile goes back to its own preset -- Default to Regular, Gaming to Rapid Trigger,
        // Gamepad to its gamepad keys -- not to one set of defaults. The firmware keeps them: Nazg's
        // own reset brought them back on the K10 HE V3 (2026-11-08). What it will be is the board's
        // to say.
        ImGui::Text("%s goes back to the settings the board was made with for it. Every change made to its keys "
                    "since is lost: their distances, Rapid Trigger, and any advanced key or SOCD pair.",
                    ProfileName(m_ShownProfile).c_str());
        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "This cannot be undone.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const float width = ButtonWidth("Cancel") + ButtonWidth("Reset") + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        if (ImGui::Button("Cancel"))
        {
            m_ResetAsked = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ColourOf(PanelColour::Error));
        if (ImGui::Button("Reset"))
        {
            m_ResetAsked   = false;
            m_PendingReset = static_cast<uint8_t>(m_ShownProfile);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------------------------------
    // Reading and writing.

    Task<void> SwitchesSection::Open()
    {
        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;
        try
        {
            m_Channel.emplace(co_await HidDeviceChannel::Open(m_Transport, m_Path));
            keychron::Protocol keychron(*m_Channel);

            m_Geometry = co_await keychron.GetProfileGeometry();
            if (!m_Geometry.Fits(rows, columns))
            {
                Fail("This board's switch settings do not fit its definition: " + std::to_string(rows) + " rows of " +
                     std::to_string(columns) + " columns would need profiles of " +
                     std::to_string(keychron::ProfileSizeFor(rows, columns, m_Geometry.dksSlots, m_Geometry.socdSlots)) +
                     " bytes, the board keeps " + std::to_string(m_Geometry.profileSize) +
                     ". The definition describes another matrix -- nothing can be placed.");
                co_return;
            }

            if (!m_TravelMeasured)
                m_FullTravel = keychron::EstimateFullTravel(co_await keychron.GetCurve());

            // The keys' names as the board types by default -- layer 2, Windows', on a K10 HE.
            try
            {
                const uint8_t layer = co_await keychron.GetDefaultLayer();
                m_NameLayer         = layer < m_Keyboard.keymap.Layers() ? layer : uint8_t{ 0 };
            }
            catch (const ProtocolError&)
            {
            }

            // The gamepad's settings; a firmware without them keeps the rest.
            m_GamepadMode.reset();
            m_HasRecognition = false;
            try
            {
                m_GamepadMode    = co_await keychron.GetGamepadMode();
                m_HasRecognition = keychron::HasRecognitionSwitch((co_await keychron.GetIdentity()).protocolVersion);
            }
            catch (const ProtocolError&)
            {
            }

            std::vector<keychron::Profile> profiles;
            for (uint8_t profile = 0; profile < m_Geometry.profileCount; ++profile)
                profiles.push_back(keychron::DecodeProfile(co_await keychron.GetProfileRaw(profile, m_Geometry), rows,
                                                           columns, m_Geometry.dksSlots, m_Geometry.socdSlots));

            m_Profiles     = std::move(profiles);
            m_ShownProfile = m_Geometry.currentProfile;
            m_WasPressed.assign(m_Keys.size(), false);
            m_LastInUseRead = ImGui::GetTime();
            m_Link          = Link::Open;
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The switch settings could not be read: ") + failure.what());
        }
    }

    Task<void> SwitchesSection::Poll(bool readProfileInUse)
    {
        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;
        try
        {
            keychron::Protocol keychron(*m_Channel);

            // The switches: a key newly pressed selects, or is followed while testing.
            if (!m_MatrixRefused)
            {
                try
                {
                    const via::SwitchMatrixState state = co_await keychron.GetSwitchMatrixState(
                        via::SwitchMatrixFormatFor(m_Keyboard.report.viaProtocol), rows, columns);
                    m_WasPressed.resize(m_Keys.size(), false);
                    for (size_t key = 0; key < m_Keys.size(); ++key)
                    {
                        const bool pressed = state.IsPressed(m_Keys[key].position.row, m_Keys[key].position.column);
                        if (pressed && !m_WasPressed[key])
                            PressSeen(key);
                        m_WasPressed[key] = pressed;
                    }
                }
                catch (const ProtocolError&)
                {
                    m_MatrixRefused = true;
                }
            }

            // The pair tested: both its keys.
            if (IsTestingPair())
            {
                const uint8_t                  slot   = *ShownPair();
                const keychron::SocdSlot       pair   = Shown()->socdSlots[slot];
                const keychron::RealtimeTravel first  = co_await keychron.GetRealtimeTravel(pair.firstRow, pair.firstColumn);
                const keychron::RealtimeTravel second = co_await keychron.GetRealtimeTravel(pair.secondRow, pair.secondColumn);
                if (IsTestingPair() && ShownPair() == slot && Shown()->socdSlots[slot] == pair)
                    PairRead(pair, first, second);
            }

            // The key followed, live; its scale factor once.
            else if (m_Testing && m_Followed && *m_Followed < m_Keys.size())
            {
                const size_t                key     = *m_Followed;
                const keychron::KeyPosition at      = m_Keys[key].position;
                const keychron::RealtimeTravel read = co_await keychron.GetRealtimeTravel(at.row, at.column);
                if (m_Followed == key)
                {
                    m_Travel = read;
                    const keychron::KeyConfig followed = ConfigOf(key);
                    if (ModeOf(followed) == Mode::Dks && followed.advancedData < Shown()->dksSlots.size())
                        m_Player.Step(Shown()->dksSlots[followed.advancedData],
                                      std::min<int>(read.travelSixtieths, (m_FullTravel ? m_FullTravel : c_SourceFullTravel) * 6));

                    // At the bottom the 1/60 mm field pins at (F + 1) x 6 - 1: full travel, measured
                    // (keychron-commands.md, "How travel is computed").
                    // The sensor past the key's calibrated bottom, not only the field at a whole
                    // 0.1 mm + 5: a key held still at 2.58 mm reads 155 = 25 x 6 + 5 too, which once
                    // measured a 2.5 mm board (Rico, 2026-11-08). The sensor falls as the key goes down.
                    if (read.pressed && read.travel > 0 && read.travelSixtieths == read.travel * 6 + 5 &&
                        read.rawAdc < read.bottomValue)
                    {
                        if (++m_ClampReadings >= c_ClampReadingsToMeasure && !m_TravelMeasured)
                        {
                            m_FullTravel     = read.travel;
                            m_TravelMeasured = true;
                        }
                    }
                    else
                    {
                        m_ClampReadings = 0;
                    }
                }
                if (m_Raw && m_CalibrationOf != key)
                {
                    m_Calibration   = co_await keychron.GetCalibratedValue(at.row, at.column);
                    m_CalibrationOf = key;
                }
            }

            // The profile in use: Fn + P + Z, X or C switches it from the keyboard, and the section
            // follows, as Keychron's Launcher does.
            if (readProfileInUse)
            {
                const keychron::ProfileGeometry geometry = co_await keychron.GetProfileGeometry();
                if (geometry.currentProfile != m_Geometry.currentProfile && !m_PendingSelect &&
                    geometry.currentProfile < m_Profiles.size())
                {
                    m_ShownProfile = geometry.currentProfile;
                    ForgetPair();
                    Say("switched to " + ProfileName(m_ShownProfile) + " from the keyboard", false);
                }
                m_Geometry.currentProfile = geometry.currentProfile;
            }
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The board stopped answering: ") + failure.what());
        }
    }

    Task<void> SwitchesSection::Write(PendingChange change)
    {
        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;
        try
        {
            keychron::Protocol keychron(*m_Channel);
            for (const keychron::AdvancedWrite& write : change.advanced)
                co_await keychron.SetKeyAdvanced(change.profile, write);
            for (const keychron::TravelWrite& write : change.writes)
                co_await keychron.SetKeysTravel(change.profile, write.config, write.keys);
            for (const keychron::SocdWrite& write : change.socd)
                co_await keychron.SetSocd(change.profile, write);
            co_await keychron.SaveProfile(change.profile);

            // Read back: what the board kept.
            keychron::Profile kept = keychron::DecodeProfile(co_await keychron.GetProfileRaw(change.profile, m_Geometry),
                                                             rows, columns, m_Geometry.dksSlots, m_Geometry.socdSlots);
            bool same = true;
            for (const keychron::AdvancedWrite& write : change.advanced)
                same = same && keychron::Holds(kept, columns, write);
            for (const keychron::SocdWrite& write : change.socd)
                same = same && keychron::Holds(kept, write);
            for (const keychron::TravelWrite& write : change.writes)
                for (const keychron::KeyPosition key : write.keys)
                {
                    const keychron::KeyConfig now = keychron::EffectiveConfig(kept, key, columns);
                    same = same && now.mode == write.config.mode && now.actuationPoint == write.config.actuationPoint &&
                           now.pressSensitivity == write.config.pressSensitivity &&
                           now.releaseSensitivity == write.config.releaseSensitivity;
                }

            // A newer change waiting keeps what the user sees; it is read back after it.
            const bool newer = std::any_of(m_PendingChanges.begin(), m_PendingChanges.end(),
                                           [&](const PendingChange& next) { return next.profile == change.profile; });
            if (!newer && change.profile < m_Profiles.size())
                m_Profiles[change.profile] = std::move(kept);

            Say(same ? "Saved, read back the same" : "The board kept something else -- shown as it is", !same);
        }
        catch (const ProtocolError& failure)
        {
            Say(std::string("The board refused it: ") + failure.what(), true);
            m_Link = Link::Closed;   // read everything again
            m_Channel.reset();
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The write failed: ") + failure.what());
        }
    }

    Task<void> SwitchesSection::Select(uint8_t profile)
    {
        try
        {
            keychron::Protocol keychron(*m_Channel);
            co_await keychron.SelectProfile(profile);
            m_Geometry.currentProfile = (co_await keychron.GetProfileGeometry()).currentProfile;
            m_ShownProfile            = m_Geometry.currentProfile;
            Say(m_ShownProfile == profile ? "The board now types with it" : "The board kept " + ProfileName(m_ShownProfile),
                m_ShownProfile != profile);
            m_LastInUseRead = ImGui::GetTime();
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The profile could not be changed: ") + failure.what());
        }
    }

    // The bits asked, and only those: the byte is read again first, the Launcher may have changed it and a
    // firmware may keep bits Nazg does not know.
    Task<void> SwitchesSection::WriteGamepadMode(uint8_t mask, uint8_t value)
    {
        try
        {
            keychron::Protocol keychron(*m_Channel);
            const uint8_t      now    = co_await keychron.GetGamepadMode();
            const uint8_t      wanted = static_cast<uint8_t>((now & ~mask) | (value & mask));
            const uint8_t      kept   = co_await keychron.SetGamepadMode(wanted);
            m_GamepadMode             = kept;
            const bool replug = (mask & (keychron::GamepadMode::XInput | keychron::GamepadMode::RecognitionOff)) != 0;
            Say(kept != wanted ? "The board kept something else -- shown as it is"
                : replug       ? "Saved: plug the board in again for it to take effect"
                               : "Saved, read back the same",
                kept != wanted);
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The gamepad's settings could not be changed: ") + failure.what());
        }
    }

    Task<void> SwitchesSection::Reset(uint8_t profile)
    {
        const uint8_t rows    = m_Keyboard.definition.matrixRows;
        const uint8_t columns = m_Keyboard.definition.matrixColumns;
        try
        {
            keychron::Protocol keychron(*m_Channel);
            co_await keychron.ResetProfile(profile);
            if (profile < m_Profiles.size())
                m_Profiles[profile] = keychron::DecodeProfile(co_await keychron.GetProfileRaw(profile, m_Geometry), rows,
                                                              columns, m_Geometry.dksSlots, m_Geometry.socdSlots);
            Say("Reset to the board's defaults", false);
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The profile could not be reset: ") + failure.what());
        }
    }
}
