"""Compila e roda port/test/x87_test.c (classificação FXAM, BUG-016)."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_x87(tmp_path):
    exe = tmp_path / "x87_test.exe"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", f"-I{ROOT / 'port/src/rt'}", str(ROOT / "port/test/x87_test.c"),
                        "-o", str(exe), "-lm"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
