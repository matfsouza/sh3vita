"""2D drawn in the centred 4:3 box (port/src/rt/ws_ui.h): what falls in the side bands is cut away, as the PS2 screen
edge did (map marks panned off the paper), and an opaque picture covering the whole box (inventory, map) asks for
black side bands. Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "ws_ui.h"
#include <math.h>
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
static int near(float a, float b) { return fabsf(a - b) < 0.01f; }
int main(void) {
    float x0, x1;
    /* 960x544: box 725.33 wide, from 117.33 */
    ok("box exists", ws_ui_box(960, 544, &x0, &x1) && near(x0, 117.333f) && near(x1, 842.667f));
    ok("no box at 4:3", !ws_ui_box(640, 480, &x0, &x1));
    ws_ui_box(960, 544, &x0, &x1);
    { float x[4] = {20, 60, 20, 60}, y[4] = {100, 100, 120, 120};
      ok("mark in the left band is skipped", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_SKIP); }
    { float x[4] = {900, 950, 900, 950}, y[4] = {0, 0, 10, 10};
      ok("mark in the right band is skipped", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_SKIP); }
    { float x[4] = {116.8f, 842.2f, 116.8f, 842.2f}, y[4] = {-0.5f, -0.5f, 543.5f, 543.5f};
      ok("full-box picture covers", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_COVER); }
    { float x[4] = {116.8f, 842.2f, 116.8f, 842.2f}, y[4] = {100, 100, 200, 200};
      ok("a strip is not a cover", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_DRAW); }
    { float x[4] = {-0.5f, 959.5f, -0.5f, 959.5f}, y[4] = {-0.5f, -0.5f, 543.5f, 543.5f};
      ok("full-width effect is not a cover", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_DRAW); }
    { float x[4] = {300, 400, 300, 400}, y[4] = {10, 10, 50, 50};
      ok("inside: plain draw", ws_ui_classify(x, y, 4, 544, x0, x1) == WS_UI_DRAW); }
    { float x[4] = {100, 200, 100, 200}, u[4] = {0, 1, 0, 1};
      ok("crossing quad clipped", ws_ui_clip_quad(x, u, x0, x1));
      ok("left x at the box", near(x[0], x0) && near(x[2], x0) && near(x[1], 200) && near(x[3], 200));
      ok("u follows", near(u[0], (x0 - 100) / 100.f) && near(u[2], u[0]) && near(u[1], 1)); }
    { float x[4] = {800, 900, 900, 800}, u[4] = {0.5f, 1, 1, 0.5f};
      ok("right side clipped", ws_ui_clip_quad(x, u, x0, x1) && near(x[1], x1) && near(x[2], x1) && near(x[0], 800));
      ok("right u", near(u[1], 0.5f + 0.5f * (x1 - 800) / 100.f)); }
    { float x[4] = {300, 400, 300, 400}, u[4] = {0, 1, 0, 1};
      ok("inside quad untouched", !ws_ui_clip_quad(x, u, x0, x1) && near(x[0], 300)); }
    { float x[4] = {-0.5f, 959.5f, -0.5f, 959.5f}, u[4] = {0, 1, 0, 1};
      ok("full-width blit left alone", !ws_ui_clip_quad(x, u, x0, x1) && near(x[0], -0.5f) && near(x[1], 959.5f)); }
    { float x[4] = {100, 200, 150, 200}, u[4] = {0, 1, 0, 1};
      ok("slanted quad left alone", !ws_ui_clip_quad(x, u, x0, x1) && near(x[0], 100)); }
    /* map: Heather's arrow (128x128 sheet, u 0..0.25, v 0.5..1) shrinks around its centre; the purple triangle stays */
    { float u[4] = {0, 0.25f, 0, 0.25f}, v[4] = {0.5f, 0.5f, 1, 1};
      ok("arrow uv recognised", ws_map_arrow_uv(u, v, 4)); }
    { float u[4] = {0.25f, 0.5f, 0.25f, 0.5f}, v[4] = {0.5f, 0.5f, 1, 1};
      ok("triangle uv not the arrow", !ws_map_arrow_uv(u, v, 4)); }
    { float u[4] = {0, 0.25f, 0, 0.25f}, v[4] = {0, 0, 0.5f, 0.5f};
      ok("upper half not the arrow", !ws_map_arrow_uv(u, v, 4)); }
    { float x[4] = {100, 126, 100, 126}, y[4] = {200, 200, 252, 252};
      ws_shrink_quad(x, y, 4, 0.6f);
      ok("shrunk around the centre", near(x[0], 105.2f) && near(x[1], 120.8f) && near(y[0], 210.4f) && near(y[2], 241.6f)); }
    puts(fails ? "ws_ui: FAILED" : "ws_ui: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_box_clip_and_cover():
    with tempfile.TemporaryDirectory(prefix="sh3-wsui-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/rt'}", str(src),
                        "-o", str(exe), "-lm"], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.returncode == 0, out.stdout
