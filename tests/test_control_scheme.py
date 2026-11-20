"""Vita control schemes (port/src/dx/control_scheme.h).

The game's default pad table (0x7214e4, used when there is no key.ini) gives each DirectInput button one action:
0 map, 1 action, 2 run/guard (and cancel), 3 flashlight, 4 search view, 5 caution mode, 6/7 side steps (both: quick
180 turn; in the options menu: previous/next page), 8 item screen, 9 pause, 10 use the equipped supply, 11 zoom.
Three schemes: Vita (the layout of 1.0/1.1), PS2 with L1/R1 on the rear touch pad, PS2 with L1/R1 on the screen.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include <stdio.h>
typedef short Sint16;
enum { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
       SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_GUIDE, SDL_CONTROLLER_BUTTON_START,
       SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
       SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
       SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
#define NO_SDL_HEADER
#include "control_scheme.h"
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
#define B(x) (1u << SDL_CONTROLLER_BUTTON_##x)
#define D(i) (1u << (i))
static unsigned map(unsigned raw, int scheme, int tl, int tr, int menu) {
    Sh3Touch t = {0, 0, 0, 0};
    if (scheme == SH3_SCHEME_PS2_REAR) { t.back_l = tl; t.back_r = tr; } else { t.front_l = tl; t.front_r = tr; }
    return sh3_scheme_buttons(raw, scheme, &t, menu);
}
int main(void) {
    /* Vita scheme: the 1.1 layout, without the use-supply and zoom buttons that rode along with circle */
    ok("vita cross", map(B(A), SH3_SCHEME_VITA, 0, 0, 0) == D(1));
    ok("vita circle = cancel/run only", map(B(B), SH3_SCHEME_VITA, 0, 0, 0) == D(2));
    ok("vita triangle", map(B(Y), SH3_SCHEME_VITA, 0, 0, 0) == D(8));
    ok("vita square", map(B(X), SH3_SCHEME_VITA, 0, 0, 0) == D(3));
    ok("vita select", map(B(BACK), SH3_SCHEME_VITA, 0, 0, 0) == D(0));
    ok("vita start", map(B(START), SH3_SCHEME_VITA, 0, 0, 0) == D(9));
    ok("vita L R", map(B(LEFTSHOULDER) | B(RIGHTSHOULDER), SH3_SCHEME_VITA, 0, 0, 0) == (D(4) | D(5)));
    ok("vita ignores touch", map(0, SH3_SCHEME_VITA, 1, 1, 0) == 0);
    /* PS2 layout (controller type B) */
    for (int s = SH3_SCHEME_PS2_REAR; s <= SH3_SCHEME_PS2_FRONT; s++) {
        ok("ps2 cross", map(B(A), s, 0, 0, 0) == D(1));
        ok("ps2 circle = flashlight/cancel", map(B(B), s, 0, 0, 0) == D(3));
        ok("ps2 square = run/guard", map(B(X), s, 0, 0, 0) == D(2));
        ok("ps2 triangle = map", map(B(Y), s, 0, 0, 0) == D(0));
        ok("ps2 select = items", map(B(BACK), s, 0, 0, 0) == D(8));
        ok("ps2 start = pause", map(B(START), s, 0, 0, 0) == D(9));
        ok("ps2 L = search view (L2)", map(B(LEFTSHOULDER), s, 0, 0, 0) == D(4));
        ok("ps2 R = caution mode (R2)", map(B(RIGHTSHOULDER), s, 0, 0, 0) == D(5));
        ok("ps2 left touch = L1", map(0, s, 1, 0, 0) == D(6));
        ok("ps2 right touch = R1", map(0, s, 0, 1, 0) == D(7));
        ok("ps2 both = quick turn", map(0, s, 1, 1, 0) == (D(6) | D(7)));
    }
    { Sh3Touch t = {1, 1, 0, 0};   /* front touch does nothing in the rear scheme, and the other way round */
      ok("rear scheme ignores the screen", sh3_scheme_buttons(0, SH3_SCHEME_PS2_REAR, &t, 0) == 0);
      Sh3Touch u = {0, 0, 1, 1};
      ok("front scheme ignores the rear pad", sh3_scheme_buttons(0, SH3_SCHEME_PS2_FRONT, &u, 0) == 0); }
    /* options menu: L / R also turn the page in every scheme */
    for (int s = 0; s < SH3_SCHEMES; s++) {
        ok("menu L = previous page", map(B(LEFTSHOULDER), s, 0, 0, 1) == D(6));
        ok("menu R = next page", map(B(RIGHTSHOULDER), s, 0, 0, 1) == D(7));
    }
    /* PS2 schemes: circle (flashlight, button 3) also cancels (button 2) outside the world: menus, item screen */
    for (int s = SH3_SCHEME_PS2_REAR; s <= SH3_SCHEME_PS2_FRONT; s++) {
        ok("ps2 circle cancels in menus", sh3_scheme_menu_cancel(B(B), s, 0) == D(2));
        ok("ps2 circle does not run in the world", sh3_scheme_menu_cancel(B(B), s, 1) == 0);
        ok("ps2 no circle no cancel", sh3_scheme_menu_cancel(B(A), s, 0) == 0);
    }
    ok("vita circle already cancels", sh3_scheme_menu_cancel(B(B), SH3_SCHEME_VITA, 0) == 0);
    /* touch side from a coordinate */
    ok("left half", sh3_touch_side(100, 1920) == 0 && sh3_touch_side(959, 1920) == 0);
    ok("right half", sh3_touch_side(960, 1920) == 1 && sh3_touch_side(1900, 1920) == 1);
    /* scheme from the settings file text, cycling */
    ok("parse", sh3_scheme_parse("2\n") == 2 && sh3_scheme_parse("0") == 0);
    ok("parse junk", sh3_scheme_parse("x") == SH3_SCHEME_VITA && sh3_scheme_parse("7") == SH3_SCHEME_VITA && sh3_scheme_parse("") == SH3_SCHEME_VITA);
    ok("next wraps", sh3_scheme_step(2, 1) == 0 && sh3_scheme_step(0, -1) == 2 && sh3_scheme_step(1, 1) == 2);
    puts(fails ? "control_scheme: FAILED" : "control_scheme: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_control_schemes():
    with tempfile.TemporaryDirectory(prefix="sh3-cs-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/dx'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
