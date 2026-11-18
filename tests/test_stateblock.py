"""Direct3D 8 state blocks hold only the states they recorded (port/src/d3d8/stateblock.h).

The game records render-state presets at start-up (0x413xxx: BeginStateBlock / Set* / EndStateBlock) and applies one
before its 3D sprites; D3DX does Begin / End / Capture / Apply around its text. Applying a block must restore only the
states set while it was recorded: the port restored a snapshot of everything, so the vertex-shader constants of
start-up (all zero) came back and the 3D sprites (save point seal, effects) collapsed to a point.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "stateblock.h"
#include <stdio.h>
#include <string.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
static SBState cur;
static SB b;
int main(void) {
    memset(&cur, 0, sizeof cur);
    cur.rs[27] = 1; cur.rs[19] = 5; cur.vsc[2][0] = 7.f; cur.vs = 0x2d; cur.tex[0] = 0x1234;

    /* recording: only what was set is in the block */
    sb_clear(&b);
    sb_set_rs(&b, 27, 0); sb_set_rs(&b, 20, 2);
    sb_set_tss(&b, 0, 1, 4);
    float c[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
    sb_set_vsc(&b, 52, c[0], 2);
    SBState s = cur;
    sb_apply(&b, &s);
    ok("recorded rs applied", s.rs[27] == 0 && s.rs[20] == 2);
    ok("unrecorded rs kept", s.rs[19] == 5);
    ok("recorded tss applied", s.tss[0][1] == 4);
    ok("recorded constants applied", s.vsc[52][0] == 1 && s.vsc[53][3] == 8);
    ok("unrecorded constants kept", s.vsc[2][0] == 7.f);
    ok("shaders and textures kept", s.vs == 0x2d && s.tex[0] == 0x1234);

    /* capture refreshes only the recorded states */
    cur.rs[27] = 1; cur.rs[20] = 6; cur.rs[19] = 9; cur.vsc[52][0] = 42.f;
    sb_capture(&b, &cur);
    s = cur; s.rs[27] = 0; s.rs[20] = 0; s.rs[19] = 0; s.vsc[52][0] = 0;
    sb_apply(&b, &s);
    ok("capture took current values", s.rs[27] == 1 && s.rs[20] == 6 && s.vsc[52][0] == 42.f);
    ok("capture did not add states", s.rs[19] == 0);

    /* vertex shader, pixel shader, texture */
    sb_clear(&b);
    sb_set_vs(&b, 0x2f); sb_set_ps(&b, 3); sb_set_tex(&b, 1, 0x99);
    s = cur; sb_apply(&b, &s);
    ok("vs ps tex applied", s.vs == 0x2f && s.ps == 3 && s.tex[1] == 0x99 && s.tex[0] == 0x1234);

    /* a block of everything (CreateStateBlock D3DSBT_ALL) */
    sb_clear(&b); sb_mark_all(&b); sb_capture(&b, &cur);
    SBState z; memset(&z, 0, sizeof z);
    sb_apply(&b, &z);
    ok("all block restores everything", !memcmp(&z, &cur, sizeof z));

    /* out-of-range indices are ignored */
    sb_clear(&b);
    sb_set_rs(&b, 300, 1); sb_set_tss(&b, 9, 1, 1); sb_set_vsc(&b, SB_VSC - 1, c[0], 2); sb_set_tex(&b, 9, 1);
    s = cur; sb_apply(&b, &s);
    ok("out of range ignored", !memcmp(&s, &cur, sizeof s) || s.vsc[SB_VSC - 1][0] == 1);
    puts(fails ? "stateblock: FAILED" : "stateblock: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_stateblock():
    with tempfile.TemporaryDirectory(prefix="sh3-sb-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/d3d8'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
