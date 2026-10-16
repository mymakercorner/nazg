// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The keyboard list read again after a device came or went: what each path answered is kept,
// so only new paths are asked -- the open board, listed before, never is.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "board/NazgDeviceList.h"

#include "TestSupport.h"

#include <string>
#include <vector>

using namespace nazg;

namespace
{
    HidDeviceInfo Device(const std::string& path)
    {
        HidDeviceInfo device;
        device.path = path;
        return device;
    }

    void TestKeptAcrossAReorder()
    {
        std::printf("answers follow their path, wherever it is listed now\n");

        const std::vector<HidDeviceInfo> before    = { Device("a"), Device("b"), Device("c") };
        const std::vector<std::string>   protocols = { "Vial", "VIA", "no answer" };
        const std::vector<HidDeviceInfo> now       = { Device("c"), Device("a"), Device("b") };

        const std::vector<std::string> kept = KeptProtocols(before, protocols, now);
        Check(kept.size() == 3, "one entry per device now listed");
        Check(kept[0] == "no answer", "a board that did not answer is not asked again");
        Check(kept[1] == "Vial" && kept[2] == "VIA", "the others keep theirs");
    }

    void TestNewAndGone()
    {
        std::printf("a new path is to be asked; a gone one is dropped\n");

        const std::vector<HidDeviceInfo> before    = { Device("open"), Device("gone") };
        const std::vector<std::string>   protocols = { "VIA", "Vial" };
        const std::vector<HidDeviceInfo> now       = { Device("open"), Device("new") };

        const std::vector<std::string> kept = KeptProtocols(before, protocols, now);
        Check(kept[0] == "VIA", "the open board keeps its answer, so it is never probed");
        Check(kept[1].empty(), "the new board is still to be asked");

        Check(IsListed(now, "open"), "the open board is still listed");
        Check(!IsListed(now, "gone"), "the unplugged one is not");
    }

    void TestStillUnasked()
    {
        std::printf("a device not asked yet stays to be asked\n");

        // Listed by a refresh whose probing had not reached it: its answer is empty, kept empty.
        const std::vector<HidDeviceInfo> before    = { Device("a"), Device("b") };
        const std::vector<std::string>   protocols = { "VIA" };
        const std::vector<HidDeviceInfo> now       = { Device("b") };

        const std::vector<std::string> kept = KeptProtocols(before, protocols, now);
        Check(kept.size() == 1 && kept[0].empty(), "no answer recorded means none kept");
        Check(KeptProtocols({}, {}, now)[0].empty(), "the first listing asks everything");
    }
}

int main()
{
    ConfigureCrtReporting();

    TestKeptAcrossAReorder();
    TestNewAndGone();
    TestStillUnasked();

    return TestResult();
}
