// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A real Keychron board, replayed: every request nazg-probe-keychron --record sent to Rico's
// borrowed K10 HE V3 (3434:0EA7) and the board's reply, before it went back to its owner. The
// adapter's reads run against those bytes and must find what the board held -- its three profiles
// at Keychron's presets, the Gamepad preset's 25 keys, the keymap, an idle switch matrix.
//
// The recording and what was on the board: tests/data/keychron/README.md.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "adapters/keychron/NazgKeychronProtocol.h"

#include "TestSupport.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace keychron = nazg::keychron;
namespace via      = nazg::via;

using nazg::Task;

namespace
{
    constexpr uint8_t c_Rows = 6;
    constexpr uint8_t c_Cols = 21;

    // A channel answering each request with the reply recorded for exactly that request. The board
    // was idle while recorded, so the same request always had the same reply: the order the reads
    // come in does not matter, only that each was recorded.
    class ReplayChannel : public nazg::DeviceChannel
    {
    public:
        explicit ReplayChannel(const std::string& path)
        {
            std::ifstream file(path);
            if (!file)
                throw std::runtime_error("cannot read " + path);

            std::string          line;
            std::vector<uint8_t> request;
            bool                 haveRequest = false;
            while (std::getline(file, line))
            {
                if (line.size() < 2 || line[0] == '#')
                    continue;
                const std::vector<uint8_t> bytes = Parse(line.substr(2));
                if (line[0] == '>')
                {
                    request     = bytes;
                    haveRequest = true;
                }
                else if (line[0] == '<' && haveRequest)
                {
                    m_Replies[request] = bytes;
                    haveRequest        = false;
                }
            }
        }

        Task<std::vector<uint8_t>> Request(std::vector<uint8_t> payload, nazg::ReplyCheck isReply = {}) override
        {
            const auto found = m_Replies.find(payload);
            if (found == m_Replies.end())
                throw nazg::HidTransportError("ReplayChannel: this request was never recorded");
            if (isReply && !isReply(found->second))
                throw nazg::HidTransportError("ReplayChannel: the recorded reply fails the protocol's check");
            ++m_Answered;
            co_return found->second;
        }

        Task<void> Send(std::vector<uint8_t>) override
        {
            throw std::logic_error("ReplayChannel: the recording holds no report sent without a reply");
            co_return;
        }

        void SetFarewell(std::vector<uint8_t>) override {}

        size_t Recorded() const noexcept { return m_Replies.size(); }
        size_t Answered() const noexcept { return m_Answered; }

    private:
        static std::vector<uint8_t> Parse(const std::string& hex)
        {
            std::vector<uint8_t> bytes;
            std::istringstream   in(hex);
            unsigned             byte = 0;
            while (in >> std::hex >> byte)
                bytes.push_back(static_cast<uint8_t>(byte));
            return bytes;
        }

        std::map<std::vector<uint8_t>, std::vector<uint8_t>> m_Replies;
        size_t                                               m_Answered = 0;
    };

    template <typename T>
    T Run(Task<T> task)
    {
        if (!task.IsDone())
            throw std::logic_error("the protocol call did not complete synchronously");
        return task.TakeResult();
    }

    size_t Index(uint8_t row, uint8_t column) { return static_cast<size_t>(row) * c_Cols + column; }

    bool AllFollow(const keychron::Profile& profile)
    {
        for (const keychron::KeyConfig& key : profile.keys)
            if (!(key == keychron::KeyConfig{}))
                return false;
        return true;
    }

    bool SlotsEmpty(const keychron::Profile& profile)
    {
        for (const keychron::DksSlot& slot : profile.dksSlots)
            if (!slot.IsEmpty())
                return false;
        for (const keychron::SocdSlot& slot : profile.socdSlots)
            if (!slot.IsEmpty())
                return false;
        return true;
    }

    void TestBoard(const std::string& path)
    {
        std::printf("the K10 HE V3, replayed\n");

        ReplayChannel      channel(path);
        keychron::Protocol protocol(channel);
        std::printf("  %zu exchanges recorded\n", channel.Recorded());

        // Who it is.
        const std::optional<keychron::Identity> identity = Run(protocol.Probe());
        Check(identity.has_value() && identity->protocolVersion == 4, "a Keychron, protocol 4");
        Check(Run(protocol.GetFirmwareVersion()) == "v1.0.0 2026-07-17-17:42:44", "firmware v1.0.0, 2026-07-17");
        Check(Run(protocol.SupportsAnalogMatrix()), "the analog matrix answers");
        Check(Run(protocol.SupportsRgb()), "Keychron RGB answers, its feature bit clear");
        Check(Run(protocol.SupportsMisc(keychron::Misc::GetNkro)), "NKRO answers, its feature bit clear");
        Check(!Run(protocol.SupportsMisc(keychron::Misc::GetDebounce)), "dynamic debounce absent");
        Check(Run(protocol.GetAnalogVersion()) == 4, "analog matrix version 4");

        // The profile store.
        keychron::ProfileGeometry geometry = Run(protocol.GetProfileGeometry());
        geometry.fullTravel                = keychron::EstimateFullTravel(Run(protocol.GetCurve()));
        Check(geometry.profileCount == 3 && geometry.profileSize == 980, "three profiles of 980 bytes");
        Check(geometry.dksSlots == 20 && geometry.socdSlots == 20, "20 DKS and 20 SOCD slots");
        Check(geometry.Fits(c_Rows, c_Cols), "the size fits 6 x 21");
        Check(geometry.fullTravel == 35, "full travel 3.5 mm, from the curve");
        Check(geometry.currentProfile < 3, "a profile in use");
        std::printf("  profile %u in use\n", geometry.currentProfile);

        std::vector<keychron::Profile> profiles;
        for (uint8_t index = 0; index < geometry.profileCount; ++index)
            profiles.push_back(keychron::DecodeProfile(Run(protocol.GetProfileRaw(index, geometry)), c_Rows, c_Cols,
                                                       geometry.dksSlots, geometry.socdSlots));

        // Default: the firmware's defaults.
        const keychron::KeyConfig regular{ keychron::KeyMode::Regular, 17, 4, 4 };
        const keychron::KeyConfig rapid{ keychron::KeyMode::RapidTrigger, 17, 4, 4 };
        Check(profiles[0].global == regular, "Default: regular 1.7, sensitivities 0.4");
        Check(AllFollow(profiles[0]), "Default: every key follows the profile");

        // Gaming: rapid trigger, every key following it.
        Check(profiles[1].global == rapid, "Gaming: rapid trigger 1.7 / 0.4 / 0.4");
        Check(AllFollow(profiles[1]), "Gaming: every key follows the profile");

        // Gamepad: rapid trigger, 25 keys driving the game controller (keychron-commands.md, "The
        // three profiles read").
        Check(profiles[2].global == rapid, "Gamepad: rapid trigger 1.7 / 0.4 / 0.4");
        size_t gamepadKeys = 0;
        for (const keychron::KeyConfig& key : profiles[2].keys)
            gamepadKeys += key.advancedMode == keychron::AdvancedMode::Gamepad ? 1u : 0u;
        Check(gamepadKeys == 25, "Gamepad: 25 gamepad keys");
        auto gamepad = [&](uint8_t row, uint8_t column, uint8_t value) {
            const keychron::KeyConfig& key = profiles[2].keys[Index(row, column)];
            return key.advancedMode == keychron::AdvancedMode::Gamepad && key.advancedData == value &&
                   key.mode == keychron::KeyMode::FollowProfile;
        };
        Check(gamepad(3, 1, 0) && gamepad(3, 3, 1) && gamepad(3, 2, 2) && gamepad(2, 2, 3),
              "Gamepad: A D S W the left stick, 0 to 3");
        Check(gamepad(1, 15, 27), "Gamepad: Home the Xbox button, 27");

        Check(SlotsEmpty(profiles[0]) && SlotsEmpty(profiles[1]) && SlotsEmpty(profiles[2]),
              "no DKS or SOCD slot in use in any profile");

        // The keymap and the switch matrix, through VIA's commands.
        const uint16_t viaProtocol = Run(protocol.GetProtocolVersion());
        const uint8_t  layers      = Run(protocol.GetLayerCount());
        Check(viaProtocol >= 12, "VIA protocol 12 or later");
        Check(layers >= 1, "at least one layer");
        const std::vector<uint8_t> keymap =
            Run(protocol.GetKeymapBuffer(0, static_cast<uint16_t>(layers * c_Rows * c_Cols * 2u)));
        auto keycode = [&](uint8_t row, uint8_t column) {
            const size_t at = Index(row, column) * 2;
            return static_cast<uint16_t>(keymap[at] << 8 | keymap[at + 1]);
        };
        Check(keymap.size() == layers * c_Rows * c_Cols * 2u, "the whole keymap read");
        Check(keycode(2, 2) == 0x001A && keycode(3, 1) == 0x0004, "layer 0: W at (2,2), A at (3,1)");
        Check(keycode(1, 15) == 0x004A, "layer 0: Home at (1,15)");

        const via::SwitchMatrixState state =
            Run(protocol.GetSwitchMatrixState(via::SwitchMatrixFormatFor(viaProtocol), c_Rows, c_Cols));
        bool idle = true;
        for (uint8_t row = 0; row < c_Rows; ++row)
            for (uint8_t column = 0; column < c_Cols; ++column)
                idle = idle && !state.IsPressed(row, column);
        Check(idle, "the switch matrix read, no key pressed");

        std::printf("  %zu requests answered from the recording\n", channel.Answered());
    }
}

int main(int argc, char** argv)
{
    ConfigureCrtReporting();

    if (argc < 2)
    {
        std::printf("usage: %s <tests/data/keychron/the recording>\n", argv[0]);
        return 1;
    }

    try
    {
        TestBoard(argv[1]);
    }
    catch (const std::exception& failure)
    {
        Check(false, failure.what());
    }
    return TestResult();
}
