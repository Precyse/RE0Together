#pragma once
#include <cstdio>

// The checks of a unit test program: each prints "ok" or "FAIL" and a failure makes main return 1.
inline int g_failures = 0;

inline void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}
