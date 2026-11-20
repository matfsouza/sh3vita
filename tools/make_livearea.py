"""Builds the VPK artwork from the user's pictures in port/assets/art/ (any of .png/.jpg/.jpeg/.webp):

    icon.* square portrait -> port/livearea_user/icon0.png 128x128
    header.* ultra-wide header -> port/livearea_user/bg.png 840x500, Heather moved to the right
    gate.* "Silent Hill 3" banner -> port/livearea_user/startup.png 280x158 (the LiveArea start button)
    loading.* Heather with the light -> port/livearea_user/pic0.png 960x544 (the boot screen loading.tga: make_prep_screen.ps1 -Loading)
    logo.* Enhanced Edition logo -> used by tools/make_credits.ps1 at the top of the credits

LiveArea PNGs are 8-bit palette images (the Vita refuses full-colour PNGs in a VPK, install error 0x8010113D).
Previews at the Vita's resolution go to build/preview/. Usage: .venv/Scripts/python tools/make_livearea.py [--bg-x N]
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "port/assets/art"
LIVE = ROOT / "port/livearea_user"   # not versioned (official artwork): port/CMakeLists.txt prefers it over port/livearea
CRED = LIVE
PREVIEW = ROOT / "build/preview"


def ffmpeg():
    exe = shutil.which("ffmpeg")
    if exe:
        return exe
    for p in (Path.home() / "tools/ffmpeg").glob("*/bin/ffmpeg.exe"):
        return str(p)
    sys.exit("ffmpeg not found")


def find(stem):
    for ext in (".png", ".jpg", ".jpeg", ".webp"):
        p = ART / (stem + ext)
        if p.exists():
            return p
    sys.exit(f"missing {ART / stem}.png/.jpg")


def run(args):
    subprocess.run([ffmpeg(), "-v", "error", "-y"] + args, check=True)


def pal8_png(src, vf, dst):
    """Scaled/cropped picture as an 8-bit palette PNG."""
    run(["-i", str(src), "-vf", f"{vf},split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=sierra2_4a",
         "-frames:v", "1", str(dst)])


def rgba(src, vf, w, h):
    out = subprocess.run([ffmpeg(), "-v", "error", "-i", str(src), "-vf", vf, "-frames:v", "1", "-f", "rawvideo",
                          "-pix_fmt", "bgra", "-"], check=True, capture_output=True).stdout
    assert len(out) == w * h * 4, (len(out), w, h)
    return out


def tga(path, w, h, bgra):
    """32-bit top-left TGA, as port/src/d3d8/boot_credits.c reads it."""
    hdr = bytearray(18)
    hdr[2] = 2
    hdr[12:16] = struct.pack("<HH", w, h)
    hdr[16], hdr[17] = 32, 0x28
    path.write_bytes(bytes(hdr) + bgra)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bg-x", type=int, default=120, help="left edge of the 840-wide window in the scaled header")
    a = ap.parse_args()
    LIVE.mkdir(parents=True, exist_ok=True); CRED.mkdir(parents=True, exist_ok=True); PREVIEW.mkdir(parents=True, exist_ok=True)

    pal8_png(find("icon"), "scale=128:128:flags=lanczos", LIVE / "icon0.png")
    # header: height 500, an 840 window starting at --bg-x (the character sits right of the start button), the
    # game logo small in the top-right corner when port/assets/art/logo.* exists
    logo = next((ART / ("logo" + e) for e in (".png", ".webp") if (ART / ("logo" + e)).exists()), None)
    if logo:
        # bottom-right: the publisher's logo (port/assets/art/konami.png, when present) above the badge saying this is
        # an unofficial fan port of Konami's game; the game logo stays in the top-right corner
        konami = ART / "konami.png"
        inputs = ["-i", str(find("header")), "-i", str(logo)] + (["-i", str(konami)] if konami.exists() else [])
        pub = "[c][2]overlay=W-w-30:H-h-44[d];" if konami.exists() else "[c]null[d];"
        badge = "UNOFFICIAL FAN PORT  -  GAME (c) KONAMI" if konami.exists() else "UNOFFICIAL FAN PORT"
        run(inputs + ["-filter_complex",
             f"[0]scale=-2:500:flags=lanczos,crop=840:500:{a.bg_x}:0[b];[1]scale=230:-1:flags=lanczos,format=rgba,"
             "colorchannelmixer=aa=0.9[l];[b][l]overlay=W-w-26:22[c];"
             + ("[2]scale=96:-1:flags=lanczos[k];" if konami.exists() else "")
             + pub.replace("[2]", "[k]") +
             # badge in the bottom-right corner (Windows font)
             f"[d]drawtext=fontfile='C\\:/Windows/Fonts/georgia.ttf':text='{badge}':fontcolor=white@0.72"
             ":fontsize=14:x=w-tw-30:y=h-th-16,format=rgb24", "-frames:v", "1", str(LIVE / "bg_rgb.png")])
        pal8_png(LIVE / "bg_rgb.png", "null", LIVE / "bg.png")
        (LIVE / "bg_rgb.png").unlink()
    else:
        pal8_png(find("header"), f"scale=-2:500:flags=lanczos,crop=840:500:{a.bg_x}:0", LIVE / "bg.png")
    gate = None
    for ext in (".png", ".jpg", ".jpeg", ".webp"):
        if (ART / ("gate" + ext)).exists():
            gate = ART / ("gate" + ext)
    if gate:   # a ready-made banner: whole picture on black
        pal8_png(gate, "scale=280:158:force_original_aspect_ratio=decrease:flags=lanczos,pad=280:158:(ow-iw)/2:(oh-ih)/2:black",
                 LIVE / "startup.png")
    else:      # no banner: the logo over a red glow with scanlines and grain, a red halo behind the letters
        bg = ("color=c=black:s=560x316,format=rgb24,"
              "geq=r='clip((95*exp(-((X-280)*(X-280)/57800+(Y-158)*(Y-158)/24200))+5)*(1-0.30*eq(mod(Y\\,3)\\,0)),0,255)'"
              ":g='clip(10*exp(-((X-280)*(X-280)/57800+(Y-158)*(Y-158)/24200)),0,255)':b='4',"
              "noise=alls=22:allf=t")
        logo = find("logo")
        run(["-f", "lavfi", "-i", bg, "-i", str(logo), "-filter_complex",
             "[1]scale=470:-1:flags=lanczos,format=rgba,split[w][r];"
             "[r]colorchannelmixer=rr=1:gg=0.15:bb=0.1,gblur=sigma=9[h];"
             "[0][h]overlay=(W-w)/2:(H-h)/2[a];[a][w]overlay=(W-w)/2:(H-h)/2,format=rgb24,scale=280:158:flags=lanczos",
             "-frames:v", "1", str(LIVE / "startup_rgb.png")])
        pal8_png(LIVE / "startup_rgb.png", "null", LIVE / "startup.png")
        (LIVE / "startup_rgb.png").unlink()
    # loading picture: zoomed to fill the screen (no black bars), the window centred on the character (62% across)
    fit = "scale=-2:544:flags=lanczos,crop=960:544:'min(max(iw*0.62-480,0),iw-960)':0"
    pal8_png(find("loading"), fit, LIVE / "pic0.png")
    # the boot screen (ui/loading.tga) is made by tools/make_prep_screen.ps1 -Loading: artwork, logo and credits

    # previews: LiveArea (background on the right, start button on the left) and the loading screen
    run(["-f", "lavfi", "-i", "color=c=0x101010:s=960x544", "-i", str(LIVE / "bg.png"), "-i", str(LIVE / "startup.png"),
         "-i", str(LIVE / "icon0.png"), "-filter_complex",
         "[0][1]overlay=120:44[a];[a][2]overlay=150:300[b];[b][3]overlay=10:60", "-frames:v", "1",
         str(PREVIEW / "livearea.png")])
    shutil.copy(LIVE / "pic0.png", PREVIEW / "loading.png")
    print("written:", *(p.name for p in (LIVE / "icon0.png", LIVE / "bg.png", LIVE / "startup.png", LIVE / "pic0.png")),
          "and previews in", PREVIEW)


if __name__ == "__main__":
    main()
