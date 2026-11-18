"""tools/vita_package.py: monta a pasta "Teste Vita" (VPK, dados e configuração separados) e diz o que mudou."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import vita_package as vp  # noqa: E402

VPK = vp.VPK_DIR + "/sh3.vpk"
DATA = vp.DATA_DIR + "/ux0/data/sh3"
ENV = vp.CONFIG_DIR + "/ux0/data/sh3/env.txt"


def make_game(root):
    (root / "data" / "pic").mkdir(parents=True)
    (root / "sh3.exe").write_bytes(b"MZ exe")
    (root / "data" / "pic" / "konami.bmp").write_bytes(b"logo")
    (root / "d3d8.dll").write_bytes(b"windows only")   # PC Fix files stay out
    (root / "savedata").mkdir()
    (root / "savedata" / "data.sys").write_bytes(b"pc save")


def test_package_separates_vpk_game_data_and_config(tmp_path):
    game, vpk, out = tmp_path / "gamedata", tmp_path / "sh3.vpk", tmp_path / "Teste Vita"
    make_game(game)
    vpk.write_bytes(b"vpk v1")
    changed = vp.package(game, vpk, out)
    assert (out / VPK).read_bytes() == b"vpk v1"
    assert (out / DATA / "sh3.exe").read_bytes() == b"MZ exe"
    assert (out / DATA / "data" / "pic" / "konami.bmp").read_bytes() == b"logo"
    assert "SH3_STATS=1" in (out / ENV).read_text()
    assert not (out / DATA / "d3d8.dll").exists() and not (out / DATA / "savedata").exists()
    assert VPK in changed and DATA + "/sh3.exe" in changed
    assert "primeira vez" in (out / vp.CHANGES).read_text(encoding="utf-8").lower()


def test_second_run_lists_only_what_changed(tmp_path):
    game, vpk, out = tmp_path / "gamedata", tmp_path / "sh3.vpk", tmp_path / "Teste Vita"
    make_game(game)
    vpk.write_bytes(b"vpk v1")
    vp.package(game, vpk, out)
    assert vp.package(game, vpk, out) == []
    vpk.write_bytes(b"vpk v2, a new build")
    assert vp.package(game, vpk, out) == [VPK]
    assert (out / VPK).read_bytes() == b"vpk v2, a new build"
    now = out / vp.NOW_DIR
    assert (now / "ux0" / "sh3.vpk").read_bytes() == b"vpk v2, a new build"
    assert [p.name for p in now.rglob("*") if p.is_file()] == ["sh3.vpk"]
    text = (out / vp.CHANGES).read_text(encoding="utf-8")
    assert "sh3.vpk" in text and "sh3.exe" not in text


def test_user_env_txt_is_kept(tmp_path):
    game, vpk, out = tmp_path / "gamedata", tmp_path / "sh3.vpk", tmp_path / "Teste Vita"
    make_game(game)
    vpk.write_bytes(b"vpk")
    vp.package(game, vpk, out)
    (out / ENV).write_text("SH3_STATS=1\nSH3_MOVIES=0\n")
    vp.package(game, vpk, out)
    assert "SH3_MOVIES=0" in (out / ENV).read_text()


def test_old_mirror_layout_is_removed(tmp_path):
    game, vpk, out = tmp_path / "gamedata", tmp_path / "sh3.vpk", tmp_path / "Teste Vita"
    make_game(game)
    vpk.write_bytes(b"vpk")
    (out / "ux0" / "data" / "sh3").mkdir(parents=True)
    (out / "ux0" / "sh3.vpk").write_bytes(b"old")
    (out / "MUDOU.txt").write_text("old")
    vp.package(game, vpk, out)
    assert not (out / "ux0").exists() and not (out / "MUDOU.txt").exists()
