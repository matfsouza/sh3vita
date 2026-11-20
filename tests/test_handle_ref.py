"""Lifetime of a Win32 handle (port/src/win32/handle_ref.h, used by the handle table in handles.c).

Windows keeps a kernel object alive while any call on its handle is still running: CloseHandle (or mmioClose,
FindClose...) from one thread while a loader thread is in ReadFile is safe there. The port freed the object at once
and reused the handle number, so the loader's next lseek used a closed descriptor (Vita crash in __vita_fd_grab,
inventory -> Options, pic.arc read by the game's loader thread). Each call now holds a reference; closing marks the
handle closed and the last reference destroys the object and frees the number.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "handle_ref.h"
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
int main(void) {
    HandleRef r = {0};
    ok("closing an idle handle destroys it now", hr_close(&r) == 1);
    HandleRef a = {0};
    ok("acquire an open handle", hr_acquire(&a));
    ok("close while in use is deferred", hr_close(&a) == 0);
    ok("a closed handle in use is still alive", hr_alive(&a));
    ok("no new calls after the close", !hr_acquire(&a));
    ok("the last release destroys", hr_release(&a) == 1);
    ok("destroyed handle is not alive", !hr_alive(&a));
    HandleRef b = {0};
    hr_acquire(&b); hr_acquire(&b);
    ok("first of two releases keeps it", hr_release(&b) == 0);
    ok("open handle is never destroyed by a release", hr_release(&b) == 0);
    ok("open idle handle is alive", hr_alive(&b));
    ok("closing after both released destroys", hr_close(&b) == 1);
    HandleRef c = {0};
    hr_acquire(&c); hr_acquire(&c); hr_close(&c);
    ok("two calls running: first release keeps", hr_release(&c) == 0);
    ok("second release destroys", hr_release(&c) == 1);
    HandleRef d = {0};
    hr_close(&d);
    ok("a second close does nothing", hr_close(&d) == 0);
    printf(fails ? "FAILED\n" : "OK\n");
    return fails != 0;
}
'''


@pytest.mark.skipif(CC is None, reason="gcc not found")
def test_handle_lifetime():
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "t.c"
        exe = Path(tmp) / "t.exe"
        src.write_text(PROGRAM)
        r = subprocess.run([CC, "-std=c11", "-Wall", "-I", str(ROOT / "port" / "src" / "win32"), str(src), "-o", str(exe)],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.stdout.strip().endswith("OK"), out.stdout
