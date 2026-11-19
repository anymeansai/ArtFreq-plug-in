#!/usr/bin/env python3
"""Makes the Windows installer's pictures from the AF emblem (docs/artfreq-emblem.png).

    python3 installer/windows/make-images.py

Writes, into installer/windows/images/ (committed; run this again only if the emblem changes):

- wizard-<w>x<h>.bmp: the tall picture on the left of the installer's first and last pages. The
  AF letters on the emblem's near-black, with "ArtFreq" and "by AnyMeans" under them. Inno Setup
  keeps its area at 164:314 and picks the file nearest the size it needs (it depends on the
  screen's scaling and on the Inno Setup version: 164x314 at 100 % up to 534x1022 at 250 %).
- small-<n>.bmp: the emblem in the top right corner of the other pages, on white (58 px square
  at 100 %, up to 159 px).
- ArtFreq.ico: the installer's and the uninstaller's icon, 16 to 256 px: the classic layout,
  32-bit bitmaps with their transparency mask up to 128 px and a PNG at 256 px.

24-bit BMPs without transparency: every Inno Setup 6 reads those. Needs Pillow.
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
EMBLEM = os.path.join(REPO, "docs", "artfreq-emblem.png")
FONTS = os.path.join(REPO, "assets", "fonts")
OUT = os.path.join(HERE, "images")

INK = (11, 13, 18)          # the emblem's tile, docs/brand/README.md's ink
WHITE = (255, 255, 255)
GREY = (140, 148, 166)      # "by AnyMeans", as on the macOS installer's dark page

# Sizes of the image areas Inno Setup 6.0 to 6.7 document, 100 % to 250 % (both families: the
# defaults changed in 6.6). Setup picks the nearest file and scales it, keeping the aspect ratio.
WIZARD_SIZES = [(164, 314), (202, 386), (240, 459), (315, 604), (416, 797), (534, 1022)]
SMALL_SIZES = [58, 71, 85, 97, 112, 124, 147, 159]
ICON_SIZES = [16, 24, 32, 48, 64, 128, 256]

SUPERSAMPLE = 4  # draw at 4x, then shrink: smooth edges and text at every size


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def letters(emblem):
    """The AF letters on the tile's ink, without the tile's border and rounded corners."""
    # The letters lie within x 186..838, y 205..818 of the 1024 px emblem; this square is
    # inside the tile everywhere (its corners are well inside the rounded corners' arcs).
    return emblem.crop((140, 140, 884, 884)).convert("RGB")


def centred_text(draw, width, y, text, face, fill):
    left, top, right, bottom = draw.textbbox((0, 0), text, font=face)
    draw.text(((width - (right - left)) / 2 - left, y - top), text, font=face, fill=fill)
    return bottom - top


def wizard_image(emblem, w, h):
    W, H = w * SUPERSAMPLE, h * SUPERSAMPLE
    image = Image.new("RGB", (W, H), INK)
    art = letters(emblem)
    side = round(W * 0.66)
    art = art.resize((side, side), Image.LANCZOS)
    top = round(H * 0.17)
    image.paste(art, ((W - side) // 2, top))
    draw = ImageDraw.Draw(image)
    y = top + side + round(W * 0.06)
    y += centred_text(draw, W, y, "ArtFreq", font("Inter-SemiBold.ttf", round(W * 0.15)), WHITE)
    y += round(W * 0.05)
    centred_text(draw, W, y, "by AnyMeans", font("Inter-Regular.ttf", round(W * 0.075)), GREY)
    return image.resize((w, h), Image.LANCZOS)


def small_image(emblem, n):
    N = n * SUPERSAMPLE
    image = Image.new("RGBA", (N, N), WHITE + (255,))
    margin = round(N * 0.04)
    tile = emblem.resize((N - 2 * margin, N - 2 * margin), Image.LANCZOS)
    image.alpha_composite(tile, (margin, margin))
    return image.convert("RGB").resize((n, n), Image.LANCZOS)


def icon_bitmap(image):
    """One icon image as an ICO bitmap: BITMAPINFOHEADER (height doubled), the BGRA pixels
    bottom-up, then the 1-bit AND mask (set where the pixel is fully transparent)."""
    n = image.width
    header = struct.pack("<IiiHHIIiiII", 40, n, 2 * n, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = bytearray()
    mask = bytearray()
    mask_row_bytes = ((n + 31) // 32) * 4
    data = image.load()
    for y in range(n - 1, -1, -1):
        row_mask = bytearray(mask_row_bytes)
        for x in range(n):
            r, g, b, a = data[x, y]
            pixels += bytes((b, g, r, a))
            if a == 0:
                row_mask[x // 8] |= 0x80 >> (x % 8)
        mask += row_mask
    return header + bytes(pixels) + bytes(mask)


def write_icon(emblem, path):
    entries = []
    for n in ICON_SIZES:
        image = emblem.resize((n, n), Image.LANCZOS)
        if n >= 256:
            buffer = io.BytesIO()
            image.save(buffer, "PNG")
            entries.append((n, buffer.getvalue()))
        else:
            entries.append((n, icon_bitmap(image)))
    offset = 6 + 16 * len(entries)
    directory = struct.pack("<HHH", 0, 1, len(entries))
    for n, blob in entries:
        side = 0 if n >= 256 else n  # 0 stands for 256
        directory += struct.pack("<BBBBHHII", side, side, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(directory)
        for _, blob in entries:
            f.write(blob)


def main():
    emblem = Image.open(EMBLEM).convert("RGBA")
    if emblem.size != (1024, 1024):
        sys.exit("make-images.py: expected a 1024 x 1024 emblem, got %d x %d" % emblem.size)
    os.makedirs(OUT, exist_ok=True)
    for name in os.listdir(OUT):
        if name.endswith((".bmp", ".ico")):
            os.remove(os.path.join(OUT, name))
    for w, h in WIZARD_SIZES:
        wizard_image(emblem, w, h).save(os.path.join(OUT, "wizard-%dx%d.bmp" % (w, h)))
    for n in SMALL_SIZES:
        small_image(emblem, n).save(os.path.join(OUT, "small-%d.bmp" % n))
    write_icon(emblem, os.path.join(OUT, "ArtFreq.ico"))
    for name in sorted(os.listdir(OUT)):
        print(os.path.join("installer", "windows", "images", name))


if __name__ == "__main__":
    main()
