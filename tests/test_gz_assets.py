"""tools/gz_assets.py: VPK pictures stored gzip-compressed (the Vita reads them with gzread; fewer bytes to install)."""
import gzip
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "gz_assets.py"


def test_files_are_compressed_under_the_same_name_and_decompress_to_the_original(tmp_path):
    a = tmp_path / "src" / "controls_1_0.tga"
    a.parent.mkdir()
    a.write_bytes(b"\0\0\2" + b"x" * 5000)
    out = tmp_path / "gz"
    r = subprocess.run([sys.executable, str(TOOL), str(out), str(a)])
    assert r.returncode == 0
    packed = (out / "controls_1_0.tga").read_bytes()
    assert packed[:2] == b"\x1f\x8b" and len(packed) < 5003
    assert gzip.decompress(packed) == a.read_bytes()


def test_output_is_reproducible(tmp_path):
    a = tmp_path / "loading.tga"
    a.write_bytes(bytes(range(256)) * 40)
    out1, out2 = tmp_path / "o1", tmp_path / "o2"
    subprocess.run([sys.executable, str(TOOL), str(out1), str(a)], check=True)
    subprocess.run([sys.executable, str(TOOL), str(out2), str(a)], check=True)
    assert (out1 / "loading.tga").read_bytes() == (out2 / "loading.tga").read_bytes()


def test_a_missing_input_fails(tmp_path):
    r = subprocess.run([sys.executable, str(TOOL), str(tmp_path / "o"), str(tmp_path / "nope.tga")])
    assert r.returncode != 0
