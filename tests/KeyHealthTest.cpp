// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Signal levels' judgment (model/NazgKeyHealth.h): on Rico's two F77 recordings, where
// every key works -- so nothing may be at risk or failing -- and on made-up traces, one per rule.
//
// The recordings' folder is the first argument (tests/data/leyden-jar, README.md there).
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "model/NazgKeyHealth.h"

#include "adapters/NazgXz.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "files/NazgFiles.h"
#include "model/NazgKeyboard.h"

#include "TestSupport.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

using nazg::BoardHealth;
using nazg::KeyLevels;
using nazg::KeyReason;
using nazg::KeyToDo;
using nazg::KeyVerdict;
using nazg::LevelScale;

namespace
{
    // One key of a recording: its legend, its trace, its bin's threshold, its controller column.
    struct RecordedKey
    {
        std::string           keycode;
        std::vector<uint16_t> trace;
        uint16_t              threshold = 0;
        int                   column    = 0;
    };

    struct Recording
    {
        std::vector<RecordedKey> keys;
        LevelScale               scale;
        int                      columns = 0;
    };

    // The drawn keys at choice 0 of every layout option, as the section judges them: each switch
    // once, mapped to the controller's position, its threshold its bin's.
    Recording Load(const std::filesystem::path& file)
    {
        const std::optional<std::vector<uint8_t>> packed = nazg::ReadWholeFile(file);
        if (!packed)
            throw std::runtime_error("cannot read " + file.string());
        const std::vector<uint8_t> text = nazg::DecompressXz(*packed, 64u << 20, "the recording");
        const nlohmann::json       json = nlohmann::json::parse(text.begin(), text.end());

        const nlohmann::json& info = json["info"];
        const std::string     definitionText = json["definition"].dump();
        const nazg::via::KeyboardDefinition definition =
            nazg::via::ParseDefinition(std::vector<uint8_t>(definitionText.begin(), definitionText.end()));
        const size_t groups = nazg::ParseLayoutGroups(definition.layoutLabels).size();

        const auto mappingColumns = info["mappingColumns"].get<std::vector<int>>();
        const auto mappingRows    = info["mappingRows"].get<std::vector<int>>();
        const auto bins           = info["bins"].get<std::vector<std::vector<int>>>();
        const auto thresholds     = info["thresholds"].get<std::vector<int>>();
        const auto references     = info["referenceLevels"].get<std::vector<int>>();

        Recording recording;
        recording.scale.direction = info["beamSpring"].get<bool>() ? -1 : 1;
        recording.scale.offset    = std::abs(thresholds[0] - references[0]);
        recording.columns         = info["controllerColumns"].get<int>();

        std::set<std::pair<int, int>> taken;
        for (const nazg::via::DefinitionKey& key : nazg::PlaceKeys(definition, std::vector<uint8_t>(groups, 0)))
        {
            if (!key.IsSwitch() || !taken.insert({ key.row, key.column }).second)
                continue;
            const int column = mappingColumns.at(key.column);
            const int row    = mappingRows.at(key.row);

            RecordedKey recorded;
            recorded.keycode   = json["keymap"].value(std::to_string(key.row) + "," + std::to_string(key.column), "");
            recorded.threshold = static_cast<uint16_t>(thresholds.at(bins.at(column).at(row)));
            recorded.column    = column;
            for (const nlohmann::json& reading : json["readings"])
                recorded.trace.push_back(reading["levels"][column][row].get<uint16_t>());
            recording.keys.push_back(std::move(recorded));
        }
        return recording;
    }

    BoardHealth Judge(const Recording& recording)
    {
        std::vector<KeyLevels> levels;
        for (const RecordedKey& key : recording.keys)
            levels.push_back({ key.trace, key.threshold, key.column });
        return nazg::JudgeBoard(levels, recording.scale, recording.columns);
    }

    size_t CountOf(const BoardHealth& health, KeyVerdict verdict, KeyToDo toDo = KeyToDo::None)
    {
        size_t count = 0;
        for (const nazg::JudgedKey& key : health.keys)
            if (key.verdict == verdict && (toDo == KeyToDo::None || key.toDo == toDo))
                ++count;
        return count;
    }

    void TestEveryKey(const std::filesystem::path& folder)
    {
        std::printf("the F77, every key pressed slowly\n");

        const Recording recording = Load(folder / "f77-every-key-2026-10-17.json.xz");

        const auto        start  = std::chrono::steady_clock::now();
        const BoardHealth health = Judge(recording);
        const auto        spent  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
        std::printf("  %zu keys, %zu readings judged in %.1f ms\n", recording.keys.size(),
                    recording.keys.front().trace.size(), spent.count());

        Check(recording.keys.size() == 75, "75 keys drawn");
        Check(CountOf(health, KeyVerdict::Failing) == 0, "no key failing: every key works");
        Check(CountOf(health, KeyVerdict::AtRisk) == 0, "no key at risk");
        Check(CountOf(health, KeyVerdict::ToCheck) == 0, "no level at the top of the range");
        Check(health.columns.empty() && health.notes.empty(), "no weak column, nothing said of the whole board");

        // As the mockup carries it: its presses were mostly brief.
        Check(CountOf(health, KeyVerdict::Good) == 26, "26 keys good");
        Check(CountOf(health, KeyVerdict::ToDo, KeyToDo::Hold) == 49, "49 to hold a moment, to be measured");

        for (const nazg::JudgedKey& key : health.keys)
            if (key.verdict != KeyVerdict::Good && key.toDo != KeyToDo::Hold)
                std::printf("  unexpected: %s\n", recording.keys[&key - health.keys.data()].keycode.c_str());
    }

    void TestHeldKeys(const std::filesystem::path& folder)
    {
        std::printf("the F77, K, F and Num Lock held\n");

        const Recording   recording = Load(folder / "f77-held-keys-2026-10-17.json.xz");
        const BoardHealth health    = Judge(recording);

        Check(CountOf(health, KeyVerdict::Failing) == 0 && CountOf(health, KeyVerdict::AtRisk) == 0,
              "no key failing or at risk");

        std::map<std::string, const nazg::JudgedKey*> byKeycode;
        for (size_t k = 0; k < recording.keys.size(); ++k)
            byKeycode[recording.keys[k].keycode] = &health.keys[k];

        for (const char* name : { "KC_K", "KC_F", "KC_NUM" })
        {
            const nazg::JudgedKey* key = byKeycode[name];
            Check(key != nullptr && key->verdict == KeyVerdict::Good, (std::string(name) + " good").c_str());
            if (key != nullptr && key->stats.pressBand)
                std::printf("  %s: %zu presses, holds %d to %d past\n", name, key->stats.presses.size(),
                            key->stats.pressBand->first, key->stats.pressBand->second);
        }

        // F held 3, 5, 5, 8 and 14 past: its strongest hold decides, the weak ones are the finger's.
        const nazg::JudgedKey* f = byKeycode["KC_F"];
        Check(f != nullptr && f->stats.pressBand == std::pair(3, 14) && f->stats.pressMargin == 14,
              "F: holds from 3 to 14 past, judged by the strongest");
    }

    // A trace: `rest` everywhere, `level` over [first, last] of each span.
    std::vector<uint16_t> Trace(size_t length, uint16_t rest, std::initializer_list<std::pair<size_t, size_t>> spans,
                                uint16_t level)
    {
        std::vector<uint16_t> trace(length);
        for (size_t i = 0; i < length; ++i)
            trace[i] = static_cast<uint16_t>(rest + (i * 7 % 3) - 1);   // a quiet board moves by a level or two
        for (const auto& [first, last] : spans)
            for (size_t i = first; i <= last; ++i)
                trace[i] = level;
        return trace;
    }

    KeyVerdict VerdictOf(const std::vector<uint16_t>& trace, uint16_t threshold, LevelScale scale,
                         std::vector<KeyReason>* reasons = nullptr, KeyToDo* toDo = nullptr)
    {
        const KeyLevels   key{ trace, threshold, 0 };
        const BoardHealth health = nazg::JudgeBoard(std::span(&key, 1), scale, 1);
        if (reasons)
            *reasons = health.keys[0].reasons;
        if (toDo)
            *toDo = health.keys[0].toDo;
        return health.keys[0].verdict;
    }

    // One made-up key per rule, on a Model F bin: rest 374, threshold 384.
    void TestRules()
    {
        std::printf("the rules, one key each\n");

        const LevelScale modelF{ 1, 10 };
        const LevelScale beamSpring{ -1, 30 };
        constexpr uint16_t c_Rest      = 374;
        constexpr uint16_t c_Threshold = 384;

        std::vector<KeyReason> reasons;
        KeyToDo                toDo = KeyToDo::None;

        Check(VerdictOf(Trace(60, c_Rest, { { 10, 14 }, { 30, 34 } }, 400), c_Threshold, modelF) == KeyVerdict::Good,
              "two holds well past: good");

        Check(VerdictOf(Trace(60, c_Rest, {}, 0), c_Threshold, modelF, &reasons, &toDo) == KeyVerdict::ToDo &&
                  toDo == KeyToDo::Press,
              "never pressed: to do, press it");

        Check(VerdictOf(Trace(60, c_Rest, { { 10, 10 }, { 30, 31 } }, 400), c_Threshold, modelF, &reasons, &toDo) ==
                      KeyVerdict::ToDo &&
                  toDo == KeyToDo::Hold,
              "pressed briefly: registered, to hold a moment");

        Check(VerdictOf(Trace(60, c_Rest, { { 10, 14 } }, 380), c_Threshold, modelF, &reasons, &toDo) ==
                      KeyVerdict::ToDo &&
                  toDo == KeyToDo::PressAgain,
              "one press short of the threshold: press it again");

        Check(VerdictOf(Trace(60, c_Rest, { { 5, 8 }, { 20, 23 }, { 40, 43 } }, 380), c_Threshold, modelF, &reasons) ==
                      KeyVerdict::Failing &&
                  reasons == std::vector<KeyReason>{ KeyReason::NeverReaches },
              "three presses never crossing: does not work");

        Check(VerdictOf(Trace(60, c_Rest, { { 10, 14 }, { 30, 34 } }, 385), c_Threshold, modelF, &reasons) ==
                      KeyVerdict::AtRisk &&
                  reasons == std::vector<KeyReason>{ KeyReason::OnlyJustPast },
              "its firmest hold 1 past: may miss presses");

        Check(VerdictOf(Trace(60, 381, { { 10, 14 } }, 400), c_Threshold, modelF, &reasons) == KeyVerdict::AtRisk &&
                  reasons.front() == KeyReason::RestsClose,
              "resting 3 short, under half the offset: may type by itself");

        std::vector<uint16_t> wandering = Trace(60, 370, { { 10, 14 } }, 400);
        for (size_t i = 20; i < 60; i += 4)
            wandering[i] = 378;
        Check(VerdictOf(wandering, c_Threshold, modelF, &reasons) == KeyVerdict::AtRisk &&
                  reasons.front() == KeyReason::WandersAtRest,
              "wandering at rest: may type by itself");

        Check(VerdictOf(Trace(60, 400, {}, 0), c_Threshold, modelF, &reasons) == KeyVerdict::Failing &&
                  reasons == std::vector<KeyReason>{ KeyReason::TypesByItself },
              "pressed all along: types by itself");

        Check(VerdictOf(Trace(60, c_Rest, { { 10, 14 } }, 499), c_Threshold, modelF, &reasons) == KeyVerdict::ToCheck &&
                  reasons.back() == KeyReason::TopOfRange,
              "a hold at 499: to check");

        // A released reading inside a hold is part of the press, not a verdict.
        std::vector<uint16_t> flick = Trace(60, c_Rest, { { 10, 16 }, { 30, 36 } }, 400);
        flick[13] = 376;
        Check(VerdictOf(flick, c_Threshold, modelF) == KeyVerdict::Good, "a reading let go inside a hold: still good");

        // The first reading of a press overshoots: left out of the hold, shown.
        std::vector<uint16_t> overshoot = Trace(60, c_Rest, { { 10, 14 } }, 395);
        overshoot[10] = 430;
        const KeyLevels   one{ overshoot, c_Threshold, 0 };
        const BoardHealth health = nazg::JudgeBoard(std::span(&one, 1), modelF, 1);
        Check(health.keys[0].stats.pressMargin == 11, "a hold measured by its median, the overshoot left out");
        Check(health.keys[0].stats.outliers == std::vector<size_t>{ 10 }, "the overshoot marked");

        // A Beam Spring presses the other way: its level falls, below the window -- 0.
        Check(VerdictOf(Trace(60, 452, { { 10, 14 }, { 30, 34 } }, 0), 422, beamSpring) == KeyVerdict::Good,
              "a Beam Spring's hold reads 0: good");
        Check(VerdictOf(Trace(60, 452, { { 10, 14 } }, 421), 422, beamSpring, &reasons) == KeyVerdict::AtRisk &&
                  reasons == std::vector<KeyReason>{ KeyReason::OnlyJustPast },
              "a Beam Spring 1 past its threshold: may miss presses");
    }

    // The keyboard's problems: a weak controller column, one problem shared by many keys.
    void TestBoard()
    {
        std::printf("the whole board\n");

        const LevelScale modelF{ 1, 10 };

        std::vector<std::vector<uint16_t>> traces;
        std::vector<KeyLevels>             keys;
        traces.reserve(40);
        for (int k = 0; k < 40; ++k)
        {
            // Column 3 holds at 5 past; the others at 30 past.
            const int column = k % 8;
            traces.push_back(Trace(60, 374, { { 10, 14 }, { 30, 34 } }, column == 3 ? 389 : 414));
            keys.push_back({ traces.back(), 384, column });
        }
        BoardHealth health = nazg::JudgeBoard(keys, modelF, 8);
        Check(health.typical == 30, "most keys get 30 past");
        Check(health.columns.size() == 1 && health.columns[0].column == 3 && health.columns[0].keys == 5,
              "column 3 said once, its five keys");
        Check(health.notes.empty(), "five keys good but weak: no problem shared");

        // A third of the keys rest close to their threshold: the keyboard's problem.
        for (size_t k = 0; k < traces.size(); k += 3)
        {
            traces[k]     = Trace(60, 381, { { 10, 14 }, { 30, 34 } }, 414);
            keys[k].trace = traces[k];   // its buffer is a new one
        }
        health = nazg::JudgeBoard(keys, modelF, 8);
        Check(health.notes.size() == 1 && health.notes[0].reason == KeyReason::RestsClose && health.notes[0].keys == 14,
              "14 of 40 rest close: said once, above the list");
    }

    void TestToolLevels()
    {
        std::printf("the tool's max, now and min\n");

        nazg::ToolLevels levels;
        Check(!levels.Now() && !levels.Max() && !levels.Min(), "nothing before a reading");

        for (uint16_t level : { 374, 375, 374 })
            levels.Add(level, 384);
        Check(levels.Min() == 374 && !levels.Max() && levels.Now() == 374, "three below: the minimum, their median");

        for (uint16_t level : { 430, 427, 426, 380 })
            levels.Add(level, 384);
        Check(levels.Max() == 427 && levels.Now() == 380, "three past: the maximum, their median -- not the overshoot");

        levels.Clear();
        Check(!levels.Now() && !levels.Max() && !levels.Min(), "cleared");
    }
}

int main(int argc, char** argv)
{
    ConfigureCrtReporting();

    if (argc < 2)
    {
        std::printf("usage: %s <tests/data/leyden-jar>\n", argv[0]);
        return 1;
    }

    try
    {
        TestEveryKey(argv[1]);
        TestHeldKeys(argv[1]);
    }
    catch (const std::exception& failure)
    {
        Check(false, failure.what());
    }
    TestRules();
    TestBoard();
    TestToolLevels();

    return TestResult();
}
