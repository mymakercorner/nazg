// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSwitchesSection.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iterator>
#include <utility>

#include "imgui.h"

#include "adapters/NazgProtocolError.h"
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

        // Two keys doing the same: mode and Actuation Distance, and the two distances only in Rapid
        // Trigger -- a Regular key's do nothing.
        bool DoSame(const keychron::KeyConfig& a, const keychron::KeyConfig& b)
        {
            if (a.mode != b.mode || a.actuationPoint != b.actuationPoint)
                return false;
            return a.mode != keychron::KeyMode::RapidTrigger ||
                   (a.pressSensitivity == b.pressSensitivity && a.releaseSensitivity == b.releaseSensitivity);
        }

        // "Regular, 1.7 mm", "Rapid Trigger, 1.7 mm, Trigger 0.4, Reset 0.4".
        std::string Briefly(const keychron::KeyConfig& config)
        {
            if (config.mode != keychron::KeyMode::RapidTrigger)
                return "Regular, " + Millimetres(config.actuationPoint) + " mm";
            return "Rapid Trigger, " + Millimetres(config.actuationPoint) + " mm, Trigger " +
                   Millimetres(config.pressSensitivity) + ", Reset " + Millimetres(config.releaseSensitivity);
        }

        std::string Says(const keychron::KeyConfig& config)
        {
            const std::string pressed  = Millimetres(config.actuationPoint);
            const std::string released = Millimetres(ReleasePoint(config.actuationPoint));
            if (config.mode != keychron::KeyMode::RapidTrigger)
                return "Pressed when it goes down past " + pressed + " mm, released when it comes back above " +
                       released + " mm.";
            // The key's movement, step by step; the sliders above name the settings (Rico, 2026-11-08: the
            // first wording, naming them in the sentence, was unclear).
            return "Pressed at " + pressed + " mm. From there, at any depth: released as soon as it comes up " +
                   Millimetres(config.releaseSensitivity) + " mm, pressed again as soon as it goes down " +
                   Millimetres(config.pressSensitivity) + " mm. Back above " + released + " mm, it starts over.";
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

    void SwitchesSection::WhileHidden()
    {
        m_HiddenThisFrame = true;
        m_ResetAsked      = false;
        m_Testing         = false;
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

    std::vector<size_t> SwitchesSection::Targets() const
    {
        std::vector<size_t> targets;
        for (size_t key = 0; key < m_Selected.size(); ++key)
            if (m_Selected[key])
                targets.push_back(key);
        if (targets.empty())
            for (size_t key = 0; key < m_Keys.size(); ++key)
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
    // The strip: the profiles. Picking one makes the board use it.

    Strip SwitchesSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Profile";
        for (size_t profile = 0; profile < m_Profiles.size(); ++profile)
            strip.entries.push_back(ProfileName(profile));
        strip.chosen = m_ShownProfile;
        return strip;
    }

    void SwitchesSection::OnStripChosen(size_t entry)
    {
        if (entry >= m_Profiles.size() || entry == m_ShownProfile)
            return;
        m_ShownProfile  = entry;
        m_PendingSelect = static_cast<uint8_t>(entry);
        m_Message.clear();
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    void SwitchesSection::DescribeBoard(BoardDescription& board)
    {
        if (!std::exchange(m_HiddenThisFrame, false))
            Step();

        // The names first, as the keycaps read on the board's default layer, then every switch's
        // Actuation Distance under it, and RT for Rapid Trigger.
        DescribeLegends(board, m_Keyboard, m_NameLayer, m_Legends, SpaceBar::Named);
        if (m_KeysSelection != m_Keyboard.layoutSelection || m_KeysNameLayer != m_NameLayer ||
            m_BoardKeys.size() != board.keys.size())
            FindKeys(board);

        const keychron::Profile* profile = Shown();
        const uint8_t            full    = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
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
                described[LegendSlot::BottomLeft] = { Millimetres(config.actuationPoint), LegendRole::Bound };
                if (config.mode == keychron::KeyMode::RapidTrigger)
                    described[LegendSlot::BottomRight] = { "RT", LegendRole::Label, CommandCategory::Behaviour };
            }

            if (key < m_Selected.size() && m_Selected[key])
                described.marks |= Mark::Selected;
            if (m_Testing && m_Followed == key && m_Travel)
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
        // A key's settings in full on hover: the key holds one value, the tooltip all of them.
        if (events.hoveredKey && *events.hoveredKey < m_BoardKeys.size() && m_BoardKeys[*events.hoveredKey] && Shown())
        {
            const size_t              key    = *m_BoardKeys[*events.hoveredKey];
            const keychron::KeyConfig config = ConfigOf(key);
            std::string               tip    = m_Keys[key].name + "\n" +
                                (config.mode == keychron::KeyMode::RapidTrigger ? "Rapid Trigger" : "Regular") +
                                "\nActuation Distance " + Millimetres(config.actuationPoint) + " mm";
            if (config.mode == keychron::KeyMode::RapidTrigger)
                tip += "\n(Press) Trigger Distance " + Millimetres(config.pressSensitivity) +
                       " mm\n(Release) Reset Distance " + Millimetres(config.releaseSensitivity) + " mm";
            ImGui::SetTooltip("%s", tip.c_str());
        }

        if (!events.clickedKey || *events.clickedKey >= m_BoardKeys.size() || !m_BoardKeys[*events.clickedKey])
            return;
        const size_t key = *m_BoardKeys[*events.clickedKey];
        if (key < m_Selected.size())
            m_Selected[key] = !m_Selected[key];
    }

    // A key pressed on the board: followed while testing, else selected or unselected, as a click.
    void SwitchesSection::PressSeen(size_t key)
    {
        if (m_Testing)
        {
            if (m_Followed != key)
            {
                m_Travel.reset();
                m_ClampReadings = 0;
            }
            m_Followed = key;
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
        PendingChange       pending{ static_cast<uint8_t>(m_ShownProfile),
                                     keychron::PlanTravelWrites(profile, columns, keys, change) };
        for (const keychron::TravelWrite& write : pending.writes)
            keychron::ApplyTravelWrite(profile, columns, write);
        m_PendingChanges.push_back(std::move(pending));
        m_Message.clear();
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

        DrawHeader();
        ImGui::Separator();

        // The Test column as wide as what it holds -- the bar and its words, the raw values beside them
        // -- so nothing in it collides (Rico, 2026-11-08), and never narrower than its sentence needs.
        const ImGuiStyle& style    = ImGui::GetStyle();
        const float       header   = ImGui::CalcTextSize("Test").x + ImGui::CalcTextSize("Raw values").x +
                                     ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ButtonWidth("Stop") +
                                     style.ItemSpacing.x * 3;
        float             contents = TravelBarWidth();
        if (m_Testing && m_Raw)
            contents += style.ItemSpacing.x + RawTableWidth();
        const float testWidth = std::max({ ImGui::GetFontSize() * 17.0f, header, contents });
        if (ImGui::BeginTable("switches", 2, ImGuiTableFlags_BordersInnerV))
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

    void SwitchesSection::DrawHeader()
    {
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(ProfileName(m_ShownProfile).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (m_PendingSelect || (IsBusy() && !m_RequestIsPoll && m_Geometry.currentProfile != m_ShownProfile))
            ColouredText(PanelColour::Muted, "switching...");
        else
            ColouredText(PanelColour::Success, "the board uses it");

        ImGui::SameLine();
        if (!m_PendingChanges.empty() || (IsBusy() && !m_RequestIsPoll))
            ColouredText(PanelColour::Muted, "Writing...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());

        // At the right end: full travel, and Reset profile.
        const std::string travel = "Full travel " + Millimetres(m_FullTravel ? m_FullTravel : c_SourceFullTravel) +
                                   " mm " + (m_TravelMeasured ? "(measured)" : "(estimated)");
        const char* reset = "Reset profile...";
        const float width = ImGui::CalcTextSize(travel.c_str()).x + ButtonWidth(reset) + ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - width));
        ColouredText(PanelColour::Muted, "%s", travel.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", m_TravelMeasured
                                        ? "Read from a key pressed fully down.\n\nEvery key is calibrated to this travel, "
                                          "whatever its switch: with a longer switch fitted, an Actuation Distance is the "
                                          "same share of its travel, so a little deeper in fact."
                                        : "Estimated from the board's gamepad curve: no command reports it. A key pressed "
                                          "fully down while testing measures it.\n\nEvery key is calibrated to this travel, "
                                          "whatever its switch: with a longer switch fitted, an Actuation Distance is the "
                                          "same share of its travel, so a little deeper in fact.");
        ImGui::SameLine();
        ImGui::BeginDisabled(IsBusy() && !m_RequestIsPoll);
        if (ImGui::Button(reset))
            m_ResetAsked = true;
        ImGui::EndDisabled();
    }

    void SwitchesSection::DrawEditor()
    {
        const std::vector<size_t> targets  = Targets();
        const size_t              selected = static_cast<size_t>(std::count(m_Selected.begin(), m_Selected.end(), true));

        // What it acts on, Select All and Clear at the right.
        ImGui::AlignTextToFramePadding();
        if (selected == 0)
        {
            ImGui::Text("All %zu keys", m_Keys.size());
        }
        else
        {
            std::vector<std::string> names;
            for (size_t i = 0; i < targets.size() && i < 10; ++i)
                names.push_back(m_Keys[targets[i]].name);
            std::string list = JoinNames(names);
            if (targets.size() > 10)
                list += " and " + std::to_string(targets.size() - 10) + " more";
            ImGui::Text("%zu key%s:", targets.size(), targets.size() == 1 ? "" : "s");
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "%s", list.c_str());
        }
        const float buttons = ButtonWidth("Select All") + ButtonWidth("Clear") + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - buttons));
        ImGui::BeginDisabled(selected == m_Keys.size());
        if (ImGui::Button("Select All"))
            m_Selected.assign(m_Keys.size(), true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(selected == 0);
        if (ImGui::Button("Clear"))
            m_Selected.assign(m_Keys.size(), false);
        ImGui::EndDisabled();
        ImGui::Spacing();

        // The settings the keys share, or none where they differ.
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
        const std::optional<uint8_t> mode =
            common([](const keychron::KeyConfig& config) { return static_cast<uint8_t>(config.mode); });
        const bool rapid = mode == static_cast<uint8_t>(keychron::KeyMode::RapidTrigger);

        const uint8_t full = m_FullTravel ? m_FullTravel : c_SourceFullTravel;
        if (ImGui::BeginTable("settings", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("control", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Mode");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::RadioButton("Regular", mode == static_cast<uint8_t>(keychron::KeyMode::Regular)) &&
                mode != static_cast<uint8_t>(keychron::KeyMode::Regular))
                Ask({ keychron::KeyMode::Regular, std::nullopt, std::nullopt, std::nullopt });
            ImGui::SameLine();
            if (ImGui::RadioButton("Rapid Trigger", rapid) && !rapid)
                Ask({ keychron::KeyMode::RapidTrigger, std::nullopt, std::nullopt, std::nullopt });
            if (!mode)
            {
                ImGui::SameLine();
                ColouredText(PanelColour::Muted, "mixed");
            }

            DrawDistance("Actuation Distance", "##actuation", "How far the key goes down before it counts as pressed.",
                         common([](const keychron::KeyConfig& config) { return config.actuationPoint; }), full,
                         Field::Actuation);
            if (rapid)
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

        // What it does, in words, when the keys all do the same.
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        // What the keys differ in, by what they do: a Regular key's Trigger and Reset Distances do
        // nothing, so they set no key apart -- Gaming's keys, alike on the board, differed only there
        // (Rico, 2026-11-08).
        std::vector<std::string> differ;
        if (!targets.empty())
        {
            const keychron::KeyConfig  first = ConfigOf(targets.front());
            std::optional<keychron::KeyConfig> firstRapid;
            bool mode = false, actuation = false, press = false, release = false;
            for (const size_t key : targets)
            {
                const keychron::KeyConfig config = ConfigOf(key);
                mode      = mode || config.mode != first.mode;
                actuation = actuation || config.actuationPoint != first.actuationPoint;
                if (config.mode != keychron::KeyMode::RapidTrigger)
                    continue;
                if (!firstRapid)
                    firstRapid = config;
                press   = press || config.pressSensitivity != firstRapid->pressSensitivity;
                release = release || config.releaseSensitivity != firstRapid->releaseSensitivity;
            }
            if (mode)
                differ.push_back("Mode");
            if (actuation)
                differ.push_back("Actuation Distance");
            if (press)
                differ.push_back("(Press) Trigger Distance");
            if (release)
                differ.push_back("(Release) Reset Distance");
        }

        if (!targets.empty() && differ.empty())
        {
            ColouredText(PanelColour::Muted, "%s", Says(ConfigOf(targets.front())).c_str());
        }
        else if (!targets.empty())
        {
            const std::string which = selected == 0 ? "All " + std::to_string(targets.size()) + " keys"
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
                                                       : "Setting one sets that one on all of them; the rest stays as "
                                                         "each key has it.");
        }
        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "%s", m_MatrixRefused
                                                   ? "Click keys to select them -- this firmware does not report its "
                                                     "switches, so pressing them selects nothing."
                                                   : "Click keys to select them, or press them on the keyboard.");
        ImGui::PopTextWrapPos();
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
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Test");
        if (!m_Testing)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - ButtonWidth("Test keys")));
            if (ImGui::Button("Test keys"))
            {
                m_Testing = true;
                m_Travel.reset();
                m_ClampReadings = 0;
                m_Followed.reset();
                for (size_t key = 0; key < m_Selected.size() && !m_Followed; ++key)
                    if (m_Selected[key])
                        m_Followed = key;
            }
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "Press keys on the board to see how far they go, and where they are pressed "
                                             "and released with these settings. While testing, pressing a key does not "
                                             "select it.");
            ImGui::PopTextWrapPos();
            return;
        }

        ImGui::SameLine();
        const float right = ImGui::CalcTextSize("Raw values").x + ImGui::GetFrameHeight() +
                            ImGui::GetStyle().ItemInnerSpacing.x + ButtonWidth("Stop") + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - right));
        ImGui::Checkbox("Raw values", &m_Raw);
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
        {
            m_Testing = false;
            m_Followed.reset();
            m_Travel.reset();
            return;
        }

        if (!m_Followed)
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Muted, "%s", m_MatrixRefused
                                                       ? "This firmware does not report its switches: select a key, then "
                                                         "click Test keys."
                                                       : "Press a key on the board.");
            ImGui::PopTextWrapPos();
            return;
        }

        const keychron::KeyConfig config = ConfigOf(*m_Followed);
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
        DrawTravelBar(travel, config);
        ImGui::EndGroup();
        if (!m_Raw)
            return;

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
        // Gamepad to its gamepad keys -- not to one set of defaults (seen through Keychron's
        // Launcher on the K10 HE V3, 2026-11-08): what it will be is the board's to say.
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

            // The key followed, live; its scale factor once.
            if (m_Testing && m_Followed && *m_Followed < m_Keys.size())
            {
                const size_t                key     = *m_Followed;
                const keychron::KeyPosition at      = m_Keys[key].position;
                const keychron::RealtimeTravel read = co_await keychron.GetRealtimeTravel(at.row, at.column);
                if (m_Followed == key)
                {
                    m_Travel = read;

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
                    m_Message      = "switched to " + ProfileName(m_ShownProfile) + " from the keyboard";
                    m_IsWarning    = false;
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
            for (const keychron::TravelWrite& write : change.writes)
                co_await keychron.SetKeysTravel(change.profile, write.config, write.keys);
            co_await keychron.SaveProfile(change.profile);

            // Read back: what the board kept.
            keychron::Profile kept = keychron::DecodeProfile(co_await keychron.GetProfileRaw(change.profile, m_Geometry),
                                                             rows, columns, m_Geometry.dksSlots, m_Geometry.socdSlots);
            bool same = true;
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

            m_Message   = same ? "saved, read back the same" : "the board kept something else -- shown as it is";
            m_IsWarning = !same;
        }
        catch (const ProtocolError& failure)
        {
            m_Message   = std::string("the board refused it: ") + failure.what();
            m_IsWarning = true;
            m_Link      = Link::Closed;   // read everything again
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
            m_Message                 = m_ShownProfile == profile ? "the board now types with it"
                                                                  : "the board kept " + ProfileName(m_ShownProfile);
            m_IsWarning               = m_ShownProfile != profile;
            m_LastInUseRead           = ImGui::GetTime();
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The profile could not be changed: ") + failure.what());
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
            m_Message   = "reset to the board's defaults";
            m_IsWarning = false;
        }
        catch (const std::exception& failure)
        {
            Fail(std::string("The profile could not be reset: ") + failure.what());
        }
    }
}
