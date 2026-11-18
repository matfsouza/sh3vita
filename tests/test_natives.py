"""Compila e roda port/test/natives_test.c."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_natives(tmp_path):
    exe = tmp_path / "natives_test.exe"
    rt = ROOT / "port" / "src" / "rt"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", f"-I{rt}", str(ROOT / "port/test/natives_test.c"), str(rt / "natives.c"),
                        "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
