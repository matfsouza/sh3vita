"""Compila e roda port/test/d3d8_pure_test.c (layout das cube textures)."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_d3d8_pure(tmp_path):
    exe = tmp_path / "d3d8_pure_test.exe"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-Wall", f"-I{ROOT / 'port/src/d3d8'}",
                        str(ROOT / "port/test/d3d8_pure_test.c"), "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
