// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgVialUnlock.h"

#include <algorithm>
#include <exception>

#include "imgui.h"

#include "transport/NazgDeviceChannel.h"
#include "ui/NazgBoardDescription.h"
#include "ui/NazgBoardView.h"
#include "ui/NazgKeycapLegend.h"
#include "ui/NazgTheme.h"

namespace nazg
{
    namespace
    {
        // More than the firmware's 100 ms, with room for a frame's lateness.
        constexpr double c_PollInterval = 0.150;   // seconds
    }

    VialUnlock::VialUnlock(HidTransport&                            transport,
                           std::string                              path,
                           const Keyboard&                          keyboard,
                           std::vector<std::pair<uint8_t, uint8_t>> combo,
                           const std::string&                       hostLayoutId)
        : m_Transport(transport), m_Path(std::move(path)), m_Keyboard(keyboard), m_Combo(std::move(combo)),
          m_HostLayoutId(hostLayoutId)
    {
        m_Request = Start();
    }

    bool VialUnlock::IsDone() const noexcept
    {
        return (m_Stage == Stage::Unlocked || m_Stage == Stage::Failed) && !IsRequesting();
    }

    void VialUnlock::Draw()
    {
        // Paced here, not in a coroutine: a poll goes out once the last one has answered and
        // the interval has passed since it was sent.
        if (m_Stage == Stage::Holding && !IsRequesting() && ImGui::GetTime() - m_LastPoll >= c_PollInterval)
        {
            m_LastPoll = ImGui::GetTime();
            m_Request  = Poll();
        }

        ImGui::TextUnformatted("Hold the outlined keys until the bar fills.");
        ColouredText(PanelColour::Muted, "Letting go starts over. It takes about %.0f seconds.",
                     c_VialUnlockSteps * c_PollInterval);

        const float done = static_cast<float>(c_VialUnlockSteps - std::min(m_Countdown, c_VialUnlockSteps)) /
                           static_cast<float>(c_VialUnlockSteps);
        ImGui::ProgressBar(done, ImVec2(-1.0f, 0.0f), m_Stage == Stage::Starting ? "starting..." : "");

        ColouredText(PanelColour::Muted, "The board takes no other change until it is unlocked. To give up, "
                                         "unplug it.");

        // The board as the definition draws it: layer 0's legends, to find the keys by, the
        // combo outlined and everything else dimmed.
        const HostLayout* found  = FindHostLayout(m_HostLayoutId);
        const HostLayout& layout = found != nullptr ? *found : UsHostLayout();

        BoardDescription board = DescribeKeyboard(m_Keyboard);
        for (BoardKey& key : board.keys)
        {
            if (key.geometry.decal)
                continue;

            const KeycapLegend legend        = LegendFor(m_Keyboard.KeycodeFor(key.geometry, 0), layout);
            key[LegendSlot::MiddleLeft].text = legend.primary;
            key[LegendSlot::TopLeft].text    = legend.secondary;

            const bool inCombo = std::find(m_Combo.begin(), m_Combo.end(),
                                           std::make_pair(key.geometry.row, key.geometry.column)) != m_Combo.end();
            key.marks |= inCombo ? Mark::Highlighted : Mark::Dimmed;
        }

        ImGui::Spacing();
        (void)DrawBoard(board, ImGui::GetContentRegionAvail().y);
    }

    Task<void> VialUnlock::Start()
    {
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);

            HidDeviceChannel channel(m_Transport, device);
            VialProtocol     vial(channel);
            co_await vial.StartUnlock();

            m_Stage = Stage::Holding;
        }
        catch (const std::exception& failure)
        {
            m_Failure = std::string("the unlock could not start: ") + failure.what();
            m_Stage   = Stage::Failed;
        }

        // Outside the catch: closing an id that never opened does nothing.
        m_Transport.Close(device);
    }

    Task<void> VialUnlock::Poll()
    {
        DeviceId device = c_InvalidDevice;
        try
        {
            device = co_await m_Transport.Open(m_Path);

            HidDeviceChannel         channel(m_Transport, device);
            VialProtocol             vial(channel);
            const VialUnlockProgress progress = co_await vial.PollUnlock();

            m_Countdown = progress.countdown;
            if (progress.unlocked)
            {
                m_Stage = Stage::Unlocked;
            }
            else if (!progress.inProgress)
            {
                // Neither unlocked nor waiting: the board restarted, which ends an unlock.
                m_Failure = "the board restarted before it was unlocked -- it is still locked";
                m_Stage   = Stage::Failed;
            }
        }
        catch (const std::exception& failure)
        {
            m_Failure = std::string("the unlock stopped: ") + failure.what();
            m_Stage   = Stage::Failed;
        }

        m_Transport.Close(device);
    }
}
