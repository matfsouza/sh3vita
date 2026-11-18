"""Conteúdo do sh3.vpk gerado (plano M3, Task 4). Pulado se build/sh3.vpk não existir (bash port/build_vita.sh)."""
import pathlib
import struct
import zipfile

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
VPK = ROOT / "build" / "sh3.vpk"
pytestmark = pytest.mark.skipif(not VPK.exists(), reason="build/sh3.vpk ausente")


def sfo_values(data):
    """{key: value} of a PARAM.SFO (strings and integers)."""
    magic, _, key_table, data_table, n = struct.unpack_from("<4sIIII", data, 0)
    assert magic == b"\0PSF"
    out = {}
    for i in range(n):
        kofs, fmt, length, _, dofs = struct.unpack_from("<HHIII", data, 20 + 16 * i)
        key = data[key_table + kofs:data.index(b"\0", key_table + kofs)].decode()
        raw = data[data_table + dofs:data_table + dofs + length]
        out[key] = struct.unpack("<I", raw)[0] if fmt == 0x0404 else raw.rstrip(b"\0").decode("utf-8")
    return out


def test_vpk_has_eboot_and_sfo():
    names = zipfile.ZipFile(VPK).namelist()
    assert "eboot.bin" in names
    assert "sce_sys/param.sfo" in names
    assert "sce_sys/icon0.png" in names


def test_title_id_and_name():
    sfo = sfo_values(zipfile.ZipFile(VPK).read("sce_sys/param.sfo"))
    assert sfo["TITLE_ID"] == "SILH00003"
    assert sfo["TITLE"] == "Silent Hill 3"


def test_no_game_files_inside():
    bad = [n for n in zipfile.ZipFile(VPK).namelist() if n.lower().endswith((".arc", ".afs", ".000", ".exe"))]
    assert bad == []
