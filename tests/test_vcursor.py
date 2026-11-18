"""Puzzle cursor driven by the left stick / d-pad through the game's own mouse path (port/src/rt/vcursor.h).

The game turns the cursor position into units with x * 512 / width (0x600920) and keeps its cursor inside
x 0..512, y 64..448 (0x4dd270). Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "vcursor.h"
#include <math.h>
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
static int near(float a, float b, float e) { return fabsf(a - b) < e; }
int main(void) {
    VCursor v; vcursor_reset(&v, 256, 256);
    vcursor_step(&v, 0, 0, 0.5f);
    ok("rest stays", near(v.x, 256, 0.01f) && near(v.y, 256, 0.01f));
    vcursor_step(&v, 2500, -2500, 0.5f);
    ok("dead zone", near(v.x, 256, 0.01f) && near(v.y, 256, 0.01f));
    for (int i = 0; i < 10; i++) vcursor_step(&v, 32767, 0, 0.1f);
    ok("full right = speed per second", near(v.x, 256 + VCURSOR_SPEED, 0.5f) && near(v.y, 256, 0.01f));
    vcursor_reset(&v, 256, 256);
    for (int i = 0; i < 10; i++) vcursor_step(&v, 16000, 0, 0.1f);
    ok("half push is slower than half speed", v.x > 256 && v.x < 256 + VCURSOR_SPEED * 0.5f);
    vcursor_reset(&v, 256, 256);
    vcursor_step(&v, 0, 32767, 10.0f);
    ok("long dt clamped", near(v.y, 256 + VCURSOR_SPEED * 0.1f, 0.5f));
    vcursor_reset(&v, 500, 70);
    for (int i = 0; i < 100; i++) vcursor_step(&v, 32767, -32767, 0.1f);
    ok("kept inside", near(v.x, 512, 0.01f) && near(v.y, 64, 0.01f));
    vcursor_reset(&v, -5, 900);
    ok("reset clamps", near(v.x, 0, 0.01f) && near(v.y, 448, 0.01f));
    /* pixels the game reads back with x * 512 / w (integer) land on the same unit */
    int px, py;
    for (float x = 0; x <= 512; x += 7.3f) {
        vcursor_reset(&v, x, 64 + x * 0.75f);
        vcursor_pixels(&v, 960, 544, &px, &py);
        ok("x round trip", px * 512 / 960 == (int)v.x);
        ok("y round trip", py * 512 / 544 == (int)v.y);
    }
    puts(fails ? "vcursor: FAILED" : "vcursor: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_virtual_cursor():
    with tempfile.TemporaryDirectory(prefix="sh3-vcur-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/rt'}", str(src),
                        "-o", str(exe), "-lm"], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.returncode == 0, out.stdout
