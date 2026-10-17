// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// KeyHealth - which keys of a capacitive board may not be recognised, now and then or at all, told
// from their level readings: the Leyden Jar diagnostics' Signal levels (ui-design.md, "Plugins",
// from "Signal levels, second round" to "The rules that follow"). The rules are the mockup's,
// statsOf() and judge() in ui-design/key-health.html, which Rico settled on his F77's readings.
//
// A level is what the controller's sweep measured for one key: 350 to 499, 0 below the window, 499
// its top -- perhaps higher (leyden_jar_detect_levels()). A key is pressed when its level reaches its
// bin's threshold: rising to it on a Model F, falling to it on a Beam Spring -- `direction` +1 or -1.
// Nothing here knows when a finger is on a key, so a press attempt is a run of readings clearly off
// the key's usual level. What the F77 taught (2026-10-17):
//
//   - a press lasts one or two readings unless held: one is enough when well past the threshold;
//     it registers when a reading crosses;
//   - a hold's level is the finger's, so a hold is measured only over 3 readings or more, by their
//     median -- a Model F's first reading overshoots -- and a key's strength is its STRONGEST hold;
//     how holds differ is never a verdict;
//   - failing: seen pressed with nothing at rest; or never past its threshold in 3 presses or more;
//   - at risk: resting less than 3 from its threshold, or wandering by 60 % of its margin, or past
//     it untouched; or even its firmest hold under 3 past its threshold -- the rule of Rico's tool,
//     one number for both sides (Rico, 2026-10-17);
//   - no verdict from a released reading inside a hold: it is counted as part of the press.
//
// Everything is judged against the key's own board -- its bin's threshold, the offset, the other
// keys -- never against fixed numbers: levels differ from board to board.
//
// Pure: no ImGui, no transport, so it tests with recordings (tests/data/leyden-jar/).

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace nazg
{
    // Rico's tool: a level less than 3 from its threshold is not safe -- it coloured such a key
    // lighter. One margin for both sides: a hold past the threshold, a key at rest short of it
    // (Rico, 2026-10-17: the draft's half the offset at rest is dropped).
    inline constexpr int c_SafeMargin = 3;

    // The margins a verdict is judged by, each side's: c_SafeMargin unless an expert changes them
    // for the session, with Advanced tools (Rico, 2026-10-17).
    struct SafeMargins
    {
        int rest  = c_SafeMargin;   // at rest: less from the threshold, the key may type by itself
        int press = c_SafeMargin;   // pressed: even its firmest hold less past it, it may miss presses

        bool operator==(const SafeMargins&) const = default;
    };

    // Presses that never crossed before a key is said not to work; fewer ask to press it again.
    inline constexpr size_t c_FailAfter = 3;

    // The top of the sweep's window: a level there may be higher.
    inline constexpr uint16_t c_LevelTop = 499;

    // How the board's levels read: `direction` +1 on a Model F -- a press raises the level -- and -1
    // on a Beam Spring; `offset` how far a bin's threshold is from its resting level, the
    // firmware's activation offset.
    struct LevelScale
    {
        int direction = 1;
        int offset    = 10;
    };

    // A run of readings clearly off the key's usual level, towards its threshold.
    struct PressAttempt
    {
        size_t first = 0;   // reading indices, both included
        size_t last  = 0;

        bool registered = false;   // a reading crossed the threshold

        // The median of its readings past the threshold, when there are 3 or more: what it held.
        std::optional<int> held;

        // Its furthest reading past the threshold -- negative when it stopped short.
        int deepest = 0;
    };

    // What one key's readings say. A margin is how far a level is from the threshold on the side
    // that is safe: positive at rest when below it on a Model F, positive pressed when past it.
    struct KeyStats
    {
        std::vector<PressAttempt> presses;

        // Readings of a hold far from what it held -- the spring's overshoot: shown, never counted.
        std::vector<size_t> outliers;

        bool top = false;   // a reading at the top of the window

        std::optional<int> restMedian;     // its usual level at rest
        std::optional<int> restMargin;     // its resting reading nearest the threshold
        std::optional<int> medianMargin;   // its usual level at rest
        int                restSpread = 0; // how much it wanders at rest: the 5th to the 95th percentile

        std::optional<int>                 pressMargin;         // its strongest hold: what it can do
        std::optional<int>                 pressMedianMargin;   // its usual hold
        std::optional<std::pair<int, int>> pressBand;           // its weakest and strongest holds

        size_t registered = 0;    // presses that crossed
        bool   seen       = false;   // one crossed
        bool   moved      = false;   // one was seen at all
        bool   measured   = false;   // one was held long enough to measure

        // When no press crossed: how far short the deepest stopped -- 0 or less touched it.
        std::optional<int> closest;

        // Seen pressed with no reading at rest at all.
        bool stuck = false;
    };

    [[nodiscard]] KeyStats AnalyseKey(std::span<const uint16_t> trace, uint16_t threshold, LevelScale scale);

    enum class KeyVerdict : uint8_t
    {
        Failing,
        AtRisk,
        ToCheck,   // reads at the top of the range
        ToDo,      // not judged yet: see KeyToDo
        Good,
    };

    enum class KeyToDo : uint8_t
    {
        None,
        Press,        // not pressed yet
        PressAgain,   // pressed without crossing yet: "press it again, firmly"
        Hold,         // registered too briefly: "hold it a moment to measure it"
    };

    // Why, in the order said; the words are the panel's.
    enum class KeyReason : uint8_t
    {
        TypesByItself,        // failing: seen pressed without being touched
        NeverReaches,         // failing: its presses never reach its threshold
        WentPastUntouched,    // may type by itself
        RestsClose,           // may type by itself
        WandersAtRest,        // may type by itself
        OnlyJustPast,         // may miss presses: even its firmest press is only just past
        NotPressedYet,
        PressAgain,
        HoldToMeasure,
        TopOfRange,           // its real level may be higher
    };

    // One key to judge: its readings, its bin's threshold, and the controller column it is wired
    // to -- a weak column is said once, as a column.
    struct KeyLevels
    {
        std::span<const uint16_t> trace;
        uint16_t                  threshold = 0;
        int                       column    = 0;
    };

    struct JudgedKey
    {
        KeyStats               stats;
        KeyVerdict             verdict = KeyVerdict::Good;
        KeyToDo                toDo    = KeyToDo::None;
        std::vector<KeyReason> reasons;   // a failing key's first alone

        // 0 to 1, the tighter of its two margins: the list's order within a verdict.
        float health = 1.0f;
    };

    // A controller column whose keys, together, press at half the board's usual hold or less.
    struct WeakColumn
    {
        int    column = 0;
        size_t keys   = 0;   // its keys with a measured hold
        int    margin = 0;   // their usual hold
    };

    // One problem shared by a quarter of the keys judged: the keyboard's, said once.
    struct BoardNote
    {
        KeyReason reason = KeyReason::RestsClose;
        size_t    keys   = 0;
    };

    struct BoardHealth
    {
        std::vector<JudgedKey>  keys;   // as given
        int                     typical = 0;   // where most keys get to pressed: their usual holds' median
        std::vector<WeakColumn> columns;
        std::vector<BoardNote>  notes;   // the most shared first
        size_t                  judged = 0;   // keys not to do
    };

    [[nodiscard]] BoardHealth JudgeBoard(std::span<const KeyLevels> keys, LevelScale scale, int columns,
                                         SafeMargins margins = {});

    // Rico's tool's numbers on a key, for firmware work: the highest and the lowest of the medians of
    // three readings in a row all at or past the threshold, and all short of it -- whichever way the
    // switch presses -- and the reading now. Kept as the readings come.
    class ToolLevels
    {
    public:
        void Add(uint16_t level, uint16_t threshold);
        void Clear();

        [[nodiscard]] std::optional<uint16_t> Max() const noexcept { return m_Max; }
        [[nodiscard]] std::optional<uint16_t> Min() const noexcept { return m_Min; }
        [[nodiscard]] std::optional<uint16_t> Now() const noexcept { return m_Count == 0 ? std::nullopt : std::optional<uint16_t>(m_Last[2]); }

    private:
        uint16_t                m_Last[3] = {};
        size_t                  m_Count   = 0;
        std::optional<uint16_t> m_Max;
        std::optional<uint16_t> m_Min;
    };
}
