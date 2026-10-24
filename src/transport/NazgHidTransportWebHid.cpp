// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// HidTransport in a browser, where hidapi does not exist and a thread would cost the page
// cross-origin isolation (client-architecture.md, "Web build"). Nothing blocks, so there is
// no worker: an operation is serviced as it is submitted and its coroutine resumed by the next
// Pump(), on the main thread as natively.
//
// For now no device is ever listed or opened. WebHID comes next: requestDevice() behind a
// click, its reports into these same completions.

#include "NazgHidTransport.h"

#include <utility>

namespace nazg
{
    HidTransport::HidTransport()
    {
        m_Running.store(true);
    }

    void HidTransport::Submit(OperationPtr operation)
    {
        if (!m_Running.load())
            operation->error = "transport is shut down";   // the coroutine still unwinds
        else
            Execute(*operation);

        m_Completed.push_back(std::move(operation));
    }

    void HidTransport::Execute(Operation& operation)
    {
        switch (operation.kind)
        {
        case OperationKind::Enumerate:
            break;   // no device, and no error: an empty list
        case OperationKind::Open:
            operation.error = "could not open " + operation.path + ": this build has no WebHID yet";
            break;
        case OperationKind::Request:
        case OperationKind::Series:
            operation.error = "request for a device that is not open";
            break;
        case OperationKind::Send:
            operation.error = "send to a device that is not open";
            break;
        case OperationKind::Farewell:
        case OperationKind::Close:
            break;   // nothing is open
        }
    }

    void HidTransport::Shutdown()
    {
        if (!m_Running.exchange(false))
            return;

        // Nothing is ever pending: what was submitted is completed, and resumed here.
        Pump();
    }
}
