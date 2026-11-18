"""Compila e roda port/test/dsfx_test.c."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_dsfx(tmp_path):
    exe = tmp_path / "dsfx_test.exe"
    src = ROOT / "port" / "src" / "dx"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-Wall", f"-I{src}", str(ROOT / "port/test/dsfx_test.c"),
                        str(src / "dsfx.c"), "-lm", "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
