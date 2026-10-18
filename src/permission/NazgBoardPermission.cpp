// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>

#include "NazgBoardPermission.h"

#include <cstdio>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace nazg
{
    namespace
    {
        constexpr size_t c_LongestName = 64;

        // pkexec's exit codes when it ran nothing (pkexec(1)): the user closed the password window;
        // no authorisation could be had -- no agent to ask with, or the password refused.
        constexpr int c_PkexecDismissed    = 126;
        constexpr int c_PkexecNotAuthorised = 127;

        // udev's ids are four lowercase hex digits, as sysfs writes idVendor and idProduct.
        std::string Hex(uint16_t value)
        {
            char text[8];
            std::snprintf(text, sizeof(text), "%04x", value);
            return text;
        }

        // The board's name as the comment may hold it: the script is run as root, and the name comes
        // from the board, so only plain characters go in -- never a quote, a $ or a line break.
        std::string SafeName(const std::string& name)
        {
            std::string safe;
            for (const char c : name)
            {
                const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                   c == ' ' || c == '.' || c == '_' || c == '+' || c == '-' || c == '(' || c == ')';
                if (plain && !(c == ' ' && (safe.empty() || safe.back() == ' ')))
                    safe += c;
                if (safe.size() == c_LongestName)
                    break;
            }
            while (!safe.empty() && safe.back() == ' ')
                safe.pop_back();
            return safe.empty() ? "keyboard" : safe;
        }

        // The file written with its text inside the command: no temporary file to put in place.
        std::string WriteRule(const std::string& name, uint16_t vendorId, uint16_t productId)
        {
            std::string command = "printf '%s\\n'";
            for (const std::string& line : PermissionRuleLines(name, vendorId, productId))
                command += " '" + line + "'";
            return command;
        }

        // What follows writing or deleting the file: udev reads its rules again, applies them to the
        // boards already plugged in, and is waited for -- so the board is asked again only once done.
        const std::vector<std::string>& Apply()
        {
            static const std::vector<std::string> steps = { "udevadm control --reload-rules",
                                                            "udevadm trigger --subsystem-match=hidraw",
                                                            "udevadm settle" };
            return steps;
        }

        std::vector<std::string> WithSudo(std::vector<std::string> commands)
        {
            for (const std::string& step : Apply())
                commands.push_back("sudo " + step);
            return commands;
        }

        std::string Joined(std::string script)
        {
            for (const std::string& step : Apply())
                script += " && " + step;
            return script;
        }
    }

    std::string PermissionRulePath(uint16_t vendorId, uint16_t productId)
    {
        return "/etc/udev/rules.d/60-nazg-" + Hex(vendorId) + "-" + Hex(productId) + ".rules";
    }

    std::vector<std::string> PermissionRuleLines(const std::string& name, uint16_t vendorId, uint16_t productId)
    {
        const std::string id = Hex(vendorId) + ":" + Hex(productId);
        return { "# Written by Nazg: lets whoever is logged in configure the " + SafeName(name) + " (" + id + ").",
                 "KERNEL==\"hidraw*\", SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"" + Hex(vendorId) +
                     "\", ATTRS{idProduct}==\"" + Hex(productId) + "\", TAG+=\"uaccess\"" };
    }

    bool IsAllowedByNazg(uint16_t vendorId, uint16_t productId)
    {
#if defined(__linux__)
        std::error_code error;
        return std::filesystem::exists(PermissionRulePath(vendorId, productId), error);
#else
        (void)vendorId;
        (void)productId;
        return false;
#endif
    }

    std::vector<std::string> AllowCommands(const std::string& name, uint16_t vendorId, uint16_t productId)
    {
        return WithSudo({ WriteRule(name, vendorId, productId) + " | sudo tee " + PermissionRulePath(vendorId, productId) });
    }

    std::vector<std::string> StopCommands(uint16_t vendorId, uint16_t productId)
    {
        return WithSudo({ "sudo rm -f " + PermissionRulePath(vendorId, productId) });
    }

    std::string AllowScript(const std::string& name, uint16_t vendorId, uint16_t productId)
    {
        return Joined(WriteRule(name, vendorId, productId) + " > " + PermissionRulePath(vendorId, productId));
    }

    std::string StopScript(uint16_t vendorId, uint16_t productId)
    {
        return Joined("rm -f " + PermissionRulePath(vendorId, productId));
    }

    // --- RootCommand ----------------------------------------------------------------------------

    RootCommand::~RootCommand()
    {
#if defined(__linux__)
        // Still waiting on the password: pkexec runs as the user until it has it, so it can be told
        // to stop. Past it, the script is root's and short -- waited for.
        if (m_Process > 0)
        {
            kill(static_cast<pid_t>(m_Process), SIGTERM);
            int status = 0;
            waitpid(static_cast<pid_t>(m_Process), &status, 0);
        }
#endif
    }

    void RootCommand::Start(const std::string& script)
    {
        if (IsRunning())
            return;
        m_Detail.clear();
#if defined(__linux__)
        // --disable-internal-agent: with no password window, pkexec would otherwise ask in the
        // terminal Nazg was started from, if any -- where nobody looks -- and wait there.
        std::string program    = "pkexec";
        std::string noTerminal = "--disable-internal-agent";
        std::string shell      = "/bin/sh";
        std::string run        = "-c";
        std::string text       = script;
        char* const argv[]     = { program.data(), noTerminal.data(), shell.data(), run.data(), text.data(), nullptr };
        pid_t       process    = 0;
        const int   failure    = posix_spawnp(&process, "pkexec", nullptr, nullptr, argv, environ);
        if (failure != 0)
        {
            // Not found is said as "permission denied" when a folder searched on the way refuses
            // to be read -- WSL puts Windows' folders on the PATH -- so both mean it is not there.
            m_Outcome = Outcome::NotAsked;
            m_Detail  = failure == ENOENT || failure == EACCES ? "pkexec is not installed"
                                                               : std::string("pkexec did not start: ") + std::strerror(failure);
            return;
        }
        m_Process = process;
        m_Outcome = Outcome::Running;
#else
        (void)script;
        m_Outcome = Outcome::NotAsked;
        m_Detail  = "only Linux asks for this";
#endif
    }

    RootCommand::Outcome RootCommand::Poll()
    {
#if defined(__linux__)
        if (m_Process <= 0)
            return m_Outcome;

        int         status = 0;
        const pid_t done   = waitpid(static_cast<pid_t>(m_Process), &status, WNOHANG);
        if (done == 0)
            return Outcome::Running;
        m_Process = 0;

        if (done < 0)
        {
            m_Outcome = Outcome::Failed;
            m_Detail  = std::strerror(errno);
        }
        else if (!WIFEXITED(status))
        {
            m_Outcome = Outcome::Failed;
            m_Detail  = "pkexec was stopped";
        }
        else
        {
            const int code = WEXITSTATUS(status);
            m_Outcome      = code == 0                     ? Outcome::Done
                             : code == c_PkexecDismissed    ? Outcome::Cancelled
                             : code == c_PkexecNotAuthorised ? Outcome::NotAsked
                                                             : Outcome::Failed;
            if (m_Outcome == Outcome::NotAsked)
                m_Detail = "the computer did not ask for the password";
            else if (m_Outcome == Outcome::Failed)
                m_Detail = "the commands stopped with code " + std::to_string(code);
        }
#endif
        return m_Outcome;
    }
}
