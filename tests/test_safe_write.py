"""Saves and settings written whole or not at all (port/src/win32/safe_write.h).

The game opens its save slots, data.sys and disp.ini with CREATE_ALWAYS, which truncates the file at once: a crash or
a power cut while it writes left a cut save. Writes now go to NAME.tmp; closing the handle swaps it in through NAME.bak,
and a missing NAME is recovered from NAME.bak. Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "safe_write.h"
#include <stdio.h>
#include <string.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
static void put(const char *p, const char *s) { FILE *f = fopen(p, "wb"); fputs(s, f); fclose(f); }
static int has(const char *p, const char *s) {
    char b[64] = {0}; FILE *f = fopen(p, "rb"); if (!f) return 0; size_t n = fread(b, 1, 63, f); fclose(f);
    return n == strlen(s) && !memcmp(b, s, n);
}
static int exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
int main(int argc, char **argv) {
    (void)argc;
    char fin[512], tmp[520], bak[520];
    snprintf(fin, sizeof fin, "%s/data.001", argv[1]);
    ok("tmp name", sw_tmp_path(fin, tmp, sizeof tmp) == 0 && !strcmp(tmp + strlen(fin), ".tmp"));
    ok("bak name", sw_bak_path(fin, bak, sizeof bak) == 0 && !strcmp(bak + strlen(fin), ".bak"));
    ok("short buffer refused", sw_tmp_path(fin, tmp, 8) != 0);

    /* first save: no old file */
    put(tmp, "new1");
    ok("commit without old file", sw_commit(fin) == 0 && has(fin, "new1") && !exists(tmp) && !exists(bak));
    /* second save replaces the old one */
    put(tmp, "new2");
    ok("commit over old file", sw_commit(fin) == 0 && has(fin, "new2") && !exists(tmp) && !exists(bak));
    /* crash while writing: the old save is untouched, the half file is dropped at the next open */
    put(tmp, "ha");
    ok("old save kept", has(fin, "new2"));
    sw_recover(fin);
    ok("stale tmp removed", !exists(tmp) && has(fin, "new2"));
    /* crash between the two renames: NAME missing, NAME.bak holds the previous save */
    remove(fin); put(bak, "old");
    ok("recover from bak", sw_recover(fin) == 1 && has(fin, "old") && !exists(bak));
    /* NAME and a leftover NAME.bak: NAME wins */
    put(bak, "older");
    ok("leftover bak dropped", sw_recover(fin) == 0 && has(fin, "old") && !exists(bak));
    /* nothing there at all */
    remove(fin);
    ok("nothing to recover", sw_recover(fin) == 0 && !exists(fin));
    puts(fails ? "safe_write: FAILED" : "safe_write: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_safe_write():
    with tempfile.TemporaryDirectory(prefix="sh3-sw-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/win32'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe), tmp], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
