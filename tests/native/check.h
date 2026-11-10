// A few assertion macros for the native tests: each test program returns the number of failures.
#pragma once
#include <cstdio>

inline int g_failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

#define CHECK_THROWS(expression)                                             \
    do {                                                                     \
        bool threw = false;                                                  \
        try {                                                                \
            (void)(expression);                                              \
        } catch (...) {                                                      \
            threw = true;                                                    \
        }                                                                    \
        if (!threw) {                                                        \
            std::printf("%s:%d: did not throw: %s\n", __FILE__, __LINE__, #expression); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)
