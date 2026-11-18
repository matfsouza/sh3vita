"""Compila e roda port/test/ws_layout_test.c (layout 16:9 do port, M4)."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_ws_layout(tmp_path):
    exe = tmp_path / "ws_layout_test.exe"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-Wall", f"-I{ROOT / 'port/src/rt'}",
                        str(ROOT / "port/test/ws_layout_test.c"), "-lm", "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
