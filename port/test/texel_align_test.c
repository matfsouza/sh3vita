// Tests for port/src/d3d8/texel_align.h: pre-transformed quads that map a texture 1:1 on integer pixel edges
// (the game's frame feedback) move to D3D pixel centres, so point sampling never depends on a rounding tie.
#include <stdio.h>
#include <string.h>
#include "texel_align.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

// x, y, z, rhw, u, v (FVF XYZRHW | TEX1, stride 24)
static void quad(float *q, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1) {
    const float xs[4] = {x1, x0, x1, x0}, ys[4] = {y0, y0, y1, y1}, us[4] = {u1, u0, u1, u0}, vs[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) {
        float *p = q + 6 * i;
        p[0] = xs[i]; p[1] = ys[i]; p[2] = 1.f; p[3] = 1.f; p[4] = us[i]; p[5] = vs[i];
    }
}

int main(void) {
    float q[24];
    // The feedback quad seen in the trace: 0..790 x 0..448 over a 790x448 texture, u, v 0..1 -> -0.5 aligned.
    quad(q, 0, 0, 790, 448, 0, 0, 1, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 4, 16, 790, 448) == 1);
    CHECK(q[0] == 789.5f && q[1] == -0.5f && q[6] == -0.5f && q[13] == 447.5f);
    CHECK(q[4] == 1.f && q[5] == 0.f);   // texture coordinates untouched
    // Already D3D-aligned (-0.5 .. w-0.5): unchanged.
    quad(q, -0.5f, -0.5f, 789.5f, 447.5f, 0, 0, 1, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 4, 16, 790, 448) == 0 && q[1] == -0.5f);
    // Not 1:1 (scaled picture, logo quads, partial rectangles): unchanged.
    quad(q, 0, 0, 640, 448, 0, 0, 1, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 4, 16, 790, 448) == 0 && q[1] == 0.f);
    quad(q, 0, 0, 395, 448, 0, 0, 0.5f, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 4, 16, 790, 448) == 1);   // half of the texture, still 1:1
    quad(q, 10, 0, 790, 448, 0, 0, 1, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 4, 16, 790, 448) == 0);
    // Other vertex counts are left alone.
    quad(q, 0, 0, 790, 448, 0, 0, 1, 1);
    CHECK(texel_align_quad((unsigned char *)q, 24, 3, 16, 790, 448) == 0);
    if (fails) return 1;
    printf("ok\n");
    return 0;
}
