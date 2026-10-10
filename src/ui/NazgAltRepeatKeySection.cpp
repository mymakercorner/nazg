// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgAltRepeatKeySection.h"

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/vial/NazgVialLoader.h"
#include "model/NazgCombo.h"
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
        constexpr float c_CardWidth  = 76.0f;   // one part of the rule: its name, key and a word under it
        constexpr float c_ArrowWidth = 68.0f;   // the arrow between them, "both ways" under it

        // Text centred in [left, left + width), cut to it.
        void CentredText(ImDrawList* list, float left, float width, float y, ImU32 colour, const std::string& text)
        {
            const float w = ImGui::CalcTextSize(text.c_str()).x;
            list->PushClipRect(ImVec2(left, y), ImVec2(left + width, y + ImGui::GetFontSize() * 2), true);
            list->AddText(ImVec2(left + std::max(0.0f, (width - w) / 2), y), colour, text.c_str());
            list->PopClipRect();
        }

        bool IsNothing(const Keycode& keycode)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named && (named->name == "KC_NO" || named->name == "KC_TRNS");
        }

        bool IsBoot(const Keycode& keycode)
        {
            const auto* named = std::get_if<NamedKey>(&keycode);
            return named && named->name == "QK_BOOT";
        }

        // "Ctrl", "Ctrl or Alt", "Ctrl, Alt or Win".
        std::string JoinOr(const std::vector<std::string>& names)
        {
            std::string joined;
            for (size_t index = 0; index < names.size(); ++index)
                joined += (index == 0 ? "" : index + 1 == names.size() ? " or " : ", ") + names[index];
            return joined;
        }

        // Which side a modifier mask holds its modifiers on, as one choice for the whole set.
        enum class Side
        {
            Either,   // both sides of each, or nothing set
            Left,
            Right,
            Mixed,    // some on one side, some on the other: shown and kept, not built
        };

        Side SideOf(uint8_t mask)
        {
            const uint8_t left = mask & 0x0F, right = mask >> 4;
            if (left == right)
                return Side::Either;
            if (right == 0)
                return Side::Left;
            if (left == 0)
                return Side::Right;
            return Side::Mixed;
        }

        // The modifiers set, by kind -- Ctrl, Shift, Alt, Gui -- whatever their side.
        uint8_t KindsOf(uint8_t mask)
        {
            return static_cast<uint8_t>((mask | (mask >> 4)) & 0x0F);
        }

        // Whether `key` types `base` -- itself, with modifiers, or tapped as a tap-hold key's tap:
        // how the firmware matches the last key.
        bool Types(const Keycode& key, const Keycode& base)
        {
            return BaseOf(key).key == base;
        }

        // The first layer, from 0 up, on which a key of the selected layout types `base`.
        std::optional<uint8_t> LayerTyping(const Keyboard& keyboard, const Keycode& base)
        {
            for (int layer = 0; layer < keyboard.keymap.Layers(); ++layer)
                for (const DefinitionKey& key : keyboard.definition.keys)
                    if (!key.decal && keyboard.IsKeyVisible(key) &&
                        Types(keyboard.KeycodeFor(key, static_cast<uint8_t>(layer)), base))
                        return static_cast<uint8_t>(layer);
            return std::nullopt;
        }
    }

    AltRepeatKeySection::AltRepeatKeySection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                             const LegendSettings& legends, const std::optional<VialUnlockStatus>& lock,
                                             const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock),
          m_AdvancedTools(advancedTools)
    {
    }

    bool AltRepeatKeySection::IsChanged(size_t slot) const
    {
        return m_IsLoaded && slot < m_Entries.size() && slot < m_Stored.size() && m_Entries[slot] != m_Stored[slot];
    }

    bool AltRepeatKeySection::IsLocked() const
    {
        return m_Lock && !m_Lock->unlocked;
    }

    bool AltRepeatKeySection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool AltRepeatKeySection::HasUnsavedChanges() const
    {
        for (size_t slot = 0; slot < m_Entries.size(); ++slot)
            if (IsChanged(slot))
                return true;
        return false;
    }

    std::string AltRepeatKeySection::UnsavedSummary() const
    {
        std::vector<std::string> changed;
        for (size_t slot = 0; slot < m_Entries.size(); ++slot)
            if (IsChanged(slot))
                changed.push_back(SlotName(slot));
        return JoinNames(changed);
    }

    std::string AltRepeatKeySection::SlotName(size_t slot) const
    {
        return "AR " + std::to_string(slot);
    }

    std::string AltRepeatKeySection::NameOf(const Keycode& keycode, Sides sides) const
    {
        const BaseKey                    base  = BaseOf(keycode);
        const std::array<const char*, 4> words = ModifierWords(m_Legends.modifierNames);
        std::string                      name;
        for (int bit = 0; bit < 4; ++bit)
        {
            const bool left = (base.mods & (0x01 << bit)) != 0, right = (base.mods & (0x10 << bit)) != 0;
            const char* side = sides == Sides::None || (left && right) ? ""
                               : right                                   ? "Right "
                               : sides == Sides::Both                    ? "Left "
                                                                         : "";
            if (left || right)
                name += std::string(side) + words[bit] + "+";
        }
        // A tap-hold key's own expression: what it holds is part of what is sent.
        if (base.mods == 0 && !std::holds_alternative<ModifiedKey>(keycode))
            return name + KeycodeLabel(keycode, m_Keyboard.keycodeVersion);
        return name + KeycodeLabel(base.key, m_Keyboard.keycodeVersion);
    }

    std::string AltRepeatKeySection::MaskWords(uint8_t mask, bool alike, bool any) const
    {
        const std::array<const char*, 4> words = ModifierWords(m_Legends.modifierNames);
        std::vector<std::string>         names;
        for (int bit = 0; bit < 4; ++bit)
        {
            const bool left = (mask & (0x01 << bit)) != 0, right = (mask & (0x10 << bit)) != 0;
            if ((left && right) || ((left || right) && alike))
                names.emplace_back(words[bit]);
            else if (left)
                names.push_back(std::string("Left ") + words[bit]);
            else if (right)
                names.push_back(std::string("Right ") + words[bit]);
        }
        if (any)
            return JoinOr(names);
        std::string joined;
        for (size_t index = 0; index < names.size(); ++index)
            joined += (index == 0 ? "" : "+") + names[index];
        return joined;
    }

    void AltRepeatKeySection::Start(AltRepeatKey& altRepeatKey) const
    {
        if (altRepeatKey.IsEmpty())
            altRepeatKey.options = NewAltRepeatKey().options;
    }

    void AltRepeatKeySection::SetKey(const Keycode& keycode)
    {
        AltRepeatKey& entry = m_Entries[m_Slot];
        Start(entry);
        if (m_Part == Part::Sends)
        {
            entry.altKey = keycode;
            return;
        }
        entry.lastKey = keycode;
        m_Part        = entry.altKey ? Part::LastKey : Part::Sends;
    }

    Strip AltRepeatKeySection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Alt repeat key";
        for (size_t slot = 0; slot < m_Keyboard.report.altRepeatKeyCount; ++slot)
        {
            const bool known = m_IsLoaded && slot < m_Entries.size();
            strip.entries.push_back(SlotName(slot));
            strip.changed.push_back(IsChanged(slot));
            strip.empty.push_back(known && m_Entries[slot].IsEmpty());
            strip.struck.push_back(known && !m_Entries[slot].IsEmpty() && !m_Entries[slot].IsOn());
        }
        strip.chosen = m_Slot;
        return strip;
    }

    void AltRepeatKeySection::OnStripChosen(size_t entry)
    {
        m_Slot = entry;
        m_Part = m_IsLoaded && entry < m_Entries.size() && !m_Entries[entry].IsEmpty() ? Part::Sends : Part::LastKey;
    }

    void AltRepeatKeySection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The keys typing the selected entry's last key, lit -- with the layer's legend and "L1" when
        // only another layer has it -- and a tag under the first: the modifiers held with it and what
        // Alt Repeat sends. Both ways, the alt key the same way back.
        if (m_IsLoaded && m_Slot < m_Entries.size())
        {
            const AltRepeatKey&      entry  = m_Entries[m_Slot];
            const bool               alike  = entry.Has(AltRepeatOption::IgnoreHandedness);
            const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
            const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                              LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
            const auto light = [&](const std::optional<Keycode>& from, const std::optional<Keycode>& to)
            {
                if (!from || NeverRemembered(*from))
                    return;
                const BaseKey base = BaseOf(*from);
                const std::optional<uint8_t> layer = LayerTyping(m_Keyboard, base.key);
                if (!layer)
                    return;
                BoardTag tag;
                for (size_t index = 0; index < board.keys.size(); ++index)
                {
                    BoardKey& key = board.keys[index];
                    if (key.geometry.decal || !Types(m_Keyboard.KeycodeFor(key.geometry, *layer), base.key))
                        continue;
                    key.marks |= Mark::Highlighted;
                    if (*layer != 0)
                    {
                        KeycapLegend legend = LegendFor(base.key, context);
                        legend.header       = { { "Layer " + std::to_string(*layer), "L" + std::to_string(*layer) },
                                                CommandCategory::Behaviour };
                        key.legends         = legend;
                        key.fallthrough     = Fallthrough::None;
                    }
                    if (tag.keys.empty())
                        tag.keys.push_back(index);
                }
                const std::string sent = to ? TagText(LegendFor(*to, context), NameOf(*to, Sides::Right)) : std::string("nothing");
                tag.text = (base.mods != 0 ? MaskWords(base.mods, alike, false) + " " : std::string()) + "\xE2\x86\x92 " + sent;
                board.tags.push_back(std::move(tag));
            };
            light(entry.lastKey, entry.altKey);
            if (entry.Has(AltRepeatOption::Bidirectional))
                light(entry.altKey, entry.lastKey);
        }

        MarkUnlockKeys(board, m_Lock);
    }

    void AltRepeatKeySection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!m_IsLoaded || m_Slot >= m_Entries.size() || !events.hoveredKey || IsBusy())
            return;

        // What the key sends on layer 0, else on the first layer that has something there.
        const DefinitionKey& geometry = board.keys[*events.hoveredKey].geometry;
        Keycode              sent     = m_Keyboard.KeycodeFor(geometry, 0);
        for (int layer = 1; IsNothing(sent) && layer < m_Keyboard.keymap.Layers(); ++layer)
            sent = m_Keyboard.KeycodeFor(geometry, static_cast<uint8_t>(layer));
        if (IsNothing(sent))
            return;

        const bool sends = m_Part == Part::Sends;
        // The last key a tap-hold key types is its tap: the firmware remembers that.
        if (!sends && (std::holds_alternative<ModTapKey>(sent) || std::holds_alternative<LayerTapKey>(sent)))
            sent = BaseOf(sent).key;
        const std::string name = NameOf(sent, Sides::Right);
        if (!sends)
            if (const std::optional<std::string_view> why = NeverRemembered(sent))
            {
                ImGui::SetTooltip("%s -- never the last key: QMK does not remember %.*s", name.c_str(),
                                  static_cast<int>(why->size()), why->data());
                return;
            }
        if (sends && IsRepeatKey(sent))
        {
            ImGui::SetTooltip("%s -- Alt Repeat cannot send a Repeat key", name.c_str());
            return;
        }

        ImGui::SetTooltip(sends ? "%s -- click: what %s sends" : "%s -- click: the last key of %s", name.c_str(),
                          SlotName(m_Slot).c_str());
        if (events.clickedKey)
        {
            if (m_Part == Part::Allowed)
                m_Part = Part::LastKey;
            SetKey(sent);
        }
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    Task<void> AltRepeatKeySection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            m_Stored   = co_await ReadAltRepeatKeys(vial, m_Keyboard.report.altRepeatKeyCount, m_Keyboard.keycodeVersion);
            m_Entries  = m_Stored;
            m_IsLoaded = true;
            OnStripChosen(m_Slot);
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the alt repeat keys could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    void AltRepeatKeySection::SaveChanges()
    {
        if (IsBusy() || !m_IsLoaded || !HasUnsavedChanges())
            return;

        std::vector<size_t> changed;
        for (size_t slot = 0; slot < m_Entries.size(); ++slot)
            if (IsChanged(slot))
            {
                if (!EncodeAltRepeatKey(m_Entries[slot], m_Keyboard.keycodeVersion))
                {
                    m_Message   = SlotName(slot) + " holds a key this board cannot store";
                    m_IsWarning = true;
                    return;
                }
                changed.push_back(slot);
            }
        m_Request = Save(std::move(changed));
    }

    Task<void> AltRepeatKeySection::Save(std::vector<size_t> slots)
    {
        m_Message.clear();
        m_IsWarning     = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            // Each slot written, then read back: what the board holds now, whatever was asked.
            std::vector<std::string> notKept;
            for (size_t slot : slots)
            {
                const VialAltRepeatKeyEntry entry = *EncodeAltRepeatKey(m_Entries[slot], m_Keyboard.keycodeVersion);
                const uint8_t               index = static_cast<uint8_t>(slot);
                co_await vial.SetAltRepeatKey(index, entry);
                const VialAltRepeatKeyEntry stored = co_await vial.GetAltRepeatKey(index);
                m_Stored[slot]                     = DecodeAltRepeatKey(stored, m_Keyboard.keycodeVersion);
                if (stored == entry)
                    m_Entries[slot] = m_Stored[slot];
                else
                    notKept.push_back(SlotName(slot));   // the edit stays, still to be written
            }

            if (notKept.empty())
                m_Message = slots.size() == 1 ? "stored " + SlotName(slots[0]) + ", read back the same"
                                              : "stored " + std::to_string(slots.size()) + " changes, read back the same";
            else
            {
                m_Message   = "the board did not keep " + JoinNames(notKept);
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

    void AltRepeatKeySection::DiscardChanges()
    {
        m_Entries = m_Stored;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void AltRepeatKeySection::DrawPanel()
    {
        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("alt repeat keys", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }
        if (m_Entries.empty())
        {
            ImGui::TextDisabled("This board has no alt repeat keys.");
            return;
        }
        if (m_Slot >= m_Entries.size())
            m_Slot = 0;

        DrawSlotLine();
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawRule();
        DrawConditions();
        DrawSays();
        ImGui::Separator();
        DrawTools();
        DrawPicker();
        ImGui::EndDisabled();
    }

    void AltRepeatKeySection::DrawSlotLine()
    {
        AltRepeatKey& entry = m_Entries[m_Slot];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(SlotName(m_Slot).c_str());
        ImGui::SetItemTooltip("Slot %zu of the board's %zu -- when two match, the one with more modifiers wins, then the first",
                              m_Slot, m_Entries.size());

        ImGui::SameLine();
        const bool on = entry.IsOn();
        if (ToggleButton(on ? "On###onoff" : "Off###onoff", on, !entry.IsEmpty() && !IsBusy()))
            entry.options ^= AltRepeatOption::Enabled;
        ImGui::SetItemTooltip("An entry off stays on the board, unused");

        ImGui::SameLine();
        ImGui::BeginDisabled(entry.IsEmpty() || IsBusy());
        if (ImGui::Button("Clear"))
        {
            entry  = AltRepeatKey{};
            m_Part = Part::LastKey;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Empties the slot -- written by Save, undone by Revert");

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

    // The rule, left to right: the last key, an arrow -- a click turns it both ways -- and what Alt
    // Repeat sends.
    void AltRepeatKeySection::DrawRule()
    {
        AltRepeatKey&            entry   = m_Entries[m_Slot];
        const bool               both    = entry.Has(AltRepeatOption::Bidirectional);
        const float              scale   = ImGui::GetStyle().FontScaleDpi;
        const ImVec2             tile    = TileSize();
        const float              cardW   = std::max(c_CardWidth * scale, tile.x + 16 * scale);
        const float              arrowW  = c_ArrowWidth * scale;
        const float              pad     = 6 * scale;
        const float              line    = ImGui::GetTextLineHeight();
        const float              cardH   = pad + line + 4 * scale + tile.y + 4 * scale + line + pad;
        ImDrawList*              list    = ImGui::GetWindowDrawList();
        const ImU32              muted   = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32              text    = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32              warning = ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Warning));
        const std::vector<Words> custom  = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };

        ImGui::Dummy(ImVec2(0, pad));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float        x      = origin.x;

        // One part: its name, its key -- an outline when empty -- and a word under it. A click
        // selects it.
        const auto card = [&](Part part, const char* name, const std::optional<Keycode>& key, const char* empty,
                              const std::string& under, bool warns, const std::string& tip)
        {
            PushHeaderFont();
            const float width = std::max(cardW, ImGui::CalcTextSize(name).x + 12 * scale);
            ImGui::PopFont();
            const ImVec2 p0(x, origin.y), p1(x + width, origin.y + cardH);
            const bool   selected = m_Part == part;
            ImGui::PushID(static_cast<int>(part));
            ImGui::SetCursorScreenPos(p0);
            if (ImGui::InvisibleButton("##part", ImVec2(width, cardH)))
                m_Part = part;
            const bool hovered = ImGui::IsItemHovered();
            if (selected || hovered)
                list->AddRectFilled(p0, p1, ImGui::GetColorU32(selected ? ImGuiCol_Header : ImGuiCol_HeaderHovered), 7 * scale);
            if (selected)
                list->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_ButtonActive), 7 * scale, 0, std::max(1.0f, 1.5f * scale));

            float y = p0.y + pad;
            PushHeaderFont();
            CentredText(list, p0.x, width, y, text, name);
            ImGui::PopFont();
            y += line + 4 * scale;
            const ImVec2 b0(p0.x + (width - tile.x) / 2, y), b1(p0.x + (width + tile.x) / 2, y + tile.y);
            if (key)
            {
                KeycodeTile keyTile = TileOf(*key, legends, false);
                keyTile.hovered     = hovered;
                DrawKeycodeTile(keyTile, FaceBox{ b0.x, b0.y, b1.x, b1.y });
                if (warns)
                    list->AddRect(b0, b1, warning, 5 * scale, 0, std::max(1.0f, 1.5f * scale));
            }
            else
            {
                list->AddRect(b0, b1, ImGui::GetColorU32(ImGuiCol_Border), 5 * scale, 0, std::max(1.0f, scale));
                CentredText(list, b0.x, b1.x - b0.x, (b0.y + b1.y - line) / 2, muted, empty);
            }
            y += tile.y + 4 * scale;
            if (!under.empty())
                CentredText(list, p0.x + 2 * scale, width - 4 * scale, y, warns ? warning : muted, under);

            if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tip.c_str());
            ImGui::PopID();
            x += width;
        };

        // The last key, and where it is.
        std::string under;
        bool        warns = false;
        if (entry.lastKey)
        {
            if (NeverRemembered(*entry.lastKey))
            {
                under = "never remembered";
                warns = true;
            }
            else if (const std::optional<uint8_t> layer = LayerTyping(m_Keyboard, BaseOf(*entry.lastKey).key))
                under = *layer == 0 ? "" : "layer " + std::to_string(*layer);
            else
            {
                under = "on no key";
                warns = true;
            }
        }
        card(Part::LastKey, "Last key", entry.lastKey, "+", under, warns,
             entry.lastKey ? KeycodeHoverText(*entry.lastKey, m_Keyboard.keycodeVersion)
                           : "The last key typed -- click a key on the board, or pick one below");

        // The arrow: one way, or both ways -- a click turns it.
        {
            const ImVec2 p0(x, origin.y);
            ImGui::SetCursorScreenPos(p0);
            if (ImGui::InvisibleButton("##bothways", ImVec2(arrowW, cardH)))
            {
                Start(entry);
                entry.options ^= AltRepeatOption::Bidirectional;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
                list->AddRectFilled(p0, ImVec2(x + arrowW, origin.y + cardH), ImGui::GetColorU32(ImGuiCol_HeaderHovered), 7 * scale);
            ImGui::SetItemTooltip(both ? "Both ways: after what is sent, Alt Repeat sends the last key -- click for one way"
                                       : "One way -- click for both ways: after what is sent, Alt Repeat sends the last key");

            const float cy = origin.y + pad + line + 4 * scale + tile.y / 2;
            const float x0 = x + 14 * scale, x1 = x + arrowW - 14 * scale, h = 4 * scale, thick = std::max(1.0f, 1.5f * scale);
            const ImU32 colour = hovered ? text : muted;
            const auto  arrow  = [&](float y, bool right)
            {
                list->AddLine(ImVec2(right ? x0 : x0 + h, y), ImVec2(right ? x1 - h : x1, y), colour, thick);
                if (right)
                    list->AddTriangleFilled(ImVec2(x1, y), ImVec2(x1 - 2 * h, y - h), ImVec2(x1 - 2 * h, y + h), colour);
                else
                    list->AddTriangleFilled(ImVec2(x0, y), ImVec2(x0 + 2 * h, y - h), ImVec2(x0 + 2 * h, y + h), colour);
            };
            if (both)
            {
                arrow(cy - 4 * scale, true);
                arrow(cy + 4 * scale, false);
            }
            else
                arrow(cy, true);
            CentredText(list, x, arrowW, origin.y + pad + line + 4 * scale + tile.y + 4 * scale, muted, both ? "both ways" : "one way");
            x += arrowW;
        }

        // What Alt Repeat sends.
        const bool wrong = entry.altKey && SentWrong(*entry.altKey);
        card(Part::Sends, "Alt Repeat sends", entry.altKey, "-", !entry.altKey ? "nothing" : wrong ? "sent wrong" : "", wrong,
             entry.altKey ? KeycodeHoverText(*entry.altKey, m_Keyboard.keycodeVersion)
                          : "What Alt Repeat sends after the last key -- pick a key below");

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + cardH));
        ImGui::Dummy(ImVec2(x - origin.x, pad));
    }

    // Under the rule: the modifiers that may be held too, as a chip naming them; the two options.
    void AltRepeatKeySection::DrawConditions()
    {
        AltRepeatKey& entry = m_Entries[m_Slot];
        const float   gap   = 18 * ImGui::GetStyle().FontScaleDpi;
        const bool    alike = entry.Has(AltRepeatOption::IgnoreHandedness);

        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("May also be held");
        ImGui::SameLine();
        const std::string label = (entry.allowed != 0 ? MaskWords(entry.allowed, alike, true) : std::string("none")) + "###allowed";
        if (ToggleButton(label.c_str(), m_Part == Part::Allowed))
            m_Part = Part::Allowed;
        ImGui::SetItemTooltip("Modifiers that may be held with the last key besides its own -- any other held, the entry does "
                              "not apply. Click, then choose them below");

        const auto box = [&](const char* words, uint8_t option, const char* tip)
        {
            ImGui::SameLine(0, gap);
            bool ticked = entry.Has(option);
            if (ImGui::Checkbox(words, &ticked))
            {
                Start(entry);
                entry.options ^= option;
            }
            ImGui::SetItemTooltip("%s", tip);
        };
        box("Left or right alike", AltRepeatOption::IgnoreHandedness,
            "Right Ctrl counts as Ctrl -- for the last key's modifiers and those allowed");
        box("After any other key too", AltRepeatOption::DefaultToThis,
            "When no entry has the last key, Alt Repeat sends this one -- and QMK's own pairs no longer answer");
    }

    // What the entry does, then what is wrong with it -- what Vial never says.
    void AltRepeatKeySection::DrawSays()
    {
        AltRepeatKey&     entry = m_Entries[m_Slot];
        const bool        alike = entry.Has(AltRepeatOption::IgnoreHandedness);
        const bool        both  = entry.Has(AltRepeatOption::Bidirectional);
        const std::string last  = entry.lastKey ? NameOf(*entry.lastKey, alike ? Sides::None : Sides::Both) : std::string("...");
        const std::string alt   = entry.altKey ? NameOf(*entry.altKey, Sides::Right) : std::string("nothing");

        // The first entry on with "after any other key": the one the firmware uses.
        std::optional<size_t> firstDefault;
        for (size_t slot = 0; slot < m_Entries.size() && !firstDefault; ++slot)
            if (m_Entries[slot].IsOn() && m_Entries[slot].Has(AltRepeatOption::DefaultToThis) && m_Entries[slot].altKey)
                firstDefault = slot;

        ImGui::PushTextWrapPos(0.0f);
        if (entry.IsEmpty())
            ColouredText(PanelColour::Muted, "Empty: click a key on the board or pick one below -- the last key typed -- then "
                                             "what Alt Repeat sends after it.");
        else
        {
            std::string does = "After " + last + ", Alt Repeat sends " + alt;
            if (both && entry.lastKey && entry.altKey)
                does += "; after " + alt + ", " + last;
            ImGui::TextUnformatted((does + ".").c_str());

            const uint8_t required = entry.lastKey ? BaseOf(*entry.lastKey).mods : 0;
            std::string   more     = entry.allowed != 0 ? MaskWords(entry.allowed, alike, true) + " may be held too."
                                     : required != 0    ? std::string("No other modifier may be held.")
                                                        : std::string("Not with a modifier held.");
            if (alike && (required != 0 || entry.allowed != 0))
                more += " Left or right alike.";
            if (entry.Has(AltRepeatOption::DefaultToThis))
                more += " After any other key no entry has, " + alt + " too.";
            ColouredText(PanelColour::Muted, "%s", more.c_str());
        }

        // What is wrong, each with its fix where there is one; then what is worth knowing.
        const auto warn = [](const std::string& words) { ColouredText(PanelColour::Warning, "%s", words.c_str()); };
        const auto note = [](const std::string& words) { ColouredText(PanelColour::Muted, "%s", words.c_str()); };
        const auto fix  = [](const std::string& label) -> bool
        {
            ImGui::SameLine();
            ImGui::PushID(label.c_str());
            const bool clicked = ImGui::SmallButton(label.c_str());
            ImGui::PopID();
            return clicked;
        };

        if (!entry.IsEmpty())
        {
            if (!entry.IsOn())
            {
                note("Off: kept on the board, never used.");
                if (fix("Turn it on"))
                    entry.options |= AltRepeatOption::Enabled;
            }
            if (!entry.altKey)
                warn("Nothing to send: pick what Alt Repeat sends.");
            if (!entry.lastKey && !entry.Has(AltRepeatOption::DefaultToThis))
                warn("No last key: it never applies.");
            if (entry.lastKey)
            {
                if (const std::optional<std::string_view> why = NeverRemembered(*entry.lastKey))
                    warn(last + " is never the last key: QMK does not remember " + std::string(*why) + ". It never applies.");
                else if (!LayerTyping(m_Keyboard, BaseOf(*entry.lastKey).key))
                    warn("No key on the board types " + NameOf(BaseOf(*entry.lastKey).key, Sides::None) + ".");
            }

            if (entry.altKey && SentWrong(*entry.altKey))
            {
                warn(alt + " is not what is sent: vial-qmk loses right-hand modifiers here -- " +
                     NameOf(BaseOf(*entry.altKey).key, Sides::None) + " alone, or another key altogether. Left ones are sent right.");
                if (fix("Use the left one"))
                    entry.altKey = WithLeftModifiers(*entry.altKey);
            }
            else if (both && entry.lastKey && entry.altKey && SentWrong(*entry.lastKey))
            {
                warn("Going back, " + NameOf(*entry.lastKey, Sides::Right) +
                     " is not what is sent: vial-qmk loses right-hand modifiers here. Left ones are sent right.");
                if (fix("Use the left one"))
                {
                    entry.lastKey = WithLeftModifiers(*entry.lastKey);
                    entry.options |= AltRepeatOption::IgnoreHandedness;   // the right one still matches
                }
            }
            if (entry.altKey && (std::holds_alternative<ModTapKey>(*entry.altKey) || std::holds_alternative<LayerTapKey>(*entry.altKey)))
                note(FormatKeycode(*entry.altKey) + " is sent as its tap key alone, " + NameOf(BaseOf(*entry.altKey).key, Sides::None) + ".");

            for (size_t other = 0; other < m_Entries.size(); ++other)
            {
                if (other == m_Slot || !entry.altKey || !SameLastKey(entry, m_Entries[other]))
                    continue;
                if (other < m_Slot)
                    warn(SlotName(other) + " has the same last key: when both apply, " + SlotName(other) + " wins -- it comes first.");
                else
                    note(SlotName(other) + " has the same last key: when both apply, this one wins.");
            }

            if (entry.IsOn() && entry.lastKey && entry.altKey && !firstDefault &&
                std::holds_alternative<NamedKey>(*entry.lastKey) && std::holds_alternative<NamedKey>(*entry.altKey) &&
                QmkPairs(*entry.lastKey, *entry.altKey))
                note("QMK pairs " + last + " and " + alt + " itself: without this entry, the same happens.");

            if (entry.IsOn() && entry.altKey && entry.Has(AltRepeatOption::DefaultToThis))
            {
                if (firstDefault && *firstDefault < m_Slot)
                    warn(SlotName(*firstDefault) + " already answers after any other key, and comes first: this one never does.");
                else
                    note("QMK's own pairs -- Left and Right, Home and End, Backspace and Delete... -- no longer answer: this "
                         "entry does first.");
            }
        }

        if (!LayerSending(m_Keyboard, NamedKey{ "QK_AREP" }))
            warn("No key on this board sends Alt Repeat: these entries are used by nothing until one does. Alt Repeat is "
                 "in Keymap's picker.");
        ImGui::PopTextWrapPos();
    }

    void AltRepeatKeySection::DrawTools()
    {
        AltRepeatKey& entry = m_Entries[m_Slot];
        const float   gap   = 14 * ImGui::GetStyle().FontScaleDpi;
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        switch (m_Part)
        {
        case Part::LastKey: ImGui::TextUnformatted("Last key"); break;
        case Part::Sends: ImGui::TextUnformatted("Alt Repeat sends"); break;
        case Part::Allowed: ImGui::TextUnformatted("May also be held"); break;
        }
        ImGui::PopFont();
        ImGui::SameLine();

        if (m_Part == Part::Allowed)
        {
            const uint8_t mask  = entry.allowed;
            const bool    alike = entry.Has(AltRepeatOption::IgnoreHandedness);
            const Side    side  = alike ? Side::Either : SideOf(mask);

            // The four modifiers: one added on the set's side, either when it has none.
            const std::array<const char*, 4> words = ModifierWords(m_Legends.modifierNames);
            for (int bit = 0; bit < 4; ++bit)
            {
                ImGui::PushID(bit);
                const uint8_t both = static_cast<uint8_t>(0x11 << bit);
                if (ToggleButton(words[bit], (mask & both) != 0))
                {
                    const uint8_t add = side == Side::Left    ? static_cast<uint8_t>(0x01 << bit)
                                        : side == Side::Right ? static_cast<uint8_t>(0x10 << bit)
                                                              : both;
                    Start(entry);
                    entry.allowed = static_cast<uint8_t>((mask & both) != 0 ? mask & ~both : mask | add);
                }
                ImGui::PopID();
                ImGui::SameLine();
            }

            // The side, for the whole set -- while left and right are told apart.
            if (!alike)
            {
                ImGui::SameLine(0, gap);
                const uint8_t kinds = KindsOf(mask);
                if (ToggleButton("Either side", mask != 0 && side == Side::Either, mask != 0))
                    entry.allowed = static_cast<uint8_t>(kinds | (kinds << 4));
                ImGui::SameLine();
                if (ToggleButton("Left", side == Side::Left, mask != 0))
                    entry.allowed = kinds;
                ImGui::SameLine();
                if (ToggleButton("Right", side == Side::Right, mask != 0))
                    entry.allowed = static_cast<uint8_t>(kinds << 4);
                if (side == Side::Mixed)
                    ImGui::SetItemTooltip("The sides are mixed, as the board holds them: a side chosen applies to them all");
                ImGui::SameLine();
            }
            ImGui::AlignTextToFramePadding();
            const uint8_t required = entry.lastKey ? BaseOf(*entry.lastKey).mods : 0;
            if (required != 0)
                ImGui::TextDisabled("besides %s, which the last key needs", MaskWords(required, alike, false).c_str());
            else
                ImGui::TextDisabled("with the last key");
        }
        else if (std::optional<Keycode>& key = m_Part == Part::LastKey ? entry.lastKey : entry.altKey)
        {
            // The modifiers, as Keymap's key line: Ctrl+Z is one key.
            if (DrawSentWith(*key, m_Legends, m_Keyboard.keycodeVersion, m_Part == Part::LastKey ? "Held with it" : "Sent with"))
                ImGui::SameLine();
            if (ImGui::SmallButton("Empty it"))
                key.reset();
            ImGui::SameLine();
            ImGui::TextDisabled(m_Part == Part::LastKey ? "or pick a key below, or click one on the board"
                                                        : "or pick a key below -- any key, a macro or a layer key too");
        }
        else
            ImGui::TextDisabled("click a key on the board, or pick one below");

        if (IsLocked() && m_Part != Part::Allowed)
            ColouredText(PanelColour::Warning, "This board is locked: Vial stores Boot as nothing in an alt repeat key, so Boot "
                                               "is greyed. To use it, click Locked in the header, then hold the outlined keys.");
    }

    void AltRepeatKeySection::DrawPicker()
    {
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };

        // The last key is one QMK remembers; what is sent is no Repeat key; a locked board stores
        // Boot as nothing in both.
        const bool sends  = m_Part == Part::Sends;
        const bool locked = IsLocked();
        const std::function<std::optional<std::string>(const Keycode&)> unavailable =
            [sends, locked](const Keycode& keycode) -> std::optional<std::string>
        {
            if (locked && IsBoot(keycode))
                return std::string("This board is locked: Vial would store Boot as nothing");
            if (!sends)
                if (const std::optional<std::string_view> why = NeverRemembered(keycode))
                    return "Never the last key: QMK does not remember " + std::string(*why);
            if (sends && IsRepeatKey(keycode))
                return std::string("Alt Repeat cannot send a Repeat key");
            return std::nullopt;
        };

        AltRepeatKey&                 entry   = m_Entries[m_Slot];
        const std::optional<Keycode>& current = sends ? entry.altKey : entry.lastKey;
        const KeycodePickerEvents     events  = DrawKeycodePicker(
            m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion, current, &m_Keyboard, unavailable });
        if (!events.picked)
            return;
        if (m_Part == Part::Allowed)
            m_Part = Part::LastKey;
        SetKey(current ? ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion) : *events.picked);
    }
}
