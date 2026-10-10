// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgTapDanceSection.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <set>
#include <tuple>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/vial/NazgVialLoader.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgKeyLine.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgKeycodeCatalogue.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // Pixels, before DPI scaling.
        constexpr float    c_CardWidth    = 132.0f;   // one action: its name, drawing, key and words
        constexpr float    c_CardGap      = 12.0f;
        constexpr float    c_DrawingWidth = 120.0f;
        constexpr float    c_DrawingHigh  = 22.0f;
        constexpr uint16_t c_LongestTerm  = 10000;    // Vial's own limit
        constexpr uint16_t c_ShortTerm    = 100;      // below it, most presses count as holds

        constexpr const char* c_ActionNames[] = { "Tap", "Hold", "Double tap", "Tap, then hold" };
        constexpr const char* c_ActionHelp[]  = {
            "Pressed and released within the tapping term",
            "Held past the tapping term",
            "Tapped twice, the second tap within the tapping term of the first",
            "Tapped, then pressed again within the tapping term and held",
        };

        const char* NameOf(DanceAction action)
        {
            return c_ActionNames[static_cast<size_t>(action)];
        }

        // The gesture, schematic and centred: the key held as bars on a time line -- a short bar a
        // tap, a long one a hold (the mockup's drawing; the term is not drawn, Rico).
        void DrawGesture(DanceAction action, ImVec2 at, float width, float scale)
        {
            static constexpr float c_Bars[4][4] = {
                { 0, 18, 0, 0 }, { 0, 70, 0, 0 }, { 0, 18, 30, 48 }, { 0, 18, 30, 88 },
            };
            const float* bars  = c_Bars[static_cast<size_t>(action)];
            const float  span  = (bars[3] > 0 ? bars[3] : bars[1]) * scale;
            const float  x0    = at.x + (width - span) / 2;
            const float  y     = at.y + c_DrawingHigh * scale / 2;
            const float  h     = 5 * scale;
            ImDrawList*  list  = ImGui::GetWindowDrawList();
            const ImU32  track = ImGui::GetColorU32(ImGuiCol_Border);
            const ImU32  held  = ImGui::GetColorU32(ImGuiCol_ButtonActive);
            list->AddLine(ImVec2(at.x + 4 * scale, y), ImVec2(at.x + width - 4 * scale, y), track, 2 * scale);
            for (int bar = 0; bar < 2; ++bar)
                if (bars[bar * 2 + 1] > 0)
                    list->AddRectFilled(ImVec2(x0 + bars[bar * 2] * scale, y - h), ImVec2(x0 + bars[bar * 2 + 1] * scale, y + h),
                                        held, 3 * scale);
        }

        // Text centred in [left, left + width), cut to it -- the whole in the card's tooltip.
        void CentredText(ImDrawList* list, float left, float width, float y, ImU32 colour, const std::string& text)
        {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            list->PushClipRect(ImVec2(left, y), ImVec2(left + width, y + ImGui::GetFontSize() * 2), true);
            list->AddText(ImVec2(left + std::max(0.0f, (width - w) / 2), y), colour, text.c_str());
            list->PopClipRect();
        }
    }

    TapDanceSection::TapDanceSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                     const LegendSettings& legends, const std::optional<VialUnlockStatus>& lock,
                                     const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock),
          m_AdvancedTools(advancedTools)
    {
        // Read with the board, as Keymap draws them; else the panel reads them when first shown.
        if (m_Keyboard.tapDances.size() == m_Keyboard.report.tapDanceCount)
        {
            m_Dances   = m_Keyboard.tapDances;
            m_IsLoaded = true;
        }
    }

    bool TapDanceSection::IsLocked() const
    {
        return m_Lock && !m_Lock->unlocked;
    }

    bool TapDanceSection::IsChanged(size_t dance) const
    {
        const std::vector<TapDance>& stored = m_Keyboard.tapDances;
        return m_IsLoaded && dance < m_Dances.size() && dance < stored.size() && m_Dances[dance] != stored[dance];
    }

    bool TapDanceSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool TapDanceSection::HasUnsavedChanges() const
    {
        for (size_t dance = 0; dance < m_Dances.size(); ++dance)
            if (IsChanged(dance))
                return true;
        return false;
    }

    std::string TapDanceSection::UnsavedSummary() const
    {
        std::vector<std::string> changed;
        for (size_t dance = 0; dance < m_Dances.size(); ++dance)
            if (IsChanged(dance))
                changed.push_back("TD " + std::to_string(dance));
        return JoinNames(changed);
    }

    // How many keys, on every layer, hold TD(dance) -- by matrix position, so a key drawn in several
    // layout options counts once.
    size_t TapDanceSection::KeysHolding(size_t dance) const
    {
        std::set<std::tuple<int, int, int>> holding;
        for (const DefinitionKey& key : m_Keyboard.definition.keys)
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer));
                if (const auto* td = std::get_if<TapDanceKey>(&keycode); td && td->index == dance)
                    holding.insert({ layer, key.row, key.column });
            }
        return holding.size();
    }

    Strip TapDanceSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Tap dance";
        for (size_t dance = 0; dance < m_Keyboard.report.tapDanceCount; ++dance)
        {
            strip.entries.push_back("TD " + std::to_string(dance));
            strip.changed.push_back(IsChanged(dance));
            strip.empty.push_back(m_IsLoaded && dance < m_Dances.size() && m_Dances[dance].IsEmpty());
        }
        strip.chosen = m_Dance;
        return strip;
    }

    void TapDanceSection::OnStripChosen(size_t entry)
    {
        m_Dance  = entry;
        m_Action = DanceAction::Tap;
    }

    void TapDanceSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The keys that hold a tap dance, on any layer -- the shown one's lit, "Layer 1" over one
        // that holds it on another layer. As Macros.
        const LegendContext context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither, 0, {} };
        for (BoardKey& key : board.keys)
        {
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                const auto*   dance   = std::get_if<TapDanceKey>(&keycode);
                if (!dance)
                    continue;
                if (layer != 0)
                {
                    KeycapLegend legend = LegendFor(keycode, context);
                    legend.header       = { { "Layer " + std::to_string(layer), "L" + std::to_string(layer) },
                                            CommandCategory::Behaviour };
                    key.legends         = legend;
                    key.fallthrough     = Fallthrough::None;
                }
                if (dance->index == m_Dance)
                    key.marks |= Mark::Highlighted;
                break;
            }
        }

        // Locked: the keys to hold to unlock -- for Boot, the one action a locked board refuses.
        MarkUnlockKeys(board, m_Lock);
    }

    void TapDanceSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!events.hoveredKey)
            return;
        const DefinitionKey& key = board.keys[*events.hoveredKey].geometry;
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
        {
            const Keycode keycode = m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer));
            if (const auto* dance = std::get_if<TapDanceKey>(&keycode))
            {
                ImGui::SetTooltip("Dances TD %d, on layer %d", dance->index, layer);
                if (events.clickedKey && dance->index < m_Keyboard.report.tapDanceCount)
                    OnStripChosen(dance->index);
                return;
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    Task<void> TapDanceSection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            m_Keyboard.tapDances =
                co_await ReadTapDances(vial, m_Keyboard.report.tapDanceCount, m_Keyboard.keycodeVersion);
            m_Dances   = m_Keyboard.tapDances;
            m_IsLoaded = true;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the tap dances could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    void TapDanceSection::SaveChanges()
    {
        if (IsBusy() || !m_IsLoaded || !HasUnsavedChanges())
            return;

        std::vector<size_t> changed;
        for (size_t dance = 0; dance < m_Dances.size(); ++dance)
            if (IsChanged(dance))
            {
                if (!EncodeTapDance(m_Dances[dance], m_Keyboard.keycodeVersion))
                {
                    m_Message   = "TD " + std::to_string(dance) + " holds a key this board cannot store";
                    m_IsWarning = true;
                    return;
                }
                changed.push_back(dance);
            }
        m_Request = Save(std::move(changed));
    }

    Task<void> TapDanceSection::Save(std::vector<size_t> dances)
    {
        m_Message.clear();
        m_IsWarning = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            // Each slot written, then read back: what the board holds now, whatever was asked.
            std::vector<std::string> notKept;
            for (size_t dance : dances)
            {
                const VialTapDanceEntry entry = *EncodeTapDance(m_Dances[dance], m_Keyboard.keycodeVersion);
                const uint8_t           index = static_cast<uint8_t>(dance);
                co_await vial.SetTapDance(index, entry);
                const VialTapDanceEntry stored = co_await vial.GetTapDance(index);
                m_Keyboard.tapDances[dance]    = DecodeTapDance(stored, m_Keyboard.keycodeVersion);
                if (stored == entry)
                    m_Dances[dance] = m_Keyboard.tapDances[dance];
                else
                    notKept.push_back("TD " + std::to_string(dance));   // the edit stays, still to be written
            }

            if (notKept.empty())
                m_Message = dances.size() == 1 ? "stored TD " + std::to_string(dances[0]) + ", read back the same"
                                               : "stored " + std::to_string(dances.size()) + " tap dances, read back the same";
            else
            {
                m_Message = "the board did not keep " + notKept[0] + (notKept.size() > 1 ? " and others" : "") +
                            (IsLocked() ? " -- a locked board stores Boot as nothing" : "");
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

    void TapDanceSection::DiscardChanges()
    {
        m_Dances = m_Keyboard.tapDances;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void TapDanceSection::DrawPanel()
    {
        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("tap dances", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }
        if (m_Dances.empty())
        {
            ImGui::TextDisabled("This board has no tap dances.");
            return;
        }
        if (m_Dance >= m_Dances.size())
            m_Dance = 0;

        DrawDanceLine();
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawActions();
        DrawTiming();
        ImGui::Separator();
        DrawTools();
        DrawPicker();
        ImGui::EndDisabled();
    }

    void TapDanceSection::DrawDanceLine()
    {
        TapDance& dance = m_Dances[m_Dance];

        ImGui::AlignTextToFramePadding();
        ImGui::Text("TD %zu", m_Dance);
        ImGui::SetItemTooltip("TD(%zu) in QMK", m_Dance);

        ImGui::SameLine();
        ImGui::BeginDisabled(dance.IsEmpty() || IsBusy());
        if (ImGui::Button("Clear"))
        {
            dance.actions = {};
            m_Action      = DanceAction::Tap;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Empties all four actions -- written by Save, undone by Revert");

        // Where it is: the board lights the keys; the count says when there is none to light.
        ImGui::SameLine();
        const size_t keys = KeysHolding(m_Dance);
        if (keys == 0)
            ColouredText(PanelColour::Muted, "On no key yet -- put TD %zu on a key in Keymap", m_Dance);
        else
            ColouredText(PanelColour::Muted, "On %zu key%s", keys, keys == 1 ? "" : "s");

        ImGui::SameLine();
        const bool changed = HasUnsavedChanges();
        DrawWriteState(IsBusy(), changed ? UnsavedSummary() : std::string(), m_Message, m_IsWarning);
        ImGui::SameLine();
        switch (DrawSaveRevert(changed && !IsBusy(), changed && !IsBusy()))
        {
        case SlotWrite::Save: SaveChanges(); break;
        case SlotWrite::Revert: DiscardChanges(); break;
        case SlotWrite::None: break;
        }
    }

    // The four actions side by side, each a card: its name, the drawing of its gesture, its key --
    // or, empty, what the firmware does instead, faint, with the words under it -- then the term.
    void TapDanceSection::DrawActions()
    {
        TapDance&         dance = m_Dances[m_Dance];
        const float       scale = ImGui::GetStyle().FontScaleDpi;
        const ImVec2      tile  = TileSize();
        const float       cardW = std::max(c_CardWidth * scale, tile.x + 16 * scale);
        const float       gap   = c_CardGap * scale;
        const float       pad   = 6 * scale;
        const float       line  = ImGui::GetTextLineHeight();
        const float       cardH = pad + line + 4 * scale + c_DrawingHigh * scale + 4 * scale + tile.y + 4 * scale + line + pad;
        ImDrawList*       list  = ImGui::GetWindowDrawList();
        const ImU32       muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32       text  = ImGui::GetColorU32(ImGuiCol_Text);
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);   // outlives the context's span
        const LegendContext legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                     LightingSystemsOf(m_Keyboard), custom };

        ImGui::Dummy(ImVec2(0, gap / 2));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        for (DanceAction action : c_DanceActions)
        {
            const size_t index = static_cast<size_t>(action);
            const ImVec2 p0(origin.x + index * (cardW + gap), origin.y);
            const ImVec2 p1(p0.x + cardW, p0.y + cardH);
            const bool   selected = m_Action == action;

            ImGui::PushID(static_cast<int>(index));
            ImGui::SetCursorScreenPos(p0);
            if (ImGui::InvisibleButton("##action", ImVec2(cardW, cardH)))
                m_Action = action;
            const bool hovered = ImGui::IsItemHovered();
            if (selected || hovered)
                list->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 7 * scale);
            if (selected)
                list->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_ButtonActive), 7 * scale, 0, std::max(1.0f, 1.5f * scale));

            float y = p0.y + pad;
            PushHeaderFont();
            CentredText(list, p0.x, cardW, y, text, NameOf(action));
            ImGui::PopFont();
            y += line + 4 * scale;
            DrawGesture(action, ImVec2(p0.x + (cardW - c_DrawingWidth * scale) / 2, y), c_DrawingWidth * scale, scale);
            y += c_DrawingHigh * scale + 4 * scale;

            const FaceBox box{ p0.x + (cardW - tile.x) / 2, y, p0.x + (cardW + tile.x) / 2, y + tile.y };
            std::string   words, tip = c_ActionHelp[index];
            if (const std::optional<Keycode>& key = dance[action])
            {
                KeycodeTile keyTile = TileOf(*key, legends, false);
                keyTile.hovered     = hovered;
                DrawKeycodeTile(keyTile, box);
                tip += "\n" + KeycodeHoverText(*key, m_Keyboard.keycodeVersion);
            }
            else if (const std::optional<DanceFallback> fallback = FallbackOf(dance, action))
            {
                KeycodeTile keyTile = TileOf(fallback->held, legends, false);
                keyTile.isFaint     = true;
                DrawKeycodeTile(keyTile, box);
                const std::string held = KeycodeLabel(fallback->held, m_Keyboard.keycodeVersion);
                if (!fallback->tappedFirst)
                    words = held + ", held";
                else if (*fallback->tappedFirst == fallback->held && action == DanceAction::DoubleTap)
                    words = held + " twice";
                else
                    words = KeycodeLabel(*fallback->tappedFirst, m_Keyboard.keycodeVersion) + ", then " + held + " held";
                tip += "\nEmpty: the board does " + words;
            }
            else
            {
                // Nothing at all: an outline and a dash.
                list->AddRect(ImVec2(box.x0, box.y0), ImVec2(box.x1, box.y1), ImGui::GetColorU32(ImGuiCol_Border),
                              5 * scale, 0, std::max(1.0f, scale));
                CentredText(list, box.x0, box.x1 - box.x0, (box.y0 + box.y1 - line) / 2, muted, "-");
                words = "nothing";
                tip += action == DanceAction::Tap ? "\nEmpty: a quick press sends nothing" : "\nEmpty: the board does nothing";
            }
            y += tile.y + 4 * scale;
            if (!words.empty())
                CentredText(list, p0.x + 2 * scale, cardW - 4 * scale, y, muted, words);

            if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tip.c_str());
            ImGui::PopID();
        }

        // The term, after a rule.
        const float termX = origin.x + 4 * (cardW + gap);
        list->AddLine(ImVec2(termX - gap / 2, origin.y + pad), ImVec2(termX - gap / 2, origin.y + cardH - pad),
                      ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::SetCursorScreenPos(ImVec2(termX + gap / 2, origin.y + pad));
        ImGui::BeginGroup();
        PushHeaderFont();
        ImGui::TextUnformatted("Tapping term");
        ImGui::PopFont();
        uint16_t term = dance.tappingTerm;
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("10000").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight() * 2);
        const uint16_t step = 10;
        if (ImGui::InputScalar("##term", ImGuiDataType_U16, &term, &step))
            dance.tappingTerm = std::min(term, c_LongestTerm);
        ImGui::SameLine();
        ImGui::TextUnformatted("ms");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 220 * scale);
        ImGui::TextDisabled("How long a press may last to count as a tap, and how long the key waits for a second one.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + cardH));
        ImGui::Dummy(ImVec2(4 * (cardW + gap), gap / 2));
    }

    // When the tap is sent -- the delay is what surprises users (vial-qmk, process_record_vial).
    void TapDanceSection::DrawTiming()
    {
        const TapDance&   dance = m_Dances[m_Dance];
        const unsigned    term  = dance.tappingTerm;
        const auto        name  = [&](DanceAction action) { return KeycodeLabel(*dance[action], m_Keyboard.keycodeVersion); };
        switch (TimingOf(dance))
        {
        case TapTiming::Empty:
            ColouredText(PanelColour::Muted, "Empty: pick an action above, then a key below.");
            break;
        case TapTiming::AtRelease:
            ColouredText(PanelColour::Muted, "%s is sent as soon as the key is released; held past %u ms, it is %s.",
                         name(DanceAction::Tap).c_str(), term, name(DanceAction::Hold).c_str());
            break;
        case TapTiming::Waits:
            ColouredText(PanelColour::Warning, "A tap waits %u ms before %s is sent", term, name(DanceAction::Tap).c_str());
            ImGui::SameLine(0, 0);
            ColouredText(PanelColour::Muted, " -- the time for a second tap to come. Fast typing on this key may feel late.");
            break;
        case TapTiming::OnlyTap:
            ColouredText(PanelColour::Warning, "Only a tap: the key waits %u ms for nothing", term);
            ImGui::SameLine(0, 0);
            ColouredText(PanelColour::Muted, " -- a plain %s key does the same at once.", name(DanceAction::Tap).c_str());
            break;
        case TapTiming::NoTap:
            ColouredText(PanelColour::Warning, "No tap: a quick press sends nothing.");
            break;
        }
        if (!dance.IsEmpty() && term < c_ShortTerm)
            ColouredText(PanelColour::Warning, "%u ms is very short: most presses will count as holds.", term);
    }

    void TapDanceSection::DrawTools()
    {
        std::optional<Keycode>& key = m_Dances[m_Dance][m_Action];
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(NameOf(m_Action));
        ImGui::PopFont();
        ImGui::SameLine();
        if (!key)
        {
            ImGui::TextDisabled("pick a key below");
        }
        else
        {
            // Sent with, as Keymap's key line: Ctrl+C is one action.
            if (DrawSentWith(*key, m_Legends, m_Keyboard.keycodeVersion))
                ImGui::SameLine();
            if (ImGui::SmallButton("Empty it"))
                key.reset();
            ImGui::SetItemTooltip(m_Action == DanceAction::Tap ? "A quick press then sends nothing"
                                                               : "The board then does what the faint key shows");
            ImGui::SameLine();
            ImGui::TextDisabled("or pick a key below to change it");
        }

        if (IsLocked())
            ColouredText(PanelColour::Warning, "This board is locked: Vial stores Boot as nothing, so Boot is greyed. To use "
                                               "it, click Locked in the header, then hold the outlined keys.");
    }

    void TapDanceSection::DrawPicker()
    {
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom };

        // A tap dance cannot play another; a locked board stores Boot as nothing.
        const bool locked = IsLocked();
        const std::function<std::optional<std::string>(const Keycode&)> unavailable =
            [locked](const Keycode& keycode) -> std::optional<std::string>
        {
            if (std::holds_alternative<TapDanceKey>(keycode))
                return std::string("A tap dance cannot play another");
            if (const auto* named = std::get_if<NamedKey>(&keycode); locked && named && named->name == "QK_BOOT")
                return std::string("This board is locked: Vial would store Boot as nothing");
            return std::nullopt;
        };

        const std::optional<Keycode>& current = m_Dances[m_Dance][m_Action];
        const KeycodePickerEvents     events  = DrawKeycodePicker(
            m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion, current, &m_Keyboard, unavailable });
        if (events.picked)
            m_Dances[m_Dance][m_Action] =
                current ? ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion) : *events.picked;
    }
}
