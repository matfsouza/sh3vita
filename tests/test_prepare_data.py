"""tools/prepare_data.py: the port's data preparer (PS2 sound, widescreen movies; docs/mapa/Preparador e mods.md)."""
import pathlib
import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import prepare_data as pd  # noqa: E402


def bmp(w, h, ink):
    """A bottom-up 24-bit BMP on a white background; ink: set of (x, y) painted red."""
    row = (w * 3 + 3) & ~3
    pixels = bytearray()
    for y in range(h):
        line = bytearray()
        for x in range(w):
            line += b"\x00\x00\xc0" if (x, y) in ink else b"\xff\xff\xff"
        pixels += line + bytes(row - len(line))
    head = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    return head + info + bytes(pixels)


def ink_columns(data):
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    row = (w * 3 + 3) & ~3
    return sorted({x for y in range(h) for x in range(w) if data[off + y * row + 3 * x:off + y * row + 3 * x + 3] != b"\xff\xff\xff"})


def test_logo_content_moves_right_and_the_freed_columns_take_the_background():
    src = bmp(32, 4, {(5, 1), (20, 2)})
    out = pd.shift_bmp(src, 3)
    assert ink_columns(out) == [8, 23]
    assert len(out) == len(src) and out[:54] == src[:54]


def test_logo_shift_refuses_to_cut_the_picture():
    with pytest.raises(ValueError):
        pd.shift_bmp(bmp(32, 4, {(30, 1)}), 3)


def test_prepare_centres_the_logos_of_the_known_pictures(tmp_path):
    src = tmp_path / "game"
    (src / "data" / "pic").mkdir(parents=True)
    (src / "sh3.exe").write_bytes(b"MZ")
    (src / "data" / "pic" / "kcet.bmp").write_bytes(bmp(64, 4, {(10, 1)}))
    (src / "data" / "pic" / "other.bmp").write_bytes(bmp(64, 4, {(10, 1)}))
    report = pd.prepare(src, tmp_path / "out", logo_shift={"kcet.bmp": 12})
    assert ink_columns((tmp_path / "out" / "data" / "pic" / "kcet.bmp").read_bytes()) == [22]
    assert ink_columns((tmp_path / "out" / "data" / "pic" / "other.bmp").read_bytes()) == [10]
    assert report["logos"] == ["kcet.bmp"]


def wav(rate, channels=1, data_len=100):
    """A PCM 16-bit RIFF/WAVE header + data, as stored in the game's .afs archives."""
    block = 2 * channels
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * block, block, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", data_len) + bytes(data_len)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def rate_of(buf, at=0):
    return struct.unpack_from("<HII", buf, at + 22)


def test_pc_rates_become_ps2_rates():
    buf = bytearray(b"pad!" + wav(22050) + wav(44100, 2) + wav(32000))
    assert pd.fix_sound_pitch(buf) == 3
    first = 4
    second = first + len(wav(22050))
    third = second + len(wav(44100, 2))
    assert rate_of(buf, first) == (1, 24000, 48000)
    assert rate_of(buf, second) == (2, 48000, 192000)
    assert rate_of(buf, third) == (1, 34830, 69660)


def test_every_rate_in_the_game_archives_is_mapped():
    # the rates found in sd.afs and demo_afs/wav.afs; 25245 is not in the original fixer's list (2 files)
    for rate in (22050, 24585, 25200, 25233, 25245, 25254, 28471, 28524, 32000, 32513, 35203, 44100):
        assert rate in pd.PS2_RATE
        assert abs(pd.PS2_RATE[rate] / rate - 48000 / 44100) < 0.001


def test_short_files_keep_their_rate():
    # 6 short effects stay at the PC rate (sd_00074, 00055, 00183, 00037, 00041, 00192 in the fixer)
    short = wav(22050, data_len=0x1A10 - 36)
    assert struct.unpack_from("<I", short, 4)[0] == 0x1A10
    buf = bytearray(short)
    assert pd.fix_sound_pitch(buf) == 0
    assert bytes(buf) == short


def test_unknown_rates_and_non_wav_bytes_are_untouched():
    other = wav(11025) + b"RIFF\x10\x00\x00\x00AVI LIST"
    buf = bytearray(other)
    assert pd.fix_sound_pitch(buf) == 0
    assert bytes(buf) == other


def test_fix_is_idempotent_on_prepared_data():
    buf = bytearray(wav(22050))
    pd.fix_sound_pitch(buf)
    once = bytes(buf)
    assert pd.fix_sound_pitch(buf) == 0
    assert bytes(buf) == once


def afs(entries, first=0x1000, names=True):
    """An AFS archive like the game's: TOC, entries aligned to 2048, optional name table at the end."""
    out = bytearray(first)
    out[0:8] = b"AFS\0" + struct.pack("<I", len(entries))
    toc = []
    for e in entries:
        toc.append((len(out), len(e)))
        out += e + bytes(-len(e) % 2048)
    for i, (o, s) in enumerate(toc):
        struct.pack_into("<II", out, 8 + 8 * i, o, s)
    if names:
        table = b"".join(f"{i}.wav".encode().ljust(32, b"\0") + bytes(16) for i in range(len(entries)))
        struct.pack_into("<II", out, 8 + 8 * len(entries), len(out), len(table))
        out += table + bytes(-len(table) % 2048)
    return bytes(out)


def test_afs_entries_are_replaced_by_index_and_the_rest_kept():
    old = [b"A" * 3000, b"B" * 100, b"C" * 5000]
    new = pd.rebuild_afs(afs(old), {1: b"N" * 9000})   # bigger than the original entry
    assert pd.read_afs(new) == [old[0], b"N" * 9000, old[2]]
    n = struct.unpack_from("<I", new, 4)[0]
    offsets = [struct.unpack_from("<I", new, 8 + 8 * i)[0] for i in range(n)]
    assert offsets[0] == 0x1000 and all(o % 2048 == 0 for o in offsets)   # same header area, aligned entries
    name_at, name_len = struct.unpack_from("<II", new, 8 + 8 * n)
    assert new[name_at:name_at + 5] == b"0.wav" and name_len == 3 * 48   # name table carried over


def test_afs_without_name_table_and_with_a_large_header_area():
    old = [b"x" * 10, b"y" * 20]
    src = afs(old, first=0x80000, names=False)   # like demo_afs/bc.afs
    new = pd.rebuild_afs(src, {0: b"z" * 30})
    assert pd.read_afs(new) == [b"z" * 30, old[1]]
    assert struct.unpack_from("<I", new, 8)[0] == 0x80000
    assert struct.unpack_from("<II", new, 8 + 16) == (0, 0)


def test_pack_entry_names_give_the_index():
    assert pd.pack_index("12.wav") == 12
    assert pd.pack_index("10_SH3_ADX_10077.adx") == 10
    assert pd.pack_index("readme.txt") is None


def test_prepare_with_the_audio_pack_replaces_sounds_and_skips_the_pitch_fix(tmp_path):
    src, dst, pack = tmp_path / "game", tmp_path / "out", tmp_path / "pack"
    (src / "data" / "sound" / "demo_afs").mkdir(parents=True)
    (src / "sh3.exe").write_bytes(b"MZ")
    (src / "data" / "sound" / "sd.afs").write_bytes(afs([wav(22050), wav(22050), b"\x80\x00music"]))
    (src / "data" / "sound" / "demo_afs" / "bc.afs").write_bytes(afs([b"adx0", b"adx1"], names=False))
    root = pack / "Silent Hill 3 Audio Enhancement Pack" / "FEmulator" / "AFS"
    (root / "sd.afs").mkdir(parents=True)
    (root / "bc.afs").mkdir()
    (root / "sd.afs" / "0.wav").write_bytes(wav(20000, data_len=5000))
    (root / "sd.afs" / "1.wav").write_bytes(wav(48000))
    (root / "bc.afs" / "1_e112.adx").write_bytes(b"ps2adx1")
    report = pd.prepare(src, dst, audio_pack=pack)
    sd = pd.read_afs((dst / "data" / "sound" / "sd.afs").read_bytes())
    assert sd == [wav(20000, data_len=5000), wav(48000), b"\x80\x00music"]   # pack rates kept, no 1.0884 on top
    assert pd.read_afs((dst / "data" / "sound" / "demo_afs" / "bc.afs").read_bytes()) == [b"adx0", b"ps2adx1"]
    assert report["audio_pack"] == 3 and report["sound_pitch"] == 0


def test_prepare_takes_the_widescreen_movies_that_differ(tmp_path):
    src, dst, ws = tmp_path / "game", tmp_path / "out", tmp_path / "ws"
    for root, movies in ((src, {"op.000": b"4:3 op", "ed_b.000": b"ending b"}),
                         (ws, {"op.000": b"16:9 op from the HD collection", "ed_b.000": b"ending b"})):
        (root / "data" / "movie").mkdir(parents=True)
        for name, data in movies.items():
            (root / "data" / "movie" / name).write_bytes(data)
    (src / "sh3.exe").write_bytes(b"MZ")
    report = pd.prepare(src, dst, movies=ws)
    assert (dst / "data" / "movie" / "op.000").read_bytes() == b"16:9 op from the HD collection"
    assert (dst / "data" / "movie" / "ed_b.000").read_bytes() == b"ending b"
    assert (src / "data" / "movie" / "op.000").read_bytes() == b"4:3 op"
    assert report["movies"] == ["op.000"]


def test_prepare_refuses_a_movie_folder_without_movies(tmp_path):
    src = tmp_path / "game"
    src.mkdir()
    (src / "sh3.exe").write_bytes(b"MZ")
    with pytest.raises(SystemExit):
        pd.prepare(src, tmp_path / "out", movies=tmp_path / "nothing")


def test_prepare_copies_game_and_never_touches_the_source(tmp_path):
    src, dst = tmp_path / "game", tmp_path / "out"
    (src / "data" / "sound" / "demo_afs").mkdir(parents=True)
    (src / "sh3.exe").write_bytes(b"MZ")
    (src / "data" / "sound" / "sd.afs").write_bytes(wav(22050))
    (src / "data" / "sound" / "demo_afs" / "wav.afs").write_bytes(wav(44100))
    (src / "data" / "sound" / "demo_afs" / "bc.afs").write_bytes(wav(22050))
    report = pd.prepare(src, dst)
    assert (dst / "sh3.exe").read_bytes() == b"MZ"
    assert rate_of((dst / "data" / "sound" / "sd.afs").read_bytes())[1] == 24000
    assert rate_of((dst / "data" / "sound" / "demo_afs" / "wav.afs").read_bytes())[1] == 48000
    # only the effect archives the fixer covers change; BGM/scene archives are copied as they are
    assert rate_of((dst / "data" / "sound" / "demo_afs" / "bc.afs").read_bytes())[1] == 22050
    assert rate_of((src / "data" / "sound" / "sd.afs").read_bytes())[1] == 22050
    assert report["sound_pitch"] == 2


def mpeg(w, h, payload=b"video"):
    """Start of an MPEG-1 program stream: pack header, then a video sequence header with the size."""
    seq = b"\x00\x00\x01\xb3" + bytes([w >> 4, ((w & 15) << 4) | (h >> 8), h & 255, 0x14]) + bytes(8)
    return b"\x00\x00\x01\xba\x21\x00\x01\x00\x01\x80\x00\x01" + seq + payload


def test_movie_obfuscation_round_trip_and_header_check():
    plain = mpeg(1280, 720) + bytes(range(256)) * 40
    movie = pd.encode_movie(plain, seed=0x5F12A07C)
    assert len(movie) == pd.MOVIE_HEADER + len(plain)
    assert movie[pd.MOVIE_HEADER:pd.MOVIE_HEADER + 4] != plain[:4]
    assert pd.decode_movie(movie) == (plain, 0x5F12A07C)
    damaged = bytearray(movie)
    damaged[100] ^= 1
    with pytest.raises(ValueError):
        pd.decode_movie(bytes(damaged))


def test_movie_size_comes_from_the_sequence_header():
    assert pd.mpeg_size(mpeg(1280, 720)) == (1280, 720)
    assert pd.mpeg_size(mpeg(512, 384)) == (512, 384)
    assert pd.mpeg_size(b"no video here") is None


def test_real_movie_decodes_to_mpeg1():
    src = pathlib.Path(__file__).resolve().parent.parent / "game" / "data" / "movie" / "huxx.000"
    if not src.is_file():
        pytest.skip("game files not present")
    head = src.open("rb").read(pd.MOVIE_HEADER + 4096)
    plain, _ = pd.decode_movie(head)
    assert plain[:4] == b"\x00\x00\x01\xba" and plain[4] >> 4 == 2   # MPEG-1 pack
    assert pd.mpeg_size(plain) == (512, 384)


FFMPEG = Path(__import__("os").environ.get("SH3_FFMPEG", "ffmpeg.exe"))


@pytest.mark.skipif(not FFMPEG.is_file(), reason="ffmpeg not installed")
def test_movie_to_h264_writes_an_mp4_next_to_the_movie(tmp_path):
    import subprocess
    mpg = tmp_path / "src.mpg"
    subprocess.run([str(FFMPEG), "-y", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc=size=512x384:rate=30:duration=1",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-c:v", "mpeg1video", "-c:a", "mp2",
                    "-f", "mpeg", str(mpg)], check=True)
    movie = tmp_path / "op.000"
    movie.write_bytes(pd.encode_movie(mpg.read_bytes(), 0x12345678))
    assert pd.movie_to_h264(movie, FFMPEG) is True
    mp4 = tmp_path / "op.mp4"
    head = mp4.read_bytes()[:64]
    assert b"ftyp" in head and mp4.stat().st_size > 1000
    assert pd.movie_to_h264(movie, FFMPEG) is False   # up to date: not converted again
