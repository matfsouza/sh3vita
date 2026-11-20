"""Lista as funções importadas pelo sh3.exe que ainda não têm implementação `imp_<DLL>_<nome>` em port/src.

Uso: python tools/check_imports.py (código de saída 1 se faltar alguma)
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import sh3exe  # noqa: E402

# Mesmos nomes que recomp/x86rec.py (Recompiler.imp_name) gera para cada slot do IAT.
DEF = re.compile(r"\b(?:void\s+)?(imp_[A-Za-z0-9_]+)\s*\(\s*CPU\s*\*\s*c\s*\)\s*\{")
MACRO = re.compile(r"^\s*([A-Z][A-Z0-9_]*)\(([A-Za-z0-9_]+)\)\s*\{", re.M)


def symbol(dll, fn):
    fn = "ord" + fn[1:] if fn.startswith("#") else fn
    return f"imp_{dll.upper().split('.')[0]}_{re.sub(r'[^A-Za-z0-9_]', '_', fn)}"


def _macro_prefixes(text):
    """#define K(name) void imp_KERNEL32_##name(CPU *c) -> {'K': 'imp_KERNEL32_'}; a macro whose body (lines joined by
    backslashes) uses one of those as M(name) gets the same prefix (sh3_mmio.c MMIO_HELD wraps W)."""
    out = {}
    for m in re.finditer(r"#define\s+([A-Z][A-Z0-9_]*)\(name\)\s+void\s+(imp_[A-Za-z0-9_]*?)##name\s*\(CPU", text):
        out[m.group(1)] = m.group(2)
    for m in re.finditer(r"#define\s+([A-Z][A-Z0-9_]*)\(name\)((?:[^\n]*\\\n)*[^\n]*)", text):
        if m.group(1) in out:
            continue
        for inner, prefix in list(out.items()):
            if re.search(rf"\b{inner}\(name\)", m.group(2)):
                out[m.group(1)] = prefix
                break
    return out


def defined(root):
    names = set()
    for p in (pathlib.Path(root) / "port" / "src").rglob("*.c"):
        text = p.read_text(encoding="utf-8", errors="replace")
        names.update(DEF.findall(text))
        prefixes = _macro_prefixes(text)
        for macro, name in MACRO.findall(text):
            if macro in prefixes:
                names.add(prefixes[macro] + name)
    return names


def missing(root):
    info = sh3exe.verify(str(pathlib.Path(root) / "game" / "sh3.exe"))
    have = defined(root)
    want = sorted({symbol(dll, fn) for dll, fns in info.imports.items() for fn in fns})
    return [s for s in want if s not in have]


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    miss = missing(root)
    for s in miss:
        print(s)
    print(f"{len(miss)} imports sem implementação")
    return 1 if miss else 0


if __name__ == "__main__":
    sys.exit(main())
