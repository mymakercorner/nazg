// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// SocdEditor - an SOCD pair's four choices, each drawn, and its test: the two keys' last seconds and
// what was sent (ui-design.md, "SOCD", after the mockup ui-design/switches-socd.html). The choices
// carry the Launcher's names; a card shows what its choice does -- the two keys going down and up, under
// them what is sent, first with the first key held and the other pressed meanwhile, then the other way
// round. Under Absolute Priority, which key wins leads the words.
//
// ImGui only, no SDL: compiled into the application, not into nazg_core.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "adapters/keychron/NazgKeychronSocd.h"

namespace nazg
{
    struct SocdPairView
    {
        std::string        first;    // the keys' names
        std::string        second;
        keychron::SocdType type       = keychron::SocdType::Off;
        uint8_t            fullTravel = 40;   // 0.1 mm
    };

    // "Absolute Priority, C wins".
    [[nodiscard]] std::string SocdBriefly(const SocdPairView& pair);

    // What it does while both are down, in a sentence or two.
    [[nodiscard]] std::string SocdWords(const SocdPairView& pair);

    // A pair on the line of pairs, as an item: its two names in keycaps, SOCD's arrows centred between them,
    // lit when it is the one shown, and an x to remove it. A name empty is a key being waited for.
    struct SocdPairItemEvents
    {
        bool clicked = false;
        bool hovered = false;
        bool removed = false;
    };
    [[nodiscard]] float              SocdPairItemWidth(const std::string& first, const std::string& second, bool hasRemove);
    [[nodiscard]] SocdPairItemEvents DrawSocdPairItem(const std::string& first, const std::string& second, bool isShown,
                                                      bool hasRemove);

    // The four choices, the words, and `warning` under them where there is one. The type to write, once
    // another is clicked.
    [[nodiscard]] std::optional<keychron::SocdType> DrawSocdEditor(const SocdPairView& pair, const std::string& warning);

    // The last seconds of the pair tested: how far each key was down, and which were sent.
    struct SocdTrail
    {
        struct Sample
        {
            double time   = 0.0;    // ImGui::GetTime()
            float  first  = 0.0f;   // of full travel
            float  second = 0.0f;
            bool   firstSent  = false;
            bool   secondSent = false;
        };

        std::vector<Sample> samples;

        void Add(Sample sample);
        void Clear() { samples.clear(); }
    };

    struct SocdTestInput
    {
        SocdPairView     pair;
        std::string      firstTravel;    // "3.21 mm"
        std::string      secondTravel;
        bool             firstSent  = false;
        bool             secondSent = false;
        bool             firstHeldBack  = false;   // down, and not sent
        bool             secondHeldBack = false;
        const SocdTrail* trail = nullptr;
    };

    void DrawSocdTest(const SocdTestInput& input);
}
