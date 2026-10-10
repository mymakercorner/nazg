// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgMacrosSection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <set>
#include <utility>
#include <variant>

#include "imgui.h"
#include "imgui_internal.h"   // MarkIniSettingsDirty(): the host layout chosen here is a setting

#include "adapters/qmk/NazgQmkKeycodeCodec.h"
#include "adapters/qmk/NazgQmkKeycodes.h"
#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgHostTyping.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgKeycodeCatalogue.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // Pixels, before DPI scaling.
        constexpr float c_PlusWidth  = 22.0f;    // a place between two steps
        constexpr float c_RowGap     = 26.0f;    // between rows: room for a press's mark and the link
        constexpr float c_LinkOut    = 10.0f;    // how far the link between rows goes past the steps
        constexpr float c_WaitWidth  = 112.0f;
        constexpr float c_TextMin    = 84.0f;
        constexpr float c_ChipPad    = 8.0f;
        constexpr float c_MarkSize   = 6.0f;     // a press's or a release's triangle
        constexpr float c_GaugeWidth = 120.0f;

        bool IsKeyStep(const MacroStep& step)
        {
            return step.kind == MacroStep::Kind::Key || step.kind == MacroStep::Kind::Press ||
                   step.kind == MacroStep::Kind::Release;
        }

        // A key for people: QMK's label when it has one -- "Left Shift" -- else its expression.
        std::string KeyName(const Keycode& keycode, QmkKeycodeVersion version)
        {
            if (const auto* named = std::get_if<NamedKey>(&keycode))
                if (const QmkKeycode* row = FindQmkKeycodeByName(named->name, version); row && row->label[0] != '\0')
                    return row->label;
            return FormatKeycode(keycode);
        }

        // The words of a modifier on the host: Ctrl, Shift, Alt or Option, Win or Cmd or Super.
        const char* ModifierWord(uint8_t mod, ModifierNames names)
        {
            switch (mod)
            {
            case Mod::LeftCtrl:  return "Ctrl";
            case Mod::LeftShift: return "Shift";
            case Mod::LeftAlt:   return names == ModifierNames::Mac ? "Option" : "Alt";
            default:             return names == ModifierNames::Mac ? "Cmd" : names == ModifierNames::Linux ? "Super" : "Win";
            }
        }

        // How each character of a text is typed, for its tooltip: "é: AltGr+E", "ê: ^ (dead, Shift+6)
        // then E" -- the characters a plain or shifted key types are left out.
        std::string HowTyped(const std::string& text, const HostLayout& layout, ModifierNames names)
        {
            const char* altgr = names == ModifierNames::Mac ? "Option+" : "AltGr+";
            const auto strokeName = [&](const HostStroke& stroke)
            {
                std::string name = (stroke.level & c_ShiftLevel) ? "Shift+" : "";
                if (stroke.level & c_AltGrLevel)
                    name = altgr + name;
                // The key by its keycap: its legend on this layout, else as US prints it.
                if (stroke.key == "KC_SPC")
                    return name + "Space";
                if (const HostLegend* legend = layout.Find(stroke.key); legend && !legend->plain.empty())
                    return name + std::string(legend->plain);
                if (stroke.key.size() == 4 && stroke.key.substr(0, 3) == "KC_")
                    return name + std::string(stroke.key.substr(3));
                return name + std::string(stroke.key);
            };

            std::string           out;
            std::set<char32_t>    seen;
            for (char32_t c : FromUtf8(text))
            {
                if (!seen.insert(c).second)
                    continue;
                const std::optional<std::vector<HostStroke>> strokes = StrokesOf(c, layout);
                std::string line = ToUtf8(std::u32string(1, c)) + ": ";
                if (!strokes)
                    line += "cannot be typed with this layout";
                else if (strokes->size() == 1 && (*strokes)[0].level <= c_ShiftLevel)
                    continue;
                else
                    for (size_t index = 0; index < strokes->size(); ++index)
                    {
                        const HostStroke& stroke = (*strokes)[index];
                        const std::optional<Typed> typed = TypedBy(stroke, layout);
                        if (index > 0)
                            line += " then ";
                        if (typed && typed->dead)
                            line += ToUtf8(std::u32string(1, typed->character)) + " (dead, " + strokeName(stroke) + ")";
                        else
                            line += strokeName(stroke);
                    }
                out += "\n" + line;
            }
            return out;
        }

        // The host layouts in a combo; true when one was chosen.
        bool HostLayoutCombo(const char* id, LegendSettings& legends)
        {
            const HostLayout& current = legends.Layout();
            bool chosen = false;
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("US International (Linux)").x + ImGui::GetFrameHeight() * 2);
            if (ImGui::BeginCombo(id, std::string(current.name).c_str()))
            {
                for (const HostLayout& layout : HostLayouts())
                    if (ImGui::Selectable(std::string(layout.name).c_str(), layout.id == current.id))
                    {
                        legends.hostLayout = std::string(layout.id);
                        chosen             = true;
                    }
                ImGui::EndCombo();
            }
            return chosen;
        }
    }

    MacrosSection::MacrosSection(HidTransport& transport, std::string path, Keyboard& keyboard, LegendSettings& legends,
                                 bool& hostLayoutChosen, const std::optional<VialUnlockStatus>& lock,
                                 const bool& advancedTools)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends),
          m_HostLayoutChosen(hostLayoutChosen), m_Lock(lock), m_AdvancedTools(advancedTools),
          m_Format(MacroFormatFor(keyboard.report))
    {
    }

    MacroContext MacrosSection::Context() const
    {
        return { m_Format, m_Keyboard.keycodeVersion, m_Legends.Layout() };
    }

    bool MacrosSection::IsLocked() const
    {
        return m_Lock && !m_Lock->unlocked;
    }

    bool MacrosSection::IsChanged(size_t macro) const
    {
        return m_IsLoaded && macro < m_Macros.size() && macro < m_Saved.size() && m_Macros[macro] != m_Saved[macro];
    }

    bool MacrosSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool MacrosSection::HasUnsavedChanges() const
    {
        for (size_t macro = 0; macro < m_Macros.size(); ++macro)
            if (IsChanged(macro))
                return true;
        return false;
    }

    std::string MacrosSection::UnsavedSummary() const
    {
        std::vector<std::string> changed;
        for (size_t macro = 0; macro < m_Macros.size(); ++macro)
            if (IsChanged(macro))
                changed.push_back("M" + std::to_string(macro));
        std::string summary;
        for (size_t index = 0; index < changed.size(); ++index)
            summary += (index == 0 ? "" : index + 1 == changed.size() ? " and " : ", ") + changed[index];
        return summary;
    }

    Strip MacrosSection::DescribeStrip() const
    {
        Strip strip;
        strip.label = "Macro";
        for (size_t macro = 0; macro < m_Keyboard.report.macroCount; ++macro)
            strip.entries.push_back("M" + std::to_string(macro) + (IsChanged(macro) ? " •" : ""));
        strip.chosen = m_Macro;
        return strip;
    }

    void MacrosSection::OnStripChosen(size_t entry)
    {
        m_Macro = entry;
        Select(std::nullopt);
        m_Caret.reset();
    }

    void MacrosSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        // The keys that play a macro, on any layer -- the shown one's lit, "Layer 1" over one that
        // plays it on another layer.
        const LegendContext context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither, 0, {} };
        for (BoardKey& key : board.keys)
        {
            for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            {
                const Keycode keycode = m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer));
                const auto*   macro   = std::get_if<MacroKey>(&keycode);
                if (!macro)
                    continue;
                if (layer != 0)
                {
                    KeycapLegend legend = LegendFor(keycode, context);
                    legend.header       = { { "Layer " + std::to_string(layer), "L" + std::to_string(layer) }, CommandCategory::Host };
                    key.legends         = legend;
                    key.fallthrough     = Fallthrough::None;
                }
                if (macro->index == m_Macro)
                    key.marks |= Mark::Highlighted;
                break;
            }

            // Locked: the keys to hold to unlock.
            if (IsLocked())
                for (const auto& [row, column] : m_Lock->combo)
                    if (key.geometry.row == row && key.geometry.column == column)
                        key.marks |= Mark::Warning;
        }
    }

    void MacrosSection::OnBoardEvents(const BoardDescription& board, const BoardEvents& events)
    {
        if (!events.hoveredKey)
            return;
        const DefinitionKey& key = board.keys[*events.hoveredKey].geometry;
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
        {
            const Keycode keycode = m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer));
            if (const auto* macro = std::get_if<MacroKey>(&keycode))
            {
                ImGui::SetTooltip("Plays M%d, on layer %d", macro->index, layer);
                if (events.clickedKey && macro->index < m_Keyboard.report.macroCount)
                    OnStripChosen(macro->index);
                return;
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The board.

    Task<void> MacrosSection::Load()
    {
        m_Message.clear();
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            m_BufferSize = co_await via.GetMacroBufferSize();
            m_Stored     = co_await via.ReadMacros(m_Keyboard.report.macroCount, m_BufferSize);
            m_StoredActions = DecodeMacros(m_Stored, m_Keyboard.report.macroCount, m_Format);
            m_Saved.assign(m_StoredActions.size(), {});
            m_Macros.assign(m_StoredActions.size(), {});
            m_StepsFor.clear();
            ReadSteps();
            m_IsLoaded = true;
        }
        catch (const std::exception& failure)
        {
            m_Message   = std::string("the macros could not be read: ") + failure.what();
            m_IsWarning = true;
        }
        m_Transport.Close(device);
    }

    void MacrosSection::ReadSteps()
    {
        const MacroContext context = Context();
        for (size_t macro = 0; macro < m_StoredActions.size(); ++macro)
        {
            Macro steps = StepsOf(m_StoredActions[macro], context);
            // A macro not edited follows: it is what the board holds, read with this layout.
            if (m_Macros[macro] == m_Saved[macro])
                m_Macros[macro] = steps;
            m_Saved[macro] = std::move(steps);
        }
        m_StepsFor = m_Legends.hostLayout;
    }

    void MacrosSection::SaveChanges()
    {
        if (IsBusy() || !m_IsLoaded || IsLocked() || !HasUnsavedChanges())
            return;

        const MacroContext        context = Context();
        std::vector<MacroActions> all;
        for (size_t macro = 0; macro < m_Macros.size(); ++macro)
        {
            MacroWriting writing = ActionsOf(m_Macros[macro], context);
            if (!writing.problems.empty())
            {
                m_Message   = "M" + std::to_string(macro) + " cannot be written: " + writing.problems[0].what;
                m_IsWarning = true;
                return;
            }
            all.push_back(std::move(writing.actions));
        }

        std::string                error;
        const std::vector<uint8_t> bytes = EncodeMacros(all, m_Format, &error);
        if (!error.empty() || bytes.size() > m_BufferSize)
        {
            m_Message   = !error.empty() ? error
                                         : "the macros need " + std::to_string(bytes.size()) + " bytes, the board has " +
                                               std::to_string(m_BufferSize);
            m_IsWarning = true;
            return;
        }
        m_Request = Save(bytes);
    }

    Task<void> MacrosSection::Save(std::vector<uint8_t> bytes)
    {
        m_Message.clear();
        m_IsWarning = false;
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);
            HidDeviceChannel channel(m_Transport, device);
            ViaProtocol      via(channel);

            co_await via.WriteMacros(m_Stored, bytes, m_BufferSize);
            std::vector<uint8_t> stored = co_await via.ReadMacros(m_Keyboard.report.macroCount, m_BufferSize);

            // What the board holds now, whatever was asked.
            const bool kept = stored == bytes;
            m_Stored        = std::move(stored);
            m_StoredActions = DecodeMacros(m_Stored, m_Keyboard.report.macroCount, m_Format);
            if (kept)
            {
                m_Saved = m_Macros;   // so ReadSteps() refreshes every one
                ReadSteps();
                m_Message = "stored every macro, read back the same";
            }
            else
            {
                // Edits stay, still to be written.
                const MacroContext context = Context();
                for (size_t macro = 0; macro < m_StoredActions.size(); ++macro)
                    m_Saved[macro] = StepsOf(m_StoredActions[macro], context);
                m_Message   = IsLocked() || m_Lock ? "the board did not keep the macros -- Vial refuses them while it is locked"
                                                   : "the board did not keep the macros";
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

    void MacrosSection::DiscardChanges()
    {
        m_Macros = m_Saved;
        Select(std::nullopt);
        m_Caret.reset();
    }

    // ------------------------------------------------------------------------------------------
    // Editing.

    void MacrosSection::Select(std::optional<size_t> step)
    {
        m_Selected = step;
        m_EditFor.reset();
        if (step)
        {
            m_Caret.reset();
            const MacroStep& selected = m_Macros[m_Macro][*step];
            if (selected.kind == MacroStep::Kind::Text)
            {
                std::snprintf(m_Edit, sizeof(m_Edit), "%s", selected.text.c_str());
                m_EditFor   = step;
                m_FocusEdit = true;
            }
            else if (selected.kind == MacroStep::Kind::Wait)
            {
                m_EditFor   = step;
                m_FocusEdit = true;
            }
        }
    }

    void MacrosSection::Insert(std::vector<MacroStep> steps, size_t caretAfter)
    {
        Macro& macro = m_Macros[m_Macro];
        const size_t at = m_Selected ? *m_Selected + 1 : m_Caret ? std::min(*m_Caret, macro.size()) : macro.size();
        macro.insert(macro.begin() + static_cast<std::ptrdiff_t>(at), steps.begin(), steps.end());
        Select(std::nullopt);
        m_Caret = at + caretAfter;
    }

    void MacrosSection::Remove(size_t step)
    {
        Macro& macro = m_Macros[m_Macro];
        const std::vector<size_t> partner = PairPressesAndReleases(macro);
        std::vector<size_t> gone = { step };
        if (partner[step] != step)
            gone.push_back(partner[step]);
        std::sort(gone.rbegin(), gone.rend());
        for (size_t index : gone)
            macro.erase(macro.begin() + static_cast<std::ptrdiff_t>(index));
        Select(std::nullopt);

        // The + stays where the step was -- but an empty macro has only one place, and no + then.
        if (macro.empty())
            m_Caret.reset();
        else
            m_Caret = gone.back();
    }

    void MacrosSection::Move(size_t step, int by)
    {
        Macro& macro = m_Macros[m_Macro];
        const long long to = static_cast<long long>(step) + by;
        if (to < 0 || to >= static_cast<long long>(macro.size()))
            return;
        std::swap(macro[step], macro[static_cast<size_t>(to)]);
        Select(static_cast<size_t>(to));
    }

    void MacrosSection::Picked(const Keycode& keycode)
    {
        Macro& macro = m_Macros[m_Macro];
        if (m_Selected && IsKeyStep(macro[*m_Selected]))
        {
            MacroStep& step = macro[*m_Selected];
            if (step.kind == MacroStep::Kind::Key)
            {
                step.key = ComposeWithKey(keycode, step.key, m_Keyboard.keycodeVersion);
            }
            else
            {
                const std::vector<size_t> partner = PairPressesAndReleases(macro);
                macro[partner[*m_Selected]].key = keycode;
                step.key                        = keycode;
            }
            return;
        }
        Insert({ { MacroStep::Kind::Key, {}, keycode } }, 1);
    }

    const std::vector<CatalogueTab>& MacrosSection::Catalogue()
    {
        const std::string settings = m_Legends.hostLayout + "/" + std::string(IdOf(m_Legends.modifierNames)) +
                                     (m_AdvancedTools ? "/advanced" : "");
        if (m_Catalogue.empty() || settings != m_CatalogueFor)
        {
            m_Catalogue    = BuildKeycodeCatalogue(m_Keyboard, m_Legends, m_AdvancedTools);
            m_CatalogueFor = settings;
        }
        return m_Catalogue;
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void MacrosSection::DrawPanel()
    {
        if (!m_IsLoaded)
        {
            if (!m_Request.IsValid() && m_Message.empty())
                m_Request = Load();
            if (IsBusy())
                ImGui::TextDisabled("Reading the macros...");
            else if (!m_Message.empty())
            {
                ColouredText(PanelColour::Error, "%s", m_Message.c_str());
                if (ImGui::Button("Try again"))
                {
                    m_Message.clear();
                    m_Request = Load();
                }
            }
            return;
        }
        if (m_StepsFor != m_Legends.hostLayout)
            ReadSteps();
        if (m_Macro >= m_Macros.size())
            m_Macro = 0;
        if (m_Macros.empty())
        {
            ImGui::TextDisabled("This board has no macros.");
            return;
        }
        if (m_Selected && *m_Selected >= m_Macros[m_Macro].size())
            Select(std::nullopt);

        DrawMacroLine();
        DrawHostQuestion();
        ImGui::Separator();
        ImGui::BeginDisabled(IsBusy());
        DrawChain();
        DrawProblems();
        ImGui::Separator();
        DrawTools();
        DrawPicker();
        ImGui::EndDisabled();
    }

    void MacrosSection::DrawMacroLine()
    {
        const MacroContext context = Context();
        size_t total = 0, mine = 0;
        for (size_t macro = 0; macro < m_Macros.size(); ++macro)
        {
            const size_t bytes = BytesOf(ActionsOf(m_Macros[macro], context).actions, m_Format);
            total += bytes;
            if (macro == m_Macro)
                mine = bytes;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("M%zu", m_Macro);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(m_Macros[m_Macro].empty() ? "Empty" : "As VIA writes it: %s",
                              ViaScriptOf(m_Macros[m_Macro]).c_str());

        // The gauge: the board's memory, shared by every macro -- the others' part, then this one's.
        const float  scale = ImGui::GetStyle().FontScaleDpi;
        ImGui::SameLine();
        const ImVec2 at   = ImGui::GetCursorScreenPos();
        const float  w    = c_GaugeWidth * scale;
        const float  h    = 6.0f * scale;
        const float  y    = at.y + (ImGui::GetFrameHeight() - h) / 2;
        ImDrawList*  list = ImGui::GetWindowDrawList();
        const float  size = std::max<float>(1.0f, m_BufferSize);
        const float  othersEnd = at.x + w * std::min(1.0f, (total - mine) / size);
        const float  mineEnd   = at.x + w * std::min(1.0f, total / size);
        list->AddRectFilled(ImVec2(at.x, y), ImVec2(at.x + w, y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), h / 2);
        list->AddRectFilled(ImVec2(at.x, y), ImVec2(othersEnd, y + h), ImGui::GetColorU32(ImGuiCol_TextDisabled), h / 2);
        list->AddRectFilled(ImVec2(othersEnd, y), ImVec2(mineEnd, y + h), ImGui::GetColorU32(ImGuiCol_PlotHistogram), h / 2);
        ImGui::Dummy(ImVec2(w, ImGui::GetFrameHeight()));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("All macros share the board's %u bytes", static_cast<unsigned>(m_BufferSize));

        ImGui::SameLine();
        if (total > m_BufferSize)
            ColouredText(PanelColour::Error, "%zu bytes -- %zu too many for the board's %u", mine, total - m_BufferSize,
                         static_cast<unsigned>(m_BufferSize));
        else
            ColouredText(PanelColour::Muted, "%zu byte%s · %zu of %u free", mine, mine == 1 ? "" : "s",
                         m_BufferSize - total, static_cast<unsigned>(m_BufferSize));

        ImGui::SameLine();
        const bool changed = HasUnsavedChanges();
        if (IsLocked())
            ColouredText(PanelColour::Warning, "Locked: macros can be read, not written");
        else if (IsBusy())
            ColouredText(PanelColour::Muted, "Writing...");
        else if (changed)
            ColouredText(PanelColour::Warning, "Not written yet -- %s changed", UnsavedSummary().c_str());
        else if (!m_Message.empty())
            ColouredText(m_IsWarning ? PanelColour::Warning : PanelColour::Success, "%s", m_Message.c_str());
        else
            ColouredText(PanelColour::Muted, "Changes are written by Save.");

        // Save and Revert, at the right.
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttons = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Revert").x +
                              4 * style.FramePadding.x + style.ItemSpacing.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
        ImGui::BeginDisabled(!changed || IsBusy() || IsLocked() || total > m_BufferSize);
        if (ImGui::Button("Save"))
            SaveChanges();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!changed || IsBusy());
        if (ImGui::Button("Revert"))
            DiscardChanges();
        ImGui::EndDisabled();
    }

    void MacrosSection::DrawHostQuestion()
    {
        const Macro& macro = m_Macros[m_Macro];
        if (m_HostLayoutChosen ||
            std::none_of(macro.begin(), macro.end(), [](const MacroStep& step) { return step.kind == MacroStep::Kind::Text; }))
            return;

        ImGui::AlignTextToFramePadding();
        ColouredText(PanelColour::Warning, "Text is typed the way your computer types it. Which layout does it use?");
        ImGui::SameLine();
        if (HostLayoutCombo("##hostquestion", m_Legends))
        {
            m_HostLayoutChosen = true;
            ImGui::MarkIniSettingsDirty();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Yes, this one"))
        {
            m_HostLayoutChosen = true;
            ImGui::MarkIniSettingsDirty();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("The same setting as Keymap's legends, in Settings too.");
    }

    void MacrosSection::DrawChain()
    {
        Macro&              macro   = m_Macros[m_Macro];
        const float         scale   = ImGui::GetStyle().FontScaleDpi;
        const ImVec2        tile    = TileSize();
        const float         plusW   = c_PlusWidth * scale;
        const float         rowGap  = c_RowGap * scale;
        const float         out     = c_LinkOut * scale;
        const float         pad     = c_ChipPad * scale;
        const float         avail   = std::max(tile.x * 2, ImGui::GetContentRegionAvail().x - 2 * out);
        const std::vector<size_t> partner = PairPressesAndReleases(macro);
        const MacroWriting  writing = ActionsOf(macro, Context());

        std::set<size_t> problemSteps;
        for (const MacroProblem& problem : writing.problems)
            problemSteps.insert(problem.step);

        const float labelW = ImGui::CalcTextSize("Text").x * 0.85f;

        // One item per place and per step: a place before each step, and one after the last.
        struct Item
        {
            bool   isPlace = true;
            size_t index   = 0;
            float  x = 0, y = 0, w = 0;
        };
        std::vector<Item> items;
        const auto widthOf = [&](size_t step)
        {
            const MacroStep& s = macro[step];
            if (IsKeyStep(s))
                return tile.x;
            if (s.kind == MacroStep::Kind::Wait)
                return c_WaitWidth * scale;
            const char* text = m_EditFor == step ? m_Edit : s.text.c_str();
            return std::max(c_TextMin * scale, ImGui::CalcTextSize(text).x + labelW + 3 * pad + (m_EditFor == step ? pad * 2 : 0));
        };
        for (size_t step = 0; step <= macro.size(); ++step)
        {
            items.push_back({ true, step, 0, 0, plusW });
            if (step < macro.size())
                items.push_back({ false, step, 0, 0, std::min(widthOf(step), avail - plusW) });
        }

        // Laid out in rows, wrapping where the panel ends.
        float x = out, y = 0;
        std::vector<std::pair<size_t, size_t>> rows;   // first and last item of each
        size_t rowStart = 0;
        for (size_t index = 0; index < items.size(); ++index)
        {
            Item& item = items[index];
            if (x + item.w > out + avail && index > rowStart)
            {
                rows.push_back({ rowStart, index - 1 });
                rowStart = index;
                x        = out;
                y += tile.y + rowGap;
            }
            item.x = x;
            item.y = y;
            x += item.w;
        }
        rows.push_back({ rowStart, items.size() - 1 });

        const ImVec2 origin(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + rowGap / 2);
        ImDrawList*  list   = ImGui::GetWindowDrawList();
        const ImU32  line   = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32  accent = ImGui::GetColorU32(ImGuiCol_ButtonActive);
        const ImU32  warn   = ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Warning));
        const ImU32  error  = ImGui::ColorConvertFloat4ToU32(ColourOf(PanelColour::Error));
        const float  thick  = std::max(1.0f, 1.5f * scale);
        const auto   at     = [&](const Item& item) { return ImVec2(origin.x + item.x, origin.y + item.y); };

        // The line through the places, then the links between rows: down from a row's last step,
        // back left through the gap, down into the next row's first.
        for (size_t index = 0; index < items.size(); ++index)
        {
            const Item& item = items[index];
            if (!item.isPlace || index == 0 || index + 1 == items.size())
                continue;
            const ImVec2 p = at(item);
            list->AddLine(ImVec2(p.x, p.y + tile.y / 2), ImVec2(p.x + item.w, p.y + tile.y / 2), line, thick);
        }
        for (size_t row = 0; row + 1 < rows.size(); ++row)
        {
            const Item&  a  = items[rows[row].second];
            const Item&  b  = items[rows[row + 1].first];
            const ImVec2 pa = at(a), pb = at(b);
            const float  ya = pa.y + tile.y / 2, yb = pb.y + tile.y / 2, yg = pa.y + tile.y + rowGap / 2;
            const float  xr = pa.x + a.w + out, xl = pb.x - out;
            const ImVec2 path[] = { ImVec2(pa.x + a.w, ya), ImVec2(xr, ya), ImVec2(xr, yg), ImVec2(xl, yg),
                                    ImVec2(xl, yb), ImVec2(pb.x, yb) };
            list->AddPolyline(path, 6, line, ImDrawFlags_None, thick);
        }

        const LegendContext legends{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                     LightingSystemsOf(m_Keyboard), {} };
        std::optional<size_t> hoveredPartner;
        for (const Item& item : items)
            if (!item.isPlace && ImGui::IsMouseHoveringRect(at(item), ImVec2(at(item).x + item.w, at(item).y + tile.y)) &&
                partner[item.index] != item.index)
                hoveredPartner = partner[item.index];

        for (const Item& item : items)
        {
            const ImVec2 p0 = at(item);
            const ImVec2 p1(p0.x + item.w, p0.y + tile.y);
            ImGui::PushID(static_cast<int>(item.index * 2 + (item.isPlace ? 0 : 1)));

            if (item.isPlace)
            {
                ImGui::SetCursorScreenPos(p0);
                if (ImGui::InvisibleButton("##place", ImVec2(item.w, tile.y)))
                {
                    Select(std::nullopt);
                    m_Caret = item.index;
                }
                const bool   isCaret = !m_Selected && m_Caret == item.index && !macro.empty();
                const ImVec2 centre((p0.x + p1.x) / 2, (p0.y + p1.y) / 2);
                if (isCaret)
                {
                    list->AddLine(ImVec2(centre.x, p0.y + 2 * scale), ImVec2(centre.x, p1.y - 2 * scale), accent, 2 * thick);
                    list->AddCircleFilled(centre, 7 * scale, accent);
                    list->AddText(ImVec2(centre.x - ImGui::CalcTextSize("+").x / 2, centre.y - ImGui::GetFontSize() / 2),
                                  ImGui::GetColorU32(ImGuiCol_Text), "+");
                }
                else if (ImGui::IsItemHovered())
                {
                    list->AddCircleFilled(centre, 7 * scale, ImGui::GetColorU32(ImGuiCol_FrameBg));
                    list->AddCircle(centre, 7 * scale, line, 0, thick);
                    list->AddText(ImVec2(centre.x - ImGui::CalcTextSize("+").x / 2, centre.y - ImGui::GetFontSize() / 2),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), "+");
                    ImGui::SetTooltip("Add a step here");
                }
                ImGui::PopID();
                continue;
            }

            MacroStep& step     = macro[item.index];
            const bool selected = m_Selected == item.index;
            const bool editing  = selected && m_EditFor == item.index;

            if (!editing)
            {
                ImGui::SetCursorScreenPos(p0);
                if (ImGui::InvisibleButton("##step", ImVec2(item.w, tile.y)))
                    Select(item.index);
            }
            const bool hovered = !editing && ImGui::IsItemHovered();

            if (IsKeyStep(step))
            {
                KeycodeTile keyTile = TileOf(step.key, legends, selected);
                keyTile.hovered     = hovered || hoveredPartner == item.index;
                DrawKeycodeTile(keyTile, { p0.x, p0.y, p1.x, p1.y });

                // A press's mark under its key, a release's over it -- dashed in the warning's colour
                // when alone.
                const bool  lone = step.kind != MacroStep::Kind::Key && partner[item.index] == item.index;
                const ImU32 mark = lone ? warn : accent;
                const float cx = (p0.x + p1.x) / 2, m = c_MarkSize * scale;
                if (step.kind == MacroStep::Kind::Press)
                    list->AddTriangleFilled(ImVec2(cx - m, p1.y + 3 * scale), ImVec2(cx + m, p1.y + 3 * scale),
                                            ImVec2(cx, p1.y + 3 * scale + m), mark);
                else if (step.kind == MacroStep::Kind::Release)
                    list->AddTriangleFilled(ImVec2(cx - m, p0.y - 3 * scale), ImVec2(cx + m, p0.y - 3 * scale),
                                            ImVec2(cx, p0.y - 3 * scale - m), mark);
                if (lone)
                    list->AddRect(p0, p1, warn, 5 * scale, 0, thick);

                if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    const char* what = step.kind == MacroStep::Kind::Key     ? "Taps"
                                     : step.kind == MacroStep::Kind::Press ? "Presses, and keeps down,"
                                                                            : "Releases";
                    const std::string name = std::holds_alternative<ModifiedKey>(step.key)
                                                 ? FormatKeycode(step.key)
                                                 : KeyName(step.key, m_Keyboard.keycodeVersion);
                    ImGui::SetTooltip("%s %s%s", what, name.c_str(),
                                      lone ? (step.kind == MacroStep::Kind::Press ? "\nnever released: it stays held after the macro"
                                                                                  : "\nnever pressed by this macro")
                                           : "");
                }
            }
            else
            {
                // Text and waits: a frame, a caption, the value -- typed in place once selected.
                list->AddRectFilled(p0, p1, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 5 * scale);
                list->AddRect(p0, p1, line, 5 * scale, 0, thick);
                const float  midY  = (p0.y + p1.y) / 2;
                const char*  label = step.kind == MacroStep::Kind::Text ? "Text" : "Wait";
                ImGui::SetWindowFontScale(0.85f);
                const float captionY = midY - ImGui::GetFontSize() / 2;
                list->AddText(ImVec2(p0.x + pad, captionY), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
                ImGui::SetWindowFontScale(1.0f);
                const float valueX = p0.x + pad + labelW + pad;

                if (editing)
                {
                    ImGui::SetCursorScreenPos(ImVec2(valueX, midY - ImGui::GetFrameHeight() / 2));
                    ImGui::SetNextItemWidth(p1.x - valueX - pad);
                    if (m_FocusEdit)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_FocusEdit = false;
                    }
                    if (step.kind == MacroStep::Kind::Text)
                    {
                        if (ImGui::InputText("##text", m_Edit, sizeof(m_Edit)))
                            step.text = m_Edit;
                    }
                    else
                    {
                        uint32_t milliseconds = step.milliseconds;
                        if (ImGui::InputScalar("##wait", ImGuiDataType_U32, &milliseconds))
                            step.milliseconds = std::min<uint32_t>(milliseconds, 65024);
                        ImGui::SameLine(0, 4 * scale);
                        ImGui::TextUnformatted("ms");
                    }
                }
                else
                {
                    const std::string value = step.kind == MacroStep::Kind::Text ? step.text
                                                                                 : std::to_string(step.milliseconds) + " ms";
                    list->PushClipRect(p0, p1, true);
                    list->AddText(ImVec2(valueX, midY - ImGui::GetFontSize() / 2), ImGui::GetColorU32(ImGuiCol_Text), value.c_str());
                    list->PopClipRect();
                }

                if (hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                {
                    if (step.kind == MacroStep::Kind::Text)
                        ImGui::SetTooltip("Types this text with the %s layout%s", std::string(m_Legends.Layout().name).c_str(),
                                          HowTyped(step.text, m_Legends.Layout(), m_Legends.modifierNames).c_str());
                    else
                        ImGui::SetTooltip("Waits; the keyboard does nothing else meanwhile");
                }
                if (selected)
                    list->AddRect(ImVec2(p0.x - 2 * scale, p0.y - 2 * scale), ImVec2(p1.x + 2 * scale, p1.y + 2 * scale), accent,
                                  6 * scale, 0, 2 * thick);
            }

            if (problemSteps.count(item.index))
                list->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p1.x + 1, p1.y + 1), error, 5 * scale, 0, thick);
            ImGui::PopID();
        }

        // Claim the room drawn into.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
        ImGui::Dummy(ImVec2(avail, items.back().y + tile.y + rowGap / 2));
        if (macro.empty())
        {
            ImGui::SameLine();
            ImGui::SetCursorScreenPos(ImVec2(origin.x + out + plusW + pad, origin.y + (tile.y - ImGui::GetFontSize()) / 2));
            ImGui::TextDisabled("Empty: pick a key below, or add text or a wait.");
        }
    }

    void MacrosSection::DrawProblems()
    {
        const Macro&       macro   = m_Macros[m_Macro];
        const MacroWriting writing = ActionsOf(macro, Context());
        std::set<std::string> said;
        for (const MacroProblem& problem : writing.problems)
            if (said.insert(problem.what).second)
                ColouredText(PanelColour::Error, "%s", problem.what.c_str());

        const std::vector<size_t> partner = PairPressesAndReleases(macro);
        for (size_t step = 0; step < macro.size(); ++step)
        {
            if (partner[step] != step || !IsKeyStep(macro[step]) || macro[step].kind == MacroStep::Kind::Key)
                continue;
            const std::string name = KeyName(macro[step].key, m_Keyboard.keycodeVersion);
            const std::string what = macro[step].kind == MacroStep::Kind::Press
                                         ? name + " is pressed and never released: it stays held after the macro ends"
                                         : name + " is released but this macro never pressed it: it releases your own " + name +
                                               ", if you hold it";
            if (said.insert(what).second)
                ColouredText(PanelColour::Warning, "%s", what.c_str());
        }
    }

    void MacrosSection::DrawTools()
    {
        if (IsLocked())
        {
            ColouredText(PanelColour::Warning, "This board is locked. Vial writes no macro until it is unlocked: click "
                                               "Locked in the header, then hold the outlined keys.");
            return;
        }

        Macro& macro = m_Macros[m_Macro];
        ImGui::AlignTextToFramePadding();
        if (m_Selected)
        {
            MacroStep&                step    = macro[*m_Selected];
            const std::vector<size_t> partner = PairPressesAndReleases(macro);
            const bool                paired  = partner[*m_Selected] != *m_Selected;

            static constexpr const char* c_Kinds[] = { "Text", "Key", "Press", "Release", "Wait" };
            ImGui::TextUnformatted(c_Kinds[static_cast<int>(step.kind)]);
            ImGui::SameLine();

            if (step.kind == MacroStep::Kind::Key)
            {
                // Sent with: a basic key, or one already sent with modifiers -- as Keymap's key line.
                const auto* named    = std::get_if<NamedKey>(&step.key);
                const auto* modified = std::get_if<ModifiedKey>(&step.key);
                const std::optional<uint16_t> value =
                    named ? std::optional<uint16_t>(EncodeQmkKeycode(step.key, m_Keyboard.keycodeVersion)) : std::nullopt;
                if (modified || (named && value && *value >= 0x04 && *value <= 0xFF))
                {
                    ImGui::TextDisabled("Sent with");
                    for (uint8_t mod : { Mod::LeftCtrl, Mod::LeftShift, Mod::LeftAlt, Mod::LeftGui })
                    {
                        const uint8_t mods = modified ? modified->mods : 0;
                        const bool    on   = (mods & mod) != 0;
                        ImGui::SameLine();
                        if (on)
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        if (ImGui::SmallButton(ModifierWord(mod, m_Legends.modifierNames)))
                        {
                            const uint8_t          now = static_cast<uint8_t>(mods ^ mod);
                            const std::string_view key = modified ? modified->key : named->name;
                            step.key = now ? Keycode{ ModifiedKey{ now, key } } : Keycode{ NamedKey{ key } };
                        }
                        if (on)
                            ImGui::PopStyleColor();
                    }
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("pick a key below to change it");
            }
            else if (step.kind == MacroStep::Kind::Press || step.kind == MacroStep::Kind::Release)
            {
                if (paired)
                    ImGui::TextDisabled("pick a key below to change it, and its %s",
                                        step.kind == MacroStep::Kind::Press ? "release" : "press");
                else
                    ImGui::TextDisabled("pick a key below to change it");
            }
            else if (step.kind == MacroStep::Kind::Wait)
            {
                ImGui::TextDisabled("in milliseconds, up to %u; the keyboard does nothing else meanwhile",
                                    LongestWait(m_Format));
            }
            else
            {
                ImGui::TextDisabled("Typed for");
                ImGui::SameLine();
                if (HostLayoutCombo("##typedfor", m_Legends))
                {
                    m_HostLayoutChosen = true;
                    ImGui::MarkIniSettingsDirty();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("right on computers with this layout");
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("<"))
                Move(*m_Selected, -1);
            ImGui::SetItemTooltip("Move before");
            ImGui::SameLine();
            if (ImGui::SmallButton(">"))
                Move(*m_Selected, 1);
            ImGui::SetItemTooltip("Move after");
            ImGui::SameLine();
            if (ImGui::SmallButton(paired ? "Remove both" : "Remove"))
            {
                Remove(*m_Selected);
                return;
            }
            if (paired)
                ImGui::SetItemTooltip("Removes the press and its release");
            ImGui::SameLine();
        }

        ImGui::TextDisabled(m_Selected ? "Add after it:" : "Add here:");
        ImGui::SameLine();
        if (ImGui::SmallButton("Text"))
        {
            Insert({ { MacroStep::Kind::Text, "" } }, 1);
            Select(*m_Caret - 1);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!HasWaits(m_Format));
        if (ImGui::SmallButton("Wait"))
        {
            Insert({ { MacroStep::Kind::Wait, {}, NamedKey{ "KC_NO" }, 100 } }, 1);
            Select(*m_Caret - 1);
        }
        ImGui::EndDisabled();
        if (!HasWaits(m_Format))
            ImGui::SetItemTooltip("This board's firmware has no waits");
        ImGui::SameLine();
        if (ImGui::SmallButton("Held key"))
            Insert({ { MacroStep::Kind::Press, {}, NamedKey{ "KC_LSFT" } }, { MacroStep::Kind::Release, {}, NamedKey{ "KC_LSFT" } } }, 1);
        ImGui::SetItemTooltip("A press and a release, the + between them: pick the key to hold, and what happens meanwhile");
        ImGui::SameLine();
        ImGui::TextDisabled("or a key below");
    }

    void MacrosSection::DrawPicker()
    {
        if (IsLocked())
            return;

        const Macro&           macro = m_Macros[m_Macro];
        std::optional<Keycode> current;
        if (m_Selected && IsKeyStep(macro[*m_Selected]))
            current = macro[*m_Selected].key;

        const std::vector<Words> custom = CustomKeycodeWordsOf(m_Keyboard);
        const LegendContext      context{ m_Legends.Layout(), m_Legends.modifierNames, KeySide::Neither,
                                          LightingSystemsOf(m_Keyboard), custom };

        // Where keys are 8-bit, only basic keys -- or one sent with modifiers, written as a chord.
        std::function<std::optional<std::string>(const Keycode&)> unavailable;
        if (!HoldsAnyKeycode(m_Format))
            unavailable = [version = m_Keyboard.keycodeVersion](const Keycode& keycode) -> std::optional<std::string>
            {
                if (const auto* modified = std::get_if<ModifiedKey>(&keycode))
                    if (const QmkKeycode* row = FindQmkKeycodeByName(modified->key, version); row && row->value <= 0xFF)
                        return std::nullopt;
                const std::optional<uint16_t> value = EncodeQmkKeycode(keycode, version);
                if (value && *value <= 0xFF)
                    return std::nullopt;
                return std::string("This board's macros hold basic keys only");
            };

        const KeycodePickerEvents events = DrawKeycodePicker(
            m_Picker, { Catalogue(), context, m_Keyboard.keycodeVersion, current, &m_Keyboard, unavailable });
        if (events.picked)
            Picked(*events.picked);
    }
}
