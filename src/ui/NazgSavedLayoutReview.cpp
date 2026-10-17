// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgSavedLayoutReview.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <utility>

#include "imgui.h"

#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgLineParts.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr const char* c_Question = "Another keyboard's file";

        std::string Plural(size_t count, const char* one, const char* many)
        {
            return std::to_string(count) + " " + (count == 1 ? one : many);
        }

        // "M0 and M3", from slots and their prefix; none past a few, the count says the rest.
        std::string SlotNames(const std::vector<size_t>& slots, const char* prefix)
        {
            std::vector<std::string> names;
            for (size_t slot : slots)
                names.push_back(prefix + std::to_string(slot));
            return JoinNames(names);
        }

        std::string Uid(uint64_t uid)
        {
            char text[24];
            std::snprintf(text, sizeof(text), "0x%016" PRIx64, uid);
            return text;
        }

        void Muted(const std::string& words)
        {
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "%s", words.c_str());
        }
    }

    SavedLayoutReview::SavedLayoutReview(HidTransport& transport, std::string path, const Keyboard& keyboard,
                                         uint16_t vendorId, uint16_t productId, const LegendSettings& legends,
                                         const std::optional<vial::UnlockStatus>& lock, std::string fileName,
                                         vial::SavedLayout file)
        : m_Transport(transport),
          m_Path(std::move(path)),
          m_Keyboard(keyboard),
          m_VendorId(vendorId),
          m_ProductId(productId),
          m_Legends(legends),
          m_Lock(lock),
          m_FileName(std::move(fileName)),
          m_File(std::move(file)),
          m_Preview(keyboard)
    {
        m_Task = Read();
    }

    SavedLayoutReview::Action SavedLayoutReview::TakeAction() noexcept
    {
        return std::exchange(m_Action, Action{});
    }

    bool SavedLayoutReview::NeedsUnlock() const
    {
        return m_Changes.NeedsUnlock(m_File);
    }

    bool SavedLayoutReview::IsLocked() const
    {
        // A VIAL_INSECURE build says it is unlocked and has no keys to unlock with.
        return m_Lock && !m_Lock->unlocked;
    }

    void SavedLayoutReview::StartWriting()
    {
        if (m_Stage != Stage::Review || IsBusy())
            return;
        m_Stage = Stage::Writing;
        m_Task  = Write();
    }

    Task<void> SavedLayoutReview::Read()
    {
        m_Failure.clear();
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            vial::Protocol   protocol(channel);
            m_Board = co_await ReadBoardContents(protocol, m_Keyboard, m_VendorId, m_ProductId);
        }
        catch (const std::exception& failure)
        {
            m_Failure = failure.what();
            co_return;
        }

        Compare();
        m_Stage = IsOtherKeyboard() ? Stage::Asking : Stage::Review;
    }

    Task<void> SavedLayoutReview::Write()
    {
        m_Failure.clear();
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            vial::Protocol   protocol(channel);
            co_await WriteChanges(protocol, m_Keyboard, m_Board, m_File, m_Changes, m_Progress);
        }
        catch (const std::exception& failure)
        {
            m_Failure = failure.what();
        }
        m_Stage = Stage::Written;
    }

    // A Vial board tells by its UID; a VIA board by the VID:PID Nazg writes. Neither said: Vial's
    // own VIA files say nothing, and are not asked about.
    bool SavedLayoutReview::IsOtherKeyboard() const
    {
        const std::optional<uint64_t>& board = m_Board.layout.uid;
        if (board)
            return m_File.uid != board;
        if (m_File.uid)
            return true;
        return m_File.vendorId && m_File.productId && (*m_File.vendorId != m_VendorId || *m_File.productId != m_ProductId);
    }

    void SavedLayoutReview::Compare()
    {
        m_Changes = CompareWithBoard(m_Board, m_File, m_Keyboard);

        // The preview: the board as the file would leave it.
        m_Preview = m_Keyboard;
        for (const KeyChange& key : m_Changes.keys)
            m_Preview.keymap.Set(key.layer, key.row, key.column, key.then);
        for (const KnobChange& knob : m_Changes.knobs)
            (knob.clockwise ? m_Preview.encoderMap[knob.layer][knob.knob].clockwise
                            : m_Preview.encoderMap[knob.layer][knob.knob].counterClockwise) = knob.then;
        if (m_Changes.layoutOptions)
        {
            m_Preview.layoutOptions   = *m_Changes.layoutOptions;
            m_Preview.layoutSelection = DecodeLayoutOptions(*m_Changes.layoutOptions,
                                                            ParseLayoutGroups(m_Keyboard.definition.layoutLabels));
        }
        for (size_t slot : m_Changes.tapDances)
            if (slot < m_Preview.tapDances.size())
                m_Preview.tapDances[slot] = *m_File.tapDances[slot];
    }

    // --- The strip and the board ---------------------------------------------------------------

    Strip SavedLayoutReview::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Layer";
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
        {
            strip.entries.push_back(std::to_string(layer));
            strip.changed.push_back(std::any_of(m_Changes.keys.begin(), m_Changes.keys.end(),
                                                [&](const KeyChange& key) { return key.layer == layer; }));
        }
        strip.chosen = m_Layer;
        return strip;
    }

    void SavedLayoutReview::OnStripChosen(size_t entry)
    {
        m_Layer = static_cast<uint8_t>(entry);
    }

    void SavedLayoutReview::OnStripHovered(std::optional<size_t> entry)
    {
        m_Peek.reset();
        if (entry)
            m_Peek = static_cast<uint8_t>(*entry);
    }

    void SavedLayoutReview::DescribeBoard(BoardDescription& board)
    {
        const uint8_t layer = m_Peek.value_or(m_Layer);
        DescribeLegends(board, m_Preview, layer, m_Legends);
        if (m_Stage == Stage::Reading || m_Stage == Stage::Asking)
        {
            for (BoardKey& key : board.keys)
                key.marks |= Mark::Dimmed;
            return;
        }

        // Lit: what changes -- or, a line under the pointer, what it concerns.
        for (BoardKey& key : board.keys)
        {
            const via::DefinitionKey& geometry = key.geometry;
            bool                      lit      = false;
            bool                      warning  = false;

            if (geometry.encoderIndex >= 0 && m_Hovered == Line::None)
                lit = std::any_of(m_Changes.knobs.begin(), m_Changes.knobs.end(), [&](const KnobChange& knob)
                                  { return knob.layer == layer && knob.knob == geometry.encoderIndex; });

            if (geometry.IsSwitch())
            {
                const auto changed = [&](const KeyChange& change)
                { return change.layer == layer && change.row == geometry.row && change.column == geometry.column; };
                const Keycode sent = m_Preview.KeycodeFor(geometry, layer);

                switch (m_Hovered)
                {
                case Line::None:
                case Line::Keymap:
                    lit = lit || std::any_of(m_Changes.keys.begin(), m_Changes.keys.end(), changed);
                    break;
                case Line::Macros:
                    if (const auto* macro = std::get_if<MacroKey>(&sent))
                        lit = std::find(m_Changes.macros.begin(), m_Changes.macros.end(), macro->index) != m_Changes.macros.end();
                    break;
                case Line::TapDance:
                    if (const auto* dance = std::get_if<TapDanceKey>(&sent))
                        lit = std::find(m_Changes.tapDances.begin(), m_Changes.tapDances.end(), dance->index) !=
                              m_Changes.tapDances.end();
                    break;
                }

                warning = std::any_of(m_File.unreadableKeys.begin(), m_File.unreadableKeys.end(),
                                      [&](const vial::UnreadableKey& unreadable)
                                      {
                                          return unreadable.layer == layer && unreadable.row == geometry.row &&
                                                 unreadable.column == geometry.column;
                                      });
            }

            if (lit)
                key.marks |= Mark::Highlighted;
            else if (warning)
                key.marks |= Mark::Warning;
            else
                key.marks |= Mark::Dimmed;
        }
    }

    // --- The panel -------------------------------------------------------------------------------

    void SavedLayoutReview::DrawPanel()
    {
        m_HoveredNow = Line::None;

        if (m_Stage == Stage::Reading)
        {
            if (DrawReading("board", IsBusy(), m_Failure))
                m_Task = Read();
            ImGui::SameLine();
            if (!IsBusy() && ImGui::Button("Cancel"))
                m_Action.cancel = true;
            return;
        }

        DrawHead();
        ImGui::Separator();
        DrawLines();
        DrawSays();
        if (m_Stage == Stage::Asking)
            DrawQuestion();

        m_Hovered = m_HoveredNow;
    }

    void SavedLayoutReview::DrawHead()
    {
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::Text("Load %s", m_FileName.c_str());
        ImGui::PopFont();
        if (m_File.vialProtocol >= 0)
            Muted("saved from Vial " + std::to_string(m_File.vialProtocol));
        else
            Muted("saved from VIA");

        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::SameLine();
        switch (m_Stage)
        {
        case Stage::Reading:
        case Stage::Asking:
        case Stage::Review:
        {
            const bool  locked = IsLocked() && NeedsUnlock();
            const char* apply  = locked ? "Unlock and apply" : "Apply";
            const float width  = ImGui::CalcTextSize("Cancel").x + ImGui::CalcTextSize(apply).x + 4 * style.FramePadding.x +
                                style.ItemSpacing.x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
            if (ImGui::Button("Cancel"))
                m_Action.cancel = true;
            ImGui::SameLine();
            ImGui::BeginDisabled(m_Stage != Stage::Review || m_Changes.IsEmpty());
            if (ImGui::Button(apply))
                m_Action.apply = true;
            ImGui::EndDisabled();
            break;
        }
        case Stage::Writing:
        {
            char words[48];
            std::snprintf(words, sizeof(words), "Writing %zu of %zu...", m_Progress.done, m_Progress.total);
            ColouredText(PanelColour::Muted, "%s", words);
            ImGui::SameLine();
            const float fraction = m_Progress.total == 0 ? 0.0f
                                                         : static_cast<float>(m_Progress.done) / static_cast<float>(m_Progress.total);
            ImGui::ProgressBar(fraction, ImVec2(ImGui::GetFontSize() * 14.0f, 0.0f), "");
            break;
        }
        case Stage::Written:
        {
            if (!m_Failure.empty())
                ColouredText(PanelColour::Error, "Stopped after %zu of %zu: %s", m_Progress.done, m_Progress.total,
                             m_Failure.c_str());
            else if (!m_Progress.notKept.empty())
                ColouredText(PanelColour::Warning, "Written -- %zu of %zu kept", m_Progress.total - m_Progress.notKept.size(),
                             m_Progress.total);
            else
                ColouredText(PanelColour::Success, "Written and read back -- %s",
                             Plural(m_Progress.total, "change", "changes").c_str());
            ImGui::SameLine();
            const char* back  = "Back to the board";
            const float width = ImGui::CalcTextSize(back).x + 2 * style.FramePadding.x;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - width));
            if (ImGui::Button(back))
                m_Action.back = true;
            break;
        }
        }
    }

    void SavedLayoutReview::DrawLines()
    {
        if (!ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);

        const BoardReport& report = m_Keyboard.report;
        const auto         same   = [] { ColouredText(PanelColour::Muted, "the same"); };
        const auto         hover  = [this](float top, Line line)
        {
            if (IsOverLine(top))
                m_HoveredNow = line;
        };

        // Keymap: by layer, lit on the board.
        float top = BeginLine("Keymap");
        if (!m_Changes.keymapFits)
            ColouredText(PanelColour::Warning, "not loaded");
        else if (m_Changes.keys.empty())
            same();
        else
        {
            std::vector<std::string> layers;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const auto count = std::count_if(m_Changes.keys.begin(), m_Changes.keys.end(),
                                                 [&](const KeyChange& key) { return key.layer == layer; });
                if (count > 0)
                    layers.push_back(std::to_string(count) + " on layer " + std::to_string(layer));
            }
            ImGui::Text("%s", Plural(m_Changes.keys.size(), "key", "keys").c_str());
            Muted("-- " + JoinNames(layers) + ", lit on the board");
        }
        hover(top, Line::Keymap);

        // Layout options: each group the file changes.
        const std::vector<LayoutOptionGroup> groups = ParseLayoutGroups(m_Keyboard.definition.layoutLabels);
        if (!groups.empty())
        {
            (void)BeginLine("Layout");
            if (!m_Changes.keymapFits)
                ColouredText(PanelColour::Warning, "not loaded");
            else if (!m_Changes.layoutOptions)
                same();
            else
            {
                const std::vector<uint8_t> then = DecodeLayoutOptions(*m_Changes.layoutOptions, groups);
                std::vector<std::string>   said;
                for (size_t group = 0; group < groups.size() && group < then.size(); ++group)
                {
                    const uint8_t now = group < m_Keyboard.layoutSelection.size() ? m_Keyboard.layoutSelection[group] : 0;
                    if (then[group] == now)
                        continue;
                    const LayoutOptionGroup& options = groups[group];
                    if (options.options.empty())
                        said.push_back(options.name + (then[group] ? " on" : " off"));
                    else if (then[group] < options.options.size())
                        said.push_back(options.name + ": " + options.options[then[group]]);
                }
                ImGui::Text("%s", JoinNames(said).c_str());
            }
        }

        if (KnobCount(m_Keyboard.definition) > 0)
        {
            (void)BeginLine("Knobs");
            if (m_Changes.knobs.empty())
                same();
            else
                ImGui::Text("%s", Plural(m_Changes.knobs.size(), "turn", "turns").c_str());
        }

        // The slots: how many of the board's, and which.
        const auto slots = [&](const char* name, size_t count, size_t inFile, const std::vector<size_t>& changed,
                               const char* prefix, Line line)
        {
            if (count == 0)
                return;
            const float lineTop = BeginLine(name);
            if (!changed.empty())
            {
                ImGui::Text("%zu of %zu", changed.size(), count);
                Muted("-- " + SlotNames(changed, prefix));
            }
            else if (inFile == 0)
                ColouredText(PanelColour::Muted, "none in the file");
            else
                same();
            if (line != Line::None)
                hover(lineTop, line);
        };
        slots("Macros", report.macroCount, m_File.macros.size(), m_Changes.macros, "M", Line::Macros);
        slots("Tap Dance", report.tapDanceCount, m_File.tapDances.size(), m_Changes.tapDances, "TD", Line::TapDance);
        slots("Combos", report.comboCount, m_File.combos.size(), m_Changes.combos, "", Line::None);
        slots("Key Overrides", report.keyOverrideCount, m_File.keyOverrides.size(), m_Changes.keyOverrides, "", Line::None);
        slots("Alt Repeat Key", report.altRepeatKeyCount, m_File.altRepeatKeys.size(), m_Changes.altRepeatKeys, "",
              Line::None);

        if (report.hasQmkSettings)
        {
            (void)BeginLine("QMK Settings");
            if (!m_Changes.qmkSettings.empty())
                ImGui::Text("%s", Plural(m_Changes.qmkSettings.size(), "setting", "settings").c_str());
            else if (m_File.qmkSettings.empty())
                ColouredText(PanelColour::Muted, "none in the file");
            else
                same();
        }

        ImGui::EndTable();
    }

    // Under the lines: what is not loaded, and why; the lock; what a .vil does not hold.
    void SavedLayoutReview::DrawSays()
    {
        ImGui::PushTextWrapPos(0.0f);

        if (IsOtherKeyboard() && m_Stage != Stage::Asking)
            ColouredText(PanelColour::Warning, "Saved from another keyboard -- loaded as you asked; what does not fit "
                                               "this one is left out.");
        for (const std::string& reason : m_Changes.leftOut)
            ColouredText(PanelColour::Warning, "Left out: %s.", reason.c_str());

        const size_t unreadable = m_File.unreadableKeys.size() + m_File.unreadable.size();
        if (unreadable > 0)
        {
            std::vector<std::string> what;
            if (!m_File.unreadableKeys.empty())
                what.push_back(Plural(m_File.unreadableKeys.size(), "key", "keys") + ", outlined");
            for (const std::string& part : m_File.unreadable)
                what.push_back(part);
            ColouredText(PanelColour::Warning, "Nazg cannot read %s: %s -- left as the board has %s.",
                         unreadable == 1 ? "one value" : "some values", JoinNames(what).c_str(),
                         unreadable == 1 ? "it" : "them");
        }

        if (m_Stage == Stage::Review && IsLocked() && NeedsUnlock())
            ColouredText(PanelColour::Warning, "The keyboard is locked: Vial refuses macros, and stores Boot as nothing, "
                                               "until it is unlocked -- Apply asks for its unlock keys first.");

        if (m_Stage == Stage::Written)
            for (const std::string& part : m_Progress.notKept)
                ColouredText(PanelColour::Warning, "Not kept by the board: %s.", part.c_str());
        else if (m_Stage == Stage::Review && m_Changes.IsEmpty())
            ColouredText(PanelColour::Muted, "Nothing to write: the board already holds what this file can load.");
        else if (m_Stage != Stage::Written)
            ColouredText(PanelColour::Muted, "Nothing is written before Apply, and only what differs is. Lighting, Audio "
                                             "and the board's own menus are not in a .vil file.");

        ImGui::PopTextWrapPos();
    }

    // Asked as Vial asks (Rico, 2026-10-18): the UID names a model, so another one is most likely
    // another keyboard.
    void SavedLayoutReview::DrawQuestion()
    {
        if (!ImGui::IsPopupOpen(c_Question))
            ImGui::OpenPopup(c_Question);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f));
        if (!ImGui::BeginPopupModal(c_Question, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            return;

        ImGui::PushTextWrapPos(0.0f);
        if (m_File.uid && m_Board.layout.uid)
            ImGui::Text("%s was saved from another keyboard: its Vial ID is %s, this one's %s. The ID names a keyboard "
                        "model, so this is most likely a different model.",
                        m_FileName.c_str(), Uid(*m_File.uid).c_str(), Uid(*m_Board.layout.uid).c_str());
        else if (m_File.uid || m_Board.layout.uid)
            ImGui::Text("%s was saved from a %s keyboard, and this one is a %s keyboard.", m_FileName.c_str(),
                        m_File.uid ? "Vial" : "VIA", m_Board.layout.uid ? "Vial" : "VIA");
        else
            ImGui::Text("%s was saved from another keyboard: %04X:%04X, this one is %04X:%04X.", m_FileName.c_str(),
                        m_File.vendorId.value_or(0), m_File.productId.value_or(0), m_VendorId, m_ProductId);
        ImGui::Spacing();
        ColouredText(PanelColour::Muted, "Nothing is written yet: Nazg shows what would change, and leaves out what does "
                                         "not fit.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (ImGui::Button("Cancel"))
        {
            m_Action.cancel = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load anyway"))
        {
            m_Stage = Stage::Review;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
