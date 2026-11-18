"""Compila e roda port/test/win32_pure_test.c (formatação do wsprintfA e busca de chunks do mmio)."""
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")


@pytest.mark.skipif(GCC is None, reason="gcc ausente (source tools/env.sh)")
def test_win32_pure(tmp_path):
    exe = tmp_path / "win32_pure_test.exe"
    src = ROOT / "port" / "src" / "win32"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", "-Wall", f"-I{src}", str(ROOT / "port/test/win32_pure_test.c"),
                        str(src / "guest_fmt.c"), str(src / "mmio_core.c"), str(src / "registry_defaults.c"), str(src / "resource_strings.c"), str(src / "w32_time.c"), "-o", str(exe)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    r = subprocess.run([str(exe), str(tmp_path / "riff.tmp")], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout
