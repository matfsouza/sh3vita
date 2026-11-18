"""Vita buttons -> SH3 DirectInput buttons (port/src/dx/pad_mapping.h), PS2 layout.

The SH3 meaning of each DI button was measured on the PC build with a virtual pad: 0 map, 1 action/confirm,
2 cancel, 3 flashlight, 4 camera behind, 8 inventory, 9 pause. Each test compiles a tiny C program against the header.
"""
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
SDL_INC = Path(os.environ["SH3_SDL2_DIR"]) / "include/SDL2" if os.environ.get("SH3_SDL2_DIR") else \
    ROOT.parent / "tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2"
CC = os.environ.get("CC") or shutil.which("gcc")

PROGRAM = r'''
#include "pad_mapping.h"
#include <stdio.h>
#define B(n) (1u << SDL_CONTROLLER_BUTTON_##n)
#define DI(n) (1u << (n))
static int fails;
static void expect(const char *what, unsigned got, unsigned want) {
    if (got != want) { printf("FAIL %s: got %03x want %03x\n", what, got, want); fails++; }
}
int main(void) {
    expect("nothing", sh3_pad_buttons(0), 0);
    expect("cross = action/confirm", sh3_pad_buttons(B(A)), DI(SH3_DI_ACTION));
    expect("circle = cancel + run", sh3_pad_buttons(B(B)), DI(SH3_DI_CANCEL) | DI(SH3_DI_RUN) | DI(SH3_DI_RUN2));
    expect("no side steps", sh3_pad_buttons((1u << SDL_CONTROLLER_BUTTON_MAX) - 1) & (DI(6) | DI(7)), 0);
    expect("triangle = inventory", sh3_pad_buttons(B(Y)), DI(SH3_DI_INVENTORY));
    expect("square = flashlight", sh3_pad_buttons(B(X)), DI(SH3_DI_LIGHT));
    expect("R = aim", sh3_pad_buttons(B(RIGHTSHOULDER)), DI(SH3_DI_AIM));
    expect("L = camera behind", sh3_pad_buttons(B(LEFTSHOULDER)), DI(SH3_DI_CAMERA));
    expect("start = pause", sh3_pad_buttons(B(START)), DI(SH3_DI_PAUSE));
    expect("select = map", sh3_pad_buttons(B(BACK)), DI(SH3_DI_MAP));
    expect("aim and shoot together", sh3_pad_buttons(B(RIGHTSHOULDER) | B(A)), DI(SH3_DI_AIM) | DI(SH3_DI_ACTION));
    expect("pause is one button", sh3_pad_buttons(B(START)) & ~DI(SH3_DI_PAUSE), 0);
    /* the d-pad stays a d-pad (POV, menus): no DI button */
    expect("d-pad", sh3_pad_buttons(B(DPAD_UP) | B(DPAD_DOWN) | B(DPAD_LEFT) | B(DPAD_RIGHT)), 0);
    for (unsigned raw = 0; raw < (1u << SDL_CONTROLLER_BUTTON_MAX); raw++)
        if (sh3_pad_buttons(raw) & ~0xfffu) { printf("FAIL raw %x: DI index above 11\n", raw); fails++; break; }
    puts(fails ? "pad mapping: FAILED" : "pad mapping: ok");
    return fails != 0;
}
'''


@pytest.mark.skipif(not CC or not SDL_INC.exists(), reason="gcc or SDL headers missing")
def test_vita_buttons_follow_the_ps2_layout():
    with tempfile.TemporaryDirectory(prefix="sh3-pad-") as tmp:
        src, exe = Path(tmp) / "t.c", Path(tmp) / "t.exe"
        src.write_text(PROGRAM, encoding="utf-8")
        subprocess.run([CC, "-std=gnu11", "-O1", "-Wall", "-Werror", f"-I{ROOT / 'port/src/dx'}", f"-I{SDL_INC}",
                        str(src), "-o", str(exe)], check=True)
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        assert out.returncode == 0, out.stdout
