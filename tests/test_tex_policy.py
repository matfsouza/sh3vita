"""Texture guest copies (port/src/d3d8/tex_policy.h).

The port keeps, in the game's heap, a copy of every texture level the game locked, even after it reached the GPU:
in the French session that crashed it held 58 MB and fragmented the heap until an 8.7 MB block could not be found.
A level the game filled once and has not touched for a while is released; levels the game rewrites (the glyph cache,
noise), textures made before the player reaches the world (the title menu locks them again), render targets and the
movie keep theirs. Small rectangles locked in a texture already on the GPU (the glyph cache: one 20x30 cell per new
letter, French / Spanish text) do not wait for the render thread.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "tex_policy.h"
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
static TexLevelInfo base(void) {
    TexLevelInfo i = {0};
    i.has_copy = 1; i.locks = 1; i.frames_idle = 400; i.uploaded = 1; i.pool = 1; i.bytes = 256 * 1024;
    i.made_in_world = 1;
    return i;
}
int main(void) {
    TexLevelInfo i = base();
    ok("static level released", tex_release_ok(&i));
    i = base(); i.locks = 2; ok("rewritten level kept", !tex_release_ok(&i));
    i = base(); i.rect_locked = 1; ok("rectangle-locked level kept", !tex_release_ok(&i));
    i = base(); i.frames_idle = 100; ok("recently locked kept", !tex_release_ok(&i));
    i = base(); i.uploaded = 0; ok("not on the GPU yet kept", !tex_release_ok(&i));
    i = base(); i.in_flight = 1; ok("still read by queued draws kept", !tex_release_ok(&i));
    i = base(); i.made_in_world = 0; ok("boot textures kept", !tex_release_ok(&i));
    i = base(); i.render_target = 1; ok("render target kept", !tex_release_ok(&i));
    i = base(); i.movie = 1; ok("movie kept", !tex_release_ok(&i));
    i = base(); i.pool = 0; ok("default pool kept", !tex_release_ok(&i));
    i = base(); i.bytes = 4096; ok("small level not worth it", !tex_release_ok(&i));
    i = base(); i.has_copy = 0; ok("nothing to release", !tex_release_ok(&i));
    i = base(); i.no_release = 1; ok("relocked after a release: never again", !tex_release_ok(&i));
    /* rectangle locks that skip the render-thread wait */
    ok("glyph cell, texture on the GPU", tex_rect_lock_nowait(512, 512, 1, 1, 0, 20, 30));
    ok("not yet on the GPU", !tex_rect_lock_nowait(512, 512, 1, 0, 0, 20, 30));
    ok("big rectangle waits", !tex_rect_lock_nowait(512, 512, 1, 1, 0, 300, 300));
    ok("mipmapped waits", !tex_rect_lock_nowait(512, 512, 2, 1, 0, 20, 30));
    ok("render target waits", !tex_rect_lock_nowait(512, 512, 1, 1, 1, 20, 30));
    puts(fails ? "tex_policy: FAILED" : "tex_policy: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_texture_copy_policy():
    with tempfile.TemporaryDirectory(prefix="sh3-tp-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/d3d8'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
