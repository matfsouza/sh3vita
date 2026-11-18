// 2D placed in the centred 4:3 box by GsX (widescreen.c ws_gsx). Pure: tested by tests/test_ws_ui.py.
// The PS2 screen ended at the picture's edges, so 2D the game puts past them (map marks once the map is panned or
// zoomed) was never seen; on the 16:9 screen it would land in the side bands. And an opaque picture that covers the
// whole box (inventory, map) must have black bands next to it, not whatever the 3D scene left there.
#pragma once

enum { WS_UI_DRAW = 0, WS_UI_SKIP = 1, WS_UI_COVER = 2 };

// The centred 4:3 box of a w x h target, in pixels: [x0, x1]. 0 when the target is not wider than 4:3.
static inline int ws_ui_box(float w, float h, float *x0, float *x1) {
    float w43 = h * 4.f / 3.f;
    if (w <= w43 + 1.f) return 0;
    *x0 = (w - w43) * 0.5f;
    *x1 = *x0 + w43;
    return 1;
}

// SKIP: every vertex in one side band. COVER: spans the whole box (within 1.5 pixels; pixel centres sit at -0.5).
static inline int ws_ui_classify(const float *x, const float *y, int n, float h, float x0, float x1) {
    if (n <= 0) return WS_UI_DRAW;
    float lo = x[0], hi = x[0], ylo = y[0], yhi = y[0];
    for (int i = 1; i < n; i++) {
        if (x[i] < lo) lo = x[i];
        if (x[i] > hi) hi = x[i];
        if (y[i] < ylo) ylo = y[i];
        if (y[i] > yhi) yhi = y[i];
    }
    if (hi <= x0 || lo >= x1) return WS_UI_SKIP;
    const float e = 1.5f;
    // only a picture of the box's own width: a full-width effect drawn over the 3D must keep the bands it covers
    if (lo <= x0 + e && lo >= x0 - e - 1.f && hi >= x1 - e - 1.f && hi <= x1 + e && ylo <= e && yhi >= h - e - 1.f)
        return WS_UI_COVER;
    return WS_UI_DRAW;
}

// Map: Heather's position arrow is the lower-left quarter of the map's 128x128 icon sheet (u 0..0.25, v 0.5..1). The
// game draws it about 2.5 times bigger than the PS2 did next to the 4:3 map; ws_shrink_quad brings it closer. The
// purple floor triangle and the other icons use other parts of the sheet and keep their size.
static inline int ws_map_arrow_uv(const float *u, const float *v, int n) {
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++)
        if (u[i] < -0.01f || u[i] > 0.26f || v[i] < 0.49f || v[i] > 1.01f) return 0;
    return 1;
}
static inline void ws_shrink_quad(float *x, float *y, int n, float k) {
    float cx = 0, cy = 0;
    for (int i = 0; i < n; i++) { cx += x[i]; cy += y[i]; }
    cx /= (float)n; cy /= (float)n;
    for (int i = 0; i < n; i++) { x[i] = cx + (x[i] - cx) * k; y[i] = cy + (y[i] - cy) * k; }
}

// Axis-aligned quad (each x is its left or right edge) crossing the box edges: x clamped to the box, u moved with it.
// Returns 1 when something changed; any other shape is left alone.
static inline int ws_ui_clip_quad(float x[4], float u[4], float x0, float x1) {
    float lo = x[0], hi = x[0];
    for (int i = 1; i < 4; i++) { if (x[i] < lo) lo = x[i]; if (x[i] > hi) hi = x[i]; }
    if (hi - lo < 0.001f || (lo >= x0 && hi <= x1)) return 0;
    if (lo < x0 && hi > x1) return 0;   // spans the whole width (a full-target copy): not 2D of the box
    float ulo = 0.f, uhi = 0.f;
    for (int i = 0; i < 4; i++) {
        if (x[i] == lo) ulo = u[i];
        else if (x[i] == hi) uhi = u[i];
        else return 0;
    }
    float nlo = lo < x0 ? x0 : lo, nhi = hi > x1 ? x1 : hi;
    float nulo = ulo + (uhi - ulo) * (nlo - lo) / (hi - lo);
    float nuhi = ulo + (uhi - ulo) * (nhi - lo) / (hi - lo);
    for (int i = 0; i < 4; i++) {
        if (x[i] == lo) { x[i] = nlo; u[i] = nulo; }
        else { x[i] = nhi; u[i] = nuhi; }
    }
    return 1;
}
