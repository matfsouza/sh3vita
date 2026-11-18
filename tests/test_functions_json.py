"""A exportação de funções do Ghidra (re/out/functions.json) cobre o sh3.exe inteiro."""
import json
import pathlib

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
FJ = ROOT / "re" / "out" / "functions.json"
TEXT_LO, TEXT_HI = 0x401000, 0x68A000

pytestmark = pytest.mark.skipif(not (ROOT / "game" / "sh3.exe").exists(), reason="game/sh3.exe ausente")


@pytest.fixture(scope="module")
def funcs():
    assert FJ.exists(), "rode tools/ghidra_export.ps1"
    return json.loads(FJ.read_text(encoding="utf-8-sig"))


def test_many_functions(funcs):
    assert len([f for f in funcs if not f["thunk"]]) > 3000


def test_entry_point_exported(funcs):
    assert any(int(f["a"], 16) == 0x662A6E for f in funcs)


def test_bodies_inside_text(funcs):
    bad = [f["a"] for f in funcs if not f["thunk"] for i in f["insns"] if not TEXT_LO <= i < TEXT_HI]
    assert bad == []


def test_import_thunks_present(funcs):
    assert any(f["thunk"] and str(f["target"]).startswith("EXTERNAL") for f in funcs)
