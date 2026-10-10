// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyOverridesSection.h"

#include <algorithm>
#include <array>
#include <exception>
#include <functional>
#include <utility>
#include <variant>

#include "imgui.h"

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
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
        constexpr float c_JoinWidth  = 22.0f;   // the + after Held, the arrow to what is sent
        constexpr float c_LayersGap  = 24.0f;
        constexpr float c_LayersWrap = 220.0f;

        // The start and stop options, open or folded as last left -- for every slot and board, until
        // Nazg closes (Rico, 2026-10-10). Folded at first.
        bool s_OptionsOpen = false;

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

        int CountOf(uint8_t kinds)
        {
            int count = 0;
            for (int bit = 0; bit < 4; ++bit)
                count += (kinds >> bit) & 1;
            return count;
        }
    }

    KeyOverridesSection::KeyOverridesSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                             const LegendSettings& legends, const std::optional<VialUnlockStatus>& lock,
                                             const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock),
          m_AdvancedTools(advancedTools)
    {
    }

    bool KeyOverridesSection::IsChanged(size_t slot) const
    {
        return m_IsLoaded && slot < m_Overrides.size() && slot < m_Stored.size() && m_Overrides[slot] != m_Stored[slot];
    }

    bool KeyOverridesSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool KeyOverridesSection::HasUnsavedChanges() const
    {
        for (size_t slot = 0; slot < m_Overrides.size(); ++slot)
            if (IsChanged(slot))
                return true;
        return false;
    }

    std::string KeyOverridesSection::UnsavedSummary() const
    {
        std::vector<std::string> changed;
        for (size_t slot = 0; slot < m_Overrides.size(); ++slot)
            if (IsChanged(slot))
                changed.push_back(SlotName(slot));
        return JoinNames(changed);
    }

    uint16_t KeyOverridesSection::BoardLayers() const
    {
        const int layers = m_Keyboard.keymap.Layers();
        return layers >= 16 ? uint16_t{ 0xFFFF } : static_cast<uint16_t>((1u << std::max(layers, 1)) - 1);
    }

    std::string KeyOverridesSection::NameOf(const Keycode& keycode) const
    {
        return KeycodeLabel(keycode, m_Keyboard.keycodeVersion);
    }

    std::string KeyOverridesSection::SlotName(size_t slot) const
    {
        return "KO " + std::to_string(slot);
    }

    std::string KeyOverridesSection::MaskWords(uint8_t mask, bool any) const
    {
        const std::array<const char*, 4> words = ModifierWords(m_Legends.modifierNames);
        std::vector<std::string>         names;
        for (int bit = 0; bit < 4; ++bit)
        {
            const bool left = (mask & (0x01 << bit)) != 0, right = (mask & (0x10 << bit)) != 0;
            if (left && right)
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
            joined += (index == 0 ? "" : " + ") + names[index];
        return joined;
    }

    bool KeyOverridesSection::CanSend(const Keycode& keycode) const
    {
        const std::optional<uint16_t> raw = EncodeQmkKeycode(keycode, m_Keyboard.keycodeVersion);
        return raw && CanBeSent(*raw);
    }

    void KeyOverridesSection::Start(KeyOverride& keyOverride) const
    {
        if (keyOverride.IsEmpty())
        {
            const KeyOverride fresh = NewKeyOverride();
            keyOverride.layers      = fresh.layers;
            keyOverride.options     = fresh.options;
        }
    }

    void KeyOverridesSection::SetMask(Part part, uint8_t mask)
    {
        KeyOverride& keyOverride = m_Overrides[m_Slot];
        Start(keyOverride);
        switch (part)
        {
        case Part::Held:
            if (keyOverride.hidden == keyOverride.held)
                keyOverride.hidden = mask;   // hidden follows held while they match
            keyOverride.held = mask;
            break;
        case Part::NotHeld: keyOverride.notHeld = mask; break;
        case Part::Hidden: keyOverride.hidden = mask; break;
        case Part::Key:
        case Part::Sends: break;
        }
    }

    void KeyOverridesSection::SetKey(const Keycode& keycode)
    {
        KeyOverride& keyOverride = m_Overrides[m_Slot];
        Start(keyOverride);
        keyOverride.trigger = keycode;
        m_Part              = keyOverride.held == 0 ? Part::Held : Part::Sends;
    }

    Strip KeyOverridesSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Key override";
        for (size_t slot = 0; slot < m_Keyboard.report.keyOverrideCount; ++slot)
        {
            const bool known = m_IsLoaded && slot < m_Overrides.size();
            strip.entries.push_back(SlotName(slot));
            strip.changed.push_back(IsChanged(slot));
            strip.empty.push_back(known && m_Overrides[slot].IsEmpty());
            strip.struck.push_back(known && !m_Overrides[slot].IsEmpty() && !m_Overrides[slot].IsOn());
        }
        strip.chosen = m_Slot;
        return strip;
    }

    void KeyOverridesSection::OnStripChosen(size_t entry)
    {
        m_Slot = entry;
        m_Part = m_IsLoaded && entry < m_Overrides.size() && !m_Overrides[entry].IsEmpty() ? Part::Sends : Part::Key;
    }

    void KeyOverridesSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The keys sending the selected override's key on its layers, lit -- with the layer's legend
        // and "L1" when only another layer sends it -- and a tag under the first: what is held and
        // what is sent. A key only a tap-hold key holds outlines that key: the whole keycode is
        // matched.
        if (m_IsLoaded && m_Slot < m_Overrides.size() && m_Overrides[m_Slot].trigger)
        {
            const KeyOverride&       keyOverride = m_Overrides[m_Slot];
            const Keycode&           trigger     = *keyOverride.trigger;
            const std::vector<Words> custom      = CustomKeycodeWordsOf(m_Keyboard);
            const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                              LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };
            if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, trigger, keyOverride.layers))
            {
                BoardTag tag;
                for (size_t index = 0; index < board.keys.size(); ++index)
                {
                    BoardKey& key = board.keys[index];
                    if (key.geometry.decal || m_Keyboard.KeycodeFor(key.geometry, *layer) != trigger)
                        continue;
                    key.marks |= Mark::Highlighted;
                    if (*layer != 0)
                    {
                        KeycapLegend legend = LegendFor(trigger, context);
                        legend.header       = { { "Layer " + std::to_string(*layer), "L" + std::to_string(*layer) },
                                                CommandCategory::Behaviour };
                        key.legends         = legend;
                        key.fallthrough     = Fallthrough::None;
                    }
                    if (tag.keys.empty())
                        tag.keys.push_back(index);
                }
                const bool        any  = (keyOverride.options & KeyOverrideOption::OneMod) != 0;
                const std::string sent = keyOverride.replacement
                                             ? TagText(LegendFor(*keyOverride.replacement, context), NameOf(*keyOverride.replacement))
                                             : std::string("nothing");
                tag.text = (keyOverride.held != 0 ? MaskWords(keyOverride.held, any) + " " : std::string()) +
                           "\xE2\x86\x92 " + sent;
                board.tags.push_back(std::move(tag));
            }
            else if (const std::optional<Keycode> tapHold = TapHoldSending(m_Keyboard, trigger))
            {
                for (BoardKey& key : board.keys)
                    if (!key.geometry.decal && m_Keyboard.KeycodeFor(key.geometry, 0) == *tapHold)
                        key.marks |= Mark::Warning;
            }
        }

        MarkUnlockKeys(board, m_Lock);
    }

    void KeyOverridesSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!m_IsLoaded || m_Slot >= m_Overrides.size() || !events.hoveredKey || IsBusy())
            return;

        // What the key sends on the override's first layer that has something there; else layer 0.
        const KeyOverride&   keyOverride = m_Overrides[m_Slot];
        const DefinitionKey& geometry    = board.keys[*events.hoveredKey].geometry;
        Keycode              sent        = m_Keyboard.KeycodeFor(geometry, 0);
        for (int layer = 0; layer < m_Keyboard.keymap.Layers() && layer < 16; ++layer)
            if ((keyOverride.layers & (1u << layer)) != 0)
            {
                const Keycode there = m_Keyboard.KeycodeFor(geometry, static_cast<uint8_t>(layer));
                if (!IsNothing(there))
                {
                    sent = there;
                    break;
                }
            }
        if (IsNothing(sent))
            return;

        if (keyOverride.trigger == sent)
            ImGui::SetTooltip("%s -- the key of %s", NameOf(sent).c_str(), SlotName(m_Slot).c_str());
        else
            ImGui::SetTooltip("%s -- click to make it the key of %s", NameOf(sent).c_str(), SlotName(m_Slot).c_str());
        if (events.clickedKey)
            SetKey(sent);
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    Task<void> KeyOverridesSection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);

            m_Stored    = co_await ReadKeyOverrides(vial, m_Keyboard.report.keyOverrideCount, m_Keyboard.keycodeVersion);
            m_Overrides = m_Stored;
            m_IsLoaded  = true;
            OnStripChosen(m_Slot);
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the key overrides could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    void KeyOverridesSection::SaveChanges()
    {
        if (IsBusy() || !m_IsLoaded || !HasUnsavedChanges())
            return;

        std::vector<size_t> changed;
        for (size_t slot = 0; slot < m_Overrides.size(); ++slot)
            if (IsChanged(slot))
            {
                if (!EncodeKeyOverride(m_Overrides[slot], m_Keyboard.keycodeVersion))
                {
                    m_Message   = SlotName(slot) + " holds a key this board cannot store";
                    m_IsWarning = true;
                    return;
                }
                changed.push_back(slot);
            }
        m_Request = Save(std::move(changed));
    }

    Task<void> KeyOverridesSection::Save(std::vector<size_t> slots)
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
                const VialKeyOverrideEntry entry = *EncodeKeyOverride(m_Overrides[slot], m_Keyboard.keycodeVersion);
                const uint8_t              index = static_cast<uint8_t>(slot);
                co_await vial.SetKeyOverride(index, entry);
                const VialKeyOverrideEntry stored = co_await vial.GetKeyOverride(index);
                m_Stored[slot]                    = DecodeKeyOverride(stored, m_Keyboard.keycodeVersion);
                if (stored == entry)
                    m_Overrides[slot] = m_Stored[slot];
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

    void KeyOverridesSection::DiscardChanges()
    {
        m_Overrides = m_Stored;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void KeyOverridesSection::DrawPanel()
    {
        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("key overrides", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }
        if (m_Overrides.empty())
        {
            ImGui::TextDisabled("This board has no key overrides.");
            return;
        }
        if (m_Slot >= m_Overrides.size())
            m_Slot = 0;

        DrawSlotLine();
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawRule();
        DrawConditions();
        DrawSays();
        DrawOptions();
        ImGui::Separator();
        DrawTools();
        DrawPicker();
        ImGui::EndDisabled();
    }

    void KeyOverridesSection::DrawSlotLine()
    {
        KeyOverride& keyOverride = m_Overrides[m_Slot];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(SlotName(m_Slot).c_str());
        ImGui::SetItemTooltip("Slot %zu of the board's %zu -- when two match, the first wins", m_Slot, m_Overrides.size());

        ImGui::SameLine();
        const bool on = keyOverride.IsOn();
        if (ToggleButton(on ? "On###onoff" : "Off###onoff", on, !keyOverride.IsEmpty() && !IsBusy()))
            keyOverride.options ^= KeyOverrideOption::Enabled;
        ImGui::SetItemTooltip("An override off stays on the board, unused");

        ImGui::SameLine();
        ImGui::BeginDisabled(keyOverride.IsEmpty() || IsBusy());
        if (ImGui::Button("Clear"))
        {
            keyOverride = KeyOverride{};
            m_Part      = Part::Key;
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

    // The rule, left to right: what is held, + the key, an arrow, what is sent; then the layers.
    void KeyOverridesSection::DrawRule()
    {
        KeyOverride&             keyOverride = m_Overrides[m_Slot];
        const float              scale       = ImGui::GetStyle().FontScaleDpi;
        const ImVec2             tile        = TileSize();
        const float              cardW       = std::max(c_CardWidth * scale, tile.x + 16 * scale);
        const float              joinW       = c_JoinWidth * scale;
        const float              pad         = 6 * scale;
        const float              line        = ImGui::GetTextLineHeight();
        const float              cardH       = pad + line + 4 * scale + tile.y + 4 * scale + line + pad;
        ImDrawList*              list        = ImGui::GetWindowDrawList();
        const ImU32              muted       = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32              text        = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32              warning     = ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Warning));
        const std::vector<Words> custom      = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };

        ImGui::Dummy(ImVec2(0, pad));
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        float        x      = origin.x;

        // One part: its name, its key or words -- an outline when empty -- and a word under it. A
        // click selects it.
        const auto card = [&](Part part, const char* name, float width, const std::function<void(const ImVec2&, const ImVec2&, bool)>& face,
                              const std::string& under, bool underWarns, const std::string& tip)
        {
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
            face(ImVec2(p0.x + (width - tile.x) / 2, y), ImVec2(p0.x + (width + tile.x) / 2, y + tile.y), hovered);
            y += tile.y + 4 * scale;
            if (!under.empty())
                CentredText(list, p0.x + 2 * scale, width - 4 * scale, y, underWarns ? warning : muted, under);

            if (!tip.empty() && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", tip.c_str());
            ImGui::PopID();
            x += width;
        };
        const auto outline = [&](const ImVec2& b0, const ImVec2& b1, const char* words, bool warns)
        {
            list->AddRect(b0, b1, warns ? warning : ImGui::GetColorU32(ImGuiCol_Border), 5 * scale, 0, std::max(1.0f, scale));
            CentredText(list, b0.x, b1.x - b0.x, (b0.y + b1.y - line) / 2, muted, words);
        };
        const auto keyFace = [&](std::optional<Keycode> key, const char* empty)
        {
            return [&, key, empty](const ImVec2& b0, const ImVec2& b1, bool hovered)
            {
                if (!key)
                {
                    outline(b0, b1, empty, false);
                    return;
                }
                KeycodeTile keyTile = TileOf(*key, legends, false);
                keyTile.hovered     = hovered;
                DrawKeycodeTile(keyTile, FaceBox{ b0.x, b0.y, b1.x, b1.y });
            };
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

        // Held: the modifiers in words, on a face as wide as they need.
        const bool        any   = (keyOverride.options & KeyOverrideOption::OneMod) != 0;
        const std::string held  = MaskWords(keyOverride.held, any);
        const float       heldW = std::max(cardW, ImGui::CalcTextSize(held.c_str()).x + 28 * scale);
        std::string       side;
        switch (SideOf(keyOverride.held))
        {
        case Side::Either: side = keyOverride.held != 0 ? "either side" : "none"; break;
        case Side::Left: side = "left only"; break;
        case Side::Right: side = "right only"; break;
        case Side::Mixed: side = ""; break;
        }
        card(Part::Held, "Held", heldW,
             [&](const ImVec2& b0, const ImVec2& b1, bool)
             {
                 if (keyOverride.held == 0)
                 {
                     outline(b0, b1, "-", false);
                     return;
                 }
                 const ImVec2 w0(x + 8 * scale, b0.y), w1(x + heldW - 8 * scale, b1.y);
                 list->AddRectFilled(w0, w1, ImGui::GetColorU32(ImGuiCol_FrameBg), 5 * scale);
                 list->AddRect(w0, w1, ImGui::GetColorU32(ImGuiCol_Border), 5 * scale, 0, std::max(1.0f, scale));
                 CentredText(list, w0.x, w1.x - w0.x, (w0.y + w1.y - line) / 2, text, held);
             },
             side, false, "The modifiers held with the key -- click, then choose them below");
        join(false);

        // The key, and where it is.
        std::string under;
        bool        underWarns = false;
        if (keyOverride.trigger)
        {
            if ((keyOverride.layers & BoardLayers()) == 0)
            {
                under      = "no layer";
                underWarns = true;
            }
            else if (const std::optional<uint8_t> layer = LayerSending(m_Keyboard, *keyOverride.trigger, keyOverride.layers))
                under = *layer == 0 ? "" : "layer " + std::to_string(*layer);
            else
            {
                under      = TapHoldSending(m_Keyboard, *keyOverride.trigger) ? "sent by no key" : "on no key";
                underWarns = true;
            }
        }
        else if (keyOverride.held != 0)
            under = "the modifiers alone";
        card(Part::Key, "Key", cardW, keyFace(keyOverride.trigger, keyOverride.held != 0 ? "-" : "+"), under, underWarns,
             keyOverride.trigger ? KeycodeHoverText(*keyOverride.trigger, m_Keyboard.keycodeVersion)
                                 : "Click a key on the board, or pick one below");
        join(true);

        // What is sent.
        const bool sendable = !keyOverride.replacement || CanSend(*keyOverride.replacement);
        card(Part::Sends, "Sends", cardW, keyFace(keyOverride.replacement, "-"),
             !keyOverride.replacement ? "nothing" : sendable ? "" : "cannot be sent", !sendable,
             keyOverride.replacement ? KeycodeHoverText(*keyOverride.replacement, m_Keyboard.keycodeVersion)
                                     : "What is sent instead -- empty, the combination is blocked");

        // The layers, after a rule: the layer the key is pressed on must be one of them.
        const float layersX = x + c_LayersGap * scale;
        list->AddLine(ImVec2(layersX - c_LayersGap * scale / 2, origin.y + pad),
                      ImVec2(layersX - c_LayersGap * scale / 2, origin.y + cardH - pad), ImGui::GetColorU32(ImGuiCol_Border));
        ImGui::SetCursorScreenPos(ImVec2(layersX, origin.y + pad));
        ImGui::BeginGroup();
        PushHeaderFont();
        ImGui::TextUnformatted("Layers");
        ImGui::PopFont();
        const uint16_t board = BoardLayers();
        const bool     all   = (keyOverride.layers & board) == board;
        if (ToggleButton("All", all))
        {
            Start(keyOverride);
            keyOverride.layers = all ? uint16_t{ 0 } : c_AllKeyOverrideLayers;
        }
        ImGui::SetItemTooltip(all ? "Takes every layer away" : "Every layer, those added later too");
        for (int layer = 0; layer < m_Keyboard.keymap.Layers() && layer < 16; ++layer)
        {
            ImGui::SameLine(0, 2 * scale);
            ImGui::PushID(layer);
            const uint16_t bit = static_cast<uint16_t>(1u << layer);
            if (ToggleButton(std::to_string(layer).c_str(), (keyOverride.layers & bit) != 0))
            {
                Start(keyOverride);
                keyOverride.layers ^= bit;
            }
            ImGui::PopID();
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + c_LayersWrap * scale);
        ImGui::TextDisabled("The layer the key is pressed on.");
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const float bottom = std::max(origin.y + cardH, ImGui::GetItemRectMax().y);

        ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
        ImGui::Dummy(ImVec2(x - origin.x, pad));
    }

    // Under the rule: the modifiers not allowed and those hidden, as chips naming them.
    void KeyOverridesSection::DrawConditions()
    {
        const KeyOverride& keyOverride = m_Overrides[m_Slot];
        const auto chip = [&](const char* words, Part part, uint8_t mask, bool any, const char* tip)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", words);
            ImGui::SameLine();
            const std::string label = (mask != 0 ? MaskWords(mask, any) : std::string("none")) + "###" + words;
            if (ToggleButton(label.c_str(), m_Part == part))
                m_Part = part;
            ImGui::SetItemTooltip("%s", tip);
        };
        chip("Not while held", Part::NotHeld, keyOverride.notHeld, true, "Any of these held stops it -- click, then choose them below");
        ImGui::SameLine(0, 18 * ImGui::GetStyle().FontScaleDpi);
        chip("Hidden from the computer", Part::Hidden, keyOverride.hidden, false,
             "Kept from the computer while it acts: hide what is held, or Shift + Backspace sends Shift + Delete");
    }

    // What the override does, then what is wrong with it -- what Vial never says.
    void KeyOverridesSection::DrawSays()
    {
        KeyOverride&   keyOverride = m_Overrides[m_Slot];
        const uint16_t board       = BoardLayers();
        const bool     any         = (keyOverride.options & KeyOverrideOption::OneMod) != 0;
        const std::string held     = MaskWords(keyOverride.held, any);
        const std::string key      = keyOverride.trigger ? NameOf(*keyOverride.trigger) : std::string();

        ImGui::PushTextWrapPos(0.0f);
        if (keyOverride.IsEmpty())
            ColouredText(PanelColour::Muted, "Empty: click a key on the board or pick one below, choose the modifiers held "
                                             "with it, then what is sent instead.");
        else
        {
            std::string does = !held.empty() && !key.empty() ? "Hold " + held + " and press " + key + ": "
                               : !key.empty()                ? "Press " + key + ": "
                                                             : "Hold " + held + ": ";
            does += keyOverride.replacement ? NameOf(*keyOverride.replacement) + " is sent instead" : "nothing is sent";
            if (keyOverride.hidden != 0)
                does += ", " + MaskWords(keyOverride.hidden) + " hidden from the computer";

            std::vector<std::string> layers;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers() && layer < 16; ++layer)
                if ((keyOverride.layers & (1u << layer)) != 0)
                    layers.push_back(std::to_string(layer));
            does += (keyOverride.layers & board) == board ? ". On every layer"
                    : layers.empty()                       ? ". On no layer"
                                                           : (layers.size() == 1 ? ". On layer " : ". On layers ") + JoinNames(layers);
            if (keyOverride.notHeld != 0)
                does += ", not while " + MaskWords(keyOverride.notHeld, true) + " is held";
            ImGui::TextUnformatted((does + ".").c_str());

            std::string how;
            if (!held.empty() && !key.empty())
            {
                if ((keyOverride.Activations() & KeyOverrideOption::RequiredModDown) != 0)
                    how += "Pressing " + held + " with " + key + " already held switches it too. ";
                how += (keyOverride.options & KeyOverrideOption::NoReregister) != 0
                           ? "Released first, " + held + " leaves " + key + " silent until it is pressed again."
                           : "Released first, " + held + " gives " + key + " back.";
            }
            if ((keyOverride.options & KeyOverrideOption::NoUnregisterOnOtherKey) != 0)
                how += (how.empty() ? "" : " ") + std::string("Other keys pressed meanwhile do not end it.");
            if (!how.empty())
                ColouredText(PanelColour::Muted, "%s", how.c_str());
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

        if (!keyOverride.IsEmpty())
        {
            if (!keyOverride.IsOn())
            {
                note("Off: kept on the board, never used.");
                if (fix("Turn it on"))
                    keyOverride.options |= KeyOverrideOption::Enabled;
            }
            if (!keyOverride.trigger && keyOverride.held == 0)
                warn("No key and no modifier held: it would act on every key press.");
            if ((keyOverride.layers & board) == 0)
            {
                warn("No layer chosen: it never acts.");
                if (fix("Every layer"))
                    keyOverride.layers = c_AllKeyOverrideLayers;
            }
            if (const uint8_t both = KindsOf(keyOverride.held) & KindsOf(keyOverride.notHeld); both != 0)
                warn(MaskWords(static_cast<uint8_t>(both | (both << 4))) + " must be held and must not be: it never acts.");
            if (keyOverride.replacement && !CanSend(*keyOverride.replacement))
                warn(NameOf(*keyOverride.replacement) + " cannot be sent: a key override sends a plain key, with modifiers. "
                     "Nothing would be sent.");

            if (keyOverride.trigger && (keyOverride.layers & board) != 0 &&
                !LayerSending(m_Keyboard, *keyOverride.trigger, keyOverride.layers))
            {
                const Keycode& trigger = *keyOverride.trigger;
                if (const std::optional<uint8_t> other = LayerSending(m_Keyboard, trigger, static_cast<uint16_t>(board & ~keyOverride.layers)))
                {
                    warn(key + " is on layer " + std::to_string(*other) + ", which this override leaves out.");
                    if (fix("Add layer " + std::to_string(*other)))
                        keyOverride.layers |= static_cast<uint16_t>(1u << *other);
                }
                else if (const std::optional<Keycode> tapHold = TapHoldSending(m_Keyboard, trigger))
                {
                    const std::string whole = FormatKeycode(*tapHold);
                    warn("No key sends " + key + ": the key typing it sends " + whole +
                         ", a tap-hold key, and a key override matches the whole keycode.");
                    if (fix("Use " + whole))
                        keyOverride.trigger = *tapHold;
                }
                else
                    warn("No key on the board sends " + key + ": it never acts.");
            }

            const uint8_t shown = static_cast<uint8_t>(keyOverride.held & ~keyOverride.hidden);
            if (shown != 0 && keyOverride.replacement && CanSend(*keyOverride.replacement))
            {
                const std::string words = MaskWords(shown);
                note(words + (CountOf(KindsOf(shown)) > 1 ? " are" : " is") + " not hidden: the computer gets " + words +
                     " with " + NameOf(*keyOverride.replacement) + ".");
                if (fix("Hide " + words))
                    keyOverride.hidden |= keyOverride.held;
            }
            if (!keyOverride.replacement && (keyOverride.trigger || keyOverride.held != 0))
                note("Sends nothing: " + (held.empty() ? key : key.empty() ? held : held + " + " + key) + " is blocked.");

            for (size_t other = 0; other < m_Overrides.size(); ++other)
            {
                if (other == m_Slot || !SameRule(keyOverride, m_Overrides[other], board))
                    continue;
                if (other < m_Slot)
                    warn("Never acts: " + SlotName(other) + " has the same key and modifiers, and the first slot wins.");
                else
                    note(SlotName(other) + " has the same key and modifiers, and never acts: this one comes first.");
            }
        }
        ImGui::PopTextWrapPos();
    }

    // The start and stop options in words, folded under one line saying whether they are QMK's
    // usual -- the two "no_" bits turned round, so every box ticked is the usual way.
    void KeyOverridesSection::DrawOptions()
    {
        KeyOverride&  keyOverride = m_Overrides[m_Slot];
        const uint8_t unusual     = KeyOverrideOption::NoReregister | KeyOverrideOption::NoUnregisterOnOtherKey;
        const bool    usual = keyOverride.Activations() == KeyOverrideOption::Activations && (keyOverride.options & unusual) == 0;

        ImGui::SetNextItemOpen(s_OptionsOpen, ImGuiCond_Always);
        s_OptionsOpen = ImGui::TreeNodeEx("When it starts and stops###options",
                                          ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanLabelWidth);
        ImGui::SameLine();
        ImGui::TextDisabled(usual ? "-- as QMK sets it" : "-- changed from QMK's usual");
        if (!s_OptionsOpen)
            return;

        const bool        any  = (keyOverride.options & KeyOverrideOption::OneMod) != 0;
        const std::string key  = keyOverride.trigger ? NameOf(*keyOverride.trigger) : std::string("the key");
        const std::string held = keyOverride.held != 0 ? MaskWords(keyOverride.held, any) : std::string("the modifiers");
        const uint8_t     acts = keyOverride.Activations();
        const float       at   = ImGui::GetCursorPosX() + ImGui::CalcTextSize("Starts when").x + 14 * ImGui::GetStyle().FontScaleDpi;

        // One box: `on` as shown; ticked or not, `bit` flips. An activation that is the last one ticked
        // stays: none set would read as all three.
        const auto box = [&](const std::string& label, uint8_t bit, bool on, bool enabled, const char* tip)
        {
            ImGui::BeginDisabled(!enabled);
            bool ticked = on;
            if (ImGui::Checkbox(label.c_str(), &ticked))
            {
                Start(keyOverride);
                if ((bit & KeyOverrideOption::Activations) != 0)
                    keyOverride.options |= acts;   // none set is all three: made plain before one goes
                keyOverride.options ^= bit;
            }
            ImGui::EndDisabled();
            if (tip != nullptr)
                ImGui::SetItemTooltip("%s", tip);
        };
        const auto row = [&](const char* words)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", words);
            ImGui::SameLine(at);
        };
        const auto last = [&](uint8_t bit) { return (acts & ~bit & KeyOverrideOption::Activations) == 0; };

        row("Starts when");
        box(key + " is pressed, " + held + " held", KeyOverrideOption::TriggerDown, (acts & KeyOverrideOption::TriggerDown) != 0,
            !last(KeyOverrideOption::TriggerDown), nullptr);
        ImGui::SameLine();
        box(held + (CountOf(KindsOf(keyOverride.held)) > 1 ? " are" : " is") + " pressed, " + key + " held",
            KeyOverrideOption::RequiredModDown, (acts & KeyOverrideOption::RequiredModDown) != 0,
            keyOverride.held != 0 && !last(KeyOverrideOption::RequiredModDown),
            "Within 500 ms of the key's press, what is sent waits until then, as a key repeat would");
        ImGui::SameLine();
        box((keyOverride.notHeld != 0 ? MaskWords(keyOverride.notHeld, true) : std::string("a modifier not allowed")) + " is released",
            KeyOverrideOption::NegativeModUp, (acts & KeyOverrideOption::NegativeModUp) != 0,
            keyOverride.notHeld != 0 && !last(KeyOverrideOption::NegativeModUp), nullptr);

        row("Ends when");
        box("another key is pressed", KeyOverrideOption::NoUnregisterOnOtherKey,
            (keyOverride.options & KeyOverrideOption::NoUnregisterOnOtherKey) == 0, true, nullptr);
        ImGui::SameLine();
        ImGui::TextDisabled("-- or %s or %s released.", key.c_str(), held.c_str());

        row("After it");
        box(key + ", still held, is sent again", KeyOverrideOption::NoReregister,
            (keyOverride.options & KeyOverrideOption::NoReregister) == 0, true, nullptr);
    }

    void KeyOverridesSection::DrawTools()
    {
        KeyOverride& keyOverride = m_Overrides[m_Slot];
        const float  gap         = 14 * ImGui::GetStyle().FontScaleDpi;
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        switch (m_Part)
        {
        case Part::Held: ImGui::TextUnformatted("Held"); break;
        case Part::Key: ImGui::TextUnformatted("Key"); break;
        case Part::Sends: ImGui::TextUnformatted("Sends"); break;
        case Part::NotHeld: ImGui::TextUnformatted("Not while held"); break;
        case Part::Hidden: ImGui::TextUnformatted("Hidden from the computer"); break;
        }
        ImGui::PopFont();
        ImGui::SameLine();

        if (m_Part == Part::Held || m_Part == Part::NotHeld || m_Part == Part::Hidden)
        {
            const uint8_t mask = m_Part == Part::Held ? keyOverride.held : m_Part == Part::NotHeld ? keyOverride.notHeld : keyOverride.hidden;
            const Side    side = SideOf(mask);

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
                    SetMask(m_Part, static_cast<uint8_t>((mask & both) != 0 ? mask & ~both : mask | add));
                }
                ImGui::PopID();
                ImGui::SameLine();
            }

            // The side, for the whole set.
            ImGui::SameLine(0, gap);
            const uint8_t kinds = KindsOf(mask);
            if (ToggleButton("Either side", mask != 0 && side == Side::Either, mask != 0))
                SetMask(m_Part, static_cast<uint8_t>(kinds | (kinds << 4)));
            ImGui::SameLine();
            if (ToggleButton("Left", side == Side::Left, mask != 0))
                SetMask(m_Part, kinds);
            ImGui::SameLine();
            if (ToggleButton("Right", side == Side::Right, mask != 0))
                SetMask(m_Part, static_cast<uint8_t>(kinds << 4));
            if (side == Side::Mixed)
                ImGui::SetItemTooltip("The sides are mixed, as the board holds them: a side chosen applies to them all");

            if (m_Part == Part::Held && CountOf(kinds) > 1)
            {
                ImGui::SameLine(0, gap);
                const bool anyOne = (keyOverride.options & KeyOverrideOption::OneMod) != 0;
                if (ToggleButton("All of them", !anyOne) && anyOne)
                    keyOverride.options &= static_cast<uint8_t>(~KeyOverrideOption::OneMod);
                ImGui::SameLine();
                if (ToggleButton("Any one", anyOne) && !anyOne)
                    keyOverride.options |= KeyOverrideOption::OneMod;
            }
            if (m_Part == Part::Hidden)
            {
                ImGui::SameLine(0, gap);
                ImGui::BeginDisabled(keyOverride.hidden == keyOverride.held);
                if (ImGui::Button("Same as held"))
                    keyOverride.hidden = keyOverride.held;
                ImGui::EndDisabled();
            }
            if (m_Part == Part::Held && keyOverride.held != 0 && keyOverride.hidden == keyOverride.held)
            {
                ImGui::SameLine(0, gap);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("hidden from the computer too");
            }
        }
        else if (m_Part == Part::Key)
        {
            if (keyOverride.trigger)
            {
                if (ImGui::SmallButton("Empty it"))
                    keyOverride.trigger.reset();
                ImGui::SetItemTooltip("With no key, the modifiers alone start it");
                ImGui::SameLine();
                ImGui::TextDisabled("pick a key below to change it, or click one on the board");
            }
            else
                ImGui::TextDisabled("click a key on the board, or pick one below");
        }
        else if (std::optional<Keycode>& sent = keyOverride.replacement)
        {
            // Sent with, as Keymap's key line: Ctrl+Y is one key sent.
            if (DrawSentWith(*sent, m_Legends, m_Keyboard.keycodeVersion))
                ImGui::SameLine();
            if (ImGui::SmallButton("Empty it"))
                sent.reset();
            ImGui::SetItemTooltip("Nothing is sent then: the combination is blocked");
            ImGui::SameLine();
            ImGui::TextDisabled("or pick a key below -- a plain key: layer keys, Boot and macros cannot be sent");
        }
        else
            ImGui::TextDisabled("pick a key below -- empty, the combination is blocked");
    }

    void KeyOverridesSection::DrawPicker()
    {
        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom, m_Keyboard.tapDances };

        // What is sent goes through register_code(): a plain key, with modifiers, and nothing else --
        // Boot among the rest, so a locked board's firewall never matters here.
        const bool sends = m_Part == Part::Sends;
        const std::function<std::optional<std::string>(const Keycode&)> unavailable =
            [this, sends](const Keycode& keycode) -> std::optional<std::string>
        {
            if (sends && !CanSend(keycode))
                return std::string("A key override can only send a plain key, with modifiers");
            return std::nullopt;
        };

        KeyOverride&                  keyOverride = m_Overrides[m_Slot];
        const std::optional<Keycode>& current     = sends ? keyOverride.replacement : keyOverride.trigger;
        const KeycodePickerEvents     events      = DrawKeycodePicker(
            m_Picker, { CatalogueOf(m_Picker, m_Keyboard, m_Legends, m_AdvancedTools), context, m_Keyboard.keycodeVersion, current, &m_Keyboard, unavailable });
        if (!events.picked)
            return;
        if (sends)
        {
            Start(keyOverride);
            keyOverride.replacement = current ? ComposeWithKey(*events.picked, *current, m_Keyboard.keycodeVersion) : *events.picked;
        }
        else
            SetKey(*events.picked);
    }
}
