"""Compila o tradutor de shaders sozinho e roda port/test/ps_translate_test.c (BUG-008)."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_ps_translate(tmp_path):
    exe = tmp_path / "ps_translate_test.exe"
    d3d8 = ROOT / "port" / "src" / "d3d8"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-DSH3_TRANSLATOR_STANDALONE", f"-I{d3d8}",
                        str(ROOT / "port/test/ps_translate_test.c"), str(d3d8 / "shader_translate.c"), "-o", str(exe)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert r.returncode == 0, r.stdout
