// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgDeviceSection.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include "imgui.h"

#include "adapters/via/NazgViaProtocol.h"
#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgLineParts.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        float ButtonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label, nullptr, true).x + 2 * ImGui::GetStyle().FramePadding.x;
        }

        void Muted(const char* words)
        {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ColouredText(PanelColour::Muted, "%s", words);
        }
    }

    DeviceSection::DeviceSection(HidTransport& transport, std::string path, const Keyboard& keyboard,
                                 const LegendSettings& legends, const std::optional<vial::UnlockStatus>& lock)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Legends(legends), m_Lock(lock)
    {
    }

    bool DeviceSection::IsBusy() const
    {
        return m_Request.IsValid() && !m_Request.IsDone();
    }

    bool DeviceSection::TakeBoardChanged()
    {
        return std::exchange(m_Changed, false);
    }

    // A VIAL_INSECURE build reports itself unlocked with no keys to unlock with -- and has no jump.
    bool DeviceSection::CanJump() const
    {
        return m_Keyboard.report.isVial && m_Lock && !(m_Lock->unlocked && m_Lock->unlockKeys.empty());
    }

    bool DeviceSection::IsLocked() const
    {
        return CanJump() && !m_Lock->unlocked;
    }

    int DeviceSection::BootLayer() const
    {
        const std::vector<via::DefinitionKey> placed = PlaceKeys(m_Keyboard.definition, m_Keyboard.layoutSelection);
        for (int layer = 0; layer < m_Keyboard.keymap.Layers(); ++layer)
            for (const via::DefinitionKey& key : placed)   // the keys drawn, the layout's choices taken
                if (key.IsSwitch() && IsNamed(m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer)), "QK_BOOT"))
                    return layer;
        return -1;
    }

    DeviceSection::LayerChange DeviceSection::LayerChangeOf(const Keycode& keycode)
    {
        if (const auto* key = std::get_if<LayerKey>(&keycode))
            switch (key->op)
            {
            case LayerOp::Momentary:
            case LayerOp::TapToggle: return { Reach::Hold, key->layer };
            case LayerOp::Toggle:
            case LayerOp::To:
            case LayerOp::OneShot:   return { Reach::Tap, key->layer };
            default:                 return {};   // DF, PDF: the default layer, not the way to a key
            }
        if (const auto* tap = std::get_if<LayerTapKey>(&keycode))
            return { Reach::Hold, tap->layer };
        if (const auto* mod = std::get_if<LayerModKey>(&keycode))
            return { Reach::Hold, mod->layer };
        return {};
    }

    std::vector<DeviceSection::PathStep> DeviceSection::BootPath() const
    {
        // The keys the board draws, with its layout choices -- another choice's key is not there to
        // press (Rico's F77: an MO(1) of a Right Shift choice not taken, found before his L1) --
        // each switch once, by its matrix position.
        const std::vector<via::DefinitionKey>  placed = PlaceKeys(m_Keyboard.definition, m_Keyboard.layoutSelection);
        std::vector<const via::DefinitionKey*> keys;
        for (const via::DefinitionKey& key : placed)
            if (key.IsSwitch() && std::none_of(keys.begin(), keys.end(), [&key](const via::DefinitionKey* other)
                                               { return other->row == key.row && other->column == key.column; }))
                keys.push_back(&key);

        const int layers = std::min(static_cast<int>(m_Keyboard.keymap.Layers()), 32);
        const auto keycodeOn = [&](const via::DefinitionKey& key, uint32_t on)
        {
            // As the firmware reads it: the highest layer on first, a transparent key falling through.
            for (int layer = layers - 1; layer > 0; --layer)
                if ((on >> layer & 1u) != 0)
                    if (Keycode keycode = m_Keyboard.KeycodeFor(key, static_cast<uint8_t>(layer)); !IsNamed(keycode, "KC_TRNS"))
                        return keycode;
            return m_Keyboard.KeycodeFor(key, 0);
        };

        // Breadth first, from layer 0 alone: the shortest way, the keys in the definition's order.
        struct State
        {
            uint32_t              on;
            std::vector<PathStep> path;
        };
        std::vector<State>    frontier{ { 1u, {} } };
        std::vector<uint32_t> seen{ 1u };
        for (int depth = 0; depth <= 4 && !frontier.empty(); ++depth)
        {
            std::vector<State> next;
            for (const State& state : frontier)
                for (const via::DefinitionKey* key : keys)
                {
                    if (std::any_of(state.path.begin(), state.path.end(), [key](const PathStep& step)
                                    { return step.row == key->row && step.column == key->column; }))
                        continue;   // held already
                    const Keycode keycode = keycodeOn(*key, state.on);
                    int           top     = 0;   // the highest layer on
                    for (int layer = layers - 1; layer > 0 && top == 0; --layer)
                        if ((state.on >> layer & 1u) != 0)
                            top = layer;
                    if (IsNamed(keycode, "QK_BOOT"))
                    {
                        std::vector<PathStep> path = state.path;
                        path.push_back({ key->row, key->column, Reach::None, top });
                        return path;
                    }
                    const LayerChange change = LayerChangeOf(keycode);
                    if (change.how == Reach::None || change.layer <= 0 || change.layer >= layers || (state.on >> change.layer & 1u) != 0)
                        continue;
                    const bool     to = std::holds_alternative<LayerKey>(keycode) && std::get<LayerKey>(keycode).op == LayerOp::To;
                    const uint32_t on = (to ? 1u : state.on) | 1u << change.layer;
                    if (std::find(seen.begin(), seen.end(), on) != seen.end())
                        continue;
                    seen.push_back(on);
                    State reached{ on, state.path };
                    reached.path.push_back({ key->row, key->column, change.how, top });
                    next.push_back(std::move(reached));
                }
            frontier = std::move(next);
        }
        return {};
    }

    // ------------------------------------------------------------------------------------------
    // The board: layer 0's legends; where the board cannot jump, the way to QK_BOOT -- the layer
    // keys bringing its layer in, then the key itself -- lit and shown pressed in turn, the way a
    // hand does it (Rico, 2026-11-17; tester-feedback.md, 28: the lit R read as a key stuck). No way
    // found: the keys holding it lit and tagged with their layer.

    void DeviceSection::DescribeBoard(BoardDescription& board)
    {
        DescribeLegends(board, m_Keyboard, 0, m_Legends);

        const int layer = CanJump() ? -1 : BootLayer();
        if (layer < 0)
            return;
        const std::vector<PathStep> path = BootPath();

        const auto stepOf = [&path](const via::DefinitionKey& key) -> std::optional<size_t>
        {
            for (size_t step = 0; step < path.size(); ++step)
                if (path[step].row == key.row && path[step].column == key.column)
                    return step;
            return std::nullopt;
        };
        std::vector<std::optional<size_t>> keyOfStep(path.size());
        std::optional<size_t>              bootKey;
        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            BoardKey& key = board.keys[index];
            bool      lit = false;
            if (key.geometry.IsSwitch() && path.empty())
                lit = IsNamed(m_Keyboard.KeycodeFor(key.geometry, static_cast<uint8_t>(layer)), "QK_BOOT");
            else if (key.geometry.IsSwitch())
                if (const std::optional<size_t> step = stepOf(key.geometry); step && !keyOfStep[*step])
                {
                    keyOfStep[*step] = index;
                    lit              = true;
                }
            if (lit && !bootKey && path.empty())
                bootKey = index;
            key.marks |= lit ? Mark::Highlighted : Mark::Dimmed;
        }

        if (path.empty())
        {
            if (bootKey)
                board.tags.push_back({ { *bootKey }, "QK_BOOT, layer " + std::to_string(layer) });
            return;
        }
        if (path.size() == 1)
        {
            if (keyOfStep[0])
                board.tags.push_back({ { *keyOfStep[0] }, "QK_BOOT" });
            return;
        }

        // A layer key past layer 0 wears its legend on the layer it is pressed on -- L2 on the F77's
        // space bar, blank on layer 0 (Rico, 2026-11-17). The QK_BOOT key keeps layer 0's: its name.
        for (size_t step = 0; step < path.size(); ++step)
            if (path[step].how != Reach::None && path[step].layer > 0 && keyOfStep[step])
            {
                BoardDescription onLayer = board;
                DescribeLegends(onLayer, m_Keyboard, static_cast<uint8_t>(path[step].layer), m_Legends);
                board.keys[*keyOfStep[step]].legends     = onLayer.keys[*keyOfStep[step]].legends;
                board.keys[*keyOfStep[step]].fallthrough = onLayer.keys[*keyOfStep[step]].fallthrough;
            }

        // The keys pressed in turn, 0.6 s apart: a held key stays down until the QK_BOOT key is let
        // go, a tapped one is let go at once. No tags: the presses say it (Rico, 2026-11-17). A key
        // down drops its highlight, whose tint is painted over the pressed one and would hide it
        // (WithStates()). The board is described every frame.
        constexpr double c_Start = 0.5, c_Apart = 0.6, c_Tap = 0.3, c_Boot = 0.8, c_Pause = 0.9;
        const double     end = c_Start + c_Apart * static_cast<double>(path.size() - 1) + c_Boot;
        const double     t   = std::fmod(ImGui::GetTime(), end + c_Pause);
        for (size_t step = 0; step < path.size(); ++step)
        {
            const double down = c_Start + c_Apart * static_cast<double>(step);
            const double up   = path[step].how == Reach::Tap ? down + c_Tap : end;
            if (keyOfStep[step] && t >= down && t < up)
            {
                uint8_t& marks = board.keys[*keyOfStep[step]].marks;
                marks          = static_cast<uint8_t>((marks & ~Mark::Highlighted) | Mark::Pressed);
            }
        }
    }

    // ------------------------------------------------------------------------------------------
    // The panel.

    void DeviceSection::DrawPanel()
    {
        const BoardReport& report = m_Keyboard.report;

        ImGui::TextUnformatted(m_Keyboard.Name().c_str());
        ImGui::SameLine();
        const std::string protocol = report.isVial ? "Vial " + std::to_string(report.vialProtocol)
                                                   : "VIA " + std::to_string(report.viaProtocol);
        ColouredText(PanelColour::Muted, "%s, keycodes %s", protocol.c_str(),
                     qmk::KeycodeVersionName(m_Keyboard.keycodeVersion));
        ImGui::Spacing();

        ImGui::BeginDisabled(IsBusy());
        if (ImGui::BeginTable("lines", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch);

            (void)BeginLine("Firmware");
            DrawBootloaderLine();

            (void)BeginLine("Keymap");
            if (ImGui::Button("Reset keymap..."))
                m_Asked = Action::ResetKeymap;
            Muted("every layer's keys and knobs, as the firmware was built");

            if (report.macroCount > 0)
            {
                (void)BeginLine("Macros");
                if (ImGui::Button("Empty macros..."))
                    m_Asked = Action::EmptyMacros;
                const std::string all = "all " + std::to_string(report.macroCount);
                Muted(all.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();

        if (!m_Message.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ColouredText(PanelColour::Error, "%s", m_Message.c_str());
            ImGui::PopTextWrapPos();
        }

        DrawQuestion();
    }

    void DeviceSection::DrawBootloaderLine()
    {
        if (CanJump())
        {
            ImGui::BeginDisabled(IsLocked());
            if (ImGui::Button("Enter bootloader..."))
                m_Asked = Action::Bootloader;
            ImGui::EndDisabled();
            if (IsLocked())
                Muted("Vial lets only an unlocked board do this: click Locked in the header.");
            return;
        }

        // Says how instead (Rico, 2026-10-18): the key holding QK_BOOT, else the reset button.
        std::string how = m_Keyboard.report.isVial ? "This Vial firmware was built insecure, without the bootloader command: "
                                                   : "VIA's firmware has no bootloader command: ";
        const int                   layer = BootLayer();
        const std::vector<PathStep> path  = layer >= 0 ? BootPath() : std::vector<PathStep>{};
        if (path.size() == 1)
            how += "press the lit key, which holds QK_BOOT.";
        else if (path.size() > 1)
            how += "press the lit keys as the board shows, in turn, keeping down those it keeps down: the last holds "
                   "QK_BOOT on layer " + std::to_string(layer) + ".";
        else if (layer >= 0)
            how += "press the lit key on layer " + std::to_string(layer) +
                   ", which holds QK_BOOT -- no layer key reaches that layer, so only the firmware can, as a tri-layer; "
                   "or press the board's reset button.";
        else
            how += "press the board's reset button, or put QK_BOOT on a key in Keymap.";
        if (m_Keyboard.report.isLeydenJar)
            how += " Or Diagnostics, in Device.";

        ImGui::AlignTextToFramePadding();
        ImGui::PushTextWrapPos(0.0f);
        ColouredText(PanelColour::Muted, "%s", how.c_str());
        ImGui::PopTextWrapPos();
    }

    // In a dialog, saying what goes and what stays (Rico, 2026-10-18: Cancel and the action, no more).
    void DeviceSection::DrawQuestion()
    {
        const std::string name = m_Keyboard.Name();
        std::string       title;
        std::string       says;
        std::string       keep;
        const char*       act = "";
        switch (m_Asked)
        {
        case Action::None:
            return;
        case Action::Bootloader:
            title = "Enter the bootloader of the " + name + "?";
            says  = "The " + name + " restarts in its bootloader, to take a new firmware: it leaves Nazg, and comes back "
                                    "in the list once flashed.";
            act   = "Enter bootloader";
            break;
        case Action::ResetKeymap:
            title = "Reset the keymap of the " + name + "?";
            says  = "Every layer's keys and knobs go back to the keymap its firmware was built with. Macros, lighting and "
                    "layout options stay" + std::string(m_Keyboard.report.isVial ? ", and Vial's features." : ".");
            keep  = "To keep this keymap, Save to file... first: Load from file... brings it back.";
            act   = "Reset keymap";
            break;
        case Action::EmptyMacros:
            title = "Empty the macros of the " + name + "?";
            says  = "All " + std::to_string(m_Keyboard.report.macroCount) +
                   " macros are emptied. Keys that play one stay, and play nothing.";
            keep  = "To keep them, Save to file... first: Load from file... brings them back.";
            act   = "Empty macros";
            break;
        }

        const std::string id = title + "###device";
        if (!ImGui::IsPopupOpen(id.c_str()))
            ImGui::OpenPopup(id.c_str());
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f));
        if (!ImGui::BeginPopupModal(id.c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            m_Asked = Action::None;
            return;
        }

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(says.c_str());
        if (!keep.empty())
        {
            ImGui::Spacing();
            ColouredText(PanelColour::Muted, "%s", keep.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        const float width = ButtonWidth("Cancel") + ButtonWidth(act) + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - width));
        const Action asked = m_Asked;
        if (ImGui::Button("Cancel"))
        {
            m_Asked = Action::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const bool loses = asked != Action::Bootloader;
        if (loses)
            ImGui::PushStyleColor(ImGuiCol_Button, ColourOf(PanelColour::Error));
        if (ImGui::Button(act))
        {
            m_Asked = Action::None;
            ImGui::CloseCurrentPopup();
            if (!IsBusy())
                m_Request = Run(asked);
        }
        if (loses)
            ImGui::PopStyleColor();
        ImGui::EndPopup();
    }

    Task<void> DeviceSection::Run(Action action)
    {
        m_Message.clear();
        try
        {
            HidDeviceChannel channel = co_await HidDeviceChannel::Open(m_Transport, m_Path);
            via::Protocol    protocol(channel);
            switch (action)
            {
            case Action::Bootloader:
                co_await protocol.JumpToBootloader();
                m_Left = "The " + m_Keyboard.Name() + " left for its bootloader. Flash it, and it comes back in the list.";
                break;
            case Action::ResetKeymap:
                co_await protocol.ResetKeymap();
                m_Changed = true;
                break;
            case Action::EmptyMacros:
                co_await protocol.ResetMacros();
                m_Changed = true;
                break;
            case Action::None:
                break;
            }
        }
        catch (const std::exception& failure)
        {
            m_Message = std::string(action == Action::Bootloader ? "The bootloader could not be entered: "
                                    : action == Action::ResetKeymap ? "The keymap could not be reset: "
                                                                    : "The macros could not be emptied: ") +
                        failure.what();
        }
    }
}
