// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// VialUnlock - unlocking a Vial board: the board drawn with the keys to hold outlined, and a
// bar that fills while they are held. It takes the board screen's place until it ends.
//
// The firmware decides everything (adapters/vial/NazgVialProtocol.h): it counts down one
// step per poll while the combo is held, and starts over on a poll too soon or a key let go.
// So the polls are paced by the frame loop, 150 ms apart -- the firmware wants more than 100
// -- and an unlock takes some 7.5 s of holding. Each poll opens the board, asks and closes,
// as a keymap write does.
//
// Once started it cannot be cancelled: the board answers nothing else until it is unlocked
// or restarted, so the screen offers no way out but unplugging.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "adapters/vial/NazgVialProtocol.h"
#include "async/NazgTask.h"
#include "model/NazgKeyboard.h"
#include "transport/NazgHidTransport.h"

namespace nazg
{
    class VialUnlock
    {
    public:
        // Starts the unlock on the board at `path`. `keyboard` draws it, `combo` is the keys to
        // hold, from its unlock status, and `hostLayoutId` the legends' setting. The transport,
        // the keyboard and the setting outlive this object.
        VialUnlock(HidTransport&                            transport,
                   std::string                              path,
                   const Keyboard&                          keyboard,
                   std::vector<std::pair<uint8_t, uint8_t>> combo,
                   const std::string&                       hostLayoutId);

        // Into the current window, every frame: draws, and polls when it is time.
        void Draw();

        // Ended: unlocked, or failed with Failure() saying why. Nothing is in flight then.
        [[nodiscard]] bool               IsDone() const noexcept;
        [[nodiscard]] bool               IsUnlocked() const noexcept { return m_Stage == Stage::Unlocked; }
        [[nodiscard]] const std::string& Failure() const noexcept { return m_Failure; }

    private:
        enum class Stage
        {
            Starting,
            Holding,
            Unlocked,
            Failed,
        };

        Task<void> Start();
        Task<void> Poll();

        [[nodiscard]] bool IsRequesting() const noexcept { return m_Request.IsValid() && !m_Request.IsDone(); }

        HidTransport&                            m_Transport;
        std::string                              m_Path;
        const Keyboard&                          m_Keyboard;
        std::vector<std::pair<uint8_t, uint8_t>> m_Combo;
        const std::string&                       m_HostLayoutId;

        Stage       m_Stage     = Stage::Starting;
        uint8_t     m_Countdown = c_VialUnlockSteps;
        std::string m_Failure;

        Task<void> m_Request;
        double     m_LastPoll = 0.0;   // ImGui::GetTime() of the last poll sent
    };
}
