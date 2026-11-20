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


def test_wrapper_macro_defines_imports():
    # a macro that expands to another import macro (sh3_mmio.c MMIO_HELD wraps W) defines the same imports
    text = ("#define W(name) void imp_WINMM_##name(CPU *c)\n"
            "#define HELD(name) \\\n    static void name##_on(CPU *c); \\\n    W(name) { name##_on(c); } \\\n"
            "    static void name##_on(CPU *c)\n"
            "HELD(mmioRead) {\n}\n")
    assert check_imports._macro_prefixes(text) == {"W": "imp_WINMM_", "HELD": "imp_WINMM_"}


def test_import_symbol_names():
    assert check_imports.symbol("KERNEL32.dll", "CreateFileA") == "imp_KERNEL32_CreateFileA"
    assert check_imports.symbol("DSOUND.dll", "#11") == "imp_DSOUND_ord11"
