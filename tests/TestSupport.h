// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Rico <rico@mymakercorner.com>
//
// The small amount of scaffolding every test executable here repeats. Deliberately NOT
// a test framework: a pass/fail counter, a printf, and the MSVC boilerplate below. The
// framework choice stays open -- see CMakeLists.txt.
//
// Shape of a test executable:
//
//     int main()
//     {
//         ConfigureCrtReporting();
//         TestSomething();
//         return TestResult();
//     }

#pragma once

#include <cstdio>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

inline int g_failureCount = 0;

inline void Check(bool condition, const char* description)
{
    std::printf("  [%s] %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++g_failureCount;
}

// A failing assert (Task::Destroy() has one) must fail the test, not open a modal
// dialog that blocks the run forever on CI or on a developer's machine.
inline void ConfigureCrtReporting()
{
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (int reportType : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(reportType, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(reportType, _CRTDBG_FILE_STDERR);
    }
#endif
}

inline int TestResult()
{
    std::printf("%s (%d failure(s))\n", g_failureCount == 0 ? "PASSED" : "FAILED", g_failureCount);
    return g_failureCount == 0 ? 0 : 1;
}
