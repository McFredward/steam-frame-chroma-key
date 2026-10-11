#!/usr/bin/env python3
"""Render the dashboard icon (icon.png). The panel itself is drawn at runtime. Needs Pillow."""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

OUT = os.path.dirname(os.path.abspath(__file__))


def font(size):
    for f in ("DejaVuSans-Bold.ttf", "arialbd.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
              "C:/Windows/Fonts/segoeuib.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            pass
    return ImageFont.load_default()


def centered(d, y, text, f, fill, W):
    w = d.textlength(text, font=f)
    d.text(((W - w) / 2, y), text, font=f, fill=fill)


def panel(name, title, sub, bg, accent):
    W, H = 1024, 512
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((8, 8, W - 8, H - 8), 48, fill=bg, outline=accent, width=10)
    centered(d, 60, "CHROMA KEY", font(64), (230, 230, 230, 255), W)
    centered(d, 160, title, font(170), accent, W)
    centered(d, 390, sub, font(40), (200, 200, 200, 255), W)
    im.save(os.path.join(OUT, name))


def icon():
    S = 256
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((8, 8, S - 8, S - 8), 40, fill=(30, 30, 34, 255))
    d.rectangle((40, 60, 128, 196), fill=(0, 200, 70, 255))
    d.rectangle((128, 60, 216, 196), fill=(0, 90, 255, 255))
    d.ellipse((84, 84, 172, 172), fill=(235, 235, 235, 255))
    im.save(os.path.join(OUT, "icon.png"))


if __name__ == "__main__":
    icon()
    print("wrote PNGs to", OUT, file=sys.stderr)
