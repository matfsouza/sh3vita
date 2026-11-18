"""Prepares the user's Silent Hill 3 PC files for the port: copies the game into a new folder and applies the port's
data fixes there. The source folder is never modified. Usage:
    .venv/Scripts/python tools/prepare_data.py <pc game folder> <output folder> [--audio-pack ZIP_OR_FOLDER]
                                               [--movies GAME_FOLDER_WITH_WSMSH3]
The output folder is what the PC build takes as argument and what goes to ux0:data/sh3/ on the Vita.

Fixes (docs/mapa/Preparador.md):
- sound pitch: the PC release kept the PS2 sample data but declares 44.1 kHz-based rates, so the effects play ~8.8%
  slower and lower than on the PS2 (48 kHz). The effect archives get their WAV rates multiplied by 48000/44100, the
  same correction as the community "SH3PC Sound Fixer" (rates table read from it; the code is ours).
- PS2 audio: with the "Silent Hill 3 Audio Enhancement Pack", its PS2 sounds replace the game's (instead of the pitch fix).
- widescreen movies: with a copy of the game where "Widescreen movies for Silent Hill 3" (WSMSH3, 16:9 movies of the
  HD collection) was installed, its data/movie/*.000 that differ from the user's are taken.
- movie size for the Vita: with --ffmpeg, movies wider than --movie-size (768x432 by default) are scaled down
  (decoding cost on the Vita); the audio is copied. The .000 obfuscation is undone and redone with the same seed,
  because the game's own reader checks the header (port/src/dx/sh3_movie_key.h, BUG-015).
"""
import argparse
import shutil
import subprocess
import tempfile
import struct
import zipfile
from pathlib import Path

# PC rate -> PS2 rate. The values of the community fixer, plus 25245 (2 files it does not list).
PS2_RATE = {
    22050: 24000, 24585: 26760, 25200: 27428, 25233: 27464, 25245: 27477, 25254: 27488, 28471: 30988,
    28524: 31046, 32000: 34830, 32513: 35388, 35203: 38316, 44100: 48000,
}
# (RIFF size, rate) of the short effects that keep the PC rate (the fixer reverts them: sd_00074, 00055, 00183,
# 00192 at 22050 Hz, sd_00037, 00041 at 25254 Hz).
KEEP_RATE = {(0x1A10, 22050), (0x1A14, 22050), (0x1A80, 22050), (0x1E44, 22050), (0x1FD2, 25254), (0x222A, 25254)}
# Archives with the sound effects, relative to the game folder (newv_afs only exists in some releases).
SOUND_ARCHIVES = ("data/sound/sd.afs", "data/sound/demo_afs/wav.afs", "data/sound/newv_afs/wav.afs")

_HEADER = b"WAVEfmt \x10\x00\x00\x00\x01\x00"   # PCM, 16-byte fmt chunk


def fix_sound_pitch(buf):
    """Rewrites, in place, the rate and byte rate of every PCM WAV header in buf (bytearray). Returns how many."""
    fixed = 0
    at = buf.find(_HEADER, 8)
    while at >= 0:
        riff = at - 8
        if buf[riff:riff + 4] == b"RIFF":
            size = struct.unpack_from("<I", buf, riff + 4)[0]
            rate = struct.unpack_from("<I", buf, at + 16)[0]     # after "WAVE", "fmt ", size, tag, channels
            block = struct.unpack_from("<H", buf, at + 24)[0]
            if rate in PS2_RATE and (size, rate) not in KEEP_RATE:
                new = PS2_RATE[rate]
                struct.pack_into("<II", buf, at + 16, new, new * block)
                fixed += 1
        at = buf.find(_HEADER, at + len(_HEADER))
    return fixed


AFS_ALIGN = 2048


def _afs_toc(data):
    if data[:4] != b"AFS\0":
        raise ValueError("not an AFS archive")
    n = struct.unpack_from("<I", data, 4)[0]
    return n, [struct.unpack_from("<II", data, 8 + 8 * i) for i in range(n)]


def read_afs(data):
    """The entries of an AFS archive, in TOC order."""
    _, toc = _afs_toc(data)
    return [bytes(data[o:o + s]) for o, s in toc]


def rebuild_afs(data, replace):
    """A new AFS archive with the entries of data, entry i replaced by replace[i] (any size). The header area (up to
    the first entry), the 2048 alignment and the name table at the end are kept as in the original."""
    n, toc = _afs_toc(data)
    name_at, name_len = struct.unpack_from("<II", data, 8 + 8 * n)
    first = min(o for o, _ in toc)
    out = bytearray(data[:first])
    for i, (o, s) in enumerate(toc):
        entry = replace.get(i, data[o:o + s])
        struct.pack_into("<II", out, 8 + 8 * i, len(out), len(entry))
        out += entry
        out += bytes(-len(out) % AFS_ALIGN)
    if name_at:
        struct.pack_into("<II", out, 8 + 8 * n, len(out), name_len)
        out += data[name_at:name_at + name_len]
        out += bytes(-len(out) % AFS_ALIGN)
    return bytes(out)


def pack_index(name):
    """AFS entry index of an Audio Enhancement Pack file: '12.wav' or '10_SH3_ADX_10077.adx'; None for other files."""
    head = name.split("_")[0].split(".")[0]
    return int(head) if head.isdigit() else None


def _pack_files(pack):
    """{archive name: {entry index: bytes}} of the "Silent Hill 3 Audio Enhancement Pack" (Reloaded-II mod, the
    FEmulator/AFS folder; a folder or the downloaded .zip)."""
    files = {}
    pack = Path(pack)
    if pack.is_file():
        with zipfile.ZipFile(pack) as z:
            items = [(n.split("/"), (lambda n=n: z.read(n))) for n in z.namelist() if not n.endswith("/")]
            entries = [(p, read()) for p, read in items if len(p) >= 4 and p[-4:-2] == ["FEmulator", "AFS"]]
    else:
        entries = [(f.parts, f.read_bytes()) for f in pack.rglob("*")
                   if f.is_file() and list(f.parts[-4:-2]) == ["FEmulator", "AFS"]]
    for parts, data in entries:
        index = pack_index(parts[-1])
        if index is not None:
            files.setdefault(parts[-2].lower(), {})[index] = data
    if not files:
        raise SystemExit(f"{pack}: no FEmulator/AFS files (expected the Silent Hill 3 Audio Enhancement Pack)")
    return files


MOVIE_HEADER = 0x8000
_SEED_AT = (0x55E2, 0x2A5C, 0x3D40, 0x2DDE)   # seed bytes in the header, high to low
_LCG = 0x5D588B65


def _movie_table(state):
    """The 4 KB XOR table that follows the header's key stream; state is the LCG value after the header."""
    out = bytearray()
    for _ in range(0x400):
        state = (state * _LCG + 1) & 0xFFFFFFFF
        out += struct.pack("<I", state)
    return bytes(out)


def _xor_movie(data, table, pos):
    """data XOR table[(pos + i) & 0xfff], in 4 KB-aligned chunks through big integers (fast enough for 300 MB)."""
    out = bytearray()
    chunk = 1 << 22
    for at in range(0, len(data), chunk):
        part = data[at:at + chunk]
        start = (pos + at) & 0xFFF
        key = (table[start:] + table * (len(part) // 0x1000 + 2))[:len(part)]
        out += (int.from_bytes(part, "little") ^ int.from_bytes(key, "little")).to_bytes(len(part), "little")
    return bytes(out)


def decode_movie(movie):
    """(MPEG data, seed) of an SH3 .000 (or of its first bytes); ValueError if the header does not check out."""
    head = movie[:MOVIE_HEADER]
    if len(head) < MOVIE_HEADER:
        raise ValueError("movie shorter than its header")
    seed = (head[0x55E2] << 24) | (head[0x2A5C] << 16) | (head[0x3D40] << 8) | head[0x2DDE]
    state = seed
    for i in range(MOVIE_HEADER // 4):
        state = (state * _LCG + 1) & 0xFFFFFFFF
        value = struct.unpack_from("<I", head, 4 * i)[0] ^ state
        for b in range(4):
            if 4 * i + b in _SEED_AT:
                value &= ~(0xFF << (8 * b))
        if value:
            raise ValueError("not an SH3 movie header")
    return _xor_movie(movie[MOVIE_HEADER:], _movie_table(state), 0), seed


def encode_movie(plain, seed):
    """An SH3 .000 holding the MPEG data plain, obfuscated with seed (what the game's reader undoes)."""
    head = bytearray()
    state = seed
    for _ in range(MOVIE_HEADER // 4):
        state = (state * _LCG + 1) & 0xFFFFFFFF
        head += struct.pack("<I", state)
    for k, at in enumerate(_SEED_AT):
        head[at] = (seed >> (24 - 8 * k)) & 0xFF
    return bytes(head) + _xor_movie(plain, _movie_table(state), 0)


def mpeg_size(plain):
    """(width, height) from the first MPEG video sequence header, or None."""
    at = plain.find(b"\x00\x00\x01\xb3")
    if at < 0 or at + 7 > len(plain):
        return None
    b = plain[at + 4:at + 7]
    return (b[0] << 4) | (b[1] >> 4), ((b[1] & 15) << 8) | b[2]


def shrink_movie(path, ffmpeg, size):
    """Scales the movie at path down to size (w, h) if it is wider; returns True if it was rewritten."""
    data = Path(path).read_bytes()
    plain, seed = decode_movie(data)
    dims = mpeg_size(plain[:1 << 20])
    if not dims or dims[0] <= size[0]:
        return False
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / "in.mpg", Path(tmp) / "out.mpg"
        src.write_bytes(plain)
        subprocess.run([str(ffmpeg), "-y", "-hide_banner", "-loglevel", "error", "-i", str(src),
                        "-vf", f"scale={size[0]}:{size[1]}:flags=lanczos", "-c:v", "mpeg1video", "-q:v", "3",
                        "-g", "15", "-c:a", "copy", "-f", "mpeg", str(dst)], check=True)
        Path(path).write_bytes(encode_movie(dst.read_bytes(), seed))
    return True


def movie_to_h264(path, ffmpeg):
    """Writes <movie>.mp4 next to the .000 at path: H.264 Main + AAC, for the Vita's hardware decoder (SceAvPlayer;
    the port falls back to the .000 without it). Same size as the movie. Returns True if it was (re)written."""
    path = Path(path)
    mp4 = path.with_suffix(".mp4")
    if mp4.is_file() and mp4.stat().st_mtime >= path.stat().st_mtime:
        return False
    plain, _ = decode_movie(path.read_bytes())
    with tempfile.TemporaryDirectory() as tmp:
        src, dst = Path(tmp) / "in.mpg", Path(tmp) / "out.mp4"
        src.write_bytes(plain)
        subprocess.run([str(ffmpeg), "-y", "-hide_banner", "-loglevel", "error", "-i", str(src),
                        "-c:v", "libx264", "-profile:v", "main", "-level", "3.1", "-preset", "slow", "-crf", "20",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "160k", "-ar", "48000", "-ac", "2",
                        "-movflags", "+faststart", str(dst)], check=True)
        shutil.copyfile(dst, mp4)
    return True


# Logo pictures whose art is left of centre (measured 2026-11-02, user report): texels to move right. KCET: its
# text block spans 129..357 of 512 (centre 243 instead of 255.5). Konami: the badge is centred only counting its
# slanted tail; the KONAMI lettering (what the eye centres) sits at ~244.
LOGO_SHIFT = {"konami.bmp": 11, "kcet.bmp": 12}


def shift_bmp(data, shift):
    """A 24-bit BMP with its picture moved shift pixels right; the freed columns take each row's background (its
    first pixel). ValueError if the picture would lose anything (the last columns must be background)."""
    data = bytearray(data)
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    if struct.unpack_from("<H", data, 28)[0] != 24 or not 0 < shift < w:
        raise ValueError("not a 24-bit picture, or shift out of range")
    row = (w * 3 + 3) & ~3
    for y in range(abs(h)):
        at = off + y * row
        line = data[at:at + w * 3]
        bg = line[:3]
        for x in range(w - shift, w):
            if sum(abs(a - b) for a, b in zip(line[3 * x:3 * x + 3], bg)) > 60:
                raise ValueError("the shift would cut the picture")
        data[at:at + w * 3] = bg * shift + line[:3 * (w - shift)]
    return bytes(data)


def _movies_to_take(src, movies):
    """Names of the data/movie/*.000 of the movies folder that differ from the game's."""
    folder = Path(movies) / "data" / "movie"
    found = sorted(folder.glob("*.000")) if folder.is_dir() else []
    if not found:
        raise SystemExit(f"{movies}: no data/movie/*.000 (expected a game copy with the widescreen movies installed)")
    take = []
    for path in found:
        mine = src / "data" / "movie" / path.name
        if not mine.is_file() or mine.stat().st_size != path.stat().st_size or mine.read_bytes() != path.read_bytes():
            take.append(path.name)
    return take


def prepare(src, dst, audio_pack=None, movies=None, ffmpeg=None, movie_size=(768, 432), logo_shift=LOGO_SHIFT):
    """Copies the game folder src into dst (new or existing) and applies the fixes to dst. With audio_pack (the Audio
    Enhancement Pack: PS2 sound effects with per-sound rates), its entries replace the game's and the generic pitch
    fix is skipped. Returns a report dict."""
    src, dst = Path(src).resolve(), Path(dst).resolve()
    if not (src / "sh3.exe").is_file():
        raise SystemExit(f"{src}: sh3.exe not found (pass the Silent Hill 3 PC folder)")
    if dst == src or src in dst.parents:
        raise SystemExit("the output folder must be outside the game folder")
    pack = _pack_files(audio_pack) if audio_pack else None
    take = _movies_to_take(src, movies) if movies else []
    shutil.copytree(src, dst, dirs_exist_ok=True)
    report = {"sound_pitch": 0, "audio_pack": 0, "movies": take}
    for name in take:
        shutil.copyfile(Path(movies) / "data" / "movie" / name, dst / "data" / "movie" / name)
    report["logos"] = []
    for name, shift in sorted(logo_shift.items()):
        path = dst / "data" / "pic" / name
        if path.is_file():
            path.write_bytes(shift_bmp((src / "data" / "pic" / name).read_bytes(), shift))
            report["logos"].append(name)
    report["shrunk"] = []
    if ffmpeg:
        for path in sorted((dst / "data" / "movie").glob("*.000")):
            if shrink_movie(path, ffmpeg, movie_size):
                report["shrunk"].append(path.name)
        report["h264"] = [p.name for p in sorted((dst / "data" / "movie").glob("*.000")) if movie_to_h264(p, ffmpeg)]
    if pack:
        archives = {p.name.lower(): p for p in (dst / "data").rglob("*.afs")}
        for name, replace in sorted(pack.items()):
            if name not in archives:
                raise SystemExit(f"audio pack: {name} not found in the game's data folder")
            path = archives[name]
            path.write_bytes(rebuild_afs(path.read_bytes(), replace))
            report["audio_pack"] += len(replace)
        return report
    for rel in SOUND_ARCHIVES:
        path = dst / rel
        if not path.is_file():
            continue
        buf = bytearray(path.read_bytes())
        report["sound_pitch"] += fix_sound_pitch(buf)
        path.write_bytes(buf)
    return report


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game")
    ap.add_argument("out")
    ap.add_argument("--audio-pack")
    ap.add_argument("--movies")
    ap.add_argument("--ffmpeg", help="ffmpeg.exe: scales wide movies down to --movie-size")
    ap.add_argument("--movie-size", default="768x432")
    a = ap.parse_args()
    size = tuple(int(v) for v in a.movie_size.lower().split("x"))
    print(prepare(a.game, a.out, a.audio_pack, a.movies, a.ffmpeg, size))
