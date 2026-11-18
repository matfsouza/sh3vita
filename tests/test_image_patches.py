"""Patches do port sobre o sh3.exe (M4 widescreen): troca de bytes conferida, ganchos e substituições por endereço."""
import pathlib
import sys

import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recomp"))
import image_patches as ip  # noqa: E402

EXE = ROOT / "game" / "sh3.exe"


class FakePE:
    """Image of 0x20 bytes mapped at 0x401000."""

    def __init__(self):
        self.data = bytearray(range(0x20))

    def off(self, va):
        return va - 0x401000 if 0x401000 <= va < 0x401020 else None


def test_apply_replaces_bytes_when_old_bytes_match():
    pe = FakePE()
    ip.apply_patches(pe, [ip.Patch(0x401004, bytes([4, 5, 6]), bytes([0x90, 0x90, 0x90]), "test")])
    assert pe.data[3:8] == bytes([3, 0x90, 0x90, 0x90, 7])


def test_apply_refuses_a_different_exe():
    pe = FakePE()
    with pytest.raises(ValueError, match="0x401004"):
        ip.apply_patches(pe, [ip.Patch(0x401004, bytes([9, 9]), bytes([0, 0]), "test")])
    assert pe.data[4:6] == bytes([4, 5])   # nothing applied


def test_apply_refuses_size_change_and_unmapped_address():
    with pytest.raises(ValueError):
        ip.apply_patches(FakePE(), [ip.Patch(0x401004, bytes([4]), bytes([0, 0]), "test")])
    with pytest.raises(ValueError):
        ip.apply_patches(FakePE(), [ip.Patch(0x500000, bytes([4]), bytes([0]), "test")])


def test_hook_code_spills_calls_and_reloads_registers():
    code = ip.hook_code("ws_hook_test")
    assert code.index("c->eax=eax") < code.index("ws_hook_test(c);") < code.index("eax=c->eax")
    assert "esp=c->esp" in code


def test_port_vars_sit_between_image_end_and_heap():
    assert 0x717D000 <= ip.PORT_VARS < ip.PORT_VARS + 0x10000 <= 0x07200000


def test_tables_do_not_overlap():
    assert not set(ip.ADDR_OVERRIDES) & set(ip.ADDR_WRAPS)
    vas = [p.va for p in ip.IMAGE_PATCHES]
    assert len(vas) == len(set(vas))


@pytest.mark.skipif(not EXE.exists(), reason="game/sh3.exe ausente")
def test_every_patch_matches_the_supported_exe():
    sys.path.insert(0, str(ROOT / "recomp"))
    from pe import PE
    pe = PE(EXE)
    pe.data = bytearray(pe.data)
    ip.apply_patches(pe, ip.IMAGE_PATCHES)   # raises if any old bytes differ
    text = next(s for s in pe.sections if s[0] == ".text")
    for va in list(ip.HOOKS) + list(ip.ADDR_OVERRIDES) + list(ip.ADDR_WRAPS):
        assert text[1] <= va < text[1] + text[2], hex(va)


def test_puzzle_cursor_is_slower():
    """Puzzle cursor (0x4dd270 init, speed x and y at 0xbb316c/0xbb3170): 210 -> 150 units/s, both axes."""
    import struct
    by_va = {p.va: p for p in ip.IMAGE_PATCHES}
    for va in (0x4DD2B2, 0x4DD2BC):
        p = by_va[va]
        assert struct.unpack("<f", p.old)[0] == 210.0
        assert struct.unpack("<f", p.new)[0] == 150.0
