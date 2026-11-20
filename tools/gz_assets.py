"""Gzip-compresses VPK pictures under their own names (port/CMakeLists.txt, at configure time).

The Vita installs a VPK by inflating and writing every file: 50 MB of raw TGA pictures made the install slow. Stored
gzip-compressed, the install writes ~3x fewer bytes and inflates almost nothing (the data is already compressed);
the port reads them with zlib's gzread (boot_credits.c, console_ui.c, vita/main.c), which also reads plain files.

Usage: python tools/gz_assets.py <output folder> <file> [<file> ...]
"""
import gzip
import sys
from pathlib import Path


def main(argv):
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    out = Path(argv[0])
    out.mkdir(parents=True, exist_ok=True)
    for name in argv[1:]:
        src = Path(name)
        if not src.is_file():
            print(f"gz_assets: missing {src}", file=sys.stderr)
            return 1
        dst = out / src.name
        data = gzip.compress(src.read_bytes(), compresslevel=9, mtime=0)   # mtime 0: the same bytes every build
        if not dst.is_file() or dst.read_bytes() != data:
            dst.write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
