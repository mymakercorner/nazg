// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// A board Linux does not allow: the rule file Nazg writes for it, and the commands that write
// or delete it -- what runs as root, so a board's name must never reach the shell as code.
//
// Registered with CTest:  ctest --test-dir build_VS2022 -C Debug --output-on-failure

#include "permission/NazgBoardPermission.h"

#include "TestSupport.h"

#include <string>
#include <vector>

using namespace nazg;

namespace
{
    constexpr uint16_t c_Vendor  = 0x1209;   // the F77 and the B104 (pid.codes)
    constexpr uint16_t c_Product = 0x4704;

    bool Contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    void TestRuleFile()
    {
        std::printf("one file per board, named after its USB id\n");

        Check(PermissionRulePath(c_Vendor, c_Product) == "/etc/udev/rules.d/60-nazg-1209-4704.rules",
              "the F77's file, sorting before 73-seat-late.rules");
        Check(PermissionRulePath(0x21C0, 0x6173) == "/etc/udev/rules.d/60-nazg-21c0-6173.rules",
              "hex in lowercase, as sysfs writes the ids");

        const std::vector<std::string> lines = PermissionRuleLines("F77 Model F Keyboard", c_Vendor, c_Product);
        Check(lines.size() == 2, "a comment and a rule");
        Check(lines[0] == "# Written by Nazg: lets whoever is logged in configure the F77 Model F Keyboard (1209:4704).",
              "the comment names the board and its id");
        Check(lines[1] == "KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"1209\", "
                          "ATTRS{idProduct}==\"4704\", TAG+=\"uaccess\"",
              "the rule: the board's hidraw entries, for whoever is logged in");
    }

    void TestNameKeptPlain()
    {
        std::printf("the board's name cannot get out of its quotes\n");

        const std::string hostile = "Evil' ; rm -rf / ; echo '$(id)`x`\\\n\"KB\"";
        const std::string comment = PermissionRuleLines(hostile, c_Vendor, c_Product)[0];
        Check(!Contains(comment, "'") && !Contains(comment, "$") && !Contains(comment, "`") &&
                  !Contains(comment, "\\") && !Contains(comment, "\n") && !Contains(comment, "\"") &&
                  !Contains(comment, ";"),
              "no quote, $, backtick, backslash, semicolon or line break");
        Check(Contains(comment, "Evil rm -rf echo (id)xKB"), "the plain characters stay, spaces folded");

        Check(Contains(PermissionRuleLines("", c_Vendor, c_Product)[0], "configure the keyboard ("),
              "a board with no name is the keyboard");
        Check(Contains(PermissionRuleLines("\xC3\xA9t\xC3\xA9", c_Vendor, c_Product)[0], "configure the t ("),
              "beyond ASCII, left out");
        Check(PermissionRuleLines(std::string(200, 'x'), c_Vendor, c_Product)[0].size() < 150, "a long name is cut");
    }

    void TestCommands()
    {
        std::printf("the commands shown are those run\n");

        const std::vector<std::string> allow = AllowCommands("F77", c_Vendor, c_Product);
        Check(allow.size() == 4, "the file, then udev told, the boards triggered, the wait");
        Check(allow[0] == "printf '%s\\n' '# Written by Nazg: lets whoever is logged in configure the F77 (1209:4704).' "
                          "'KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"1209\", "
                          "ATTRS{idProduct}==\"4704\", TAG+=\"uaccess\"' | sudo tee /etc/udev/rules.d/60-nazg-1209-4704.rules",
              "written through sudo tee: a redirection would be the user's, not root's");
        Check(allow[1] == "sudo udevadm control --reload-rules" &&
                  allow[2] == "sudo udevadm trigger --subsystem-match=hidraw" && allow[3] == "sudo udevadm settle",
              "then the rules read again and applied to the boards plugged in");

        const std::string script = AllowScript("F77", c_Vendor, c_Product);
        Check(Contains(script, "' > /etc/udev/rules.d/60-nazg-1209-4704.rules && udevadm control --reload-rules && "
                               "udevadm trigger --subsystem-match=hidraw && udevadm settle"),
              "as root, the same steps, each only if the one before worked");
        Check(!Contains(script, "sudo"), "no sudo inside pkexec");
        Check(Contains(script, PermissionRuleLines("F77", c_Vendor, c_Product)[1]), "the same rule");

        const std::vector<std::string> stop = StopCommands(c_Vendor, c_Product);
        Check(stop.size() == 4 && stop[0] == "sudo rm -f /etc/udev/rules.d/60-nazg-1209-4704.rules",
              "stopping deletes the file -- nothing to edit");
        Check(StopScript(c_Vendor, c_Product) == "rm -f /etc/udev/rules.d/60-nazg-1209-4704.rules && udevadm control "
                                                "--reload-rules && udevadm trigger --subsystem-match=hidraw && udevadm settle",
              "and as root the same");
    }

    void TestNothingRunsOffLinux()
    {
#if !defined(__linux__)
        std::printf("elsewhere, nothing is asked\n");

        Check(!IsAllowedByNazg(c_Vendor, c_Product), "no board is Nazg's to allow");
        RootCommand command;
        command.Start(StopScript(c_Vendor, c_Product));
        Check(command.Poll() == RootCommand::Outcome::NotAsked && !command.IsRunning(), "and no command runs");
#endif
    }
}

int main()
{
    ConfigureCrtReporting();

    TestRuleFile();
    TestNameKeptPlain();
    TestCommands();
    TestNothingRunsOffLinux();

    return TestResult();
}
