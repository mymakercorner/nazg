// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// LightingSection - QMK's own lighting on a board (ui-design.md, "The Lighting section", after the
// mockup ui-design/lighting-section.html): backlight, underglow and RGB Matrix, each system in the
// strip when there are several, one at a time. In the panel: On, the effect -- underglow's as an
// effect and its own variant buttons, Speed, Spread, Direction, Colours -- Brightness, Speed where
// it does something, and the colour as hue and saturation sliders where the effect uses them.
//
// The board lights the keys that change the system from the keyboard, on any layer -- the keyboard
// itself being the preview -- and, under the pointer, those that change one line. What they change
// makes the panel stale, so the values are read again each time the section is shown.
//
// Written at once: the keyboard changes while a slider moves -- QMK's RAM-only set -- and the
// value is stored when it is let go, then read back; anything else is written and stored at once.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "model/NazgLighting.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class LightingSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`. All outlive the section -- ui/NazgSection.h.
        LightingSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends);

        [[nodiscard]] std::string_view Name() const override { return "Lighting"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Bulb; }

        [[nodiscard]] Strip DescribeStrip() const override;
        void                OnStripChosen(size_t entry) override;

        void DescribeBoard(BoardDescription& board) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;
        void               WhileHidden() override;

    private:
        struct System
        {
            LightingTarget target;
            LightingValues values;   // as the board holds them, or as being written

            // The effect the light turns on with: the last one it was seen or set to. A board that
            // reads 0 while off cannot say which it had.
            uint16_t lastEffect = 1;

            // On the Legacy wire, whether the light is on cannot be read: what was written, once.
            std::optional<bool> knownOn;

            std::vector<uint16_t> effects;              // VialRGB: those the board lists
            uint8_t               maxBrightness = 255;   // VialRGB's scale
        };

        struct PendingWrite
        {
            size_t         system = 0;
            LightingLine   line   = LightingLine::Any;
            LightingValues values;
            bool           store = false;   // stored and read back, not only shown
        };

        // The panel's parts, for the chosen system.
        void DrawHeader(const System& system);
        void DrawLines(System& system);
        void DrawUnderglowEffect(System& system);
        void DrawMatrixEffect(System& system);
        void DrawColour(System& system, ColourUse use);
        void DrawSays(const System& system);

        // A 0-255 slider, `track` drawn under it: none, the hue circle, white to `hue`. Writes
        // while it moves, stores when let go.
        enum class Track : uint8_t
        {
            Plain,
            Hue,
            Saturation,
        };
        void Slider(const char* id, uint8_t& value, LightingLine line, Track track, uint8_t hue = 0, bool percent = true);

        // Queues `line` of the chosen system for writing: `store`, written, stored and read back.
        void Change(LightingLine line, bool store);

        [[nodiscard]] bool AnyKeyChanges(LightingKind kind) const;

        Task<void> Load();
        Task<void> Write(PendingWrite write);

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;

        std::vector<System> m_Systems;
        size_t              m_Chosen = 0;

        bool        m_IsLoaded = false;
        bool        m_IsStale  = false;   // shown again since read: read again
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        std::optional<PendingWrite> m_Pending;

        // The line under the pointer last frame, for the board, and this frame's.
        std::optional<LightingLine> m_Hovered;
        std::optional<LightingLine> m_HoveredNow;
    };
}
