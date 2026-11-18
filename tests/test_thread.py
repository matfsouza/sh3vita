"""Threads do jogo com SDL real: CREATE_SUSPENDED, SuspendThread na própria thread, ResumeThread."""
import os
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
GCC = shutil.which("gcc")
SDL = os.environ.get("SH3_SDL2_DIR", os.path.expanduser("~/tools/sdl2/SDL2-2.32.10/x86_64-w64-mingw32"))


@pytest.mark.skipif(GCC is None or not pathlib.Path(SDL).exists(), reason="gcc ou SDL2 ausentes (source tools/env.sh)")
def test_thread(tmp_path):
    exe = tmp_path / "thread_test.exe"
    src = ROOT / "port" / "src"
    r = subprocess.run([GCC, "-std=gnu11", "-O1", f"-I{src}", f"-I{src / 'rt'}", f"-I{src / 'win32'}",
                        f"-I{SDL}/include", f"-I{SDL}/include/SDL2",
                        str(ROOT / "port/test/thread_test.c"), str(src / "win32/w32_thread.c"), str(src / "win32/handles.c"),
                        "-o", str(exe), f"-L{SDL}/lib", "-lSDL2"], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    env = dict(os.environ, PATH=f"{SDL}/bin;" + os.environ["PATH"])
    r = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60, env=env)
    assert r.returncode == 0, r.stdout
