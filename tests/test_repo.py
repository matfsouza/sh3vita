"""Garante que arquivos do jogo e código gerado nunca entram no git."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent

IGNORED = [
    "game/sh3.exe",
    "port/gen/x86_f000.c",
    "re/out/functions.json",
    "build/pc/sh3.exe",
    "data/bgam.arc",
    ".superpowers/sdd/x/progress.md",
]


def ignored(path):
    r = subprocess.run(["git", "check-ignore", "-q", "--no-index", path], cwd=ROOT)
    return r.returncode == 0


def test_ignored_paths():
    missing = [p for p in IGNORED if not ignored(p)]
    assert missing == []


def test_project_docs_not_ignored():
    assert not ignored("docs/images/logo-white.png")
