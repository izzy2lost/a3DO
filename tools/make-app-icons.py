#!/usr/bin/env python3
"""Builds the a3DO launcher icon and every size Android, iOS and macOS ask for.

The icon matches the app's theme: the "a3DO" wordmark with the 3, D and O in
red, gold and blue, over a red-gold-blue rule, on deep charcoal. It is set in
Roboto Medium, the same face the app's interface uses.

    python3 tools/make-app-icons.py

Run from anywhere. Overwrites assets/branding/a3do-icon-1024.png, the Apple
asset catalogue, the Android mipmaps and the Play store icon.
"""

from __future__ import annotations

import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(HERE, "third_party", "imgui", "misc", "fonts", "Roboto-Medium.ttf")
if not os.path.exists(FONT):
    raise SystemExit("Roboto-Medium.ttf not found; run git submodule update --init")

SIZE = 1024

BG_TOP = (24, 24, 31)
BG_BOTTOM = (8, 8, 11)

# The theme colours, as in src/ui/ui.cpp.
WHITE = (243, 243, 247)
RED = (227, 38, 43)
GOLD = (252, 189, 26)
BLUE = (69, 120, 250)


def vertical_gradient(size, colours):
    width, height = size
    grad = Image.new("RGB", (1, height))
    pixels = grad.load()
    steps = len(colours) - 1
    for y in range(height):
        position = y / max(1, height - 1) * steps
        index = min(int(position), steps - 1)
        blend = position - index
        start, end = colours[index], colours[index + 1]
        pixels[0, y] = tuple(
            int(start[c] + (end[c] - start[c]) * blend) for c in range(3)
        )
    return grad.resize((width, height))


def background():
    canvas = vertical_gradient((SIZE, SIZE), [BG_TOP, BG_BOTTOM]).convert("RGBA")
    # A faint warm-to-cool glow behind the wordmark.
    glow = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    draw = ImageDraw.Draw(glow)
    draw.ellipse((120, 280, SIZE - 120, SIZE - 280), fill=(90, 60, 120, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(140))
    return Image.alpha_composite(canvas, glow)


def artwork(width):
    """The wordmark and rule, centred in a transparent square of `width`."""
    scale = 2  # draw large, then downsample for clean edges
    w = width * scale
    layer = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    letters = [("a", WHITE), ("3", RED), ("D", GOLD), ("O", BLUE)]
    target = w * 0.92
    size = 40
    while True:
        font = ImageFont.truetype(FONT, size + 8)
        if font.getlength("a3DO") > target:
            break
        size += 8
    font = ImageFont.truetype(FONT, size)
    total = font.getlength("a3DO")
    ascent, _ = font.getmetrics()
    cap = font.getbbox("D")
    text_height = cap[3] - cap[1]
    rule = round(w * 0.045)
    gap = round(w * 0.06)
    block = text_height + gap + rule
    top = (w - block) / 2 - cap[1]
    x = (w - total) / 2
    for char, colour in letters:
        draw.text((x, top), char, font=font, fill=colour + (255,))
        x += font.getlength(char)

    rule_top = (w - block) / 2 + text_height + gap
    left = (w - total) / 2
    third = total / 3
    for i, colour in enumerate((RED, GOLD, BLUE)):
        draw.rectangle((left + third * i, rule_top,
                        left + third * (i + 1), rule_top + rule), fill=colour + (255,))
    return layer.resize((width, width), Image.LANCZOS)


def master():
    canvas = background()
    art = artwork(round(SIZE * 0.80))
    canvas.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return canvas


def foreground():
    # Adaptive icons crop to the inner ~61% safe zone.
    layer = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    art = artwork(round(SIZE * 0.58))
    layer.alpha_composite(art, ((SIZE - art.width) // 2, (SIZE - art.width) // 2))
    return layer


def rounded(image, radius_fraction=0.22):
    mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, image.width - 1, image.height - 1),
        radius=round(image.width * radius_fraction),
        fill=255,
    )
    out = image.copy()
    out.putalpha(mask)
    return out


def main():
    icon = master()
    fore = foreground()
    flat = icon.convert("RGB")

    flat.save(os.path.join(HERE, "assets", "branding", "a3do-icon-1024.png"))

    # Apple: one alpha-free 1024 for iOS, the 16..1024 ladder for macOS.
    apple = os.path.join(HERE, "assets", "apple", "Assets.xcassets", "AppIcon.appiconset")
    flat.save(os.path.join(apple, "icon-ios-1024.png"))
    for px in (16, 32, 64, 128, 256, 512, 1024):
        flat.resize((px, px), Image.LANCZOS).save(os.path.join(apple, f"icon-mac-{px}.png"))

    res = os.path.join(HERE, "android", "app", "src", "main", "res")
    legacy = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    layers = {"mdpi": 108, "hdpi": 162, "xhdpi": 216, "xxhdpi": 324, "xxxhdpi": 432}
    for density, px in legacy.items():
        folder = os.path.join(res, f"mipmap-{density}")
        os.makedirs(folder, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(os.path.join(folder, "ic_launcher.png"))
        rounded(icon.resize((px, px), Image.LANCZOS), 0.5).save(
            os.path.join(folder, "ic_launcher_round.png"))
        fore.resize((layers[density],) * 2, Image.LANCZOS).save(
            os.path.join(folder, "ic_launcher_foreground.png"))

    plate = "#%02X%02X%02X" % BG_TOP
    path = os.path.join(res, "values", "ic_launcher_background.xml")
    with open(path, "w") as handle:
        handle.write(
            '<?xml version="1.0" encoding="utf-8"?>\n<resources>\n'
            '    <color name="ic_launcher_background">' + plate + "</color>\n"
            "</resources>\n")

    store = os.path.join(HERE, "play", "graphics", "store")
    if os.path.isdir(store):
        flat.resize((512, 512), Image.LANCZOS).save(os.path.join(store, "icon.png"))

    print("icons written")


if __name__ == "__main__":
    main()
