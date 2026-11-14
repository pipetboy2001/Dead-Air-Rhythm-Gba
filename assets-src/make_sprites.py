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
# Enemigos: 4 frames = [frente0, frente1, lado0, lado1]. Los carriles
# laterales usan el ciclo de lado (espejado por código según dirección);
# arriba/abajo usan el de frente.
SPRITES = [
    ("enemy_bat", "enemies/bat.png", [(0, 0), (1, 0), (0, 2), (1, 2)], None),
    ("enemy_rat", "enemies/rat.png", [(0, 0), (1, 0), (0, 2), (1, 2)], None),
    ("enemy_skeleton", "enemies/skeleton.png", [(0, 0), (1, 0), (0, 2), (1, 2)], None),
    ("enemy_slime", "enemies/slime.png", [(0, 0), (1, 0), (0, 2), (1, 2)], None),
    # La mosca no tiene frames de frente limpios: el ciclo lateral sirve
    # para ambas orientaciones.
    ("enemy_fly", "enemies/fly.png", [(1, 2), (2, 2), (1, 2), (2, 2)], None),
    ("enemy_flying_skull", "enemies/flying_skull.png", [(1, 0), (2, 0), (0, 2), (1, 2)], None),
    ("enemy_ghost", "enemies/ghost.png", [(1, 0), (2, 0), (0, 2), (1, 2)], None),
    ("enemy_goblin", "enemies/goblin.png", [(1, 0), (2, 0), (0, 2), (1, 2)], None),
    ("enemy_spider", "enemies/spider.png", [(1, 2), (2, 2), (0, 4), (1, 4)], None),
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


def make_spark():
    """Chispa 8x8 para las particulas de golpe: rombo dorado con nucleo
    blanco, dibujado a mano (no viene del pack)."""
    palette = [TRANSPARENT, (0xFF, 0xD8, 0x3E), (0xFF, 0xFF, 0xF0)]
    out = Image.new("P", (8, 8))
    pixels = []
    for y in range(8):
        for x in range(8):
            dist = abs(x - 3.5) + abs(y - 3.5)
            pixels.append(2 if dist <= 1.5 else 1 if dist <= 3.5 else 0)
    out.putdata(pixels)
    out.putpalette([c for rgb in palette for c in rgb] + [0] * (768 - 9))
    out.save(OUT / "spark.bmp")
    (OUT / "spark.json").write_text('{\n    "type": "sprite",\n\t"height": 8\n}\n')
    print("spark: 1 frame, 3 colores")
    return out, palette


def make_mote():
    """Mota de polvo 8x8 para la pantalla de titulo: puntito tenue con
    halo, flota lento para que la caverna no se sienta estatica."""
    palette = [TRANSPARENT, (0x6B, 0x5F, 0x8E), (0xA9, 0x9D, 0xCC)]
    out = Image.new("P", (8, 8))
    pixels = [0] * 64
    for x, y in ((3, 3), (4, 3), (3, 4), (4, 4)):
        pixels[y * 8 + x] = 2
    for x, y in ((3, 2), (4, 2), (2, 3), (5, 4), (3, 5), (4, 5), (2, 4), (5, 3)):
        pixels[y * 8 + x] = 1
    out.putdata(pixels)
    out.putpalette([c for rgb in palette for c in rgb] + [0] * (768 - 9))
    out.save(OUT / "mote.bmp")
    (OUT / "mote.json").write_text('{\n    "type": "sprite",\n\t"height": 8\n}\n')
    print("mote: 1 frame, 3 colores")
    return out, palette


def make_flash():
    """Destello 16x16 del anillo: anillo blanco con halo dorado que se
    muestra unos frames en el punto del carril al presionar."""
    import math
    palette = [TRANSPARENT, (0xFF, 0xD8, 0x3E), (0xFF, 0xFF, 0xF4)]
    out = Image.new("P", (16, 16))
    pixels = []
    for y in range(16):
        for x in range(16):
            d = math.hypot(x - 7.5, y - 7.5)
            pixels.append(2 if 5.2 <= d <= 7.4 else 1 if 3.6 <= d < 5.2 else 0)
    out.putdata(pixels)
    out.putpalette([c for rgb in palette for c in rgb] + [0] * (768 - 9))
    out.save(OUT / "hit_flash.bmp")
    (OUT / "hit_flash.json").write_text('{\n    "type": "sprite",\n\t"height": 16\n}\n')
    print("hit_flash: 1 frame, 3 colores")
    return out, palette


def make_meter():
    """Segmento 8x8 del medidor de euforia, 2 frames: vacio (ranura) y
    lleno (bloque dorado) — reemplaza la barra de texto [####----]."""
    palette = [TRANSPARENT, (0x6B, 0x5F, 0x8E), (0xFF, 0xB2, 0x3E), (0xFF, 0xE2, 0x8A)]
    out = Image.new("P", (8, 16))
    pixels = [0] * 128
    for frame, fill in ((0, None), (1, 2)):
        base = frame * 8
        for y in range(1, 7):
            for x in range(8):
                border = y in (1, 6) or x in (0, 7)
                idx = 1 if border else (fill or 0)
                if fill and y == 2 and 1 <= x <= 6:
                    idx = 3  # brillo superior del bloque lleno
                pixels[(base + y) * 8 + x] = idx
    out.putdata(pixels)
    out.putpalette([c for rgb in palette for c in rgb] + [0] * (768 - 12))
    out.save(OUT / "meter_seg.bmp")
    (OUT / "meter_seg.json").write_text('{\n    "type": "sprite",\n\t"height": 8\n}\n')
    print("meter_seg: 2 frames, 4 colores")
    return out, palette


def main():
    results = [convert(*spec) for spec in SPRITES]
    results.append(make_spark())
    results.append(make_mote())
    results.append(make_flash())
    results.append(make_meter())

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
