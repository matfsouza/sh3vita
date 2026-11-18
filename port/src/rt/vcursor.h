// Puzzle cursor moved by the left stick / d-pad. Pure: tested by tests/test_vcursor.py.
// The PC game reads the puzzle cursor from the mouse (GetCursorPos -> x * 512 / width, 0x600920); the port feeds
// that path with this position, in the game's own units: x 0..512, y 64..448 (the limits set by 0x4dd270).
#pragma once

#define VCURSOR_SPEED 150.f   // units per second at full tilt (the game's own pad cursor: 210)

typedef struct { float x, y; } VCursor;

static inline float vcursor_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline void vcursor_reset(VCursor *v, float x, float y) {
    v->x = vcursor_clamp(x, 0.f, 512.f);
    v->y = vcursor_clamp(y, 64.f, 448.f);
}

// Stick axis (-32768..32767) -> -1..1 with a dead zone and a gentle start, so small moves are precise.
static inline float vcursor_axis(int s) {
    float f = (float)s / 32767.f, a = f < 0 ? -f : f;
    if (a < 0.12f) return 0.f;
    a = (a - 0.12f) / 0.88f;
    if (a > 1.f) a = 1.f;
    a = a * (0.35f + 0.65f * a);
    return f < 0 ? -a : a;
}

static inline void vcursor_step(VCursor *v, int sx, int sy, float dt) {
    if (dt > 0.1f) dt = 0.1f;   // a long hitch must not throw the cursor across the picture
    if (dt < 0.f) dt = 0.f;
    vcursor_reset(v, v->x + vcursor_axis(sx) * VCURSOR_SPEED * dt, v->y + vcursor_axis(sy) * VCURSOR_SPEED * dt);
}

// Client pixels for GetCursorPos: the game's integer x * 512 / w gives back the same whole unit.
static inline void vcursor_pixels(const VCursor *v, int w, int h, int *px, int *py) {
    int ux = (int)v->x, uy = (int)v->y;
    *px = (ux * w + 511) / 512;
    *py = (uy * h + 511) / 512;
}
