"""Effects projected from the 3D world keep the whole 16:9 width (port/src/rt/ws_world.h).

The effect system projects blood, footprints and the other floor effects with 0x59ef90, using the 3D projection
(Hor+ in 16:9), then the GS packet translator (0x5ebdd0..0x5eca10) turns the coordinates into pixels through GsX,
which puts 2D into the centred 4:3 box: the effects slid away from the floor. The x values 0x59ef90 wrote this frame
are remembered, and GsX gives the full width to those it receives from the packet translator.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "ws_world.h"
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
int main(void) {
    WsWorld w; ws_world_reset(&w);
    ok("empty", !ws_world_has(&w, 1, 0x8123));
    ws_world_add(&w, 1, 0x8123);
    ws_world_add(&w, 1, 0x7ff0);
    ok("found in its frame", ws_world_has(&w, 1, 0x8123) && ws_world_has(&w, 1, 0x7ff0));
    ok("other value not found", !ws_world_has(&w, 1, 0x8124));
    ok("only the low 16 bits count", ws_world_has(&w, 1, 0x18123));
    ok("next frame forgets", !ws_world_has(&w, 2, 0x8123));
    for (unsigned i = 0; i < 5000; i++) ws_world_add(&w, 3, 0x6000 + i);
    ok("many values", ws_world_has(&w, 3, 0x6000) && ws_world_has(&w, 3, 0x6000 + 4999));
    ok("packet translator callers", ws_world_caller(0x5ebeea) && ws_world_caller(0x5ec11a) && ws_world_caller(0x5eca40));
    ok("other callers", !ws_world_caller(0x468b85) && !ws_world_caller(0x4dc869) && !ws_world_caller(0x5fbd0c));
    puts(fails ? "ws_world: FAILED" : "ws_world: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_world_effects_full_width():
    with tempfile.TemporaryDirectory(prefix="sh3-wsw-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/rt'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
