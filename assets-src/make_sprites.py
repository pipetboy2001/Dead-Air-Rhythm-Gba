#!/usr/bin/env python3
"""Convierte frames 16x16 de las hojas de assets-src a sprites de Butano.

Mismo formato que los enemy_*.bmp ya convertidos: BMP indexado con el
indice 0 = (0,255,0) transparente. El heroe alternativo (elf) sale como
16x48 (3 frames apilados), igual que hero.bmp.

Correr desde esta carpeta: python make_sprites.py
Deja sprites_preview.png al lado para mirar sin compilar.
"""
import colorsys
from pathlib import Path
from PIL import Image

HERE = Path(__file__).parent
OUT = HERE.parent / "graphics"
TRANSPARENT = (0, 255, 0)

def vivid(color):
    """Paleta estilo era-GBA para pantallas sin retroiluminación:
    oversaturation (s x1.5) + contraste alto con pivote en los medios
    (las sombras bajan y las luces suben — ramping marcado, nada de
    gradientes sutiles que la TFT se come)."""
    r, g, b = (c / 255 for c in color)
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    s = min(1.0, s * 1.5)
    v = 0.55 + (v - 0.55) * 1.5
    v = max(0.05, min(1.0, v))
    return tuple(round(c * 255) for c in colorsys.hsv_to_rgb(h, s, v))


def bluify(color):
    """Palette swap: los verdes dominantes rotan a azul. hero.bmp ya es este
    mismo elfo en verde, asi que el segundo heroe se distingue por color."""
    r, g, b = color
    if g > r + 10 and g > b + 10:
        return (b, r, g)
    return color


# (salida, hoja, [(col, fila), ...], recolor) — cada celda es 16x16; varias
# celdas se apilan verticalmente (frames de animacion, como hero.bmp).
SPRITES = [
    ("enemy_bat", "enemies/bat.png", [(0, 0), (1, 0)], None),
    ("enemy_rat", "enemies/rat.png", [(0, 0), (1, 0)], None),
    ("enemy_skeleton", "enemies/skeleton.png", [(0, 0), (1, 0)], None),
    ("enemy_slime", "enemies/slime.png", [(0, 0), (1, 0)], None),
    ("enemy_fly", "enemies/fly.png", [(1, 2), (2, 2)], None),
    ("enemy_flying_skull", "enemies/flying_skull.png", [(1, 0), (2, 0)], None),
    ("enemy_ghost", "enemies/ghost.png", [(1, 0), (2, 0)], None),
    ("enemy_goblin", "enemies/goblin.png", [(1, 0), (2, 0)], None),
    ("enemy_spider", "enemies/spider.png", [(1, 2), (2, 2)], None),
    ("hero", "dungeon/elf.png", [(0, 0), (1, 0), (2, 0)], None),
    ("hero_elf", "dungeon/elf.png", [(0, 0), (1, 0), (2, 0)], bluify),
]


def convert(name, sheet_path, cells, recolor=None):
    sheet = Image.open(HERE / sheet_path).convert("RGBA")
    frames = len(cells)
    palette = [TRANSPARENT]
    out = Image.new("P", (16, 16 * frames))
    pixels = []
    for f, (cx, cy) in enumerate(cells):
        frame = sheet.crop((cx * 16, cy * 16, cx * 16 + 16, cy * 16 + 16))
        for y in range(16):
            for x in range(16):
                r, g, b, a = frame.getpixel((x, y))
                if a < 128:
                    pixels.append(0)
                    continue
                color = (r, g, b)
                if color not in palette:
                    palette.append(color)
                pixels.append(palette.index(color))
    assert len(palette) <= 16, f"{name}: demasiados colores: {len(palette)}"
    if recolor:
        palette = [palette[0]] + [recolor(c) for c in palette[1:]]
    palette = [palette[0]] + [vivid(c) for c in palette[1:]]
    out.putdata(pixels)
    flat = [c for rgb in palette for c in rgb] + [0] * (768 - 3 * len(palette))
    out.putpalette(flat)
    out.save(OUT / f"{name}.bmp")
    (OUT / f"{name}.json").write_text('{\n    "type": "sprite",\n\t"height": 16\n}\n')
    print(f"{name}: {frames} frame(s), {len(palette)} colores")
    return out, palette


def main():
    results = [convert(*spec) for spec in SPRITES]

    # Preview ampliada para revisar los frames elegidos.
    Z = 6
    total_w = sum(im.width for im, _ in results) * Z + (len(results) - 1) * 4
    total_h = max(im.height for im, _ in results) * Z
    preview = Image.new("RGB", (total_w, total_h), (25, 19, 45))
    x = 0
    for im, palette in results:
        rgba = im.convert("RGBA")
        data = [(0, 0, 0, 0) if p == 0 else rgba.getpixel((i % im.width, i // im.width))
                for i, p in enumerate(im.getdata())]
        rgba.putdata(data)
        big = rgba.resize((im.width * Z, im.height * Z), Image.NEAREST)
        preview.paste(big, (x, 0), big)
        x += im.width * Z + 4
    preview.save(HERE / "sprites_preview.png")
    print(HERE / "sprites_preview.png")


if __name__ == "__main__":
    main()
