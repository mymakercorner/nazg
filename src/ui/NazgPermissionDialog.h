// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// PermissionDialog - Allow... and Stop allowing, for one board Linux does not let Nazg open, or
// that Nazg allowed (ui-design.md, "A board Linux does not allow"; the mockup is
// ui-design/linux-permission.html).
//
// The dialog says what allowing means, then asks the computer for its password through pkexec
// -- the desktop's own window, never Nazg's -- without waiting for it on the frame loop (the
// rule file and the commands: permission/NazgBoardPermission.h). Show the commands shows what
// runs, to read or to paste into a terminal; when the computer cannot ask -- no pkexec, no
// polkit agent -- they are what is left, with Try again.
//
// It draws and reports; acting on the list is the caller's. ImGui only, no SDL.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "permission/NazgBoardPermission.h"
#include "transport/NazgHidTransport.h"

namespace nazg
{
    class PermissionDialog
    {
    public:
        // A board whose permission changed, or which should be asked again: every board with its
        // USB id is asked its protocol again, and the list says `said`, if anything.
        struct Outcome
        {
            uint16_t    vendorId  = 0;
            uint16_t    productId = 0;
            std::string said;
        };

        void AskAllow(const HidDeviceInfo& device);
        void AskStop(const HidDeviceInfo& device);

        [[nodiscard]] bool IsOpen() const noexcept { return m_Kind != Kind::None; }

        // Every frame, within it: the dialog while one is asked, and its outcome once done.
        [[nodiscard]] std::optional<Outcome> Draw();

    private:
        enum class Kind
        {
            None,
            Allow,
            Stop,
        };

        void Ask(Kind kind, const HidDeviceInfo& device);
        void DrawCommands();

        Kind        m_Kind      = Kind::None;
        std::string m_Name;
        uint16_t    m_VendorId  = 0;
        uint16_t    m_ProductId = 0;

        bool        m_ShowCommands = false;
        bool        m_IsWaiting    = false;   // the password asked for, pkexec still running
        bool        m_CannotAsk    = false;   // the computer did not ask: the commands are the way
        std::string m_Problem;                // why it did not work, when it did not
        RootCommand m_Command;
    };
}
