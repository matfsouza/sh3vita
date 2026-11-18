// 16:9 screen layout of the port (M4, "PS2 in 16:9"). Pure: tested by port/test/ws_layout_test.c.
// The display (back buffer) is the Vita screen; the game's 3D renders at a lower size with the same shape, so
// nothing is stretched.
#pragma once
#include <stdio.h>

#define WS_DISPLAY_W 960
#define WS_DISPLAY_H 544
#define WS_RES_DEFAULT_W 790   // 448 lines like the PS2; 790:448 = 960:544
#define WS_RES_DEFAULT_H 448

// Hor+ factor for the game's horizontal projection scale: 1 at 4:3, smaller when wider (more scene on the sides).
static inline float ws_hor_plus(float aspect) { return aspect > 0.f ? (4.0f / 3.0f) / aspect : 1.0f; }

// PS2 2D x coordinate (GS 12.4 fixed point: 512 units wide, centred on 2048) -> render-target pixels, as the
// game's GsX (0x682ba0) does for a w-wide target. In 4:3 mode the 2D sits in a centred 4:3 box (menus, HUD);
// stretch mode fills the whole width (full-screen effects).
static inline float ws_gs_x(int x, int w, int h, int stretch) {
    float u = ((float)x * 0.0625f - 2048.f + 256.f) / 512.f;
    if (stretch || w * 3 <= h * 4) return (float)w * u;
    float w43 = (float)h * 4.f / 3.f;
    return ((float)w - w43) * 0.5f + w43 * u;
}

// x of a full-screen quad vertex (-0.5 .. w-0.5, D3D pixel centres) moved into the centred 4:3 box of a w x h target.
static inline float ws_quad_x43(float x, float w, float h) {
    float w43 = h * 4.f / 3.f;
    if (w <= w43) return x;
    return (w - w43) * 0.5f + (x + 0.5f) * w43 / w - 0.5f;
}

// In-engine cutscenes: the PS2 covered bar_total of the height with two cinematic bars (0x41baf0: 2 x 0.5 x
// [0x68eabc] = 0.2009). Without bars, the 3D projection is enlarged by this factor so the screen shows exactly what
// was between them (WidescreenFix "Cutscenes"). Out-of-range factors: 1, the game's view.
static inline float ws_cine_zoom(float bar_total) {
    return bar_total > 0.f && bar_total < 0.5f ? 1.f / (1.f - bar_total) : 1.f;
}

// Side band of a quad placed in the centred 4:3 box (x, u of its 4 vertices): the band goes from the screen edge
// (-0.5 or W-0.5) to the picture and every vertex samples the picture's edge column (u slightly inside, so linear
// filtering does not blend in the opposite edge). right: 0 left band, 1 right band. y and v stay the picture's.
static inline void ws_side_band(const float x[4], const float u[4], float W, int right, float ox[4], float ou[4]) {
    float lo = x[0], hi = x[0];
    for (int i = 1; i < 4; i++) { if (x[i] < lo) lo = x[i]; if (x[i] > hi) hi = x[i]; }
    float mid = (lo + hi) * 0.5f, ulo = 0.f, uhi = 0.f;
    for (int i = 0; i < 4; i++) { if (x[i] < mid) ulo = u[i]; else uhi = u[i]; }
    float edge = right ? uhi + (ulo - uhi) * 0.002f : ulo + (uhi - ulo) * 0.002f;
    for (int i = 0; i < 4; i++) {
        int outer = right ? x[i] >= mid : x[i] < mid;   // the vertex that moves to the screen edge
        ox[i] = outer ? (right ? W - 0.5f : -0.5f) : (right ? hi : lo);
        ou[i] = edge;
    }
}

// Movie rectangle {x0, y0, x1, y1}. The game (0x59c850) passes {0, H/14, W, H - H/14}: a letterbox to 6/7 of the
// height made for its 4:3 movies. A wide movie (16:9 of the HD collection) fills the screen with its own shape; a
// 4:3 movie keeps the game's letterbox in a centred 4:3 box; size unknown (0): the game's rectangle.
static inline void ws_movie_rect(const float game[4], int mw, int mh, float out[4]) {
    float W = game[2] + game[0], H = game[3] + game[1];
    for (int i = 0; i < 4; i++) out[i] = game[i];
    if (mw <= 0 || mh <= 0 || W <= 0.f || H <= 0.f) return;
    float va = (float)mw / (float)mh;
    if (va > 1.5f) {
        float w = W, h = W / va;
        if (h > H) { h = H; w = H * va; }
        out[0] = (W - w) * 0.5f; out[2] = out[0] + w;
        out[1] = (H - h) * 0.5f; out[3] = out[1] + h;
    } else if (W * 3.f > H * 4.f) {
        float w43 = H * 4.f / 3.f;
        out[0] = (W - w43) * 0.5f; out[2] = out[0] + w43;
    }
}

// "WxH" (SH3_RES) -> 3D render size, clamped to [320x180, display]; NULL or malformed -> default.
static inline void ws_parse_res(const char *s, int *w, int *h) {
    int pw, ph;
    if (!s || sscanf(s, "%dx%d", &pw, &ph) != 2) { pw = WS_RES_DEFAULT_W; ph = WS_RES_DEFAULT_H; }
    *w = pw < 320 ? 320 : pw > WS_DISPLAY_W ? WS_DISPLAY_W : pw;
    *h = ph < 180 ? 180 : ph > WS_DISPLAY_H ? WS_DISPLAY_H : ph;
}
