"""O recompilador não tem nomes indefinidos (BUG-002: definições genéricas cortadas junto com tabelas do SH4)."""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def test_no_undefined_names():
    r = subprocess.run([sys.executable, "-m", "pyflakes", "recomp/x86rec.py", "recomp/pe.py", "tools"],
                       cwd=ROOT, capture_output=True, text=True)
    undefined = [l for l in r.stdout.splitlines() if "undefined name" in l]
    assert undefined == []
