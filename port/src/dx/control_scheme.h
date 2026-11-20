// Vita control schemes. Pure: tested by tests/test_control_scheme.py.
// The game's default pad table (0x7214e4, used when there is no key.ini) gives each DirectInput button one action:
// 0 map, 1 action / confirm, 2 run / guard (and cancel), 3 flashlight, 4 search view, 5 caution mode, 6 / 7 side
// steps (both at once: quick 180 turn; in the options menu: previous / next page), 8 item screen, 9 pause,
// 10 use the equipped supply, 11 zoom. The player picks one of three schemes on the Controls screen:
// - Vita: the layout of 1.0 / 1.1 (circle cancels and runs, square flashlight, triangle items, select map);
// - PS2 rear: the PS2 controller type B, L / R as L2 / R2, L1 / R1 on the left / right half of the rear touch pad;
// - PS2 front: the same with L1 / R1 on the left / right half of the screen.
// In the options menu L / R turn the page in every scheme.
#pragma once
#ifndef NO_SDL_HEADER
#include <SDL_gamecontroller.h>
#endif

enum { SH3_SCHEME_VITA = 0, SH3_SCHEME_PS2_REAR = 1, SH3_SCHEME_PS2_FRONT = 2, SH3_SCHEMES = 3 };
enum {
    SH3_DIB_MAP = 0, SH3_DIB_ACTION = 1, SH3_DIB_RUN = 2, SH3_DIB_LIGHT = 3, SH3_DIB_SEARCH = 4, SH3_DIB_CAUTION = 5,
    SH3_DIB_STEP_L = 6, SH3_DIB_STEP_R = 7, SH3_DIB_ITEMS = 8, SH3_DIB_PAUSE = 9
};

typedef struct { int front_l, front_r, back_l, back_r; } Sh3Touch;   // a finger on each half of each touch pad

// raw: bit SDL_CONTROLLER_BUTTON_x per pressed button. Returns bit i per DI button i.
static inline unsigned sh3_scheme_buttons(unsigned raw, int scheme, const Sh3Touch *t, int options_menu) {
#define SH3_SB(b) ((raw >> SDL_CONTROLLER_BUTTON_##b) & 1u)
    unsigned out = 0;
    if (SH3_SB(A)) out |= 1u << SH3_DIB_ACTION;
    if (SH3_SB(START)) out |= 1u << SH3_DIB_PAUSE;
    if (scheme == SH3_SCHEME_PS2_REAR || scheme == SH3_SCHEME_PS2_FRONT) {
        if (SH3_SB(B)) out |= 1u << SH3_DIB_LIGHT;       // circle: flashlight / cancel
        if (SH3_SB(X)) out |= 1u << SH3_DIB_RUN;         // square: run / guard
        if (SH3_SB(Y)) out |= 1u << SH3_DIB_MAP;         // triangle: map
        if (SH3_SB(BACK)) out |= 1u << SH3_DIB_ITEMS;    // select: item screen
        int l1 = scheme == SH3_SCHEME_PS2_REAR ? t->back_l : t->front_l;
        int r1 = scheme == SH3_SCHEME_PS2_REAR ? t->back_r : t->front_r;
        if (l1) out |= 1u << SH3_DIB_STEP_L;
        if (r1) out |= 1u << SH3_DIB_STEP_R;
    } else {
        if (SH3_SB(B)) out |= 1u << SH3_DIB_RUN;         // circle: cancel / run
        if (SH3_SB(X)) out |= 1u << SH3_DIB_LIGHT;       // square: flashlight
        if (SH3_SB(Y)) out |= 1u << SH3_DIB_ITEMS;       // triangle: item screen
        if (SH3_SB(BACK)) out |= 1u << SH3_DIB_MAP;      // select: map
    }
    if (options_menu) {   // L / R: previous / next page
        if (SH3_SB(LEFTSHOULDER)) out |= 1u << SH3_DIB_STEP_L;
        if (SH3_SB(RIGHTSHOULDER)) out |= 1u << SH3_DIB_STEP_R;
    } else {               // L2 / R2 on the PS2
        if (SH3_SB(LEFTSHOULDER)) out |= 1u << SH3_DIB_SEARCH;
        if (SH3_SB(RIGHTSHOULDER)) out |= 1u << SH3_DIB_CAUTION;
    }
#undef SH3_SB
    return out;
}

// PS2 schemes: circle is the flashlight (button 3), which the game's menus do not take as cancel (button 2, square on
// the PS2). Outside the world (menus, item screen, map: no collision runs), circle cancels too, as on the PS2.
static inline unsigned sh3_scheme_menu_cancel(unsigned raw, int scheme, int in_world) {
    if (scheme == SH3_SCHEME_VITA || in_world || !((raw >> SDL_CONTROLLER_BUTTON_B) & 1u)) return 0;
    return 1u << SH3_DIB_RUN;
}

// Which half of a touch pad a finger is on: 0 left, 1 right.
static inline int sh3_touch_side(int x, int width) { return x >= width / 2; }

// The settings file holds the scheme number; anything else means the Vita scheme.
static inline int sh3_scheme_parse(const char *s) {
    return s && s[0] >= '0' && s[0] < '0' + SH3_SCHEMES ? s[0] - '0' : SH3_SCHEME_VITA;
}
static inline int sh3_scheme_step(int scheme, int dir) { return ((scheme + dir) % SH3_SCHEMES + SH3_SCHEMES) % SH3_SCHEMES; }
