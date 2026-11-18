"""Instruções inteiras da CRT que o tradutor não cobria (BUG-020): rotações gerais, cmovcc, xadd, xlatb, lods, clc/stc/cmc.

O comportamento de cada forma usada pelo jogo é comparado com a CPU de referência pelo recomp/difftest.py; este teste
garante que nenhuma delas cai em rt_unimpl (o difftest exclui essas formas em silêncio).
"""
import collections
import pathlib
import sys

import capstone
import pytest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "recomp"))
import x86rec  # noqa: E402

CASES = {
    "rol eax, cl": "d3c0", "rol eax, 5": "c1c005", "ror byte ptr [esi], 3": "c00e03", "ror cx, 1": "66d1c9",
    "rcl eax, 3": "c1d003", "rcr eax, cl": "d3d8", "rcr dl, 2": "c0da02",
    "cmove eax, ecx": "0f44c1", "cmovb eax, dword ptr [ebx]": "0f4203", "cmova edx, esi": "0f47d6",
    "xadd dword ptr [ecx], eax": "0fc101", "xlatb": "d7", "lodsb": "ac", "lodsd": "ad",
    "clc": "f8", "stc": "f9", "cmc": "f5",
}


def translate(code_hex):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    ins = next(md.disasm(bytes.fromhex(code_hex), 0x401000))
    rec = x86rec.Recompiler.__new__(x86rec.Recompiler)   # only what translating one instruction needs
    rec.names, rec.x87net, rec.iat, rec.entries = {}, {}, {}, set()
    rec.report, rec.report_sites = collections.Counter(), collections.defaultdict(list)
    f = x86rec.Func.__new__(x86rec.Func)
    f.rec, f.entry, f.insns, f.addrs = rec, ins.address, [ins], {ins.address}
    f.labels, f.switches, f.out, f.flagsrc = set(), {}, [], None
    f.static = False
    f.insn(ins)
    return ins, "\n".join(f.out)


@pytest.mark.parametrize("name", sorted(CASES))
def test_translated_without_unimpl(name):
    ins, body = translate(CASES[name])
    assert f"{ins.mnemonic} {ins.op_str}".strip().startswith(name)   # lods* also prints its implicit operands
    assert "rt_unimpl" not in body, body
