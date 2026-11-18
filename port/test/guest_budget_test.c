// Minimum guest memory on the Vita (plan M3, Task 3). Built and run by tests/test_guest_budget.py.
#include "guest_budget.h"
#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    // The sh3.exe image ends below G_HEAP_BASE (0x07200000 = 114 MiB); the game heap peaked at 60 MB on PC.
    CHECK(guest_min_mb(0x07200000u, 64) == 178, "got %u", guest_min_mb(0x07200000u, 64));
    CHECK(guest_min_mb(0x07200001u, 64) == 179, "partial MiB rounds up, got %u", guest_min_mb(0x07200001u, 64));
    CHECK(guest_min_mb(0x01400000u, 0) == 20, "20 MiB image, got %u", guest_min_mb(0x01400000u, 0));
    // Peak game heap measured on PC at the start of a new game (Lakeside Amusement Park): 68 MB.
    CHECK(SH3_MIN_HEAP_MB >= 68, "SH3_MIN_HEAP_MB=%u is below the measured peak", SH3_MIN_HEAP_MB);
    printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
    return fails != 0;
}
