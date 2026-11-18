// Tests for port/src/rt/ftol.h: the MSVC __ftol2 conversion (truncation toward zero, 64-bit result, x87 "integer
// indefinite" 0x8000000000000000 for NaN and out-of-range values), replaced natively for speed (BUG-029).
#include <math.h>
#include <stdio.h>
#include "ftol.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void) {
    CHECK(sh3_ftol(0.0) == 0);
    CHECK(sh3_ftol(2.999) == 2);
    CHECK(sh3_ftol(-2.999) == -2);       // toward zero, not floor
    CHECK(sh3_ftol(0.5) == 0 && sh3_ftol(-0.5) == 0 && sh3_ftol(1.5) == 1 && sh3_ftol(2.5) == 2);   // never rounds
    CHECK(sh3_ftol(4294967295.0) == 4294967295LL);   // beyond 32 bits: the 64-bit result (edx:eax)
    CHECK(sh3_ftol(-4294967296.5) == -4294967296LL);
    CHECK(sh3_ftol(9.2233720368547748e18) == 9223372036854774784LL);
    CHECK(sh3_ftol(9.3e18) == (int64_t)0x8000000000000000ULL);
    CHECK(sh3_ftol(-9.3e18) == (int64_t)0x8000000000000000ULL);
    CHECK(sh3_ftol(NAN) == (int64_t)0x8000000000000000ULL);
    CHECK(sh3_ftol(INFINITY) == (int64_t)0x8000000000000000ULL);
    CHECK(sh3_ftol(-9223372036854775808.0) == (int64_t)0x8000000000000000ULL);   // exactly INT64_MIN
    if (fails) return 1;
    printf("ok\n");
    return 0;
}
