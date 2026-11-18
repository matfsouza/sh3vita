"""Substituições nativas por nome: o memcpy/memmove da CRT saltam por tabelas internas que o recompilador não resolve."""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recomp"))
import x86rec  # noqa: E402


def test_name_overrides_map_ghidra_names_to_natives():
    names = {0x66F9C0: "_memcpy", 0x661D20: "_memmove", 0x401000: "FUN_00401000"}
    assert x86rec.name_overrides(names) == {0x66F9C0: "nat_memcpy", 0x661D20: "nat_memmove"}


def test_name_overrides_ignore_missing_names():
    assert x86rec.name_overrides({0x401000: "entry"}) == {}
