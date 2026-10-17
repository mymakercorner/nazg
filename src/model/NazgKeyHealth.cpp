// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgKeyHealth.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

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

        bool IsVerdictWithReasons(KeyVerdict verdict)
        {
            return verdict == KeyVerdict::AtRisk || verdict == KeyVerdict::Failing;
        }
    }

    KeyStats AnalyseKey(std::span<const uint16_t> trace, uint16_t threshold, LevelScale scale)
    {
        const size_t n  = trace.size();
        const int    th = threshold;
        const int    d  = scale.direction;

        KeyStats stats;

        std::vector<bool> pressed(n);
        std::vector<int>  released;
        for (size_t i = 0; i < n; ++i)
        {
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
            away[i] = pressed[i] || (usual && d * (trace[i] - *usual) > clear);
        // Let go inside a hold, for one reading: still the press.
        for (size_t i = 1; i + 1 < n; ++i)
            if (!pressed[i] && pressed[i - 1] && pressed[i + 1])
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
            if (!inPress[i])
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

        std::vector<int> usualHolds;
        for (const JudgedKey& judged : board.keys)
            if (judged.stats.pressMedianMargin)
                usualHolds.push_back(*judged.stats.pressMedianMargin);
        board.typical = Median(usualHolds).value_or(0);

        for (JudgedKey& judged : board.keys)
        {
            const KeyStats& st = judged.stats;
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
            judged.health     = v == KeyVerdict::Failing ? 0.0f : std::clamp(std::min(rest, press), 0.0f, 1.0f);
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
