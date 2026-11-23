// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// DksEditor - a key's One Key Multiple Commands (keychron::DksSlot), drawn and edited
// (ui-design.md, "Round two: advanced keys", after the mockup ui-design/switches-advanced-keys.html):
// the key going down and coming back with its four points on the line, and under them a row per
// command, what it sends and the bars of what it does.
//
// A point is dragged up or down, its depth typed beside it, or the wheel turned over either, 0.1 mm
// at a time. In a row a click on a point taps there, a drag from one to another holds between them,
// a drag of a bar's end moves it, and a click on a tap or a bar removes it. Only the commands in use
// show, and one more to add. A command's key is chosen on the board above (Rico, 2026-11-23: a keyboard
// in a popup was too small): clicked here, its row waits for it, with the picker for keys the board lacks.
//
// It only draws and says what changed: the Switches section writes it. ImGui only, no SDL.

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "adapters/keychron/NazgKeychronAdvanced.h"

namespace nazg
{
    struct DksEditorInput
    {
        const keychron::DksSlot& slot;
        uint8_t                  fullTravel = 40;   // 0.1 mm

        std::function<std::string(uint16_t)> keycodeName;

        // The slots the profile has and uses, for the help.
        size_t slotsUsed  = 0;
        size_t slotsCount = 0;

        // The command whose key is being chosen on the board, and whether pressing one chooses it too.
        std::optional<size_t> picking;
        bool                  pickByPressing = true;

        // The key followed by the test, when it is this one: where it is, and what it holds.
        const keychron::DksPlayer* player = nullptr;
    };

    // What it remembers between frames: a drag in progress, shown and not written until let go.
    struct DksEditorState
    {
        std::optional<size_t>            draggedPoint;
        std::optional<keychron::DksSlot> dragged;   // the slot as the drag has it

        struct Lane
        {
            size_t                         command = 0;
            uint8_t                        from    = 0;
            uint8_t                        to      = 0;
            uint8_t                        anchor  = 0;   // where the bar stays while its end moves
            std::optional<keychron::DksBar> bar;          // the one whose end is dragged
        };
        std::optional<Lane> lane;

        char typed[keychron::c_DksPoints][8] = {};
    };

    struct DksEditorEvents
    {
        std::optional<keychron::DksSlot> changed;   // what to write
        std::optional<size_t>            pick;      // a command's key was clicked: choose what it sends

        // While one is being chosen: from the whole picker instead, none at all, or not after all.
        bool more   = false;
        bool remove = false;
        bool cancel = false;
    };

    [[nodiscard]] DksEditorEvents DrawDksEditor(DksEditorState& state, const DksEditorInput& input);

    // What is odd in the depths, said with what happens, or nothing.
    [[nodiscard]] std::string DksWarning(const keychron::DksSlot& slot, uint8_t fullTravel);
}
