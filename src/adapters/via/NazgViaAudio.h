// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// ViaAudio - QMK's audio through VIA V3's channel 4 (via-vial-commands.md, "Audio"): two bits of
// the firmware's audio_config, audio on and clicky on, set in RAM and stored by the channel's
// save. A firmware built without audio answers 0xFF: ProtocolError. Vial has no audio wire.
//
// And the one way to make the board play on request: VIA's device indication, which plays the
// board's indication song while audio is on.

#pragma once

#include "adapters/via/NazgViaProtocol.h"
#include "async/NazgTask.h"

namespace nazg
{
    struct AudioValues
    {
        bool on     = false;
        bool clicky = false;   // heard only while `on`: QMK checks both
    };

    // The channel's value ids.
    enum class AudioSetting : uint8_t
    {
        On     = 1,   // id_qmk_audio_enable
        Clicky = 2,   // id_qmk_audio_clicky_enable
    };

    [[nodiscard]] Task<AudioValues> ReadAudio(ViaProtocol& via);

    // Sets one bit and stores the channel. Silent, unlike the AU_ON key, which plays a song.
    [[nodiscard]] Task<void> WriteAudio(ViaProtocol& via, AudioSetting setting, bool value);

    // Device indication 0 then 1: QMK's default plays STARTUP_SOUND on 0 and toggles each lighting
    // system once per call, so the pair leaves the lighting as it was. Boards override it -- the
    // WT60-XT plays its own song only. ProtocolError if the firmware has no device indication.
    [[nodiscard]] Task<void> PlayIndication(ViaProtocol& via);
}
