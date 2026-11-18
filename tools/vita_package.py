"""Builds the "Teste Vita" folder: what goes on the Vita, in three separate folders (docs/VITA-TESTE.md). Usage:
    .venv/Scripts/python tools/vita_package.py <prepared game folder> <sh3.vpk> <output folder>
Layout:
    1 - VPK (instalar no VitaShell)/sh3.vpk
    2 - Dados do jogo (copiar uma vez)/ux0/data/sh3/   sh3.exe and data/ (4.5 GB)
    3 - Configuracao/ux0/data/sh3/env.txt              options; the user's own file is kept
Only what the Vita uses is taken: the PC Fix / Windows files and the PC saves stay out. Files are hard links to the
prepared folder when possible (no second 4.5 GB copy). "O QUE MUDOU.txt" lists what changed since the previous run,
so a new test only needs those files copied."""
import json
import os
import shutil
import sys
from pathlib import Path

VPK_DIR = "1 - VPK (instalar no VitaShell)"
DATA_DIR = "2 - Dados do jogo (copiar uma vez)"
CONFIG_DIR = "3 - Configuracao"
CHANGES = "O QUE MUDOU.txt"
NOW_DIR = "0 - COPIAR DESTA VEZ"   # only what changed, already laid out as the Vita's ux0:


def _vita_path(rel):
    """Where a package file goes on the Vita, relative to the card root (ux0/...)."""
    if rel.startswith(VPK_DIR + "/"):
        return "ux0/" + rel[len(VPK_DIR) + 1:]
    return rel.split("/", 1)[1]
ENV_DEFAULT = "SH3_STATS=1\n"


def _wanted(game, vpk):
    """{path in the package: source file}"""
    base = DATA_DIR + "/ux0/data/sh3/"
    files = {VPK_DIR + "/sh3.vpk": Path(vpk), base + "sh3.exe": game / "sh3.exe"}
    for f in sorted((game / "data").rglob("*")):
        if f.is_file():
            files[base + "data/" + f.relative_to(game / "data").as_posix()] = f
    return files


def _place(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def _drop_old_layout(out):
    """The first layout mirrored ux0: directly (ux0/, MUDOU.txt); its files are links to the same data."""
    if (out / "ux0").is_dir():
        shutil.rmtree(out / "ux0")
    (out / "MUDOU.txt").unlink(missing_ok=True)


def package(game, vpk, out):
    """Updates out from the prepared game folder and the VPK; returns the package paths that changed (sorted)."""
    game, out = Path(game), Path(out)
    if not (game / "sh3.exe").is_file() or not (game / "data").is_dir():
        raise SystemExit(f"{game}: sh3.exe and data/ expected (the folder made by tools/prepare_data.py)")
    out.mkdir(parents=True, exist_ok=True)
    _drop_old_layout(out)
    manifest_path = out / ".manifest.json"
    old = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
    new, changed = {}, []
    for rel, src in _wanted(game, vpk).items():
        st = src.stat()
        sig = [st.st_size, st.st_mtime_ns]
        new[rel] = sig
        if old.get(rel) != sig or not (out / rel).is_file():
            _place(src, out / rel)
            changed.append(rel)
    removed = sorted(rel for rel in set(old) - set(new) if not rel.startswith("ux0/"))
    for rel in removed:
        (out / rel).unlink(missing_ok=True)
    env = out / CONFIG_DIR / "ux0" / "data" / "sh3" / "env.txt"
    if not env.is_file():   # the user's own env.txt is kept
        env.parent.mkdir(parents=True, exist_ok=True)
        env.write_text(ENV_DEFAULT)
        changed.append(CONFIG_DIR + "/ux0/data/sh3/env.txt")
    manifest_path.write_text(json.dumps(new))
    changed.sort()
    now = out / NOW_DIR
    if now.is_dir():
        shutil.rmtree(now)
    for rel in changed:
        _place(out / rel, now / _vita_path(rel))
    first = not any(k.startswith(DATA_DIR) for k in old)
    if first:
        lines = ["Primeira vez: copie as tres pastas para o Vita (veja o LEIA-ME.txt).", ""]
    else:
        lines = ["O que mudou desde o pacote anterior. Tudo isso ja esta na pasta \"" + NOW_DIR + "\":",
                 "arraste a pasta ux0 de dentro dela para a raiz do Vita (o Windows junta as pastas).", ""]
        lines += [f"  {rel}" for rel in changed] or ["  nada"]
    if any(rel.startswith(VPK_DIR) for rel in changed):
        lines += ["", "VPK novo: copie o sh3.vpk para o ux0: e instale pelo VitaShell, por cima do anterior."]
    if removed:
        lines += ["", "Apagar no Vita:"] + [f"  {rel}" for rel in removed]
    (out / CHANGES).write_text("\n".join(lines) + "\n", encoding="utf-8")
    return changed


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    changed = package(*sys.argv[1:])
    print(f"{len(changed)} arquivo(s) mudaram; lista em {Path(sys.argv[3]) / CHANGES}")
