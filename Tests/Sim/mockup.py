"""Put PunchPixel frames on the real UNA watch.

Usage:
    python mockup.py <frames dir> <out dir> [--gif name frame_glob ms]

Reads 240x240 frames (.ppm from the host renderer, or the simulator's 480x480
.png screenshots), masks them to the round 240 px display, and composites them
into the UNA product photo (watch/front.png) with the photo's own glass glare
laid back on top. Writes one PNG per frame and a contact sheet; with --gif
also an animated GIF of the matching frames.

The display circle in the photo was measured from the stock face's arcs:
centre (803.5, 808.5), outer arc edge 284.5 px = 111 of the display's 120 px.
"""

import glob
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
PHOTO = os.path.join(HERE, "watch", "front.png")
CX, CY = 803.5, 808.5
R_DISP = 284.5 * 120 / 111          # photo px for the display's 120 px radius
CROP = 1000                         # square crop of the photo around the watch
OUT_SIZE = 560


def load_frame(path):
    im = Image.open(path).convert("RGB")
    if im.size != (240, 240):
        im = im.resize((240, 240), Image.NEAREST)
    return im


def round_mask(size):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).ellipse((0, 0, size - 1, size - 1), fill=255)
    return m


def flat(frame, scale=2):
    """The frame as the round display shows it, on black."""
    s = 240 * scale
    big = frame.resize((s, s), Image.NEAREST)
    out = Image.new("RGB", (s, s), (0, 0, 0))
    out.paste(big, (0, 0), round_mask(s))
    return out


def on_watch(frame, photo):
    d = int(round(R_DISP * 2))
    x0, y0 = int(round(CX - R_DISP)), int(round(CY - R_DISP))
    screen = frame.resize((d, d), Image.NEAREST)
    # Glare: the photo's diagonal sheen across the upper-left of the glass,
    # redrawn (copying it would bring the stock face's digits along).
    sheen = Image.new("L", (d, d), 0)
    sd = ImageDraw.Draw(sheen)
    sd.polygon([(0, 0), (d * 0.92, 0), (0, d * 0.78)], fill=26)
    sd.polygon([(0, 0), (d * 0.55, 0), (0, d * 0.42)], fill=34)
    sheen = sheen.filter(ImageFilter.GaussianBlur(d / 40))
    screen = ImageChops.add(screen, Image.merge("RGB", (sheen, sheen, sheen)))
    out = photo.copy()
    out.paste(screen, (x0, y0), round_mask(d))
    h = CROP // 2
    out = out.crop((int(CX) - h, int(CY) - h - 20, int(CX) + h, int(CY) + h - 20))
    bg = Image.new("RGB", out.size, (245, 245, 245))
    bg.paste(out, (0, 0), out)
    return bg.resize((OUT_SIZE, OUT_SIZE), Image.LANCZOS)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    photo = Image.open(PHOTO).convert("RGBA")
    files = sorted(glob.glob(os.path.join(src, "*.ppm")) + glob.glob(os.path.join(src, "[0-9]*.png")))
    if "--gif" in sys.argv:
        i = sys.argv.index("--gif")
        name, pattern, ms = sys.argv[i + 1], sys.argv[i + 2], int(sys.argv[i + 3])
        seq = sorted(glob.glob(os.path.join(src, pattern)))
        frames = [on_watch(load_frame(f), photo).convert("P", palette=Image.ADAPTIVE) for f in seq]
        frames[0].save(os.path.join(dst, name), save_all=True, append_images=frames[1:], duration=ms, loop=0)
        print(f"{name}: {len(frames)} frames")
        files = [f for f in files if f not in seq]
    tiles = []
    for f in files:
        base = os.path.splitext(os.path.basename(f))[0]
        w = on_watch(load_frame(f), photo)
        w.save(os.path.join(dst, base + ".png"))
        flat(load_frame(f)).save(os.path.join(dst, base + "_screen.png"))
        tiles.append((base, w))
    if tiles:
        cols = 4
        t = 300
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * t, rows * (t + 14)), (245, 245, 245))
        dr = ImageDraw.Draw(sheet)
        for k, (base, w) in enumerate(tiles):
            x, y = (k % cols) * t, (k // cols) * (t + 14)
            sheet.paste(w.resize((t, t), Image.LANCZOS), (x, y + 14))
            dr.text((x + 6, y + 1), base, fill=(0, 0, 0))
        sheet.save(os.path.join(dst, "sheet.png"))
    print(f"{len(tiles)} mockups -> {dst}")


if __name__ == "__main__":
    main()
