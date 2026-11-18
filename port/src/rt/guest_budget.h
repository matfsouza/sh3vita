// Minimum guest memory (pure: testable on the host). The guest block holds the sh3.exe image (up to image_end,
// ~114 MiB because of its large .data/BSS) and then the game heap; a smaller block fails later and at random.
#pragma once
#include <stdint.h>

// Heap the game needs on top of the image. Peaks measured on PC: 60 MB at the title screen and attract demo,
// 68 MB at the start of a new game (Lakeside Amusement Park). Margin on top of the highest peak.
#define SH3_MIN_HEAP_MB 80u

// MiB needed for the image (rounded up to a whole MiB) plus heap_mb.
static inline uint32_t guest_min_mb(uint32_t image_end, uint32_t heap_mb) {
    return (uint32_t)(((uint64_t)image_end + 0xfffffu) >> 20) + heap_mb;
}
