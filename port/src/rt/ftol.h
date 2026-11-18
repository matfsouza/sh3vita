// MSVC __ftol2 (sh3.exe 0x662c40): the conversion behind every (int) / (long long) cast of a float in the game.
// Truncation toward zero to 64 bits; NaN and values outside the int64 range give the x87 "integer indefinite"
// 0x8000000000000000. Pure: tested by port/test/ftol_test.c.
#pragma once
#include <stdint.h>

static inline int64_t sh3_ftol(double v) {
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) return INT64_MIN;   // NaN fails both tests
    return (int64_t)v;
}
