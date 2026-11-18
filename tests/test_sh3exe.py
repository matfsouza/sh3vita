"""Verificador do sh3.exe: aceita só o exe suportado e explica cada recusa."""
import pathlib
import shutil
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import sh3exe  # noqa: E402

GOOD = ROOT / "game" / "sh3.exe"
DISC = ROOT.parent / "Silent Hill 3 Computador" / "Silent Hill 3" / "sh3.exe"

need_good = pytest.mark.skipif(not GOOD.exists(), reason="game/sh3.exe ausente")
need_disc = pytest.mark.skipif(not DISC.exists(), reason="exe do disco ausente")


@need_good
def test_supported_exe():
    info = sh3exe.verify(str(GOOD))
    assert info.ok and info.reason == "ok"
    assert info.text_lo == 0x401000
    assert info.text_hi == 0x68A000
    assert info.image_end == 0x717D000
    assert "Direct3DCreate8" in info.imports["d3d8.dll"]


@need_disc
def test_disc_exe_rejected():
    info = sh3exe.verify(str(DISC))
    assert not info.ok and info.reason == "securom"


@need_good
def test_same_size_other_hash(tmp_path):
    data = bytearray(GOOD.read_bytes())
    data[0x500] ^= 0xFF
    p = tmp_path / "sh3.exe"
    p.write_bytes(bytes(data))
    info = sh3exe.verify(str(p))
    assert not info.ok and info.reason == "hash"


def test_not_pe(tmp_path):
    p = tmp_path / "sh3.exe"
    p.write_text("isto não é um executável")
    info = sh3exe.verify(str(p))
    assert not info.ok and info.reason == "not_pe"


@need_good
def test_path_with_spaces_and_accents(tmp_path):
    d = tmp_path / "ação teste"
    d.mkdir()
    shutil.copy(GOOD, d / "sh3.exe")
    assert sh3exe.verify(str(d / "sh3.exe")).ok
