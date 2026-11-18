// Pre-transformed (XYZRHW) quads that map a texture 1:1 with their edges on integer pixels: the game's frame
// feedback in transitions draws its 790x448 render target this way (x from 0 to w, u = x / w) with point sampling.
// D3D samples pixel centre i at exactly u = i / w and keeps texel i; a GPU that lands a hair below takes texel i-1,
// and every feedback pass moves the picture one pixel right and down (BUG-024 on the PC, BUG-026 on the Vita, where
// a 1/64 pixel bias was not enough). Moving such a quad by -0.5 gives the same D3D result with samples at texel
// centres, half a texel away from any tie. Pure: tested by port/test/texel_align_test.c.
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// v: n vertices of stride bytes (x, y at 0, 4; u, v at uv_off); tw x th: size of the sampled texture. Returns 1 if
// the quad was moved.
static inline int texel_align_quad(uint8_t *v, uint32_t stride, uint32_t n, uint32_t uv_off, float tw, float th) {
    if (n != 4 || tw <= 0.f || th <= 0.f) return 0;
    float lo = 1e30f, hi = -1e30f;
    for (uint32_t i = 0; i < n; i++) {
        float p[2], t[2];
        memcpy(p, v + i * stride, 8);
        memcpy(t, v + i * stride + uv_off, 8);
        if (p[0] != floorf(p[0]) || p[1] != floorf(p[1])) return 0;               // edges on integer pixels
        if (fabsf(t[0] * tw - p[0]) > 1e-3f || fabsf(t[1] * th - p[1]) > 1e-3f) return 0;   // 1:1 texel mapping
        if (p[0] < lo) lo = p[0];
        if (p[0] > hi) hi = p[0];
    }
    if (hi <= lo) return 0;
    for (uint32_t i = 0; i < n; i++) {
        float p[2];
        memcpy(p, v + i * stride, 8);
        p[0] -= 0.5f;
        p[1] -= 0.5f;
        memcpy(v + i * stride, p, 8);
    }
    return 1;
}
