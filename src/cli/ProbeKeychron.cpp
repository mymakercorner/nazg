// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// nazg-probe-keychron: Keychron's own commands against a real board, without a window -- the
// adapter's own code over the real transport, which is the only thing a scripted test cannot
// prove. For checking a tester's board, and for settling the layouts keychron-commands.md still
// marks derived rather than seen.
//
//   nazg-probe-keychron [--matrix <rows>x<columns>] [--profile <n>|all] [--travel <row>,<col>]
//
// --profile all reads every profile: Keychron's Launcher calls them Default, Gaming and Gamepad,
// and what its presets hold -- and whether their name fields are empty -- is what it shows.
//
// READ-ONLY. It sends nothing that changes the board: no write, no save, no calibration, and
// never 0xAA or 0xAB, which the adapter does not even name. --travel polls one key for a few
// seconds so a press can be watched; everything else is one pass.
//
// Without --matrix it takes the matrix from the profile size when that is unambiguous, which it
// rarely is -- the firmware reports rows x columns as one product. Pass it for a known board.
//
// Exit code: 0 when the board answered, 1 when it did not, 2 on a usage error.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "adapters/keychron/NazgKeychronProtocol.h"
#include "async/NazgTask.h"
#include "transport/NazgDeviceChannel.h"
#include "transport/NazgHidTransport.h"

namespace keychron = nazg::keychron;

using nazg::HidDeviceChannel;
using nazg::HidTransport;
using nazg::Task;

namespace
{
    struct Options
    {
        uint8_t  rows      = 0;
        uint8_t  columns   = 0;
        int      profile   = -1;   // -1: the current one; c_AllProfiles: every one
        int      travelRow = -1;
        int      travelCol = -1;
        uint16_t vendorId  = 0;    // 0: ask every board plugged in
        uint16_t productId = 0;
    };

    constexpr int c_AllProfiles = -2;

    // VIA's raw HID interface, the one Keychron's commands ride on.
    constexpr uint16_t c_UsagePage = 0xFF60;
    constexpr uint16_t c_Usage     = 0x61;

    void PrintKeyConfig(const char* label, const keychron::KeyConfig& key)
    {
        const char* mode = key.mode == keychron::KeyMode::Regular        ? "regular"
                           : key.mode == keychron::KeyMode::RapidTrigger ? "rapid trigger"
                                                                         : "follow the profile";
        std::printf("  %-10s %-18s actuation %.1f mm  press %.1f  release %.1f", label, mode,
                    key.actuationPoint / 10.0, key.pressSensitivity / 10.0, key.releaseSensitivity / 10.0);
        if (key.advancedMode != keychron::AdvancedMode::None)
            std::printf("  advanced %u/%u", static_cast<unsigned>(key.advancedMode),
                        static_cast<unsigned>(key.advancedData));
        std::printf("\n");
    }

    // One profile decoded: its global word, its name field byte for byte, every key grouped by the
    // word it holds -- with where each group's keys are -- and the advanced slots in use.
    Task<void> PrintProfile(keychron::Protocol& protocol, const keychron::ProfileGeometry& geometry,
                            const Options& options, uint8_t index)
    {
        std::printf("\n--- profile %u%s ---\n", index, index == geometry.currentProfile ? " (in use)" : "");

        const std::vector<uint8_t> raw = co_await protocol.GetProfileRaw(index, geometry);
        const keychron::Profile    profile =
            keychron::DecodeProfile(raw, options.rows, options.columns, geometry.dksSlots, geometry.socdSlots);

        PrintKeyConfig("global", profile.global);

        // The name field as stored: 30 bytes before the CRC at the profile's end.
        const size_t nameAt = raw.size() - 2 - 30;
        bool         empty  = true;
        for (size_t i = nameAt; i < nameAt + 30; ++i)
            empty = empty && raw[i] == 0;
        if (empty)
            std::printf("  name       (30 zero bytes: the Launcher names it itself)\n");
        else
        {
            std::printf("  name       \"%s\"  bytes:", profile.name.c_str());
            for (size_t i = nameAt; i < nameAt + 30; ++i)
                std::printf(" %02X", raw[i]);
            std::printf("\n");
        }

        // The keys by word, the largest group first.
        std::vector<std::pair<uint32_t, std::vector<size_t>>> groups;
        for (size_t i = 0; i < profile.keys.size(); ++i)
        {
            const uint32_t word  = keychron::EncodeKeyConfig(profile.keys[i]);
            auto           group = std::find_if(groups.begin(), groups.end(),
                                                [&](const auto& known) { return known.first == word; });
            if (group == groups.end())
                groups.push_back({ word, { i } });
            else
                group->second.push_back(i);
        }
        std::sort(groups.begin(), groups.end(),
                  [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });

        std::printf("  the %zu matrix positions, by the word each holds (22 at 0 are no key on a K10 HE ANSI):\n",
                    profile.keys.size());
        for (const auto& [word, keys] : groups)
        {
            char label[24];
            std::snprintf(label, sizeof label, "%3zu x %08X", keys.size(), static_cast<unsigned>(word));
            PrintKeyConfig(label, profile.keys[keys.front()]);
            if (keys.size() <= 24)
            {
                std::printf("               at");
                for (const size_t i : keys)
                    std::printf(" (%zu,%zu)", i / options.columns, i % options.columns);
                std::printf("\n");
            }
        }

        for (size_t i = 0; i < profile.dksSlots.size(); ++i)
        {
            const keychron::DksSlot& slot = profile.dksSlots[i];
            if (slot.IsEmpty())
                continue;
            std::printf("  DKS %2zu  points %.1f %.1f %.1f %.1f  keycodes %04X %04X %04X %04X\n", i,
                        slot.shallowPress / 10.0, slot.shallowRelease / 10.0, slot.deepPress / 10.0,
                        slot.deepRelease / 10.0, slot.keycodes[0], slot.keycodes[1], slot.keycodes[2],
                        slot.keycodes[3]);
        }
        for (size_t i = 0; i < profile.socdSlots.size(); ++i)
        {
            const keychron::SocdSlot& slot = profile.socdSlots[i];
            if (slot.IsEmpty())
                continue;
            std::printf("  SOCD %2zu (%u,%u) + (%u,%u)  type %u\n", i, slot.firstRow, slot.firstColumn,
                        slot.secondRow, slot.secondColumn, static_cast<unsigned>(slot.type));
        }
    }

    Task<int> Probe(HidTransport& transport, Options options)
    {
        const std::vector<nazg::HidDeviceInfo> devices = co_await transport.Enumerate(c_UsagePage, c_Usage);
        if (devices.empty())
        {
            std::fprintf(stderr, "No VIA raw-HID device found. Is a board plugged in?\n");
            co_return 1;
        }

        // Every board is asked, in turn, until one answers 0xA0: a stock VIA or Vial board
        // refuses it, which is the probe doing its job and is worth printing.
        std::printf("--- the boards plugged in ---\n");
        std::optional<keychron::Identity> identity;
        HidDeviceChannel                  channel{ co_await HidDeviceChannel::Open(transport, devices[0].path) };

        for (const nazg::HidDeviceInfo& device : devices)
        {
            if (options.vendorId && (device.vendorId != options.vendorId || device.productId != options.productId))
                continue;

            std::printf("  %04X:%04X  %-28s ", device.vendorId, device.productId, device.product.c_str());
            std::fflush(stdout);

            HidDeviceChannel   candidate{ co_await HidDeviceChannel::Open(transport, device.path) };
            keychron::Protocol probe(candidate);

            const std::optional<keychron::Identity> answer = co_await probe.Probe();
            std::printf("%s\n", answer ? "Keychron" : "refuses 0xA0: not a Keychron");

            if (answer && !identity)
            {
                identity = answer;
                channel  = std::move(candidate);
            }
        }

        if (!identity)
        {
            std::printf("\nNo Keychron board answered.\n");
            co_return 1;
        }
        keychron::Protocol protocol(channel);

        std::printf("\n--- the board ---\n");
        std::printf("  Keychron protocol %u, command set %u\n", identity->protocolVersion, identity->commandSet);
        std::printf("  feature bits 0x%02X, byte 3 0x%02X\n", identity->featureBits, identity->extraBits);
        std::printf("  firmware     %s\n", (co_await protocol.GetFirmwareVersion()).c_str());
        std::printf("  default layer %u\n", co_await protocol.GetDefaultLayer());

        // ---- what answers, against what the bits claim ----
        std::printf("\n--- what answers (the bits under-report: only 0xFF is absent) ---\n");
        const bool hasAnalog = co_await protocol.SupportsAnalogMatrix();
        const bool hasRgb    = co_await protocol.SupportsRgb();
        const bool hasNkro   = co_await protocol.SupportsMisc(keychron::Misc::GetNkro);
        const bool hasDeb    = co_await protocol.SupportsMisc(keychron::Misc::GetDebounce);

        auto line = [](const char* what, bool answers, bool bit) {
            std::printf("  %-16s %-8s  bit says %-5s%s\n", what, answers ? "answers" : "absent",
                        bit ? "yes" : "no", answers != bit ? "   <-- they disagree" : "");
        };
        line("analog matrix", hasAnalog, identity->Has(keychron::Feature::AnalogMatrix));
        line("Keychron RGB", hasRgb, identity->Has(keychron::Feature::KeychronRgb));
        line("NKRO", hasNkro, false);
        line("debounce", hasDeb, identity->Has(keychron::Feature::DynamicDebounce));

        if (!hasAnalog)
        {
            std::printf("\nNo analog matrix: nothing more to read.\n");
            co_return 0;
        }

        // ---- the analog matrix ----
        std::printf("\n--- the analog matrix ---\n");
        std::printf("  version %u\n", co_await protocol.GetAnalogVersion());

        keychron::ProfileGeometry geometry = co_await protocol.GetProfileGeometry();
        const std::vector<keychron::CurvePoint> curve = co_await protocol.GetCurve();
        geometry.fullTravel                           = keychron::EstimateFullTravel(curve);

        std::printf("  profile %u of %u, %u bytes, %u DKS and %u SOCD slots\n", geometry.currentProfile,
                    geometry.profileCount, geometry.profileSize, geometry.dksSlots, geometry.socdSlots);
        std::printf("  curve  ");
        for (const keychron::CurvePoint& point : curve)
            std::printf(" (%u, %u)", point.x, point.y);
        std::printf("\n  full travel %.1f mm (from the curve's last x), default actuation %.1f mm\n",
                    geometry.fullTravel / 10.0, keychron::DefaultActuationPoint(geometry.fullTravel) / 10.0);

        // The matrix: given, or derived when the product has only one plausible split.
        if (options.rows == 0)
        {
            const uint32_t fixed = 2u + 4u + 30u + 19u * geometry.dksSlots + 3u * geometry.socdSlots;
            if (geometry.profileSize > fixed && (geometry.profileSize - fixed) % 4 == 0)
                std::printf("  rows x columns = %u (pass --matrix to split it)\n",
                            (geometry.profileSize - fixed) / 4);
            std::printf("\nNo --matrix given: the profile cannot be decoded. Stopping here.\n");
            co_return 0;
        }

        if (!geometry.Fits(options.rows, options.columns))
        {
            std::fprintf(stderr,
                         "\nThe board reports %u bytes, but %ux%u with %u and %u slots needs %u.\n"
                         "The definition and the firmware disagree: nothing read could be placed.\n",
                         geometry.profileSize, options.rows, options.columns, geometry.dksSlots,
                         geometry.socdSlots,
                         keychron::ProfileSizeFor(options.rows, options.columns, geometry.dksSlots,
                                                  geometry.socdSlots));
            co_return 1;
        }
        std::printf("  the size matches %ux%u\n", options.rows, options.columns);

        // ---- one profile, or all of them ----
        if (options.profile == c_AllProfiles)
        {
            for (uint8_t index = 0; index < geometry.profileCount; ++index)
                co_await PrintProfile(protocol, geometry, options, index);
        }
        else
        {
            const uint8_t wanted = options.profile < 0 ? geometry.currentProfile : static_cast<uint8_t>(options.profile);
            if (wanted >= geometry.profileCount)
            {
                std::fprintf(stderr, "\nThere are %u profiles, 0 to %u.\n", geometry.profileCount,
                             geometry.profileCount - 1u);
                co_return 2;
            }
            co_await PrintProfile(protocol, geometry, options, wanted);
        }

        // ---- one key, live ----
        if (options.travelRow >= 0)
        {
            const uint8_t row = static_cast<uint8_t>(options.travelRow);
            const uint8_t col = static_cast<uint8_t>(options.travelCol);

            const keychron::CalibratedValue calibration = co_await protocol.GetCalibratedValue(row, col);
            std::printf("\n--- key (%u, %u) ---\n", row, col);
            if (calibration.IsAbsent())
            {
                std::printf("  no key at that matrix position (rest 0, bottom 0)\n");
                co_return 0;
            }
            std::printf("  calibration: rest %u, bottom %u, scale %.6f\n", calibration.restValue,
                        calibration.bottomValue, static_cast<double>(calibration.scaleFactor));

            std::printf("  polling for 8 s -- press it to the bottom\n");
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
            uint8_t    peak = 0, peakSixtieths = 0;
            while (std::chrono::steady_clock::now() < until)
            {
                const keychron::RealtimeTravel travel = co_await protocol.GetRealtimeTravel(row, col);
                if (travel.travel > peak)
                {
                    peak          = travel.travel;
                    peakSixtieths = travel.travelSixtieths;
                }
            }
            std::printf("  deepest seen: %.1f mm (%u in 1/60 mm)\n", peak / 10.0, peakSixtieths);
            std::printf("  the curve estimated %.1f mm -- %s\n", geometry.fullTravel / 10.0,
                        peak == geometry.fullTravel ? "they agree" : "THEY DISAGREE");
        }
        co_return 0;
    }

    int Usage()
    {
        std::fprintf(stderr,
                     "usage: nazg-probe-keychron [--matrix <rows>x<columns>] [--profile <n>|all]\n"
                     "                           [--travel <row>,<col>] [--device <vid>:<pid>]\n");
        return 2;
    }
}

int main(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--matrix" && i + 1 < argc)
        {
            unsigned rows = 0, columns = 0;
            if (std::sscanf(argv[++i], "%ux%u", &rows, &columns) != 2 || rows == 0 || columns == 0 ||
                rows > 255 || columns > 255)
                return Usage();
            options.rows    = static_cast<uint8_t>(rows);
            options.columns = static_cast<uint8_t>(columns);
        }
        else if (argument == "--profile" && i + 1 < argc)
        {
            const std::string which = argv[++i];
            options.profile         = which == "all" ? c_AllProfiles : std::atoi(which.c_str());
        }
        else if (argument == "--device" && i + 1 < argc)
        {
            unsigned vendor = 0, product = 0;
            if (std::sscanf(argv[++i], "%x:%x", &vendor, &product) != 2 || vendor > 0xFFFF || product > 0xFFFF)
                return Usage();
            options.vendorId  = static_cast<uint16_t>(vendor);
            options.productId = static_cast<uint16_t>(product);
        }
        else if (argument == "--travel" && i + 1 < argc)
        {
            unsigned row = 0, column = 0;
            if (std::sscanf(argv[++i], "%u,%u", &row, &column) != 2 || row > 254 || column > 254)
                return Usage();
            options.travelRow = static_cast<int>(row);
            options.travelCol = static_cast<int>(column);
        }
        else
        {
            return Usage();
        }
    }

    try
    {
        HidTransport transport;
        if (!transport.IsAvailable())
        {
            std::fprintf(stderr, "No HID transport available.\n");
            return 1;
        }

        Task<int> task = Probe(transport, options);
        while (!task.IsDone())
        {
            transport.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return task.TakeResult();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "\n%s\n", error.what());
        return 1;
    }
}
