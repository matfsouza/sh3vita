"""Procura sobras do SH4 no runtime (port/) e no recompilador (recomp/).

- Endereços: literais hexadecimais dentro da imagem do sh3.exe [0x401000, 0x717d000) em port/src e recomp/*.py.
  Exceções explicadas ficam em tools/leftovers_allow.txt: `<caminho relativo>\t<0xvalor>\t<motivo>`.
- Nomes: SH4_, sh4log, ux0:data/sh4, SILENT HILL 4 em port/ e recomp/.

Uso: python tools/check_leftovers.py   (código de saída 1 se houver sobras)
"""
import pathlib
import re
import sys

IMAGE_LO, IMAGE_HI = 0x401000, 0x717D000
HEX = re.compile(r"\b0x([0-9a-fA-F]{6,8})u?\b")
NAMES = re.compile(r"SH4_|sh4log|ux0:data/sh4|SILENT HILL 4|SH4KEY|\"[^\"\n]*sh4[^\"\n]*\"", re.IGNORECASE)
NAME_EXTS = {".c", ".h", ".py", ".txt", ".cmake", ".sh", ".ld", ".xml", ".json", ".java"}


def _allow(root):
    allow = set()
    f = root / "tools" / "leftovers_allow.txt"
    if f.exists():
        for line in f.read_text(encoding="utf-8").splitlines():
            if line.strip() and not line.startswith("#"):
                path, value, _ = line.split("\t", 2)
                allow.add((path, int(value, 16)))
    return allow


def _files(root, dirs, exts):
    for d in dirs:
        base = root / d
        if base.exists():
            for p in sorted(base.rglob("*")):
                if p.is_file() and (p.suffix in exts or p.name in ("CMakeLists.txt", "Makefile.pc")):
                    yield p


def find_addresses(root):
    """[(caminho, linha, valor)] dos endereços da imagem fora da lista de exceções."""
    root = pathlib.Path(root)
    allow, hits = _allow(root), []
    for p in _files(root, ["port/src", "recomp"], {".c", ".h", ".py", ".inc"}):
        if "third_party" in p.parts:
            continue
        rel = p.relative_to(root).as_posix()
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            for m in HEX.finditer(line):
                v = int(m.group(1), 16)
                if IMAGE_LO <= v < IMAGE_HI and (rel, v) not in allow:
                    hits.append((p, n, v))
    return hits


VITA_BUILD = re.compile(r"sh4", re.IGNORECASE)   # build files of the Vita package: any sh4, quoted or not


def _is_vita_build_file(root, p):
    rel = p.relative_to(root).as_posix()
    return rel == "port/CMakeLists.txt" or (rel.startswith("port/") and rel.count("/") == 1 and p.suffix == ".sh") \
        or rel.startswith("port/livearea/")


def find_names(root):
    """[(caminho, linha, texto)] com nomes do SH4. Linha 0: o próprio nome do arquivo (ex.: patches do vitaGL)."""
    root = pathlib.Path(root)
    hits = []
    port = root / "port"
    if port.exists():
        for p in sorted(port.rglob("*")):
            if p.is_file() and "third_party" not in p.parts and "gen" not in p.relative_to(root).parts[:2] \
                    and VITA_BUILD.search(p.name):
                hits.append((p, 0, p.name))
    for p in _files(root, ["port", "recomp"], NAME_EXTS):
        if "third_party" in p.parts:
            continue
        strict = _is_vita_build_file(root, p)
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if NAMES.search(line) or (strict and VITA_BUILD.search(line)):
                hits.append((p, n, line.strip()))
    return hits


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    a, n = find_addresses(root), find_names(root)
    for p, line, v in a:
        print(f"{p.relative_to(root).as_posix()}:{line}: endereço 0x{v:x}")
    for p, line, t in n:
        print(f"{p.relative_to(root).as_posix()}:{line}: nome do SH4: {t[:100]}")
    print(f"{len(a)} endereços, {len(n)} nomes")
    return 1 if a or n else 0


if __name__ == "__main__":
    sys.exit(main())
