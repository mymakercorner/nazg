// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// AudioSection - QMK's audio on a VIA board whose definition declares `qmk_audio` (ui-design.md,
// "The Audio section", after the mockup ui-design/audio-section.html): Audio and Clicky, Clicky
// greyed while audio is off -- QMK sounds the clicks only with both -- and Play a sound, the board
// playing its indication song on its own speaker.
//
// The board lights the keys that change it from the keyboard, on any layer, and, under the pointer,
// those that change one line. What they change makes the panel stale, so the values are read again
// each time the section is shown. Written and stored at once, then read back.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <optional>
#include <string>

#include "adapters/via/NazgViaAudio.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"
#include "ui/NazgSection.h"

namespace nazg
{
    class AudioSection : public Section
    {
    public:
        // The board at `path`, loaded into `keyboard`. All outlive the section -- ui/NazgSection.h.
        AudioSection(HidTransport& transport, std::string path, Keyboard& keyboard, const LegendSettings& legends);

        [[nodiscard]] std::string_view Name() const override { return "Audio"; }
        [[nodiscard]] Icon             ColumnIcon() const override { return Icon::Volume; }

        void DescribeBoard(BoardDescription& board) override;
        void DrawPanel() override;

        [[nodiscard]] bool IsBusy() const override;
        void               WhileHidden() override;

    private:
        // What waits to go to the board: a setting written and stored, or the song played.
        struct PendingWrite
        {
            std::optional<via::AudioSetting> setting;   // none: play
            bool                             value = false;
        };

        void DrawLines();
        void DrawSays();
        [[nodiscard]] bool AnyKeyChanges() const;

        Task<void> Load();
        Task<void> Write(PendingWrite write);

        HidTransport&         m_Transport;
        std::string           m_Path;
        Keyboard&             m_Keyboard;
        const LegendSettings& m_Legends;

        via::AudioValues m_Values;   // as the board holds them, or as being written

        bool        m_IsLoaded  = false;
        bool        m_IsStale   = false;   // shown again since read: read again
        bool        m_NoAudio   = false;   // the firmware answers it has none
        Task<void>  m_Request;
        std::string m_Message;
        bool        m_IsWarning = false;

        std::optional<PendingWrite> m_Pending;

        // The line under the pointer last frame, for the board, and this frame's.
        std::optional<via::AudioSetting> m_Hovered;
        std::optional<via::AudioSetting> m_HoveredNow;
    };
}
