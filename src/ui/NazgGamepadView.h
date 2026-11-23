// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// GamepadView - a game controller's controls, flat on two rows, and its live test (ui-design.md, "Round
// two: advanced keys", after the mockup ui-design/switches-advanced-keys.html): each control a tile, its
// badge -- the same as on the board's keys (Rico, 2026-11-23) -- and the key that plays it under it, the
// badge outlined where no key does. A tile fills as its control goes while the test reads it.
//
// It only draws and says what was clicked: the Switches section writes it. ImGui only, no SDL.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "adapters/keychron/NazgKeychronAdvanced.h"

namespace nazg
{
    struct GamepadViewInput
    {
        std::array<std::string, keychron::c_GamepadControlCount> keys;   // what plays each control
        std::optional<uint8_t>                                   selected;
        const keychron::GamepadLevels*                           levels = nullptr;   // while testing

        // Times the mockup's size: as large as the room allows (GamepadViewScale()).
        float scale = 1.0f;
    };

    // The scale that fills `width` by `height` pixels, never under what reads well (Rico, 2026-11-23: at
    // the mockup's size the controls were very small) nor over what looks in proportion.
    [[nodiscard]] float GamepadViewScale(float width, float height);

    struct GamepadViewEvents
    {
        std::optional<uint8_t> clicked;
        std::optional<uint8_t> hovered;
    };

    [[nodiscard]] GamepadViewEvents DrawGamepadView(const GamepadViewInput& input);

    // Where the two sticks have just been, for the test to draw: gone in half a second, leaving nothing
    // behind (Rico, 2026-11-23: lines that stayed were not wanted).
    struct GamepadTrail
    {
        struct Point
        {
            ImVec2 at;
            double time = 0.0;   // ImGui::GetTime()
        };

        std::vector<Point> left;
        std::vector<Point> right;

        void Add(const keychron::GamepadLevels& levels);
        void Clear();
    };

    struct GamepadTestInput
    {
        bool                           isSeen = false;
        bool                           isXbox = false;
        size_t                         others = 0;
        std::string                    whyNotSeen;         // "... plug it in again.", when it is not
        bool                           sharesTriggers = false;   // a plain joystick's, both played by keys
        const keychron::GamepadLevels* levels = nullptr;
        const GamepadTrail*            trail  = nullptr;
    };

    void                DrawGamepadTest(const GamepadTestInput& input);
    [[nodiscard]] float GamepadTestWidth();   // what it needs, in pixels

    // A control's badge at (x, y), its top left -- or its top middle, `isCentred`; outlined where no key
    // plays it. And as an item in a line of text. The badge's size.
    ImVec2 DrawGamepadBadge(ImDrawList* list, ImVec2 at, const std::string& label, bool isFilled, bool isCentred = false,
                            float scale = 1.0f);
    void   GamepadBadgeItem(const std::string& label, bool isFilled = true);
}
