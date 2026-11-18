// x87 helpers of the runtime (x86rt.h): FXAM classification (BUG-016). Built and run by tests/test_x87.py.
#include "x86rt.h"
#include <float.h>
#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    // C0 0x100, C1 0x200 (sign), C2 0x400, C3 0x4000. Classes (C3 C2 C0): zero 100, normal 010, infinity 011, NaN 001.
    // A double subnormal is a normal number once loaded in the 80-bit register.
    static const struct { double v; uint32_t fsw; const char *name; } cases[] = {
        {0.0, 0x4000, "+0"}, {-0.0, 0x4200, "-0"}, {1.5, 0x0400, "1.5"}, {-2.0, 0x0600, "-2"},
        {INFINITY, 0x0500, "+inf"}, {-INFINITY, 0x0700, "-inf"}, {DBL_MIN / 4, 0x0400, "double subnormal"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        uint32_t fsw = 0x3800 | 0x0001;   // TOP and an exception flag must be kept
        rt_fxam(cases[i].v, &fsw);
        CHECK(fsw == (0x3801u | cases[i].fsw), "%s: fsw=%04x expected %04x", cases[i].name, fsw, 0x3801u | cases[i].fsw);
    }
    uint32_t fsw = 0x4700;   // stale C3/C2/C1/C0 must be cleared
    rt_fxam(NAN, &fsw);
    CHECK((fsw & 0x4500) == 0x0100, "NaN: fsw=%04x", fsw);
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
