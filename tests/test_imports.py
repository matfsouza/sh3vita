"""Toda função do Windows que o sh3.exe importa tem implementação no runtime (mesmo que seja um stub com log)."""
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_imports  # noqa: E402

pytestmark = pytest.mark.skipif(not (ROOT / "game" / "sh3.exe").exists(), reason="game/sh3.exe ausente")


def test_every_import_defined():
    assert check_imports.missing(ROOT) == []


def test_import_symbol_names():
    assert check_imports.symbol("KERNEL32.dll", "CreateFileA") == "imp_KERNEL32_CreateFileA"
    assert check_imports.symbol("DSOUND.dll", "#11") == "imp_DSOUND_ord11"
