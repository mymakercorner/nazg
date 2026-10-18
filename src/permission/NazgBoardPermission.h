// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// Board permission - a keyboard Linux does not let Nazg open, allowed or no longer
// (ui-design.md, "A board Linux does not allow"; build-and-release.md, "Nazg allowing the
// boards itself").
//
// One udev rule file per board, named after its USB id: allowing creates it, stopping deletes
// it -- nothing is ever edited, and the file being there is how Nazg knows it allowed the
// board. The rule hands the board's hidraw entries to whoever is logged in at the screen
// (TAG+="uaccess"); its name sorts before 73-seat-late.rules, which acts on that tag.
//
// What is written as root is one shell script, run through pkexec -- the desktop's password
// window, never Nazg's -- or shown as commands to paste into a terminal. Both come from here,
// so the dialog shows exactly what runs. Nothing from the board reaches the script unchecked:
// its name is cut to plain characters, its ids printed as hex.
//
// The text is plain functions, the same on every OS; only RootCommand runs anything, and only
// on Linux. POSIX, no SDL.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nazg
{
    // Where the rule for a board would be: /etc/udev/rules.d/60-nazg-1209-4704.rules.
    [[nodiscard]] std::string PermissionRulePath(uint16_t vendorId, uint16_t productId);

    // The rule file's two lines: a comment naming the board, then the rule.
    [[nodiscard]] std::vector<std::string> PermissionRuleLines(const std::string& name, uint16_t vendorId,
                                                               uint16_t productId);

    // Whether Nazg allowed this board: its rule file is there. False on every OS but Linux.
    [[nodiscard]] bool IsAllowedByNazg(uint16_t vendorId, uint16_t productId);

    // The commands, as a user types them in a terminal: sudo where root is needed, one per line.
    [[nodiscard]] std::vector<std::string> AllowCommands(const std::string& name, uint16_t vendorId, uint16_t productId);
    [[nodiscard]] std::vector<std::string> StopCommands(uint16_t vendorId, uint16_t productId);

    // The same, as the one script pkexec runs as root: no sudo, each step only if the one before
    // it worked.
    [[nodiscard]] std::string AllowScript(const std::string& name, uint16_t vendorId, uint16_t productId);
    [[nodiscard]] std::string StopScript(uint16_t vendorId, uint16_t productId);

    // A script run as root through pkexec, without blocking: started, then asked each frame
    // whether it has finished. pkexec waits on the user's password, so this must never wait for
    // it. Destroyed while still waiting, it ends pkexec -- the password window goes with it.
    class RootCommand
    {
    public:
        enum class Outcome
        {
            Running,
            Done,        // the script ran and worked
            Cancelled,   // the user closed the password window: nothing changed
            NotAsked,    // no pkexec, or no password window to ask with (no polkit agent)
            Failed,      // asked, but the script failed: `Detail()` says how
        };

        RootCommand() = default;
        ~RootCommand();

        RootCommand(const RootCommand&)            = delete;
        RootCommand& operator=(const RootCommand&) = delete;

        // Starts it, unless one is running. On every OS but Linux, NotAsked at once.
        void Start(const std::string& script);

        // Running until it finishes; then what happened, kept until the next Start().
        [[nodiscard]] Outcome Poll();

        [[nodiscard]] bool               IsRunning() const noexcept { return m_Process > 0; }
        [[nodiscard]] const std::string& Detail() const noexcept { return m_Detail; }

    private:
        long        m_Process = 0;   // pid_t, while running
        Outcome     m_Outcome = Outcome::Done;
        std::string m_Detail;
    };
}
