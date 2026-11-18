"""Verifica se um sh3.exe é o executável suportado pelo port.

Uso: python tools/sh3exe.py <caminho do sh3.exe> (código de saída 0 só quando suportado)
"""
import dataclasses
import hashlib
import struct
import sys

SUPPORTED_SHA256 = "a51f956bd5be21fd704c4d19cf0674a175d002081da43b1d377be83226c36e5e"
SECUROM_SECTIONS = {".flio", ".urdf"}

MESSAGES = {
    "ok": "sh3.exe suportado.",
    "securom": "Este é o sh3.exe original do disco, protegido com SecuROM. O port precisa da versão sem proteção.",
    "hash": "Este sh3.exe é de outra versão. O port só aceita a versão suportada (SHA-256 " + SUPPORTED_SHA256 + ").",
    "not_pe": "O arquivo não é um executável do Windows.",
}


@dataclasses.dataclass
class ExeInfo:
    ok: bool
    reason: str
    sha256: str
    text_lo: int = 0
    text_hi: int = 0
    image_end: int = 0
    imports: dict = dataclasses.field(default_factory=dict)


def _parse(data):
    """(seções, image_base, image_size, imports); seções = [(nome, va, vsize, raw_off, raw_size)]. ValueError se não for PE32."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError("MZ")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 0xF8 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("PE")
    nsec, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    if struct.unpack_from("<H", data, opt)[0] != 0x10B:
        raise ValueError("not PE32")
    base, = struct.unpack_from("<I", data, opt + 28)
    align, = struct.unpack_from("<I", data, opt + 32)
    image_size, = struct.unpack_from("<I", data, opt + 56)
    secs = []
    for i in range(nsec):
        o = opt + opt_size + 40 * i
        name, vsize, va, rsize, roff = struct.unpack_from("<8sIIII", data, o)
        secs.append((name.rstrip(b"\0").decode("latin1"), va, vsize, roff, rsize))
    # Como o loader do Windows: a imagem vai até a última seção, arredondada ao alinhamento de seção.
    # (O SizeOfImage do exe suportado não está alinhado e é menor que o fim das seções.)
    end = max([image_size] + [va + vs for _, va, vs, _, _ in secs])
    image_size = (end + align - 1) // align * align

    def off(rva):
        for _, va, vs, ro, rs in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError(f"rva {rva:#x}")

    def cstr(o):
        return data[o:data.index(b"\0", o)].decode("latin1")

    imports = {}
    irva, = struct.unpack_from("<I", data, opt + 96 + 8)
    if irva:
        o = off(irva)
        while True:
            oft, _, _, name, ft = struct.unpack_from("<5I", data, o)
            o += 20
            if not name:
                break
            fns, t = [], off(oft or ft)
            while True:
                v, = struct.unpack_from("<I", data, t)
                t += 4
                if not v:
                    break
                fns.append(f"#{v & 0xFFFF}" if v & 0x80000000 else cstr(off(v) + 2))
            imports[cstr(off(name)).lower()] = fns
    return secs, base, image_size, imports


def verify(path):
    data = open(path, "rb").read()
    sha = hashlib.sha256(data).hexdigest()
    try:
        secs, base, image_size, imports = _parse(data)
    except (ValueError, struct.error):
        return ExeInfo(False, "not_pe", sha)
    text = next((s for s in secs if s[0] == ".text"), None)
    info = ExeInfo(sha == SUPPORTED_SHA256, "ok", sha,
                   text_lo=base + text[1] if text else 0,
                   text_hi=base + text[1] + text[2] if text else 0,
                   image_end=base + image_size,
                   imports=imports)
    if not info.ok:
        info.reason = "securom" if SECUROM_SECTIONS & {s[0] for s in secs} else "hash"
    return info


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    info = verify(argv[1])
    print(MESSAGES[info.reason])
    if info.reason != "not_pe":
        print(f"SHA-256: {info.sha256}")
    return 0 if info.ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
