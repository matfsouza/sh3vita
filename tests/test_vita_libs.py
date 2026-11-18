"""Bibliotecas do Vita que o port linka (plano M3, Task 2). Pulado se VITASDK não estiver definido."""
import os
import pathlib
import subprocess

import pytest

VITASDK = os.environ.get("VITASDK")
pytestmark = pytest.mark.skipif(not VITASDK, reason="VITASDK não definido (source tools/env_vita.sh)")
LIB = pathlib.Path(VITASDK or ".") / "arm-vita-eabi" / "lib"

# The libraries named in port/CMakeLists.txt target_link_libraries (besides the SDK's own *_stub libraries).
LIBS = ["vitaGL", "vitashark", "SceShaccCgExt", "mathneon", "taihen_stub", "SDL2", "z"]


@pytest.mark.parametrize("name", LIBS)
def test_library_installed(name):
    assert (LIB / f"lib{name}.a").exists(), f"lib{name}.a"


def test_vitagl_preserves_depth_between_scenes():
    # Same check as port/build.sh: without STORE_DEPTH_STENCIL=1 actors vanish after render-target switches.
    nm = pathlib.Path(VITASDK) / "bin" / "arm-vita-eabi-nm.exe"
    r = subprocess.run([str(nm), "-u", str(LIB / "libvitaGL.a")], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    for mode in ("Load", "Store"):
        assert f"sceGxmDepthStencilSurfaceSetForce{mode}Mode" in r.stdout, mode
