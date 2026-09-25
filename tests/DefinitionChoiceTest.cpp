// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Choosing a definition on connect, as plain values: which choice a device finds, which
// candidate draws it without asking, and the order a picker offers them in.
//
// The boards are the design's own example: a KBDfans D60B and a CannonKeys Instant60 both
// report 0xCA04:0x1600.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "library/NazgDefinitionChoice.h"

#include "TestSupport.h"

#include <string>
#include <vector>

using nazg::DefinitionCandidate;
using nazg::DefinitionChoice;
using nazg::DefinitionRef;
using nazg::DeviceIdentity;

namespace
{
    DeviceIdentity D60b(uint16_t release = 0x0001)
    {
        return { 0xCA04, 0x1600, "KBDfans", "D60B", release, "" };
    }

    DeviceIdentity Instant60()
    {
        return { 0xCA04, 0x1600, "CannonKeys", "Instant60", 0x0001, "" };
    }

    DefinitionCandidate Candidate(DefinitionRef ref, const char* name)
    {
        DefinitionCandidate candidate;
        candidate.ref             = std::move(ref);
        candidate.definition.name = name;
        return candidate;
    }

    void TestRefs()
    {
        std::printf("definition refs\n");

        Check(nazg::FormatDefinitionRef(DefinitionRef::User(7)) == "user:7", "a user definition is user:<entry>");
        Check(nazg::FormatDefinitionRef(DefinitionRef::Official("v3/3389265408")) == "official:v3/3389265408",
              "an official one is official:<path in the bundle>");

        Check(nazg::ParseDefinitionRef("user:7") == DefinitionRef::User(7), "user:7 reads back");
        Check(nazg::ParseDefinitionRef("official:v3/3389265408") == DefinitionRef::Official("v3/3389265408"),
              "and so does an official path");

        const char* bad[] = { "", "user:", "user:7x", "user:-1", "official:", "community:3", "7" };
        bool        none  = true;
        for (const char* text : bad)
            none = none && !nazg::ParseDefinitionRef(text);
        Check(none, "anything else is refused");
    }

    void TestFindChoice()
    {
        std::printf("finding a choice\n");

        std::vector<DefinitionChoice> choices = { { D60b(), DefinitionRef::User(7) },
                                                  { Instant60(), DefinitionRef::Official("v3/3389265408") } };

        Check(nazg::FindChoice(choices, D60b()) == &choices[0], "a D60B finds its own choice");
        Check(nazg::FindChoice(choices, Instant60()) == &choices[1], "an Instant60 on the same id finds its own");

        Check(nazg::FindChoice(choices, D60b(0x0002)) == &choices[0],
              "a firmware update that bumps the release number keeps the choice");

        DeviceIdentity withSerial = D60b();
        withSerial.serial         = "A1B2";
        Check(nazg::FindChoice(choices, withSerial) == &choices[0], "and so does a serial the choice did not have");

        DeviceIdentity other = D60b();
        other.product        = "D60B Hot-swap";
        Check(nazg::FindChoice(choices, other) == nullptr, "a different product string finds nothing");

        other           = D60b();
        other.productId = 0x1601;
        Check(nazg::FindChoice(choices, other) == nullptr, "nor does a different product id");

        // Two revisions of one board, same strings, each with its own choice.
        choices.push_back({ D60b(0x0002), DefinitionRef::User(9) });
        Check(nazg::FindChoice(choices, D60b(0x0001)) == &choices[0], "an exact match beats a newer near one");
        Check(nazg::FindChoice(choices, D60b(0x0002)) == &choices[2], "each revision finds its own");
        Check(nazg::FindChoice(choices, D60b(0x0003)) == &choices[2], "a third release takes the newest choice");
    }

    void TestResolve()
    {
        std::printf("resolving the candidates\n");

        const std::vector<DefinitionCandidate> none;
        Check(!nazg::ResolveCandidate(none, std::nullopt), "no candidate: ask -- or rather, say to import one");

        const std::vector<DefinitionCandidate> one = { Candidate(DefinitionRef::Official("v3/1"), "Instant60") };
        Check(nazg::ResolveCandidate(one, std::nullopt) == 0u, "one candidate is used without asking");
        Check(nazg::ResolveCandidate(one, DefinitionRef::User(7)) == 0u,
              "even when the remembered one has gone");

        const std::vector<DefinitionCandidate> two = { Candidate(DefinitionRef::User(7), "D60B"),
                                                       Candidate(DefinitionRef::Official("v3/1"), "Instant60") };
        Check(!nazg::ResolveCandidate(two, std::nullopt), "two and no choice: ask");
        Check(nazg::ResolveCandidate(two, DefinitionRef::Official("v3/1")) == 1u, "two and a choice: the choice");
        Check(!nazg::ResolveCandidate(two, DefinitionRef::User(8)), "two and a choice no longer among them: ask");
    }

    void TestRank()
    {
        std::printf("ranking the candidates\n");

        std::vector<DefinitionCandidate> candidates = { Candidate(DefinitionRef::Official("v3/1"), "D60B"),
                                                        Candidate(DefinitionRef::User(3), "Instant 60"),
                                                        Candidate(DefinitionRef::User(4), "KBDfans D-60B"),
                                                        Candidate(DefinitionRef::User(5), "Something") };

        nazg::RankCandidates(candidates, "D60B");
        Check(candidates[0].ref == DefinitionRef::User(4), "a user definition whose name matches the product comes first");
        Check(candidates[1].ref == DefinitionRef::User(3) && candidates[2].ref == DefinitionRef::User(5),
              "then the other user definitions, in their order");
        Check(candidates[3].ref.kind == DefinitionRef::Kind::Official,
              "and the official one last, even when its name matches");

        std::vector<DefinitionCandidate> unnamed = { Candidate(DefinitionRef::User(1), "A"),
                                                     Candidate(DefinitionRef::User(2), "B") };
        nazg::RankCandidates(unnamed, "");
        Check(unnamed[0].ref == DefinitionRef::User(1), "an empty product string matches nothing and moves nothing");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRefs();
    TestFindChoice();
    TestResolve();
    TestRank();

    return TestResult();
}
