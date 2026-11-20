"""tools/shader_cache_bundle.py: compiled shader programs shipped in the VPK (app0:/shadercache/<abi>/programs)."""
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import shader_cache_bundle as scb  # noqa: E402

TOOL = ROOT / "tools" / "shader_cache_bundle.py"


def fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def program(vs=b"VSBIN", fs=b"FSBINARY", magic=b"SH3GXP1\0"):
    payload = struct.pack("<I", len(vs)) + vs + struct.pack("<I", len(fs)) + fs
    return magic + struct.pack("<IIQ", 1, len(payload), fnv(payload)) + payload


def test_valid_program_is_accepted():
    assert scb.check_program(program())


def test_old_writer_magic_is_accepted():
    assert scb.check_program(program(magic=b"SH4GXP1\0"))


def test_corrupt_or_truncated_programs_are_rejected():
    good = program()
    assert not scb.check_program(good[:-1])
    flipped = bytearray(good)
    flipped[-1] ^= 1
    assert not scb.check_program(bytes(flipped))
    assert not scb.check_program(program(magic=b"XXXXXXX\0"))
    assert not scb.check_program(b"")


def test_collect_keeps_valid_programs_and_writes_manifest(tmp_path):
    src = tmp_path / "shc-r1"
    src.mkdir()
    (src / "0123456789abcdef.bin").write_bytes(program())
    (src / "fedcba9876543210.bin").write_bytes(program()[:-3])   # truncated: dropped
    (src / "notes.txt").write_text("x")                           # not a program: ignored
    out = tmp_path / "bundle"
    kept = scb.collect(src, out, abi="r1")
    assert kept == ["programs.pak"]
    assert scb.read_pak((out / "programs.pak").read_bytes()) == {0x0123456789abcdef: program()}
    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["abi"] == "r1" and manifest["files"] == kept


def entry(vs, fs):
    key = fnv(vs + b"\xff" + fs)
    return b"@@SH3PROG %016x %d %d\n" % (key, len(vs), len(fs)) + vs + fs


def test_program_list_is_checked_and_shipped(tmp_path):
    src = tmp_path / "shc-r1"
    src.mkdir()
    (src / "0123456789abcdef.bin").write_bytes(program())
    lst = tmp_path / "shaders.lst"
    lst.write_bytes(entry(b"attribute vec4 aV0;\n", b"precision mediump float;\n") + entry(b"VS2", b"FS2"))
    assert scb.check_list(lst.read_bytes()) == 2
    out = tmp_path / "bundle"
    kept = scb.collect(src, out, abi="r1", program_list=lst)
    assert kept[-1] == "shaders.lst" and (out / "shaders.lst").read_bytes() == lst.read_bytes()
    files, errors = scb.validate(out)
    assert not errors and "shaders.lst" in files


def test_a_list_entry_whose_text_does_not_match_its_key_is_rejected():
    bad = bytearray(entry(b"VS", b"FS"))
    bad[-1] ^= 1
    assert scb.check_list(bytes(bad)) is None
    assert scb.check_list(entry(b"VS", b"FS")[:-1]) is None


def test_programs_are_packed_in_one_file_with_a_sorted_index(tmp_path):
    src = tmp_path / "shc-r1"
    src.mkdir()
    a, b = program(b"VA", b"FA"), program(b"VB2", b"FB22")
    (src / "00000000000000ff.bin").write_bytes(b)
    (src / "0000000000000001.bin").write_bytes(a)
    out = tmp_path / "bundle"
    kept = scb.collect(src, out, abi="r1")
    assert kept == ["programs.pak"]
    pak = (out / "programs.pak").read_bytes()
    assert pak[:8] == b"SH3PAK1\0"
    count, = struct.unpack_from("<I", pak, 8)
    entries = [struct.unpack_from("<QII", pak, 12 + 16 * i) for i in range(count)]
    assert [e[0] for e in entries] == [1, 0xff]
    assert pak[entries[0][1]:entries[0][1] + entries[0][2]] == a
    assert pak[entries[1][1]:entries[1][1] + entries[1][2]] == b
    assert scb.read_pak(pak) == {1: a, 0xff: b}


def test_validate_writes_cmake_list_and_fails_on_a_bad_file(tmp_path):
    src = tmp_path / "shc-r1"
    src.mkdir()
    (src / "0123456789abcdef.bin").write_bytes(program())
    out = tmp_path / "bundle"
    scb.collect(src, out, abi="r1")
    cm = tmp_path / "files.cmake"
    r = subprocess.run([sys.executable, str(TOOL), "validate", str(out), "--cmake-list", str(cm)])
    assert r.returncode == 0
    assert 'set(SH3_BUNDLED_CACHE_FILES "programs.pak")' in cm.read_text()
    pak = bytearray((out / "programs.pak").read_bytes())
    pak[-1] ^= 1   # one program's bytes no longer match its checksum
    (out / "programs.pak").write_bytes(bytes(pak))
    r = subprocess.run([sys.executable, str(TOOL), "validate", str(out)])
    assert r.returncode != 0
