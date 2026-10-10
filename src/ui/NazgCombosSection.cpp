// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgCombosSection.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/vial/NazgVialLoader.h"
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
        constexpr float    c_CardWidth   = 76.0f;   // one part of the chord: its name, key and where it is
        constexpr float    c_JoinWidth   = 22.0f;   // the + between inputs, the arrow to the output
        constexpr float    c_TermGap     = 24.0f;
        constexpr uint16_t c_LongestTerm = 10000;   // Vial's own limit

        // Text centred in [left, left + width), cut to it.
        void CentredText(ImDrawList* list, float left, float width, float y, ImU32 colour, const std::string& text)
        {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            list->PushClipRect(ImVec2(left, y), ImVec2(left + width, y + ImGui::GetFontSize() * 2), true);
            list->AddText(ImVec2(left + std::max(0.0f, (width - w) / 2), y), colour, text.c_str());
            list->PopClipRect();
        }

        // What the board's tag says: the output as its keycap reads, on one line -- "Esc", "Ctrl Z",
        // "Caps Word" -- or its label when the keycap draws a symbol, an arrow.
        std::string TagText(const KeycapLegend& legend, const std::string& label)
        {
            std::string main = legend.placement == PlacementClass::Character
                                   ? legend.plain
                                   : (legend.cylindrical.shortForm.empty() ? legend.cylindrical.full
                                                                            : legend.cylindrical.shortForm);
            const Header& header = legend.hold.IsEmpty() ? legend.header : legend.hold;
            if (!header.IsEmpty())
            {
                const std::string& words = header.words.shortForm.empty() ? header.words.full : header.words.shortForm;
                main                     = main.empty() ? words : words + " " + main;
            }
            return main.empty() ? label : main;
        }

        bool IsNothing(const Keycode& keycode)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named && (named->name == "KC_NO" || named->name == "KC_TRNS");
        }
    }

    CombosSection::CombosSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                 const LegendSettings& legends, const std::optional<VialUnlockStatus>& lock,
                                 const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock),
          m_AdvancedTools(advancedTools)
    {
    }

    bool CombosSection::IsLocked() const
    {
        return m_Lock && !m_Lock->unlocked;
    }

    bool CombosSection::IsChanged(size_t combo) const
    {
        return m_IsLoaded && combo < m_Combos.size() && combo < m_Stored.size() && m_Combos[combo] != m_Stored[combo];
    }

    bool CombosSection::IsTermChanged() const
    {
        return m_IsLoaded && m_Term != m_StoredTerm;
    }

    bool CombosSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool CombosSection::HasUnsavedChanges() const
    {
        for (size_t combo = 0; combo < m_Combos.size(); ++combo)
            if (IsChanged(combo))
                return true;
        return IsTermChanged();
    }

    std::string CombosSection::UnsavedSummary() const
    {
        std::vector<std::string> changed;
        for (size_t combo = 0; combo < m_Combos.size(); ++combo)
            if (IsChanged(combo))
                changed.push_back(SlotName(combo));
        if (IsTermChanged())
            changed.push_back("the combo term");
        return JoinNames(changed);
    }

    std::optional<Keycode>& CombosSection::Part()
    {
        Combo& combo = m_Combos[m_Combo];
        return m_Part == c_Output ? combo.output : combo.inputs[m_Part];
    }

    std::string CombosSection::NameOf(const Keycode& keycode) const
    {
        return KeycodeLabel(keycode, m_Keyboard.keycodeVersion);
    }

    std::string CombosSection::SlotName(size_t combo) const
    {
        return "C " + std::to_string(combo);
    }

    Strip CombosSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Combo";
        for (size_t combo = 0; combo < m_Keyboard.report.comboCount; ++combo)
        {
            strip.entries.push_back(SlotName(combo));
            strip.changed.push_back(IsChanged(combo));
            strip.empty.push_back(m_IsLoaded && combo < m_Combos.size() && m_Combos[combo].IsEmpty());
        }
        strip.chosen = m_Combo;
        return strip;
    }

    void CombosSection::OnStripChosen(size_t entry)
    {
        m_Combo = entry;
        m_Part  = 0;
        if (m_IsLoaded && entry < m_Combos.size() && !m_Combos[entry].IsEmpty())
            m_Part = c_Output;
    }

    void CombosSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The keys sending the selected combo's inputs, lit -- with the layer's legend and "L1" when
        // only another layer sends one -- and a tag between them saying what they send. An input
        // only a tap-hold key holds outlines that key: the combo needs its whole keycode.
        if (m_IsLoaded && m_Combo < m_Combos.size())
        {
            const Combo&             combo  = m_Combos[m_Combo];
            const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
            const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                              LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
            BoardTag tag;
            for (const Keycode& input : combo.MatchedInputs())
            {
                if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, input))
                {
                    bool first = true;
                    for (size_t index = 0; index < board.keys.size(); ++index)
                    {
                        BoardKey& key = board.keys[index];
                        if (key.geometry.decal || m_Keyboard.KeycodeFor(key.geometry, *layer) != input)
                            continue;
                        key.marks |= Mark::Highlighted;
                        if (*layer != 0)
                        {
                            KeycapLegend legend = LegendFor(input, context);
                            legend.header       = { { "Layer " + std::to_string(*layer), "L" + std::to_string(*layer) },
                                                    CommandCategory::Behaviour };
                            key.legends         = legend;
                            key.fallthrough     = Fallthrough::None;
                        }
                        if (first)
                            tag.keys.push_back(index);
                        first = false;
                    }
                }
                else if (const std::optional<Keycode> tapHold = TapHoldSending(m_Keyboard, input))
                {
                    for (BoardKey& key : board.keys)
                        if (!key.geometry.decal && m_Keyboard.KeycodeFor(key.geometry, 0) == *tapHold)
                            key.marks |= Mark::Warning;
                }
            }
            if (!tag.keys.empty())
            {
                tag.text = combo.output ? TagText(LegendFor(*combo.output, context), NameOf(*combo.output)) : "-";
                board.tags.push_back(std::move(tag));
            }
        }

        // Locked: the keys to hold to unlock -- for Boot, the one output a locked board refuses.
        MarkUnlockKeys(board, m_Lock);
    }

    void CombosSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!m_IsLoaded || m_Combo >= m_Combos.size() || !events.hoveredKey || IsBusy())
            return;

        // What the key sends on layer 0, added to the inputs or taken out (Rico: inputs from the board
        // and the picker).
        const Keycode sent = m_Keyboard.KeycodeFor(board.keys[*events.hoveredKey].geometry, 0);
        if (IsNothing(sent))
            return;
        const Combo& combo = m_Combos[m_Combo];
        const bool   in    = std::find(combo.inputs.begin(), combo.inputs.end(), std::optional<Keycode>(sent)) !=
                        combo.inputs.end();
        const bool full = std::all_of(combo.inputs.begin(), combo.inputs.end(), [](const auto& input) { return input.has_value(); });
        if (in)
            ImGui::SetTooltip("%s -- click to take it out of %s", NameOf(sent).c_str(), SlotName(m_Combo).c_str());
        else if (full)
            ImGui::SetTooltip("%s has four keys, the most a combo takes", SlotName(m_Combo).c_str());
        else
            ImGui::SetTooltip("%s -- click to add it to %s", NameOf(sent).c_str(), SlotName(m_Combo).c_str());

        if (events.clickedKey)
            ToggleInput(sent);
    }

    void CombosSection::ToggleInput(const Keycode& keycode)
    {
        Combo& combo = m_Combos[m_Combo];
        for (std::optional<Keycode>& input : combo.inputs)
            if (input == keycode)
            {
                input.reset();
                combo.CloseGaps();   // Nazg never writes a gap
                SelectNext();
                return;
            }
        for (std::optional<Keycode>& input : combo.inputs)
            if (!input)
            {
                input = keycode;
                SelectNext();
                return;
            }
    }

    void CombosSection::SelectNext()
    {
        const Combo& combo = m_Combos[m_Combo];
        if (combo.MatchedInputs().size() < 2)
        {
            for (size_t input = 0; input < combo.inputs.size(); ++input)
                if (!combo.inputs[input])
                {
                    m_Part = input;
                    return;
                }
        }
        else if (!combo.output)
            m_Part = c_Output;
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    Task<void> CombosSection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            m_Stored = co_await ReadCombos(vial, m_Keyboard.report.comboCount, m_Keyboard.keycodeVersion);

            // The term, when the board's QMK Settings have it; else the build's, unreadable.
            m_StoredTerm.reset();
            if (m_Keyboard.report.hasQmkSettings)
            {
                try
                {
                    m_StoredTerm = static_cast<uint16_t>(co_await vial.GetQmkSetting(c_QmkSettingComboTerm, 2));
                }
                catch (const ProtocolError&)
                {
                }
            }

            m_Combos   = m_Stored;
            m_Term     = m_StoredTerm;
            m_IsLoaded = true;
            OnStripChosen(m_Combo);
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the combos could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    void CombosSection::SaveChanges()
    {
        if (IsBusy() || !m_IsLoaded || !HasUnsavedChanges())
            return;

        std::vector<size_t> changed;
        for (size_t combo = 0; combo < m_Combos.size(); ++combo)
            if (IsChanged(combo))
            {
                if (!EncodeCombo(m_Combos[combo], m_Keyboard.keycodeVersion))
                {
                    m_Message   = SlotName(combo) + " holds a key this board cannot store";
                    m_IsWarning = true;
                    return;
                }
                changed.push_back(combo);
            }
        m_Request = Save(std::move(changed), IsTermChanged());
    }

    Task<void> CombosSection::Save(std::vector<size_t> combos, bool term)
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
            for (size_t combo : combos)
            {
                const VialComboEntry entry = *EncodeCombo(m_Combos[combo], m_Keyboard.keycodeVersion);
                const uint8_t        index = static_cast<uint8_t>(combo);
                co_await vial.SetCombo(index, entry);
                const VialComboEntry stored = co_await vial.GetCombo(index);
                m_Stored[combo]             = DecodeCombo(stored, m_Keyboard.keycodeVersion);
                if (stored == entry)
                    m_Combos[combo] = m_Stored[combo];
                else
                    notKept.push_back(SlotName(combo));   // the edit stays, still to be written
            }

            if (term && m_Term)
            {
                co_await vial.SetQmkSetting(c_QmkSettingComboTerm, *m_Term);
                m_StoredTerm = static_cast<uint16_t>(co_await vial.GetQmkSetting(c_QmkSettingComboTerm, 2));
                if (m_StoredTerm != m_Term)
                    notKept.push_back("the combo term");
            }

            const size_t written = combos.size() + (term ? 1 : 0);
            if (notKept.empty())
                m_Message = written == 1 ? "stored " + (combos.empty() ? std::string("the combo term") : SlotName(combos[0])) +
                                               ", read back the same"
                                         : "stored " + std::to_string(written) + " changes, read back the same";
            else
            {
                m_Message = "the board did not keep " + JoinNames(notKept) +
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

    void CombosSection::DiscardChanges()
    {
        m_Combos = m_Stored;
        m_Term   = m_StoredTerm;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void CombosSection::DrawPanel()
    {
        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("combos", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }
        if (m_Combos.empty())
        {
            ImGui::TextDisabled("This board has no combos.");
            return;
        }
        if (m_Combo >= m_Combos.size())
            m_Combo = 0;

        DrawComboLine();
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawChord();
        DrawSays();
        ImGui::Separator();
        DrawTools();
        DrawPicker();
        ImGui::EndDisabled();
    }

    void CombosSection::DrawComboLine()
    {
        Combo& combo = m_Combos[m_Combo];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(SlotName(m_Combo).c_str());
        ImGui::SetItemTooltip("Slot %zu of the board's %zu", m_Combo, m_Combos.size());

        ImGui::SameLine();
        ImGui::BeginDisabled(combo.IsEmpty() || IsBusy());
        if (ImGui::Button("Clear"))
        {
            combo  = Combo{};
            m_Part = 0;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Empties the keys and what they send -- written by Save, undone by Revert");

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

    // The chord, left to right: the inputs as stored, joined by + -- a gap shown where the board
    // holds one -- an outlined + offering the next, an arrow, and what they send; then the term.
    void CombosSection::DrawChord()
    {
        Combo&              combo   = m_Combos[m_Combo];
        const float         scale   = ImGui::GetStyle().FontScaleDpi;
        const ImVec2        tile    = TileSize();
        const float         cardW   = std::max(c_CardWidth * scale, tile.x + 16 * scale);
        const float         joinW   = c_JoinWidth * scale;
        const float         pad     = 6 * scale;
        const float         line    = ImGui::GetTextLineHeight();
        const float         cardH   = pad + line + 4 * scale + tile.y + 4 * scale + line + pad;
        ImDrawList*         list    = ImGui::GetWindowDrawList();
        const ImU32         muted   = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32         text    = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32         warning = ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Warning));
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                     LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
        const size_t        matched = combo.MatchedInputs().size();

        size_t last = 0;   // inputs drawn: up to the last one set
        for (size_t input = 0; input < combo.inputs.size(); ++input)
            if (combo.inputs[input])
                last = input + 1;

        ImGui::Dummy(ImVec2(0, pad));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float        x      = origin.x;

        // One part: its name, its key -- or an outline, with "+" for the next input or "-" for none --
        // and a word under it. A click selects it.
        const auto card = [&](size_t part, const char* name, const std::optional<Keycode>& key, const char* empty,
                              bool emptyWarns, const std::string& under, bool underWarns, const std::string& tip)
        {
            const ImVec2 p0(x, origin.y), p1(x + cardW, origin.y + cardH);
            const bool   selected = m_Part == part;
            ImGui::PushID(static_cast<int>(part));
            ImGui::SetCursorScreenPos(p0);
            if (ImGui::InvisibleButton("##part", ImVec2(cardW, cardH)))
                m_Part = part;
            const bool hovered = ImGui::IsItemHovered();
            if (selected || hovered)
                list->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 7 * scale);
            if (selected)
                list->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_ButtonActive), 7 * scale, 0, std::max(1.0f, 1.5f * scale));

            float y = p0.y + pad;
            PushHeaderFont();
            CentredText(list, p0.x, cardW, y, text, name);
            ImGui::PopFont();
            y += line + 4 * scale;

            const FaceBox box{ p0.x + (cardW - tile.x) / 2, y, p0.x + (cardW + tile.x) / 2, y + tile.y };
            if (key)
            {
                KeycodeTile keyTile = TileOf(*key, legends, false);
                keyTile.hovered     = hovered;
                DrawKeycodeTile(keyTile, box);
            }
            else
            {
                list->AddRect(ImVec2(box.x0, box.y0), ImVec2(box.x1, box.y1),
                              emptyWarns ? warning : ImGui::GetColorU32(ImGuiCol_Border), 5 * scale, 0, std::max(1.0f, scale));
                CentredText(list, box.x0, box.x1 - box.x0, (box.y0 + box.y1 - line) / 2, muted, empty);
            }
            y += tile.y + 4 * scale;
            if (!under.empty())
                CentredText(list, p0.x + 2 * scale, cardW - 4 * scale, y, underWarns ? warning : muted, under);

            if (!tip.empty() && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tip.c_str());
            ImGui::PopID();
            x += cardW;
        };
        const auto join = [&](bool arrow)
        {
            const float cy = origin.y + pad + line + 4 * scale + tile.y / 2;
            if (arrow)
            {
                const float x0 = x + 4 * scale, x1 = x + joinW - 4 * scale, h = 4 * scale;
                list->AddLine(ImVec2(x0, cy), ImVec2(x1 - h, cy), muted, std::max(1.0f, 1.5f * scale));
                list->AddTriangleFilled(ImVec2(x1, cy), ImVec2(x1 - 2 * h, cy - h), ImVec2(x1 - 2 * h, cy + h), muted);
            }
            else
                CentredText(list, x, joinW, cy - line / 2, muted, "+");
            x += joinW;
        };

        for (size_t input = 0; input < last; ++input)
        {
            if (input != 0)
                join(false);
            const std::string name = "Key " + std::to_string(input + 1);
            const std::optional<Keycode>& key = combo.inputs[input];
            if (!key)
            {
                card(input, name.c_str(), key, "-", true, "a gap", true,
                     "Empty, with a key after it: the board stops here, and never reaches the keys after");
                continue;
            }
            std::string under, tip = KeycodeHoverText(*key, m_Keyboard.keycodeVersion);
            bool        warns = true;
            if (input >= matched)
                under = "never reached";
            else if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, *key))
            {
                under = *layer == 0 ? "" : "layer " + std::to_string(*layer);
                warns = false;
            }
            else
                under = TapHoldSending(m_Keyboard, *key) ? "sent by no key" : "on no key";
            card(input, name.c_str(), key, "", false, under, warns, tip);
        }
        if (last < combo.inputs.size())
        {
            if (last != 0)
                join(false);
            const std::string name = last == 0 ? "Keys" : "Key " + std::to_string(last + 1);
            card(last, name.c_str(), std::nullopt, "+", false, last >= 2 ? "optional" : "", false,
                 last == 0 ? "Click keys on the board, or pick them below" : "Another key, if the combo needs it");
        }
        join(true);
        card(c_Output, "Sends", combo.output, "-", false, "", false,
             combo.output ? KeycodeHoverText(*combo.output, m_Keyboard.keycodeVersion) : "What the keys send together");

        // The term, after a rule: one for every combo.
        const float termX = x + c_TermGap * scale;
        list->AddLine(ImVec2(termX - c_TermGap * scale / 2, origin.y + pad), ImVec2(termX - c_TermGap * scale / 2, origin.y + cardH - pad),
                      ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::SetCursorScreenPos(ImVec2(termX, origin.y + pad));
        ImGui::BeginGroup();
        PushHeaderFont();
        ImGui::TextUnformatted("Combo term");
        ImGui::PopFont();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240 * scale);
        if (m_Term)
        {
            uint16_t term = *m_Term;
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("10000").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight() * 2);
            const uint16_t step = 5;
            if (ImGui::InputScalar("##term", ImGuiDataType_U16, &term, &step))
                m_Term = std::min(term, c_LongestTerm);
            ImGui::SameLine();
            ImGui::TextUnformatted("ms, every combo");
            ImGui::TextDisabled("How close together the keys must come, and how long a combo key waits before it is "
                                "sent alone. Also in QMK Settings.");
        }
        else
        {
            ImGui::TextUnformatted("Fixed by the firmware");
            ImGui::TextDisabled("This board has no combo term in QMK Settings: its term is the one it was built "
                                "with -- 50 ms unless its keymap says otherwise. No app can read it.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const float bottom = std::max(origin.y + cardH, ImGui::GetItemRectMax().y);

        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
        ImGui::Dummy(ImVec2(x - origin.x, pad));
    }

    // What the combo does and costs, then what is wrong with it -- what Vial never says.
    void CombosSection::DrawSays()
    {
        Combo&                     combo   = m_Combos[m_Combo];
        const std::vector<Keycode> matched = combo.MatchedInputs();
        const std::string          term    = m_Term ? std::to_string(*m_Term) + " ms" : "the combo term";
        std::vector<std::string>   names;
        for (const Keycode& input : matched)
            names.push_back(NameOf(input));
        const std::string keys = JoinNames(names);

        ImGui::PushTextWrapPos(0.0f);
        if (combo.IsEmpty())
            ColouredText(PanelColour::Muted, "Empty: click two keys or more on the board, or pick them below, then what "
                                             "they send.");
        else if (matched.size() >= 2)
        {
            std::string does = "Press " + keys + " together -- in any order, each within " + term + " of the last -- and ";
            if (combo.output)
            {
                const std::string out = NameOf(*combo.output);
                does += out + " is sent instead. Held, " + out + " stays held until " +
                        (matched.size() == 2 ? "both are" : "all are") + " released.";
            }
            else
                does += "nothing is sent.";
            ImGui::TextUnformatted(does.c_str());
            ColouredText(PanelColour::Muted, "Typed alone, %s wait up to %s before being sent.", keys.c_str(), term.c_str());
        }

        // What is wrong, each with its fix where there is one.
        const auto warn = [](const std::string& words) { ColouredText(PanelColour::Warning, "%s", words.c_str()); };
        const auto note = [](const std::string& words) { ColouredText(PanelColour::Muted, "%s", words.c_str()); };

        if (combo.HasGap())
        {
            if (matched.empty())
                warn("The first key is empty: the board takes the whole combo as unused.");
            else
            {
                std::vector<std::string> lost;
                for (size_t input = matched.size(); input < combo.inputs.size(); ++input)
                    if (combo.inputs[input])
                        lost.push_back(NameOf(*combo.inputs[input]));
                warn(JoinNames(lost) + (lost.size() == 1 ? " comes" : " come") +
                     " after an empty key: the board stops at the gap, so this combo is " + keys + " alone.");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Close the gap"))
                combo.CloseGaps();
        }

        std::vector<std::string> elsewhere;
        for (const Keycode& input : matched)
        {
            if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, input))
            {
                if (*layer != 0)
                    elsewhere.push_back(NameOf(input) + " (layer " + std::to_string(*layer) + ")");
            }
            else if (const std::optional<Keycode> tapHold = TapHoldSending(m_Keyboard, input))
            {
                const std::string whole = FormatKeycode(*tapHold);
                warn("No key sends " + NameOf(input) + ": the key typing it sends " + whole +
                     ", a tap-hold key, and a combo matches the whole keycode.");
                ImGui::SameLine();
                ImGui::PushID(whole.c_str());
                if (ImGui::SmallButton(("Use " + whole).c_str()))
                    for (std::optional<Keycode>& slot : combo.inputs)
                        if (slot == input)
                            slot = *tapHold;
                ImGui::PopID();
            }
            else
                warn("No key on the board sends " + NameOf(input) + ": the combo can never fire.");
        }
        if (!elsewhere.empty())
            note(JoinNames(elsewhere) + (elsewhere.size() == 1 ? " is" : " are") +
                 " on another layer only: the combo works while that layer is on.");

        if (matched.size() == 1)
            warn("One key alone: every press of " + keys + " waits up to " + term + ", then sends " +
                 (combo.output ? NameOf(*combo.output) : std::string("nothing")) +
                 ". Set on the key itself in Keymap, it would not wait.");
        if (matched.size() >= 2 && !combo.output)
            warn("Nothing to send: pressed together, " + keys + " are swallowed and nothing is sent.");

        // How it gets along with the others sharing its keys (process_combo.c, overlaps()).
        for (size_t other = 0; other < m_Combos.size(); ++other)
        {
            if (other == m_Combo)
                continue;
            const Combo&             them = m_Combos[other];
            std::vector<std::string> theirs;
            for (const Keycode& input : them.MatchedInputs())
                theirs.push_back(NameOf(input));
            const std::string what = SlotName(other) + " (" + JoinNames(theirs) + " to " +
                                     (them.output ? NameOf(*them.output) : std::string("nothing")) + ")";
            switch (RelationOf(combo, them))
            {
            case ComboRelation::SameKeys:
                if (other > m_Combo)
                    warn("Never fires: " + what + " has the same keys, and the later slot wins.");
                else
                    note("Same keys as " + what + ", which never fires because of this one.");
                break;
            case ComboRelation::Inside:
                note("Part of " + what + ": with all its keys pressed in time, that one wins.");
                break;
            case ComboRelation::Holds:
                note("Holds " + what + ": with all " + std::to_string(matched.size()) +
                     " keys pressed in time, this one wins.");
                break;
            case ComboRelation::None:
                break;
            }
        }
        ImGui::PopTextWrapPos();
    }

    void CombosSection::DrawTools()
    {
        Combo&                  combo = m_Combos[m_Combo];
        std::optional<Keycode>& key   = Part();
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(m_Part == c_Output ? "Sends" : ("Key " + std::to_string(m_Part + 1)).c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        if (m_Part == c_Output)
        {
            if (!key)
                ImGui::TextDisabled("pick a key below");
            else
            {
                // Sent with, as Keymap's key line: Ctrl+Z is one output.
                if (DrawSentWith(*key, m_Legends, m_Keyboard.keycodeVersion))
                    ImGui::SameLine();
                if (ImGui::SmallButton("Empty it"))
                    key.reset();
                ImGui::SetItemTooltip("Pressed together, the keys then send nothing");
                ImGui::SameLine();
                ImGui::TextDisabled("or pick a key below to change it");
            }
        }
        else if (key)
        {
            if (ImGui::SmallButton("Remove"))
            {
                key.reset();
                combo.CloseGaps();   // Nazg never writes a gap
                SelectNext();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("pick a key below to change it -- or click keys on the board: a click adds a key, a "
                                "second click takes it out");
        }
        else
            ImGui::TextDisabled("click a key on the board, or pick one below");

        if (IsLocked())
            ColouredText(PanelColour::Warning, "This board is locked: Vial stores Boot as nothing in what a combo sends, so "
                                               "Boot is greyed there. To use it, click Locked in the header, then hold "
                                               "the outlined keys.");
    }

    void CombosSection::DrawPicker()
    {
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };

        // A locked board stores Boot as nothing in the output -- the firewall leaves the inputs alone.
        const bool locked = IsLocked() && m_Part == c_Output;
        const std::function<std::optional<std::string>(const Keycode&)> unavailable =
            [locked](const Keycode& keycode) -> std::optional<std::string>
        {
            if (const auto* named = std::get_if<NamedKey>(&keycode); locked && named && named->name == "QK_BOOT")
                return std::string("This board is locked: Vial would store Boot as nothing");
            return std::nullopt;
        };

        Combo&                        combo   = m_Combos[m_Combo];
        const std::optional<Keycode>& current = Part();
        const KeycodePickerEvents     events  = DrawKeycodePicker(
            m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion, current, &m_Keyboard, unavailable });
        if (!events.picked)
            return;
        if (m_Part == c_Output)
        {
            combo.output = current ? ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion) : *events.picked;
            return;
        }

        // An input: a key once only.
        for (size_t input = 0; input < combo.inputs.size(); ++input)
            if (input != m_Part && combo.inputs[input] == events.picked)
            {
                m_Message   = NameOf(*events.picked) + " is in " + SlotName(m_Combo) + " already";
                m_IsWarning = true;
                return;
            }
        combo.inputs[m_Part] = *events.picked;
        SelectNext();
    }
}
