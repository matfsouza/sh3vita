"""Compila e roda port/test/sh3_movie_key_test.c."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_sh3_movie_key(tmp_path):
    exe = tmp_path / "sh3_movie_key_test.exe"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-Wall", f"-I{ROOT / 'port/src/dx'}",
                        str(ROOT / "port/test/sh3_movie_key_test.c"), "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
