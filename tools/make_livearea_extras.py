"""LiveArea start button and credits on the background, in the style of the boot screens.

- port/livearea_user/startup.png (280x158, the button): the Controls wallpaper (port/assets/art/controls_wallpaper.png),
  darkened, a red glow behind the Enhanced Edition logo, vignette and grain.
- port/livearea_user/bg.png (840x500): the background tools/make_livearea.py made, with the author and the repository
  under the button (style a1 puts the button over x 30..310, y 256..414 of the background).
Both stay 8-bit palette PNGs, as the LiveArea requires. Needs Pillow (python -I tools/make_livearea_extras.py).
"""
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_livearea as la  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "port" / "assets" / "art"
LIVE = ROOT / "port" / "livearea_user"
FONTS = Path("C:/Windows/Fonts")
GOLD = (214, 168, 96)
CREAM = (238, 230, 216)


def font(name, size):
    return ImageFont.truetype(str(FONTS / name), size)


def grain(im, amount, seed):
    rnd = random.Random(seed)
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            n = rnd.randint(-amount, amount)
            r, g, b = px[x, y][:3]
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))
    return im


def vignette(size, strength):
    w, h = size
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).ellipse((-w * 0.25, -h * 0.35, w * 1.25, h * 1.35), fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(min(w, h) * 0.18))
    dark = Image.new("RGB", (w, h), (0, 0, 0))
    return dark, mask.point(lambda v: int(255 - (255 - v) * strength))


def quantize(rgb, dst):
    tmp = dst.with_name(dst.stem + "_rgb.png")
    rgb.save(tmp)
    la.pal8_png(tmp, "null", dst)
    tmp.unlink()


def startup():
    W, H = 280, 158
    wall = Image.open(ART / "controls_wallpaper.png").convert("RGB")
    # the light streak on the left, Heather kneeling by the bed on the lower right
    crop = wall.crop((560, 300, 560 + 1360, 300 + 767)).resize((W, H), Image.LANCZOS)
    im = Image.blend(crop, Image.new("RGB", (W, H), (0, 0, 0)), 0.45)
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((20, 18, W - 20, 104), fill=150)
    glow = glow.filter(ImageFilter.GaussianBlur(22))
    im = Image.composite(Image.new("RGB", (W, H), (150, 22, 12)), im, glow)
    dark, mask = vignette((W, H), 0.85)
    im = Image.composite(im, dark, mask)
    logo = Image.open(ART / "logo.png").convert("RGBA")
    lw = 236
    logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
    shadow = Image.new("RGBA", logo.size, (0, 0, 0, 0))
    shadow.putalpha(logo.getchannel("A").point(lambda a: a * 0.8).filter(ImageFilter.GaussianBlur(3)))
    lx, ly = (W - lw) // 2, 34
    im.paste((0, 0, 0), (lx + 1, ly + 2), shadow)
    im.paste(logo, (lx, ly), logo)
    d = ImageDraw.Draw(im)
    sub = "PlayStation Vita port"
    f = font("georgiai.ttf", 13)
    tw = d.textlength(sub, font=f)
    d.text(((W - tw) / 2 + 1, ly + logo.height + 9), sub, font=f, fill=(0, 0, 0))
    d.text(((W - tw) / 2, ly + logo.height + 8), sub, font=f, fill=GOLD)
    quantize(grain(im, 6, 7), LIVE / "startup.png")


def background():
    # The start button sits in the middle of the LiveArea (the system places it; the template cannot move it), so
    # Heather goes left of it (x ~140), as on the boot screen; the strip right of the picture mirrors its own dark edge
    # (seamless at the join). Right column: logo at the top, credits and the publisher's notice at the bottom.
    W, H = 840, 500
    art = Image.open(next(ART.glob("header.*"))).convert("RGB")
    art = art.resize((round(art.width * H / art.height), H), Image.LANCZOS)
    shift = 140 - round(art.width * 0.5)   # Heather sits about mid-picture
    bg = Image.new("RGB", (W, H), (0, 0, 0))
    bg.paste(art, (shift, 0))
    end = shift + art.width
    if end < W:
        bg.paste(art.crop((art.width - (W - end), 0, art.width, H)).transpose(Image.FLIP_LEFT_RIGHT), (end, 0))
    dark, mask = vignette((W, H), 0.5)
    bg = Image.composite(bg, dark, mask)
    # Right of the start button, in the free dark area (on the Vita: background x ~570..830, y ~100..440; the system's
    # own buttons cover the top from x ~270 to 570 and the page curl the top-right corner): who made it and where to
    # report problems (players from homebrew stores), big enough to read.
    x, w = 586, 236
    d = ImageDraw.Draw(bg)

    def fit(text, name, size):
        while d.textlength(text, font=font(name, size)) > w - 14 and size > 10:
            size -= 1
        return font(name, size)
    y0 = 140
    shade = Image.new("L", bg.size, 0)
    ImageDraw.Draw(shade).rounded_rectangle((x - 16, y0 - 18, x + w + 4, y0 + 222), 14, fill=165)
    bg = Image.composite(Image.new("RGB", bg.size, (0, 0, 0)), bg, shade.filter(ImageFilter.GaussianBlur(8)))
    d = ImageDraw.Draw(bg)
    d.rectangle((x, y0 + 4, x + 1, y0 + 92), fill=GOLD)
    d.text((x + 14, y0), "P O R T E D   B Y", font=font("georgia.ttf", 12), fill=(176, 164, 148))
    d.text((x + 12, y0 + 18), "Matheus", font=font("georgia.ttf", 32), fill=CREAM)
    d.text((x + 14, y0 + 64), "github.com/matfsouza/sh3vita", font=fit("github.com/matfsouza/sh3vita", "georgia.ttf", 18),
           fill=GOLD)
    y1 = y0 + 128
    d.rectangle((x, y1 + 4, x + 1, y1 + 74), fill=GOLD)
    d.text((x + 14, y1), "Found a problem?", font=font("georgia.ttf", 18), fill=CREAM)
    d.text((x + 14, y1 + 26), "Please report it as an issue:", font=fit("Please report it as an issue:", "georgia.ttf", 15),
           fill=(200, 190, 176))
    d.text((x + 14, y1 + 50), "github.com/matfsouza/sh3vita/issues",
           font=fit("github.com/matfsouza/sh3vita/issues", "georgia.ttf", 16), fill=GOLD)
    # bottom-right, as before: the publisher's logo above the unofficial-port notice
    badge = "UNOFFICIAL FAN PORT  -  GAME © KONAMI"
    bf = font("georgia.ttf", 14)
    konami = ART / "konami.png"
    if konami.exists():
        k = Image.open(konami).convert("RGBA")
        k = k.resize((96, round(k.height * 96 / k.width)), Image.LANCZOS)
        bg.paste(k, (W - 30 - k.width, H - 44 - k.height), k)
    d.text((W - 30 - d.textlength(badge, font=bf), H - 34), badge, font=bf, fill=(235, 228, 216))
    quantize(bg, LIVE / "bg.png")


def main():
    for p in ("startup.png", "bg.png"):
        src = LIVE / p
        backup = ROOT / "build" / (src.stem + "_before_extras.png")
        if src.exists() and not backup.exists():
            backup.write_bytes(src.read_bytes())
    # the start button stays the one tools/make_livearea.py makes (the logo centred over a red glow), not a picture
    # behind the logo (startup is kept for reference)
    background()
    print("written", LIVE / "bg.png")


if __name__ == "__main__":
    main()
