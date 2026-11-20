"""Options menu pages (port/src/rt/options_menu.h).

The options menu is one list of rows, [0xbcc178] the selected one: rows 1..10 are the Options page, 11..21 Extra
Options, 22..28 Configuration (resolution, rendering, shadows: settings the port fixes). 0x5e3560 / 0x5e35e0 turn the
page; Configuration is skipped by turning once more. Extra Options keeps its gameplay rows; Display Mode is turned off.
Each test compiles a tiny C program against the header."""
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
CC = shutil.which("gcc")

PROGRAM = r'''
#include "options_menu.h"
#include <stdio.h>
static int fails;
static void ok(const char *what, int cond) { if (!cond) { printf("FAIL %s\n", what); fails++; } }
int main(void) {
    ok("options page", opt_page(1) == 0 && opt_page(10) == 0);
    ok("extra page", opt_page(11) == 1 && opt_page(21) == 1);
    ok("configuration page", opt_page(22) == 2 && opt_page(28) == 2);
    ok("only configuration is hidden", opt_hidden_page(22) && opt_hidden_page(28) && !opt_hidden_page(10) && !opt_hidden_page(11));
    unsigned char flags[32] = {0};
    for (int i = 11; i <= 21; i++) flags[i] = 1;
    opt_block_rows(flags);
    ok("display mode off", flags[OPT_ROW_DISPLAY_MODE] == 0);
    ok("other extras kept", flags[11] && flags[12] && flags[21]);
    ok("selection moves off a blocked row", opt_blocked_row(OPT_ROW_DISPLAY_MODE) && !opt_blocked_row(11));
    puts(fails ? "options_menu: FAILED" : "options_menu: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC, reason="gcc missing")
def test_options_menu_pages():
    with tempfile.TemporaryDirectory(prefix="sh3-om-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/rt'}", str(src),
                        "-o", str(exe)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        assert r.returncode == 0, r.stdout + r.stderr
