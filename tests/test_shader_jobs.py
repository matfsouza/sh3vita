"""Background shader compilation queue (port/src/d3d8/shader_jobs.h).

A GPU program the cache does not have used to be compiled on the render thread: 1.2-1.7 s frozen (a pistol shot, a
room entered for the first time). Now the render thread asks for it, skips that draw, and a worker thread compiles
it; a later frame takes the finished binaries and links them in ~20 ms. The queue is the pure state machine; the
caller holds a lock around it. Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "shader_jobs.h"
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
int main(void) {
    ShaderJobs q; sj_init(&q);
    ok("unknown key is queued", sj_request(&q, 0x11) == SJ_QUEUED);
    ok("asking again does not queue twice", sj_request(&q, 0x11) == SJ_QUEUED && sj_count(&q) == 1);
    int i = sj_next(&q);
    ok("worker takes the queued job", i >= 0 && q.key[i] == 0x11 && q.state[i] == SJ_COMPILING);
    ok("nothing else to take", sj_next(&q) < 0);
    ok("compiling while asked", sj_request(&q, 0x11) == SJ_COMPILING);
    sj_finish(&q, i, 1);
    ok("done after the worker finishes", sj_request(&q, 0x11) == SJ_DONE);
    ok("take returns the slot of a finished job", sj_take(&q, 0x11) == i);
    sj_release(&q, i);
    ok("slot free again", sj_count(&q) == 0);
    /* a failed job stays failed: the caller compiles it the old way once */
    sj_request(&q, 0x22); i = sj_next(&q); sj_finish(&q, i, 0);
    ok("failed job reported", sj_request(&q, 0x22) == SJ_FAILED);
    ok("failed job can be taken", sj_take(&q, 0x22) == i); sj_release(&q, i);
    /* first come, first served */
    sj_request(&q, 0xa); sj_request(&q, 0xb); sj_request(&q, 0xc);
    int a = sj_next(&q), b = sj_next(&q);
    ok("jobs in request order", q.key[a] == 0xa && q.key[b] == 0xb);
    /* a full table refuses new keys instead of overwriting */
    ShaderJobs f; sj_init(&f);
    for (unsigned k = 1; k <= SJ_MAX; k++) sj_request(&f, k);
    ok("full table refuses", sj_request(&f, 0x9999) == SJ_FULL);
    ok("take of an unknown key", sj_take(&f, 0x9999) < 0);
    ok("idle when nothing queued or compiling", sj_busy(&q) == 1 && (sj_init(&f), sj_busy(&f) == 0));
    printf(fails ? "FAILED\n" : "OK\n");
    return fails != 0;
}
'''


@pytest.mark.skipif(CC is None, reason="gcc not found")
def test_shader_job_queue():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "t.c"
        exe = Path(tmp) / "t.exe"
        src.write_text(PROGRAM)
        r = subprocess.run([CC, "-std=c11", "-Wall", "-I", str(ROOT / "port" / "src" / "d3d8"), str(src), "-o", str(exe)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.stdout.strip().endswith("OK"), out.stdout
