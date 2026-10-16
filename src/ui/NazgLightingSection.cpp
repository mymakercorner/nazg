// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgLightingSection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <utility>
#include <variant>

#include "imgui.h"
#include "imgui_internal.h"   // ImGuiItemFlags_MixedValue, for an On the firmware cannot read

#include "adapters/via/NazgViaLighting.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgKeyLine.h"
#include "ui/NazgLineParts.h"
#include "ui/NazgSlotParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        constexpr std::array<const char*, 4> c_Speeds{ "Slow", "Medium", "Fast", "Fastest" };
        constexpr std::array<const char*, 5> c_Spreads{ "Full", "2/3", "1/2", "1/3", "1/4" };
        constexpr std::array<const char*, 2> c_Directions{ "Forward", "Backward" };
        constexpr std::array<const char*, 2> c_Colours{ "One", "Random" };

        // A named key's name, or empty.
        std::string_view NameOf(const Keycode& keycode)
        {
            const NamedKey* named = std::get_if<NamedKey>(&keycode);
            return named ? named->name : std::string_view();
        }

        // A muted (?) whose tooltip says `words`.
        void HelpMark(const char* words)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip())
            {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(words);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        // A row of buttons, one lit: the one clicked, if any.
        std::optional<uint8_t> Buttons(const char* label, const char* const* names, size_t count, uint8_t chosen)
        {
            std::optional<uint8_t> clicked;
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s", label);
            ImGui::PushID(label);
            for (size_t index = 0; index < count; ++index)
            {
                ImGui::SameLine(0.0f, index == 0 ? -1.0f : 1.0f);
                if (ToggleButton(names[index], index == chosen))
                    clicked = static_cast<uint8_t>(index);
            }
            ImGui::PopID();
            return clicked;
        }

        std::string EffectWords(LightingTarget target, uint16_t effect)
        {
            if (target.kind == LightingKind::Underglow)
            {
                if (const UnderglowEffect* known = UnderglowEffectOf(effect))
                    return std::string(known->name);
            }
            else if (const RgbMatrixEffect* known = RgbMatrixEffectOf(target.wire, effect))
                return std::string(known->name);
            return "effect " + std::to_string(effect);
        }

        constexpr const char* c_NumberingNote =
            "QMK numbers the effects among those built into the firmware: these names are right when every effect is "
            "built in, as on most boards. A board built with fewer numbers them otherwise -- it cannot say, and what "
            "it answers is what is shown.";
    }

    LightingSection::LightingSection(HidTransport& transport, std::string path, Keyboard& keyboard,
                                     const LegendSettings& legends)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends)
    {
        for (const LightingTarget& target : LightingTargetsOf(keyboard))
        {
            System system;
            system.target = target;
            if (target.kind == LightingKind::RgbMatrix && target.wire == LightingWire::VialRgb)
                system.lastEffect = 2;   // Solid Color; VialRGB's 1 is the direct mode
            m_Systems.push_back(std::move(system));
        }
    }

    bool LightingSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    void LightingSection::WhileHidden()
    {
        // What the lighting keys changed meanwhile: read again when shown.
        if (m_IsLoaded)
            m_IsStale = true;
        m_Hovered.reset();
    }

    bool LightingSection::AnyKeyChanges(LightingKind kind) const
    {
        const bool underglow = (LightingSystemsOf(m_Keyboard) & LightingSystem::Underglow) != 0;
        for (const via::DefinitionKey& key : m_Keyboard.definition.keys)
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
                if (ChangesLighting(NameOf(m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer))), kind, LightingLine::Any,
                                    underglow))
                    return true;
        return false;
    }

    // ------------------------------------------------------------------------------------------
    // The strip and the board.

    Strip LightingSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "System";
        if (m_Systems.size() < 2)
            return strip;
        for (const System& system : m_Systems)
            strip.entries.emplace_back(NameOf(system.target.kind));
        strip.chosen = m_Chosen;
        return strip;
    }

    void LightingSection::OnStripChosen(size_t entry)
    {
        if (entry < m_Systems.size())
            m_Chosen = entry;
    }

    void LightingSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);
        if (m_Systems.empty())
            return;

        // The keys that change the chosen system -- or, under the pointer, one line of it -- on the
        // first layer that has them, that layer's legend and "L1" when it is not the base.
        const LightingKind       kind      = m_Systems[m_Chosen].target.kind;
        const LightingLine       line      = m_Hovered.value_or(LightingLine::Any);
        const uint8_t            lighting  = LightingSystemsOf(m_Keyboard);
        const bool               underglow = (lighting & LightingSystem::Underglow) != 0;
        const std::vector<Words> custom    = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither, lighting, custom,
                                          m_Keyboard.tapDances };
        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                if (!ChangesLighting(NameOf(keycode), kind, line, underglow))
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
                break;
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void LightingSection::DrawPanel()
    {
        m_HoveredNow.reset();

        if (m_Systems.empty())
        {
            ImGui::TextDisabled("Nazg cannot speak to this board's lighting yet: vial-qmk's RGB Matrix without VialRGB.");
            return;
        }

        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (DrawReading("lighting", IsBusy(), m_Message))
            {
                m_Message.clear();
                m_Request = Load();
            }
            return;
        }

        if (m_IsStale && !IsBusy() && !m_Pending)
        {
            m_IsStale = false;
            m_Request = Load();
        }

        System& system = m_Systems[std::min(m_Chosen, m_Systems.size() - 1)];
        DrawHeader(system);
        ImGui::Separator();
        DrawLines(system);
        DrawSays(system);

        m_Hovered = m_HoveredNow;

        // One write at a time; a slider moving faster than the board answers sends its latest value.
        if (m_Pending && !IsBusy())
        {
            m_Request = Write(*m_Pending);
            m_Pending.reset();
        }
    }

    void LightingSection::DrawHeader(const System& system)
    {
        ImGui::AlignTextToFramePadding();
        PushHeaderFont();
        ImGui::TextUnformatted(NameOf(system.target.kind).data());
        ImGui::PopFont();
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "%s", WordsOf(system.target.kind).data());
        ImGui::SameLine();
        if (IsBusy() || m_Pending)
            ColouredText(PanelColour::Muted, "Writing...");
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        else
            ColouredText(PanelColour::Muted, "The keyboard shows each change at once; it is stored when let go.");
    }

    void LightingSection::DrawLines(System& system)
    {
        if (!ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
            return;
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch);

        LightingValues& values = system.values;
        const bool      off    = values.on.has_value() && !*values.on;
        const auto      hover  = [this](float top, LightingLine line)
        {
            if (IsOverLine(top))
                m_HoveredNow = line;
        };

        if (system.target.kind == LightingKind::Backlight)
        {
            float top = BeginLine("Brightness");
            Slider("##brightness", values.brightness, LightingLine::Brightness, Track::Plain);
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "0: off");
            HelpMark("The backlight has a few steps, set when the firmware is built: the slider settles on the nearest "
                     "one, as the board answers.");
            hover(top, LightingLine::Brightness);

            top          = BeginLine("Breathing");
            bool breathe = values.effect != 0;
            if (ImGui::Checkbox("##breathing", &breathe))
            {
                values.effect = breathe ? 1 : 0;
                Change(LightingLine::Effect, true);
            }
            hover(top, LightingLine::Effect);
            ImGui::EndTable();
            return;
        }

        // On: the firmware's own state, or -- on the Legacy wire, which cannot read it -- what was
        // written, else neither.
        float top = BeginLine("On");
        {
            bool on = values.on.value_or(false);
            ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, !values.on.has_value());
            if (ImGui::Checkbox("##on", &on))
            {
                values.on = on;
                if (on)
                    values.effect = system.lastEffect;
                if (system.target.wire == LightingWire::Legacy)
                    system.knownOn = on;
                Change(LightingLine::On, true);
            }
            ImGui::PopItemFlag();
            if (!values.on.has_value())
                ImGui::SetItemTooltip("This firmware cannot say whether the light is on: tick or untick it to set it.");
        }
        hover(top, LightingLine::On);

        ImGui::BeginDisabled(off);
        top = BeginLine("Effect");
        if (system.target.kind == LightingKind::Underglow)
            DrawUnderglowEffect(system);
        else
            DrawMatrixEffect(system);
        hover(top, LightingLine::Effect);

        if (system.target.kind == LightingKind::RgbMatrix)
        {
            top = BeginLine("Speed");
            Slider("##speed", values.speed, LightingLine::Speed, Track::Plain);
            hover(top, LightingLine::Speed);
        }

        top = BeginLine("Brightness");
        Slider("##brightness", values.brightness, LightingLine::Brightness, Track::Plain);
        hover(top, LightingLine::Brightness);

        const uint16_t  effect = off || values.effect == 0 ? system.lastEffect : values.effect;
        const ColourUse use    = system.target.kind == LightingKind::Underglow ? UnderglowColourUse(effect) : [&]
        {
            const RgbMatrixEffect* known = RgbMatrixEffectOf(system.target.wire, effect);
            return known ? known->colour : ColourUse::HueAndSaturation;
        }();
        if (use != ColourUse::None)
        {
            top = BeginLine("Colour");
            DrawColour(system, use);
            hover(top, LightingLine::Colour);
        }
        ImGui::EndDisabled();
        ImGui::EndTable();
    }

    void LightingSection::DrawUnderglowEffect(System& system)
    {
        LightingValues&        values  = system.values;
        const uint16_t         mode    = values.effect != 0 ? values.effect : system.lastEffect;
        const UnderglowEffect* effect  = UnderglowEffectOf(mode);
        const UnderglowVariant variant = UnderglowVariantOf(mode);

        const auto choose = [&](uint16_t chosen)
        {
            values.effect     = chosen;
            system.lastEffect = chosen;
            if (system.target.wire == LightingWire::Legacy)
                system.knownOn = true;   // an effect written turns the light on
            if (values.on.has_value())
                values.on = true;
            Change(LightingLine::Effect, true);
        };

        float longest = 0.0f;
        for (const UnderglowEffect& each : UnderglowEffects())
            longest = std::max(longest, ImGui::CalcTextSize(each.name.data()).x);
        ImGui::SetNextItemWidth(longest + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##effect", EffectWords(system.target, mode).c_str()))
        {
            for (const UnderglowEffect& each : UnderglowEffects())
                if (ImGui::Selectable(each.name.data(), &each == effect) && &each != effect)
                {
                    // Keep the speed where the new effect has it.
                    UnderglowVariant kept;
                    if (each.speeds != 0)
                        kept.speed = std::min<uint8_t>(variant.speed, static_cast<uint8_t>(each.speeds - 1));
                    choose(UnderglowMode(each, kept));
                }
            ImGui::EndCombo();
        }

        if (effect)
        {
            UnderglowVariant changed = variant;
            bool             clicked = false;
            if (effect->speeds != 0)
                if (const auto speed = Buttons("Speed", c_Speeds.data(), effect->speeds, variant.speed))
                {
                    changed.speed = *speed;
                    clicked       = true;
                }
            if (effect->spreads != 0)
                if (const auto spread = Buttons("Spread", c_Spreads.data(), effect->spreads, variant.spread))
                {
                    changed.spread = *spread;
                    clicked        = true;
                }
            if (effect->directions)
                if (const auto direction = Buttons("Direction", c_Directions.data(), c_Directions.size(), variant.direction))
                {
                    changed.direction = *direction;
                    clicked           = true;
                }
            if (effect->randomColours)
                if (const auto random = Buttons("Colours", c_Colours.data(), c_Colours.size(), variant.random ? 1 : 0))
                {
                    changed.random = *random != 0;
                    clicked        = true;
                }
            if (clicked && !(changed == variant))
                choose(UnderglowMode(*effect, changed));
        }
        HelpMark(c_NumberingNote);
    }

    void LightingSection::DrawMatrixEffect(System& system)
    {
        LightingValues& values = system.values;
        const uint16_t  shown  = values.effect != 0 ? values.effect : system.lastEffect;
        const bool      vial   = system.target.wire == LightingWire::VialRgb;

        // VialRGB: the effects the board lists, in VialRGB's order -- one Nazg has no name for by
        // its number. Otherwise QMK's, as VIA names them.
        std::vector<std::pair<uint16_t, std::string>> effects;
        if (vial)
            for (uint16_t id : system.effects)
                effects.emplace_back(id, EffectWords(system.target, id));
        else
            for (const RgbMatrixEffect& each : RgbMatrixEffects(system.target.wire))
                effects.emplace_back(each.value, std::string(each.name));

        float longest = 0.0f;
        for (const auto& [id, name] : effects)
            longest = std::max(longest, ImGui::CalcTextSize(name.c_str()).x);
        ImGui::SetNextItemWidth(longest + 2 * ImGui::GetStyle().FramePadding.x + ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##effect", EffectWords(system.target, shown).c_str(), ImGuiComboFlags_HeightLarge))
        {
            for (const auto& [id, name] : effects)
            {
                ImGui::PushID(id);
                if (ImGui::Selectable(name.c_str(), id == shown) && id != shown)
                {
                    values.effect     = id;
                    system.lastEffect = id;
                    if (values.on.has_value())
                        values.on = true;
                    Change(LightingLine::Effect, true);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (vial)
        {
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%zu effects on this board", effects.size());
        }
        else
            HelpMark(c_NumberingNote);
    }

    void LightingSection::DrawColour(System& system, ColourUse use)
    {
        LightingValues& values = system.values;
        ImGui::BeginGroup();
        if (use == ColourUse::HueAndSaturation)
        {
            Slider("##hue", values.hue, LightingLine::Colour, Track::Hue, 0, false);
            ImGui::SameLine();
            ColouredText(PanelColour::Muted, "Hue");
        }
        Slider("##saturation", values.saturation, LightingLine::Colour, Track::Saturation,
               use == ColourUse::HueAndSaturation ? values.hue : 0, false);
        ImGui::SameLine();
        ColouredText(PanelColour::Muted, "Saturation");
        ImGui::EndGroup();

        if (use == ColourUse::HueAndSaturation)
        {
            ImGui::SameLine();
            float r = 0, g = 0, b = 0;
            ImGui::ColorConvertHSVtoRGB(values.hue / 255.0f, values.saturation / 255.0f, 1.0f, r, g, b);
            ImGui::ColorButton("##colour", ImVec4(r, g, b, 1.0f), ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                               ImVec2(ImGui::GetFrameHeight() * 1.5f, ImGui::GetFrameHeight()));
        }
    }

    void LightingSection::Slider(const char* id, uint8_t& value, LightingLine line, Track track, uint8_t hue, bool percent)
    {
        const float width = ImGui::GetFontSize() * 16.0f;

        int  number = value;
        bool moved  = false;
        bool letGo  = false;
        if (track == Track::Plain)
        {
            ImGui::SetNextItemWidth(width);
            moved = ImGui::SliderInt(id, &number, 0, 255, "", ImGuiSliderFlags_AlwaysClamp);
            letGo = ImGui::IsItemDeactivatedAfterEdit();
        }
        else
            moved = ColourSlider(id, number, width, track == Track::Hue, hue, letGo);

        if (moved)
        {
            value = static_cast<uint8_t>(number);
            Change(line, false);
        }
        if (letGo)
            Change(line, true);

        if (percent)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%d %%", (value * 100 + 127) / 255);
        }
    }

    void LightingSection::DrawSays(const System& system)
    {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        const std::string what = system.target.kind == LightingKind::RgbMatrix ? "RGB Matrix"
                                                                                : std::string(system.target.kind == LightingKind::Underglow ? "underglow" : "backlight");
        if (AnyKeyChanges(system.target.kind))
            ColouredText(PanelColour::Muted, "The keys lit change the %s from the keyboard too -- a line under the pointer, "
                                             "the keys that change it. Nazg reads it again each time you come back here.",
                         what.c_str());
        else
            ColouredText(PanelColour::Muted, "No key on this keymap changes the %s -- Keymap's picker has them, in its "
                                             "Lighting tab.", what.c_str());
        ImGui::PopTextWrapPos();
    }

    void LightingSection::Change(LightingLine line, bool store)
    {
        m_Pending = PendingWrite{ m_Chosen, line, m_Systems[m_Chosen].values, store };
    }

    // ------------------------------------------------------------------------------------------
    // Reading and writing.

    Task<void> LightingSection::Load()
    {
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            via::Protocol via(channel);

            for (System& system : m_Systems)
            {
                if (system.target.wire == LightingWire::VialRgb && system.effects.empty())
                {
                    system.maxBrightness = (co_await via::ReadVialRgbInfo(via)).maxBrightness;
                    system.effects       = co_await via::ReadVialRgbEffects(via);
                    if (!system.effects.empty() &&
                        std::find(system.effects.begin(), system.effects.end(), system.lastEffect) == system.effects.end())
                        system.lastEffect = system.effects.front();
                }
                LightingValues read = co_await via::ReadLighting(via, system.target, system.maxBrightness);
                if (!read.on.has_value())
                    read.on = system.knownOn;
                if (read.effect != 0 && read.on.value_or(true))
                    system.lastEffect = read.effect;
                system.values = read;
            }
            m_IsLoaded = true;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the lighting could not be read: ") + failure.what();
            m_IsWarning = true;
        }
    }

    Task<void> LightingSection::Write(PendingWrite write)
    {
        if (write.store)
        {
            m_Message.clear();
            m_IsWarning = false;
        }
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            via::Protocol via(channel);

            System& system = m_Systems[write.system];
            co_await via::WriteLighting(via, system.target, write.line, write.values, system.maxBrightness);
            if (write.store)
            {
                co_await via::SaveLighting(via, system.target);
                LightingValues read = co_await via::ReadLighting(via, system.target, system.maxBrightness);
                if (!read.on.has_value())
                    read.on = system.knownOn;

                // What the board kept, said where it is not what was written.
                const bool lit = write.values.on.value_or(true);
                std::string kept;
                if ((write.line == LightingLine::Effect || write.line == LightingLine::On) && lit &&
                    read.effect != write.values.effect && system.target.kind != LightingKind::Backlight)
                    kept = "the board answered " + EffectWords(system.target, read.effect) + " for " +
                           EffectWords(system.target, write.values.effect) + ": it numbers its effects otherwise, built "
                           "with fewer";
                else if (write.line == LightingLine::Effect && system.target.kind == LightingKind::Backlight &&
                         read.effect != write.values.effect)
                    kept = "the backlight does not breathe: its firmware was built without it";
                else if (write.line == LightingLine::Brightness &&
                         std::abs(read.brightness - write.values.brightness) > 2)   // VialRGB's scale rounds
                {
                    const std::string percent = std::to_string((read.brightness * 100 + 127) / 255) + " %";
                    if (system.target.kind == LightingKind::Backlight)
                        m_Message = "stored -- the backlight settled on its nearest step, " + percent;
                    else
                        kept = "the board kept " + percent;
                }
                else if (write.line == LightingLine::Speed && read.speed != write.values.speed)
                    kept = "the board kept another speed";
                else if (write.line == LightingLine::Colour &&
                         (read.hue != write.values.hue || read.saturation != write.values.saturation))
                    kept = "the board kept another colour";

                if (read.effect != 0 && read.on.value_or(true))
                    system.lastEffect = read.effect;
                // A newer change waiting keeps what the user sees; it is read back after it.
                if (!m_Pending)
                    system.values = read;

                if (!kept.empty())
                {
                    m_Message   = kept;
                    m_IsWarning = true;
                }
                else if (m_Message.empty())   // the backlight's step may have said it
                    m_Message = "stored, read back the same";
            }
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("write failed: ") + failure.what();
            m_IsWarning = true;
        }
    }
}
