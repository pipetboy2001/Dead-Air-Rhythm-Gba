#!/usr/bin/env python3
"""Genera graphics/zone_floor.bmp: piso de mazmorra tileado para el fondo.

Usa las losas del pack de dungeon (grounds.png): un bloque base de 2x2
que tilea limpio + variantes desgastadas salpicadas con RNG fijo, todo
oscurecido para que la arena y los sprites queden como la capa brillante.
main.cpp lo tiñe por cancion con set_fade(zone_color).

Correr desde esta carpeta: python make_zone_bg.py
Deja zone_floor_preview.png al lado para mirar sin compilar.
"""
import struct
import zlib
from pathlib import Path
from PIL import Image

HERE = Path(__file__).parent
W = H = 256
CELL = 16
DARKEN = 0.5

# Bloque base 2x2 (col, fila) en grounds.png — losas crema que tilean —
# y variantes sueltas que lo rompen de a poco.
BASE = [[(5, 1), (6, 1)], [(5, 2), (6, 2)]]
VARIANTS = [(6, 6), (5, 6), (9, 6)]
VARIANT_CHANCE = 5  # 1 de cada 5 celdas


def main():
    sheet = Image.open(HERE / "dungeon" / "grounds.png").convert("RGB")

    def cell_pixels(cx, cy):
        return sheet.crop((cx * CELL, cy * CELL, (cx + 1) * CELL, (cy + 1) * CELL))

    palette = [(255, 0, 255)]  # indice 0: no se usa, el fondo es opaco
    lookup = {}
    indices = bytearray(W * H)
    rng_state = 12345
    for gy in range(H // CELL):
        for gx in range(W // CELL):
            rng_state = (rng_state * 1103515245 + 12345) & 0x7FFFFFFF
            if rng_state % VARIANT_CHANCE == 0:
                cx, cy = VARIANTS[rng_state % len(VARIANTS)]
            else:
                cx, cy = BASE[gy % 2][gx % 2]
            tile = cell_pixels(cx, cy)
            for y in range(CELL):
                for x in range(CELL):
                    r, g, b = tile.getpixel((x, y))
                    color = tuple(round(c * DARKEN) for c in (r, g, b))
                    if color not in lookup:
                        lookup[color] = len(palette)
                        palette.append(color)
                    indices[(gy * CELL + y) * W + gx * CELL + x] = lookup[color]

    assert len(palette) <= 16, f"demasiados colores: {len(palette)}"

    # BMP 4bpp indexado (ver make_arena.py: 8bpp acapara los bancos de
    # paleta de fondos), filas de abajo hacia arriba.
    out = HERE.parent / "graphics" / "zone_floor.bmp"
    pal = palette + [(0, 0, 0)] * (16 - len(palette))
    row_bytes = W // 2
    pixel_offset = 14 + 40 + 16 * 4
    data = bytearray()
    data += struct.pack("<2sIHHI", b"BM", pixel_offset + row_bytes * H, 0, 0, pixel_offset)
    data += struct.pack("<IiiHHIIiiII", 40, W, H, 1, 4, 0, row_bytes * H, 2835, 2835, 16, 0)
    for r, g, b in pal:
        data += bytes((b, g, r, 0))
    for y in range(H - 1, -1, -1):
        row = indices[y * W:(y + 1) * W]
        data += bytes((row[i] << 4) | row[i + 1] for i in range(0, W, 2))
    out.write_bytes(data)
    print(f"{out} ({len(data)} bytes, {len(palette)} colores)")

    raw = b"".join(
        b"\x00" + bytes(c for x in range(W) for c in palette[indices[y * W + x]])
        for y in range(H)
    )
    png = b"\x89PNG\r\n\x1a\n"
    for tag, payload in ((b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)),
                         (b"IDAT", zlib.compress(raw)), (b"IEND", b"")):
        png += struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))
    (HERE / "zone_floor_preview.png").write_bytes(png)
    print(HERE / "zone_floor_preview.png")


if __name__ == "__main__":
    main()
