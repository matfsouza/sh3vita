"""Nenhuma sobra do SH4 no runtime nem no recompilador.

Um endereço fixo do SH4 dentro da imagem do SH3 escreveria na memória do jogo errado sem nenhum erro visível.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_leftovers  # noqa: E402


def test_no_sh4_addresses():
    assert check_leftovers.find_addresses(ROOT) == []


def test_no_sh4_names():
    assert check_leftovers.find_names(ROOT) == []


def test_detects_planted_address(tmp_path):
    src = tmp_path / "port" / "src" / "rt"
    src.mkdir(parents=True)
    (src / "x.c").write_text("W32(0x5d524c, w);\n")
    (tmp_path / "recomp").mkdir()
    (tmp_path / "tools").mkdir()
    (tmp_path / "tools" / "leftovers_allow.txt").write_text("")
    hits = check_leftovers.find_addresses(tmp_path)
    assert [(h[0].name, h[2]) for h in hits] == [("x.c", 0x5D524C)]


def test_detects_sh4_in_string_literal(tmp_path):
    (tmp_path / "port").mkdir()
    (tmp_path / "recomp").mkdir()
    (tmp_path / "port" / "a.c").write_text('if (!strcmp(name, "bgm.sh4")) return;\n// SH3: removido (SH4 hook)\n')
    hits = check_leftovers.find_names(tmp_path)
    assert [h[1] for h in hits] == [1]


def test_detects_unquoted_sh4_in_vita_build_files(tmp_path):
    (tmp_path / "port").mkdir()
    (tmp_path / "recomp").mkdir()
    (tmp_path / "port" / "CMakeLists.txt").write_text("project(sh4vita C)\nadd_executable(sh3 a.c)\n")
    (tmp_path / "port" / "patches").mkdir()
    (tmp_path / "port" / "patches" / "vitagl-sh4-cache.patch").write_text("x\n")
    hits = check_leftovers.find_names(tmp_path)
    assert sorted((h[0].name, h[1]) for h in hits) == [("CMakeLists.txt", 1), ("vitagl-sh4-cache.patch", 0)]


def test_detects_planted_name(tmp_path):
    (tmp_path / "port").mkdir()
    (tmp_path / "recomp").mkdir()
    (tmp_path / "port" / "a.c").write_text('#define LOG "ux0:data/sh4/log.txt"\n')
    assert len(check_leftovers.find_names(tmp_path)) == 1
