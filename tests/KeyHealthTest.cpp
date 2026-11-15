// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Pins the Signal levels' judgment (model/NazgKeyHealth.h): on Rico's two F77 recordings, where
// every key works -- so nothing may be at risk or failing -- and on made-up traces, one per rule; then
// the keys at rest while another is pressed, on the recording and on a made-up board.
//
// The recordings' folder is the first argument (tests/data/leyden-jar, README.md there).
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "model/NazgKeyHealth.h"

#include "adapters/NazgXz.h"
#include "adapters/leydenjar/NazgLeydenJarRecording.h"
#include "report/NazgDiagnosticsReport.h"
#include "adapters/via/NazgKeyboardDefinition.h"
#include "files/NazgFiles.h"
#include "model/NazgKeyboard.h"

#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
        int                   row       = 0;   // the controller's
        int                   bin       = 0;
        nazg::via::DefinitionKey geometry;     // where it is drawn, its QMK row and column
    };

    struct Recording
    {
        std::vector<RecordedKey> keys;
        LevelScale               scale;
        int                      columns = 0;

        // For a report: the whole recording, the definition's name and every key it draws.
        nlohmann::json                         json;
        std::string                            name;
        std::vector<nazg::via::DefinitionKey>  drawn;
    };

    // A recording's JSON, XZ-compressed as those kept here or plain as Save readings... writes it.
    nlohmann::json ReadRecording(const std::filesystem::path& file)
    {
        const std::optional<std::vector<uint8_t>> packed = nazg::ReadWholeFile(file);
        if (!packed)
            throw std::runtime_error("cannot read " + file.string());
        static const uint8_t c_XzMagic[] = { 0xFD, '7', 'z', 'X', 'Z', 0x00 };
        if (packed->size() >= 6 && std::equal(std::begin(c_XzMagic), std::end(c_XzMagic), packed->begin()))
        {
            const std::vector<uint8_t> text = nazg::DecompressXz(*packed, 64u << 20, "the recording");
            return nlohmann::json::parse(text.begin(), text.end());
        }
        return nlohmann::json::parse(packed->begin(), packed->end());
    }

    // The drawn keys at choice 0 of every layout option, as the section judges them: each switch
    // once, mapped to the controller's position, its threshold its bin's.
    Recording Load(const std::filesystem::path& file)
    {
        const nlohmann::json json = ReadRecording(file);

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

        recording.json = json;
        recording.name = definition.name;
        std::set<std::pair<int, int>> taken;
        for (const nazg::via::DefinitionKey& key : nazg::PlaceKeys(definition, std::vector<uint8_t>(groups, 0)))
        {
            if (!key.decal)
                recording.drawn.push_back(key);
            if (!key.IsSwitch() || !taken.insert({ key.row, key.column }).second)
                continue;
            const int column = mappingColumns.at(key.column);
            const int row    = mappingRows.at(key.row);

            RecordedKey recorded;
            recorded.keycode   = json["keymap"].value(std::to_string(key.row) + "," + std::to_string(key.column), "");
            recorded.threshold = static_cast<uint16_t>(thresholds.at(bins.at(column).at(row)));
            recorded.column    = column;
            recorded.row       = row;
            recorded.bin       = bins.at(column).at(row);
            recorded.geometry  = key;
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

        // Its coupling: a press moves the keys on the same controller column, the next row, by 2 to 4
        // towards their threshold -- 56 keys pressed twice or more alone -- and none comes close. A
        // first measure, from a single press, also found a few on the same controller row: noise.
        // (ui-design.md, "Signal levels, fourth round").
        int    most = 0, closest = 99;
        size_t onColumn = 0, onRow = 0;
        for (const nazg::KeyCoupling& coupling : health.couplings)
        {
            const RecordedKey& pressed = recording.keys[coupling.pressed];
            const RecordedKey& moved   = recording.keys[coupling.moved];
            if (pressed.column == moved.column && std::abs(pressed.row - moved.row) == 1)
                ++onColumn;
            else if (pressed.row == moved.row)
                ++onRow;
            else
                std::printf("  moved elsewhere: %s by %s, %d\n", moved.keycode.c_str(), pressed.keycode.c_str(), coupling.move);
            most    = std::max(most, coupling.move);
            closest = std::min(closest, coupling.left);
        }
        std::printf("  %zu keys moved by another's press -- %zu on its column, the next row, %zu on its row -- by %d at most, "
                    "%d from a threshold at the closest\n",
                    health.couplings.size(), onColumn, onRow, most, closest);
        Check(health.couplings.size() >= 50, "pressing a key moves others: the board's coupling found");
        Check(onColumn == health.couplings.size(), "every key moved is the next row on the pressed key's controller column");
        Check(most <= 5 && closest >= nazg::c_SafeMargin, "none moved close to its threshold");
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

        Check(VerdictOf(Trace(60, 382, { { 10, 14 } }, 400), c_Threshold, modelF, &reasons) == KeyVerdict::AtRisk &&
                  reasons.front() == KeyReason::RestsClose,
              "resting 2 short, under 3: may type by itself");

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

        // An expert's margin: 1 past is safe enough when the press margin is 1.
        {
            const std::vector<uint16_t> trace = Trace(60, c_Rest, { { 10, 14 }, { 30, 34 } }, 385);
            const KeyLevels             key{ trace, c_Threshold, 0 };
            Check(nazg::JudgeBoard(std::span(&key, 1), modelF, 1, { 3, 1 }).keys[0].verdict == KeyVerdict::Good,
                  "1 past with a press margin of 1: good");
        }

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
            traces[k]     = Trace(60, 382, { { 10, 14 }, { 30, 34 } }, 414);
            keys[k].trace = traces[k];   // its buffer is a new one
        }
        health = nazg::JudgeBoard(keys, modelF, 8);
        Check(health.notes.size() == 1 && health.notes[0].reason == KeyReason::RestsClose && health.notes[0].keys == 14,
              "14 of 40 rest close: said once, above the list");
    }

    // A made-up board, Model F bins at rest 374, threshold 384, every key quiet but for what is said.
    void TestCoupling()
    {
        std::printf("keys at rest while another is pressed\n");

        const LevelScale modelF{ 1, 10 };
        constexpr size_t c_Length = 400;
        enum : size_t { A, B, C, E, F, G, H, I, J, K, L, Count };

        std::vector<std::vector<uint16_t>> traces(Count);
        traces[A] = Trace(c_Length, 374, { { 10, 14 }, { 30, 34 }, { 50, 54 } }, 414);   // pressed three times, alone
        traces[B] = Trace(c_Length, 374, { { 70, 74 } }, 414);                            // pressed once itself
        traces[C] = Trace(c_Length, 374, {}, 0);                                          // never pressed
        traces[E] = Trace(c_Length, 374, { { 90, 94 } }, 414);
        traces[F] = Trace(c_Length, 374, { { 100, 104 } }, 414);                          // F and G together: a chord
        traces[G] = Trace(c_Length, 374, { { 100, 104 } }, 414);
        traces[H] = Trace(c_Length, 374, { { 110, 114 } }, 414);
        traces[I] = Trace(c_Length, 374, {}, 0);                                          // never pressed
        const std::pair<size_t, size_t> pressesOfA[] = { { 10, 14 }, { 30, 34 }, { 50, 54 } };
        for (const auto& [first, last] : pressesOfA)
            for (size_t i = first; i <= last; ++i)
            {
                traces[B][i] = 382;   // A brings B to 2 from its threshold
                traces[C][i] = 386;   // and takes C 2 past it
                traces[E][i] = 377;   // and moves E by 3: safe
                traces[I][i] = 381;   // and I by 7, 3 left: safe, but clearly off its rest
            }
        for (size_t i = 100; i <= 104; ++i)
            traces[H][i] = 386;   // past its threshold during the chord: nothing can be told

        // What must not be called moved (Rico, 2026-11-15: pressing 5 dashed many keys, different
        // ones each time). J: moved by 3 in one of A's presses only -- one odd press.
        traces[J] = Trace(c_Length, 374, {}, 0);
        for (size_t i = 30; i <= 34; ++i)
            traces[J][i] = 377;
        // K: wanders at rest from 371 to 377, and reads 378 during A's presses.
        traces[K] = Trace(c_Length, 374, {}, 0);
        for (size_t i = 0; i < c_Length; ++i)
            traces[K][i] = static_cast<uint16_t>(371 + (i * 5 % 7));
        for (const auto& [first, last] : pressesOfA)
            for (size_t i = first; i <= last; ++i)
                traces[K][i] = 378;
        // L: its resting level drifts up by 2 around A's later presses, and A moves it by 1 on top.
        traces[L] = Trace(c_Length, 374, {}, 0);
        for (size_t i = 27; i <= 37; ++i)
            traces[L][i] = 376;
        for (size_t i = 47; i <= 57; ++i)
            traces[L][i] = 376;
        for (const auto& [first, last] : pressesOfA)
            for (size_t i = first; i <= last; ++i)
                traces[L][i] = static_cast<uint16_t>(first == 10 ? 375 : 377);

        std::vector<KeyLevels> keys;
        for (const std::vector<uint16_t>& trace : traces)
            keys.push_back({ trace, 384, 0 });
        const BoardHealth health = nazg::JudgeBoard(keys, modelF, 1);

        const auto find = [&health](size_t pressed, size_t moved) -> const nazg::KeyCoupling*
        {
            for (const nazg::KeyCoupling& coupling : health.couplings)
                if (coupling.pressed == pressed && coupling.moved == moved)
                    return &coupling;
            return nullptr;
        };
        const nazg::KeyCoupling* ab = find(A, B);
        const nazg::KeyCoupling* ac = find(A, C);
        const nazg::KeyCoupling* ae = find(A, E);
        Check(health.couplings.size() == 4, "four keys moved: B, C, E and I, by A");
        Check(ab && ab->move == 8 && ab->left == 2 && ab->presses == 3, "B moved by 8, 2 left, over A's three presses");
        Check(ac && ac->left == -2, "C taken 2 past");
        Check(ae && ae->move == 3 && ae->left == 7, "E moved by 3, 7 left");
        Check(!health.couplings.empty() && health.couplings.front().moved == C, "the closest to its threshold first");

        const nazg::JudgedKey& b = health.keys[B];
        Check(b.verdict == KeyVerdict::AtRisk && b.reasons == std::vector<KeyReason>{ KeyReason::NearsWhenPressed } &&
                  b.movedBy == size_t{ A },
              "B at risk: may type by itself when A is pressed");

        const nazg::JudgedKey& c = health.keys[C];
        Check(c.verdict == KeyVerdict::Failing && c.reasons == std::vector<KeyReason>{ KeyReason::TypesWhenPressed } &&
                  c.movedBy == size_t{ A },
              "C failing: types by itself when A is pressed");

        // Moved three times short of its threshold, I would be a key whose presses never reach: they
        // are A's, set aside -- I is only not pressed yet.
        const nazg::JudgedKey& i = health.keys[I];
        Check(i.verdict == KeyVerdict::ToDo && i.toDo == KeyToDo::Press && i.stats.presses.empty() && !i.movedBy,
              "I moved by A, never pressed: to do, not failing");
        Check(b.stats.presses.size() == 1, "B's moves set aside, its own press kept");

        Check(health.keys[E].verdict == KeyVerdict::Good && !health.keys[E].movedBy, "E moved but safe: good");
        Check(!find(A, J), "J moved in one press of A's three: not moved");
        Check(!find(A, K), "K wandering by 6 at rest, 378 during A's presses: not moved");
        Check(!find(A, L), "L drifting up around A's presses, 1 more on top: not moved");

        // A key pressed once alone says nothing yet.
        {
            std::vector<std::vector<uint16_t>> two = { Trace(60, 374, { { 10, 14 } }, 414), Trace(60, 374, {}, 0) };
            for (size_t i = 10; i <= 14; ++i)
                two[1][i] = 382;
            const KeyLevels   pair[] = { { two[0], 384, 0 }, { two[1], 384, 0 } };
            const BoardHealth once   = nazg::JudgeBoard(pair, modelF, 1);
            Check(once.couplings.empty() && once.keys[1].verdict != KeyVerdict::AtRisk,
                  "one press alone: nothing said yet, however far it moved a key");
        }
        Check(health.keys[H].verdict == KeyVerdict::Good, "H past its threshold during a chord: nothing said");
        Check(health.keys[A].verdict == KeyVerdict::Good && health.keys[F].verdict == KeyVerdict::Good, "A and F good");
        Check(health.keys[A].pressesAlone == 3 && health.keys[B].pressesAlone == 1 && health.keys[F].pressesAlone == 0 &&
                  health.keys[C].pressesAlone == 0,
              "presses alone counted: A's three, B's one, none for F's chord or C, never pressed");

        // An expert's rest margin of 1: 2 left is enough.
        const BoardHealth relaxed = nazg::JudgeBoard(keys, modelF, 1, { 1, 3 });
        Check(relaxed.keys[B].verdict == KeyVerdict::Good && !relaxed.keys[B].movedBy, "B good with a rest margin of 1");
    }

    // Rico's first session saved by Save readings... (2026-11-15): 5 pressed twice, D once. Live, a key
    // pressed once dashed nothing and looked broken: what its press moves waits for a second press.
    void TestFiveAndD(const std::filesystem::path& folder)
    {
        std::printf("the F77 today, 5 pressed twice and D once\n");

        const Recording   recording = Load(folder / "f77-five-and-d-2026-11-15.json.xz");
        const BoardHealth health    = Judge(recording);

        std::map<std::string, size_t> index;
        for (size_t k = 0; k < recording.keys.size(); ++k)
            index[recording.keys[k].keycode] = k;
        const auto moves = [&](const char* pressed, const char* moved) -> const nazg::KeyCoupling*
        {
            for (const nazg::KeyCoupling& coupling : health.couplings)
                if (coupling.pressed == index[pressed] && coupling.moved == index[moved])
                    return &coupling;
            return nullptr;
        };

        const nazg::KeyCoupling* y   = moves("KC_5", "KC_Y");
        const nazg::KeyCoupling* six = moves("KC_5", "KC_6");
        Check(y && y->move == 3 && six && six->move == 2, "5 moves Y by 3 and 6 by 2, as in October");
        size_t byFive = 0, byD = 0;
        for (const nazg::KeyCoupling& coupling : health.couplings)
        {
            byFive += coupling.pressed == index["KC_5"];
            byD += coupling.pressed == index["KC_D"];
        }
        Check(byFive == 2, "5 moves those two only");
        Check(health.keys[index["KC_D"]].pressesAlone == 1 && byD == 0, "D, pressed once: nothing said of what it moves yet");
        Check(CountOf(health, KeyVerdict::Failing) == 0 && CountOf(health, KeyVerdict::AtRisk) == 0, "nothing failing or at risk");
    }

    // The saved report, made as the section makes it, from Rico's session: one HTML file with every
    // key's detail and the readings inside, which read back as the recording. With
    // NAZG_REPORT_OUT set, it is written there to be looked at.
    void TestReport(const std::filesystem::path& folder)
    {
        std::printf("the report, from the F77 today\n");

        const Recording   recording = Load(folder / "f77-five-and-d-2026-11-15.json.xz");
        const BoardHealth health    = Judge(recording);
        const nlohmann::json& info  = recording.json["info"];

        nazg::DiagnosticsReport report;
        report.title   = recording.name;
        report.usb     = recording.json["board"].get<std::string>();
        report.savedBy = "Nazg (test)";
        report.savedAt = "15 November 2026 at 21:29";
        nazg::leydenjar::ControllerInfo& controller = report.info;
        controller.version                   = { 0, 9, 1 };
        controller.details.logicalRows       = info["logicalRows"].get<uint8_t>();
        controller.details.logicalColumns    = info["logicalColumns"].get<uint8_t>();
        controller.details.controllerRows    = info["controllerRows"].get<uint8_t>();
        controller.details.controllerColumns = info["controllerColumns"].get<uint8_t>();
        controller.thresholds                = info["thresholds"].get<std::vector<uint16_t>>();
        controller.referenceLevels           = info["referenceLevels"].get<std::vector<uint16_t>>();
        controller.bins                      = info["bins"].get<std::vector<std::vector<uint8_t>>>();
        report.scale  = recording.scale;
        report.health = health;

        for (const RecordedKey& key : recording.keys)
        {
            nazg::ReportKey one;
            one.name             = key.keycode.rfind("KC_", 0) == 0 ? key.keycode.substr(3) : key.keycode;
            one.row              = key.geometry.row;
            one.column           = key.geometry.column;
            one.controllerRow    = static_cast<uint8_t>(key.row);
            one.controllerColumn = static_cast<uint8_t>(key.column);
            one.isMapped         = true;
            one.threshold        = key.threshold;
            one.bin              = static_cast<uint8_t>(key.bin);
            report.keys.push_back(one);
        }
        for (const nazg::via::DefinitionKey& drawn : recording.drawn)
        {
            nazg::ReportShape shape;
            shape.geometry = drawn;
            for (size_t k = 0; k < recording.keys.size(); ++k)
                if (drawn.IsSwitch() && recording.keys[k].geometry.row == drawn.row && recording.keys[k].geometry.column == drawn.column)
                    shape.key = k;
            report.shapes.push_back(shape);
        }

        const size_t rows = controller.details.controllerRows;
        report.traces.resize(size_t{ controller.details.controllerColumns } * rows);
        for (const nlohmann::json& reading : recording.json["readings"])
        {
            report.times.push_back(reading["t"].get<double>());
            const auto levels = reading["levels"].get<std::vector<std::vector<uint16_t>>>();
            for (size_t column = 0; column < levels.size(); ++column)
                for (size_t row = 0; row < levels[column].size(); ++row)
                    report.traces[column * rows + row].push_back(levels[column][row]);
        }
        report.readings = recording.json.dump();

        const std::string html = nazg::DiagnosticsReportHtml(report);
        const auto count = [&html](const std::string& what)
        {
            size_t n = 0;
            for (size_t at = html.find(what); at != std::string::npos; at = html.find(what, at + what.size()))
                ++n;
            return n;
        };
        std::printf("  %zu bytes, %zu of them the readings\n", html.size(), report.readings.size());
        Check(html.rfind("<!DOCTYPE html>", 0) == 0 && html.find("</html>") != std::string::npos, "one HTML page");
        Check(count("<section class=\"detail\"") == recording.keys.size(), "every key's detail");
        Check(count("<div class=\"view\"") == 4, "the board and the controller, plain and with Levels");
        Check(html.find("data-moves=\"") != std::string::npos && html.find(":safe") != std::string::npos,
              "what a key's press moves, for the dashes");
        Check(nlohmann::json::parse(nazg::ReadingsOfReport(html)) == recording.json, "its readings read back as the recording");

        if (const char* out = std::getenv("NAZG_REPORT_OUT"))
            if (FILE* file = std::fopen(out, "wb"))
            {
                std::fwrite(html.data(), 1, html.size(), file);
                std::fclose(file);
                std::printf("  written to %s\n", out);
            }
    }

    // What Save readings... writes is a recording these tests read: the F77's, taken apart into what
    // the section holds and written again, says the same.
    void TestRecordingWritten(const std::filesystem::path& folder)
    {
        std::printf("a recording written as the section writes it\n");

        const nlohmann::json original = ReadRecording(folder / "f77-held-keys-2026-10-17.json.xz");
        const nlohmann::json& info    = original["info"];

        nazg::leydenjar::Recording recording;
        recording.board = original["board"].get<std::string>();
        nazg::leydenjar::ControllerInfo& controller = recording.info;
        int major = 0, mid = 0, minor = 0;
        std::sscanf(info["version"].get<std::string>().c_str(), "%d.%d.%d", &major, &mid, &minor);
        controller.version                   = { static_cast<uint8_t>(major), static_cast<uint8_t>(mid), static_cast<uint16_t>(minor) };
        controller.details.logicalRows       = info["logicalRows"].get<uint8_t>();
        controller.details.logicalColumns    = info["logicalColumns"].get<uint8_t>();
        controller.details.controllerRows    = info["controllerRows"].get<uint8_t>();
        controller.details.controllerColumns = info["controllerColumns"].get<uint8_t>();
        controller.details.technology        = info["beamSpring"].get<bool>() ? nazg::leydenjar::SwitchTechnology::BeamSpring
                                                                              : nazg::leydenjar::SwitchTechnology::ModelF;
        controller.mapping.wiring            = info["wiring"].get<uint8_t>();
        controller.mapping.columns           = info["mappingColumns"].get<std::vector<uint8_t>>();
        controller.mapping.rows              = info["mappingRows"].get<std::vector<uint8_t>>();
        controller.thresholds                = info["thresholds"].get<std::vector<uint16_t>>();
        controller.referenceLevels           = info["referenceLevels"].get<std::vector<uint16_t>>();
        controller.bins                      = info["bins"].get<std::vector<std::vector<uint8_t>>>();
        controller.isLeftHalf                = info["isLeftHalf"].get<bool>();

        const std::string definition = original["definition"].dump();
        recording.definition.assign(definition.begin(), definition.end());
        for (const auto& [cell, name] : original["keymap"].items())
            recording.keymap.emplace_back(cell, name.get<std::string>());

        const size_t rows = controller.details.controllerRows;
        recording.traces.resize(size_t{ controller.details.controllerColumns } * rows);
        for (const nlohmann::json& reading : original["readings"])
        {
            recording.times.push_back(reading["t"].get<double>());
            const auto levels = reading["levels"].get<std::vector<std::vector<uint16_t>>>();
            for (size_t column = 0; column < levels.size(); ++column)
                for (size_t row = 0; row < levels[column].size(); ++row)
                    recording.traces[column * rows + row].push_back(levels[column][row]);
        }

        const std::string    text    = nazg::leydenjar::RecordingJson(recording);
        const nlohmann::json written = nlohmann::json::parse(text);
        Check(written["board"] == original["board"] && written["info"] == original["info"], "the board and its controller");
        Check(written["definition"] == original["definition"] && written["keymap"] == original["keymap"],
              "its definition and layer 0");

        bool same = written["readings"].size() == original["readings"].size();
        for (size_t reading = 0; same && reading < written["readings"].size(); ++reading)
            same = written["readings"][reading]["levels"] == original["readings"][reading]["levels"] &&
                   std::abs(written["readings"][reading]["t"].get<double>() - original["readings"][reading]["t"].get<double>()) < 1e-6;
        Check(same, "every reading, at its time");
        std::printf("  %zu readings, %zu bytes\n", recording.times.size(), text.size());
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
        TestRecordingWritten(argv[1]);
        TestFiveAndD(argv[1]);
        TestReport(argv[1]);
    }
    catch (const std::exception& failure)
    {
        Check(false, failure.what());
    }
    TestRules();
    TestBoard();
    TestCoupling();
    TestToolLevels();

    return TestResult();
}
