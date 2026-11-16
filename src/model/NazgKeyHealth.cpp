// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyHealth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>

namespace nazg
{
    namespace
    {
        // The upper of the two middle values for an even count, as the mockup's.
        std::optional<int> Median(std::vector<int> values)
        {
            if (values.empty())
                return std::nullopt;
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            return *middle;
        }

        // The lower of the two middle values: of two presses, the one that moved a key less.
        std::optional<int> LowerMedian(std::vector<int> values)
        {
            if (values.empty())
                return std::nullopt;
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>((values.size() - 1) / 2);
            std::nth_element(values.begin(), middle, values.end());
            return *middle;
        }

        // How much levels wander: the 5th to the 95th percentile.
        int SpreadOf(std::vector<int> values)
        {
            if (values.empty())
                return 0;
            std::sort(values.begin(), values.end());
            const auto percentile = [&values](double q)
            {
                return values[std::min(values.size() - 1, static_cast<size_t>(std::floor(q * static_cast<double>(values.size()))))];
            };
            return percentile(0.95) - percentile(0.05);
        }

        bool IsVerdictWithReasons(KeyVerdict verdict)
        {
            return verdict == KeyVerdict::AtRisk || verdict == KeyVerdict::Failing;
        }
    }

    namespace
    {
        // The keys at rest while another is pressed (ui-design.md, "Signal levels, fourth round"),
        // in two passes over the keys analysed once.
        //
        // A finger's press is one that registered and is no echo: an echo crosses only inside another
        // key's registered press, a reading's slack either side, and less than half as deep -- a key
        // taken past its threshold by its neighbour's press. Depth alone would not do: on Rico's F77
        // left Win and Keypad 4 press 9 and 10 past, short of half most keys' hold, so their presses
        // never counted and "press twice" never went (2026-11-15).
        //
        // First, a key's attempts made only inside a finger's press on another key, never reaching its
        // threshold, are set aside -- neither its presses nor its rest -- and the key analysed again:
        // moved towards its threshold three times, it would otherwise be a key that "does not work".
        // One that crossed stays a press: a weak key pressed with others cannot be told from a key
        // taken past by them, and the second fails anyway, for that reason.
        //
        // Then every finger's press made alone -- no other finger's press meanwhile: chords tell
        // nothing -- is measured against every other key: its furthest reading towards its threshold
        // during the press, from where it rested just before -- a level drifting over the minutes
        // moves nothing. A key is moved when that happens in two presses alone or more, by the lower
        // middle of them, and beyond how much it wanders while no key is pressed: one odd press, or a
        // key that wanders, accuses nothing (Rico, 2026-11-15: pressing 5 dashed many keys, and
        // different ones each time). Not told apart: a finger resting lightly on a key while another
        // is pressed.
        void FindCouplings(BoardHealth& board, std::span<const KeyLevels> keys, LevelScale scale)
        {
            const int    d     = scale.direction;
            const size_t count = board.keys.size();

            // Which attempts are a finger's, key by key, on the keys as analysed now.
            std::vector<std::vector<bool>> fingerFlags(count);
            const auto findFingers = [&]
            {
                for (size_t b = 0; b < count; ++b)
                {
                    const std::vector<PressAttempt>& presses = board.keys[b].stats.presses;
                    fingerFlags[b].assign(presses.size(), false);
                    for (size_t j = 0; j < presses.size(); ++j)
                    {
                        const PressAttempt& own  = presses[j];
                        bool                echo = false;
                        for (size_t a = 0; a < count && !echo; ++a)
                            if (a != b)
                                for (const PressAttempt& other : board.keys[a].stats.presses)
                                    if (other.registered && other.first <= own.first + 1 && own.last <= other.last + 1 &&
                                        own.deepest * 2 < other.deepest)
                                    {
                                        echo = true;
                                        break;
                                    }
                        fingerFlags[b][j] = own.registered && !echo;
                    }
                }
            };
            const auto isFinger = [&](size_t key, const PressAttempt& press)
            {
                const std::vector<PressAttempt>& presses = board.keys[key].stats.presses;
                const size_t                     j       = static_cast<size_t>(&press - presses.data());
                return j < fingerFlags[key].size() && fingerFlags[key][j];
            };
            findFingers();

            // Set aside: an attempt inside another key's finger press, a reading's slack either side --
            // all decided on the first analysis, then the keys concerned analysed again.
            std::vector<std::vector<bool>> setAside(count);
            for (size_t b = 0; b < count; ++b)
            {
                std::vector<bool>& skipped = setAside[b];
                for (const PressAttempt& own : board.keys[b].stats.presses)
                {
                    // One that crossed stays: a weak key pressed with others is pressed.
                    if (own.registered)
                        continue;
                    bool inside = false;
                    for (size_t a = 0; a < count && !inside; ++a)
                        if (a != b)
                            for (const PressAttempt& other : board.keys[a].stats.presses)
                                if (isFinger(a, other) && other.first <= own.first + 1 && own.last <= other.last + 1)
                                {
                                    inside = true;
                                    break;
                                }
                    if (!inside)
                        continue;
                    if (skipped.empty())
                        skipped.resize(keys[b].trace.size());
                    for (size_t x = own.first; x <= own.last; ++x)
                        skipped[x] = true;
                }
            }
            for (size_t b = 0; b < count; ++b)
                if (const std::vector<bool>& skipped = setAside[b]; !skipped.empty())
                {
                    // std::vector<bool> is packed: a plain copy the span can see.
                    const std::unique_ptr<bool[]> flags(new bool[skipped.size()]);
                    std::copy(skipped.begin(), skipped.end(), flags.get());
                    board.keys[b].stats = AnalyseKey(keys[b].trace, keys[b].threshold, scale, std::span(flags.get(), skipped.size()));
                }
            findFingers();   // the keys analysed again have new attempts

            std::vector<std::pair<size_t, const PressAttempt*>> fingers;
            for (size_t a = 0; a < count; ++a)
                for (const PressAttempt& press : board.keys[a].stats.presses)
                    if (isFinger(a, press))
                        fingers.emplace_back(a, &press);

            // The board quiet: no finger's press. Each key's own attempts, and how much it wanders
            // while the board is quiet and it rests.
            size_t length = 0;
            for (const KeyLevels& key : keys)
                length = std::max(length, key.trace.size());
            std::vector<bool> busy(length);
            for (const auto& [a, press] : fingers)
                for (size_t x = press->first; x <= press->last; ++x)
                    busy[x] = true;

            std::vector<std::vector<bool>> own(count);
            for (size_t b = 0; b < count; ++b)
            {
                own[b].resize(keys[b].trace.size());
                for (const PressAttempt& press : board.keys[b].stats.presses)
                    for (size_t x = press.first; x <= press.last; ++x)
                        own[b][x] = true;
            }
            // Asked only of a key that looks moved: few are.
            std::vector<std::optional<int>> noise(count);
            const auto noiseOf = [&](size_t b)
            {
                if (!noise[b])
                {
                    std::vector<int> quiet;
                    quiet.reserve(keys[b].trace.size());
                    for (size_t x = 0; x < keys[b].trace.size(); ++x)
                        if (!busy[x] && !own[b][x])
                            quiet.push_back(keys[b].trace[x]);
                    noise[b] = SpreadOf(std::move(quiet));
                }
                return *noise[b];
            };

            // Where key `b` rested just before reading `first`: its last five quiet readings, by their
            // median -- or, from the very start, its first five after `last`.
            std::vector<int> before;   // reused: this runs for every key at every press
            before.reserve(5);
            const auto restBefore = [&](size_t b, size_t first, size_t last) -> std::optional<int>
            {
                before.clear();
                const std::span<const uint16_t> trace = keys[b].trace;
                for (size_t x = first; x > 0 && before.size() < 5 && first - x < 20;)
                    if (--x < trace.size() && !busy[x] && !own[b][x])
                        before.push_back(trace[x]);
                for (size_t x = last + 1; before.empty() && x < trace.size() && x <= last + 5; ++x)
                    if (!busy[x] && !own[b][x])
                        before.push_back(trace[x]);
                if (before.empty())
                    return std::nullopt;
                std::sort(before.begin(), before.end());
                return before[before.size() / 2];
            };

            // By pressed * count + moved: each press's move and the margin left.
            std::vector<std::vector<int>> moves(count * count), lefts(count * count);
            for (const auto& [a, press] : fingers)
            {
                bool alone = true;
                for (const auto& [other, its] : fingers)
                    if (other != a && its->first <= press->last && press->first <= its->last)
                    {
                        alone = false;
                        break;
                    }
                if (!alone)
                    continue;
                ++board.keys[a].pressesAlone;

                for (size_t b = 0; b < count; ++b)
                {
                    const KeyStats& st = board.keys[b].stats;
                    if (b == a || !st.restMedian || st.stuck)
                        continue;
                    int furthest = d * keys[b].trace[press->first];
                    for (size_t x = press->first; x <= press->last; ++x)
                        furthest = std::max(furthest, d * static_cast<int>(keys[b].trace[x]));
                    const int level = d * furthest;
                    const int rest  = restBefore(b, press->first, press->last).value_or(*st.restMedian);
                    moves[a * count + b].push_back(d * (level - rest));
                    lefts[a * count + b].push_back(d * (keys[b].threshold - level));
                }
            }

            for (size_t a = 0; a < count; ++a)
                for (size_t b = 0; b < count; ++b)
                {
                    const std::vector<int>& m = moves[a * count + b];
                    if (m.size() < c_CoupledPresses)
                        continue;
                    // The lower middle of the moves; of the margins left, the upper -- the same presses.
                    KeyCoupling coupling;
                    coupling.pressed = a;
                    coupling.moved   = b;
                    coupling.move    = *LowerMedian(m);
                    coupling.most    = *std::max_element(m.begin(), m.end());
                    coupling.left    = *Median(lefts[a * count + b]);
                    coupling.presses = m.size();
                    if (coupling.move >= c_CoupledShown && coupling.move > noiseOf(b))
                        board.couplings.push_back(coupling);
                }
            std::stable_sort(board.couplings.begin(), board.couplings.end(), [](const KeyCoupling& x, const KeyCoupling& y)
            {
                return x.left != y.left ? x.left < y.left : x.move > y.move;
            });
        }
    }

    KeyStats AnalyseKey(std::span<const uint16_t> trace, uint16_t threshold, LevelScale scale, std::span<const bool> skipped)
    {
        const size_t n  = trace.size();
        const int    th = threshold;
        const int    d  = scale.direction;
        const auto   skip = [skipped](size_t i) { return i < skipped.size() && skipped[i]; };

        KeyStats stats;

        std::vector<bool> pressed(n);
        std::vector<int>  released;
        for (size_t i = 0; i < n; ++i)
        {
            if (skip(i))
                continue;
            pressed[i] = d > 0 ? trace[i] >= th : trace[i] <= th;
            if (!pressed[i])
                released.push_back(trace[i]);
        }
        const std::optional<int> usual = Median(released);

        // Its own noise, from the median absolute deviation: a move is clear past three times it,
        // or 30 % of the offset.
        double sigma = 0.0;
        if (usual)
        {
            std::vector<int> deviations;
            deviations.reserve(released.size());
            for (int level : released)
                deviations.push_back(std::abs(level - *usual));
            sigma = 1.4826 * *Median(std::move(deviations));
        }
        const double clear = std::max(scale.offset * 0.3, 3.0 * sigma);

        std::vector<bool> away(n);
        for (size_t i = 0; i < n; ++i)
            away[i] = !skip(i) && (pressed[i] || (usual && d * (trace[i] - *usual) > clear));
        // Let go inside a hold, for one reading: still the press.
        for (size_t i = 1; i + 1 < n; ++i)
            if (!skip(i) && !pressed[i] && pressed[i - 1] && pressed[i + 1])
                away[i] = true;

        // A press: one reading is enough when well past the threshold -- a quick press -- two
        // otherwise, one reading barely off being noise.
        for (size_t i = 0; i < n;)
        {
            if (!away[i])
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < n && away[j])
                ++j;

            if (j - i >= 2 || (pressed[i] && d * (trace[i] - th) >= c_SafeMargin))
            {
                PressAttempt     press;
                std::vector<int> past;
                press.first   = i;
                press.last    = j - 1;
                press.deepest = d * (trace[i] - th);
                for (size_t x = i; x < j; ++x)
                {
                    if (pressed[x])
                        past.push_back(trace[x]);
                    press.deepest = std::max(press.deepest, d * (trace[x] - th));
                }
                press.registered = !past.empty();
                if (past.size() >= 3)
                    press.held = Median(std::move(past));
                stats.presses.push_back(press);
            }
            i = j;
        }

        std::vector<bool> inPress(n);
        for (const PressAttempt& press : stats.presses)
            for (size_t x = press.first; x <= press.last; ++x)
                inPress[x] = true;

        std::vector<int> resting;
        for (size_t i = 0; i < n; ++i)
            if (!inPress[i] && !skip(i))
                resting.push_back(trace[i]);

        // Shown, never counted: readings of a hold far from what it held -- the spring's overshoot.
        const double far = std::max(6.0, scale.offset * 0.6);
        for (const PressAttempt& press : stats.presses)
            if (press.held)
                for (size_t x = press.first; x <= press.last; ++x)
                    if (pressed[x] && std::abs(trace[x] - *press.held) > far)
                        stats.outliers.push_back(x);

        stats.top        = std::find(trace.begin(), trace.end(), c_LevelTop) != trace.end();
        stats.restMedian = Median(resting);

        // Its resting reading nearest the threshold, the first of equals.
        std::optional<int> nearest;
        for (int level : resting)
            if (!nearest || std::abs(level - th) < std::abs(*nearest - th))
                nearest = level;

        if (!resting.empty())
        {
            std::vector<int> sorted = resting;
            std::sort(sorted.begin(), sorted.end());
            const auto percentile = [&sorted](double q)
            {
                return sorted[std::min(sorted.size() - 1, static_cast<size_t>(std::floor(q * static_cast<double>(sorted.size()))))];
            };
            stats.restSpread = percentile(0.95) - percentile(0.05);
        }

        if (nearest)
            stats.restMargin = d * (th - *nearest);
        if (stats.restMedian)
            stats.medianMargin = d * (th - *stats.restMedian);

        std::vector<int> holds;
        for (const PressAttempt& press : stats.presses)
        {
            if (press.held)
                holds.push_back(d * (*press.held - th));
            if (press.registered)
                ++stats.registered;
        }
        if (!holds.empty())
        {
            const auto [weakest, strongest] = std::minmax_element(holds.begin(), holds.end());
            stats.pressMargin       = *strongest;
            stats.pressBand         = { *weakest, *strongest };
            stats.pressMedianMargin = Median(holds);
        }

        stats.seen     = stats.registered > 0;
        stats.moved    = !stats.presses.empty();
        stats.measured = !holds.empty();

        if (stats.moved && !stats.seen)
        {
            int closest = -stats.presses.front().deepest;
            for (const PressAttempt& press : stats.presses)
                closest = std::min(closest, -press.deepest);
            stats.closest = closest;
        }

        stats.stuck = resting.empty() && stats.seen;
        return stats;
    }

    BoardHealth JudgeBoard(std::span<const KeyLevels> keys, LevelScale scale, int columns, SafeMargins margins)
    {
        BoardHealth board;
        board.keys.reserve(keys.size());
        for (const KeyLevels& key : keys)
            board.keys.push_back({ AnalyseKey(key.trace, key.threshold, scale) });

        const auto typicalHold = [&board]
        {
            std::vector<int> usualHolds;
            for (const JudgedKey& judged : board.keys)
                if (judged.stats.pressMedianMargin)
                    usualHolds.push_back(*judged.stats.pressMedianMargin);
            return Median(usualHolds).value_or(0);
        };
        board.typical = typicalHold();

        FindCouplings(board, keys, scale);
        board.typical = typicalHold();

        for (size_t index = 0; index < board.keys.size(); ++index)
        {
            JudgedKey&      judged = board.keys[index];
            const KeyStats& st     = judged.stats;

            // Another key's press taking it closest to its threshold: couplings are sorted so.
            const KeyCoupling* coupled = nullptr;
            for (const KeyCoupling& coupling : board.couplings)
                if (coupling.moved == index)
                {
                    coupled = &coupling;
                    break;
                }
            KeyVerdict      v  = KeyVerdict::Good;
            std::vector<KeyReason>& reasons = judged.reasons;

            const auto risk = [&](KeyReason why)
            {
                reasons.push_back(why);
                if (v == KeyVerdict::Good || v == KeyVerdict::ToDo || v == KeyVerdict::ToCheck)
                    v = KeyVerdict::AtRisk;
            };

            if (st.stuck)
            {
                v = KeyVerdict::Failing;
                reasons.push_back(KeyReason::TypesByItself);
            }
            else
            {
                if (st.restMargin && *st.restMargin <= 0)
                    risk(KeyReason::WentPastUntouched);
                else if (st.medianMargin && *st.medianMargin < margins.rest)
                    risk(KeyReason::RestsClose);
                else if (st.medianMargin && st.restSpread >= *st.medianMargin * 0.6)
                    risk(KeyReason::WandersAtRest);

                // Judged by the rest margin, as resting close is: past it, it types by itself.
                if (coupled && coupled->left <= 0)
                {
                    v = KeyVerdict::Failing;
                    reasons.insert(reasons.begin(), KeyReason::TypesWhenPressed);
                    judged.movedBy = coupled->pressed;
                }
                else if (coupled && coupled->left < margins.rest)
                {
                    risk(KeyReason::NearsWhenPressed);
                    judged.movedBy = coupled->pressed;
                }

                if (!st.seen && st.presses.size() >= c_FailAfter)
                {
                    v = KeyVerdict::Failing;
                    reasons.insert(reasons.begin(), KeyReason::NeverReaches);
                }
                else if (st.measured && *st.pressMargin < margins.press)
                {
                    risk(KeyReason::OnlyJustPast);
                }

                // To do, not problems: a key not pressed, pressed without crossing yet, or too
                // briefly to measure.
                if (v == KeyVerdict::Good)
                {
                    if (!st.moved)
                        judged.toDo = KeyToDo::Press;
                    else if (!st.seen)
                        judged.toDo = KeyToDo::PressAgain;
                    else if (!st.measured)
                        judged.toDo = KeyToDo::Hold;

                    if (judged.toDo != KeyToDo::None)
                    {
                        v = KeyVerdict::ToDo;
                        reasons.push_back(judged.toDo == KeyToDo::Press      ? KeyReason::NotPressedYet
                                          : judged.toDo == KeyToDo::PressAgain ? KeyReason::PressAgain
                                                                               : KeyReason::HoldToMeasure);
                    }
                }

                if (st.top)
                {
                    if (v == KeyVerdict::Good)
                        v = KeyVerdict::ToCheck;
                    reasons.push_back(KeyReason::TopOfRange);
                }
            }

            judged.verdict = v;
            if (v == KeyVerdict::Failing)
                reasons.resize(1);

            const float rest  = st.restMargin ? static_cast<float>(std::max(0, *st.restMargin)) / static_cast<float>(scale.offset)
                                              : 1.0f;
            const float press = st.pressMargin ? static_cast<float>(*st.pressMargin) / static_cast<float>(std::max(1, board.typical))
                                               : (v == KeyVerdict::Failing ? 0.0f : 1.0f);
            const float moved = coupled ? static_cast<float>(std::max(0, coupled->left)) / static_cast<float>(scale.offset) : 1.0f;
            judged.health     = v == KeyVerdict::Failing ? 0.0f : std::clamp(std::min({ rest, press, moved }), 0.0f, 1.0f);
        }

        // A controller column pressing at half the board's usual hold or less, together.
        for (int column = 0; column < columns; ++column)
        {
            std::vector<int> holds;
            for (size_t k = 0; k < keys.size(); ++k)
                if (keys[k].column == column && board.keys[k].stats.pressMedianMargin)
                    holds.push_back(*board.keys[k].stats.pressMedianMargin);
            const std::optional<int> margin = Median(holds);
            if (holds.size() >= 3 && *margin <= board.typical / 2.0)
                board.columns.push_back({ column, holds.size(), *margin });
        }

        // One problem shared by a quarter of the keys judged, eight at least: the keyboard's.
        std::map<KeyReason, size_t> shared;
        for (const JudgedKey& judged : board.keys)
        {
            if (judged.verdict != KeyVerdict::ToDo)
                ++board.judged;
            if (IsVerdictWithReasons(judged.verdict))
                for (KeyReason reason : judged.reasons)
                    ++shared[reason];
        }
        const double enough = std::max(8.0, static_cast<double>(board.judged) / 4.0);
        for (const auto& [reason, count] : shared)
            if (static_cast<double>(count) >= enough)
                board.notes.push_back({ reason, count });
        std::stable_sort(board.notes.begin(), board.notes.end(),
                         [](const BoardNote& a, const BoardNote& b) { return a.keys > b.keys; });

        return board;
    }

    void ToolLevels::Add(uint16_t level, uint16_t threshold)
    {
        m_Last[0] = m_Last[1];
        m_Last[1] = m_Last[2];
        m_Last[2] = level;
        if (++m_Count < 3)
            return;

        uint16_t three[3] = { m_Last[0], m_Last[1], m_Last[2] };
        std::sort(std::begin(three), std::end(three));
        const uint16_t median = three[1];

        if (three[2] < threshold)
            m_Min = m_Min ? std::min(*m_Min, median) : median;
        if (three[0] >= threshold)
            m_Max = m_Max ? std::max(*m_Max, median) : median;
    }

    void ToolLevels::Clear()
    {
        *this = ToolLevels{};
    }
}
