"""Compiled shader programs shipped inside the VPK.

The Vita build compiles each GPU program the first time the game needs it (0.3 to 1 s per program) and keeps the
result in ux0:data/sh3/shc-<abi>. Shipping those files in the VPK (app0:/shadercache/<abi>/programs) spares every
player the compile pauses in areas visited for the first time. Files are looked up by a hash of the shader source,
so a program the current build no longer generates is simply never read.

Usage:
  python tools/shader_cache_bundle.py collect <shc-r1 folder from the Vita> <bundle folder> [--abi r1] [--list shaders.lst]
  python tools/shader_cache_bundle.py validate <bundle folder> [--cmake-list out.cmake]
"""
import argparse
import json
import re
import shutil
import struct
import sys
from pathlib import Path

HEADER = struct.Struct("<8sIIQ")
MAGICS = (b"SH3GXP1\0", b"SH4GXP1\0")   # the second one: files written before the writer's magic was fixed
NAME = re.compile(r"^[0-9a-f]{16}\.bin$")
MAX_BYTES = 2 * 1024 * 1024


def _fnv(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def check_program(data):
    """Same checks as shc_load_file (port/src/d3d8/draw.c): header, checksum, both stage lengths."""
    if len(data) <= HEADER.size + 8 or len(data) > MAX_BYTES:
        return False
    magic, version, nbytes, checksum = HEADER.unpack_from(data)
    payload = data[HEADER.size:]
    if magic not in MAGICS or version != 1 or nbytes != len(payload) or _fnv(payload) != checksum:
        return False
    vl, = struct.unpack_from("<I", payload)
    if not 0 < vl < len(payload) - 8:
        return False
    fl, = struct.unpack_from("<I", payload, 4 + vl)
    return fl > 0 and fl == len(payload) - 8 - vl


def check_list(data):
    """Number of programs in a list written by the PC (SH3_SHADER_LIST, port/src/d3d8/draw.c shader_list_note):
    entries "@@SH3PROG <key> <vs bytes> <fs bytes>\\n" + VS + FS, each key = FNV-1a of VS, 0xff, FS. None if broken."""
    n, i = 0, 0
    while i < len(data):
        nl = data.find(b"\n", i)
        m = re.match(rb"@@SH3PROG ([0-9a-f]{16}) (\d+) (\d+)$", data[i:nl]) if nl >= 0 else None
        if not m:
            return None
        vl, fl = int(m.group(2)), int(m.group(3))
        vs, fs = data[nl + 1:nl + 1 + vl], data[nl + 1 + vl:nl + 1 + vl + fl]
        if not vl or not fl or len(vs) != vl or len(fs) != fl or _fnv(vs + b"\xff" + fs) != int(m.group(1), 16):
            return None
        n += 1
        i = nl + 1 + vl + fl
    return n


PAK_MAGIC = b"SH3PAK1\0"
PAK_ENTRY = struct.Struct("<QII")   # key, offset from the start of the file, size


def write_pak(programs):
    """One file for every program (installing hundreds of small files is slow on the Vita): "SH3PAK1\\0", count,
    entries sorted by key (key, offset, size), then each program file as written by the Vita's cache."""
    keys = sorted(programs)
    pos = len(PAK_MAGIC) + 4 + PAK_ENTRY.size * len(keys)
    index, blobs = [], []
    for k in keys:
        index.append(PAK_ENTRY.pack(k, pos, len(programs[k])))
        blobs.append(programs[k])
        pos += len(programs[k])
    return PAK_MAGIC + struct.pack("<I", len(keys)) + b"".join(index) + b"".join(blobs)


def read_pak(data):
    """{key: program bytes} of a pack; ValueError if it is malformed."""
    if data[:8] != PAK_MAGIC:
        raise ValueError("not a program pack")
    count, = struct.unpack_from("<I", data, 8)
    out = {}
    for i in range(count):
        k, off, size = PAK_ENTRY.unpack_from(data, 12 + PAK_ENTRY.size * i)
        if off + size > len(data):
            raise ValueError("entry past the end")
        out[k] = data[off:off + size]
    return out


def collect(src, out, abi="r1", program_list=None):
    """Packs the valid programs of src into out/programs.pak (and the PC's program list, compiled on the Vita at boot
    for whatever the bundle lacks) and writes out/manifest.json. Returns the kept files."""
    src, out = Path(src), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    old = out / "programs"
    if old.exists():
        shutil.rmtree(old)
    programs = {}
    for f in sorted(src.iterdir()):
        if f.is_file() and NAME.match(f.name):
            data = f.read_bytes()
            if check_program(data):
                programs[int(f.name[:16], 16)] = data
    (out / "programs.pak").write_bytes(write_pak(programs))
    kept = ["programs.pak"]
    if program_list:
        data = Path(program_list).read_bytes()
        if check_list(data) is None:
            raise SystemExit(f"{program_list}: broken program list")
        (out / "shaders.lst").write_bytes(data)
        kept.append("shaders.lst")
    (out / "manifest.json").write_text(json.dumps({"abi": abi, "files": kept}, indent=1) + "\n")
    return kept


def validate(bundle):
    """Returns (files, errors) for a bundle folder."""
    bundle = Path(bundle)
    manifest = json.loads((bundle / "manifest.json").read_text())
    errors = []
    for name in manifest["files"]:
        f = bundle / name
        if name == "shaders.lst":
            if not f.is_file() or check_list(f.read_bytes()) is None:
                errors.append(f"{name}: missing or broken")
        elif name != "programs.pak":
            errors.append(f"{name}: unexpected file")
        elif not f.is_file():
            errors.append(f"{name}: missing")
        else:
            try:
                bad = [k for k, v in read_pak(f.read_bytes()).items() if not check_program(v)]
            except ValueError as e:
                bad = [str(e)]
            if bad:
                errors.append(f"{name}: {len(bad)} corrupt program(s)")
    return manifest["files"], errors


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("collect")
    c.add_argument("src")
    c.add_argument("out")
    c.add_argument("--abi", default="r1")
    c.add_argument("--list", help="program list written on the PC with SH3_SHADER_LIST")
    v = sub.add_parser("validate")
    v.add_argument("bundle")
    v.add_argument("--cmake-list")
    a = ap.parse_args(argv)
    if a.cmd == "collect":
        collect(a.src, a.out, a.abi, a.list)
        n = len(read_pak((Path(a.out) / "programs.pak").read_bytes()))
        print(f"{n} programs packed in {a.out}" + (" (+ program list)" if a.list else ""))
        return 0
    files, errors = validate(a.bundle)
    for e in errors:
        print("shader cache bundle:", e, file=sys.stderr)
    if errors:
        return 1
    if a.cmake_list:
        Path(a.cmake_list).write_text('set(SH3_BUNDLED_CACHE_FILES "' + ";".join(files) + '")\n')
    print(f"shader cache bundle: {', '.join(files)} ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
