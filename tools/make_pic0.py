"""LiveArea start picture (sce_sys/pic0.png, shown while the game launches) from the boot screen.

The Vita wants pic0.png as a 960x544 8-bit palette PNG. It is made from port/livearea_user/loading.tga
(tools/make_prep_screen.ps1 -Loading), so launching from the LiveArea and the boot screen show the same picture.
Usage: python tools/make_pic0.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_livearea as la  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def main():
    src = ROOT / "port" / "livearea_user" / "loading.tga"
    dst = ROOT / "port" / "livearea_user" / "pic0.png"
    if not src.is_file():
        sys.exit(f"{src} missing: run tools/make_prep_screen.ps1 -Loading first")
    la.pal8_png(src, "format=rgb24", dst)
    print("written", dst, dst.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
