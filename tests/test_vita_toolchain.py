"""Toolchain do Vita nativo do Windows (plano M3, Task 1). Pulado se VITASDK não estiver definido (source tools/env_vita.sh)."""
import os
import pathlib
import struct
import subprocess

import pytest

VITASDK = os.environ.get("VITASDK")
pytestmark = pytest.mark.skipif(not VITASDK, reason="VITASDK não definido (source tools/env_vita.sh)")


def tool(name):
    base = pathlib.Path(VITASDK) / "bin" / name
    return base.with_suffix(".exe") if base.with_suffix(".exe").exists() else base


def test_tools_present():
    for name in ("arm-vita-eabi-gcc", "vita-elf-create", "vita-make-fself", "vita-pack-vpk", "vita-mksfoex"):
        assert tool(name).exists(), name
    assert (pathlib.Path(VITASDK) / "share" / "vita.toolchain.cmake").exists()


def test_compiles_an_arm_elf(tmp_path):
    src = tmp_path / "m.c"
    src.write_text("int main(void) { return 0; }\n")
    out = tmp_path / "m.elf"
    r = subprocess.run([str(tool("arm-vita-eabi-gcc")), str(src), "-o", str(out)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    data = out.read_bytes()
    assert data[:4] == b"\x7fELF"
    assert struct.unpack_from("<H", data, 18)[0] == 40   # e_machine EM_ARM
