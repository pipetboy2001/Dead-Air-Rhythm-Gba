#!/usr/bin/env python3
"""Genera graphics/logo.bmp: el logotipo RHYTHM CAVERNS como BG regular.

Letras Impact sin antialias (pixel art limpio), relleno dorado con brillo
en la mitad superior, contorno tinta y sombra desplazada — la paleta del
juego. Exterior transparente (indice 0); se muestra solo en el menu
principal, sobre la tarjeta.

Correr desde esta carpeta: python make_logo.py
Deja logo_preview.png al lado para mirar sin compilar.
"""
import struct
import zlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).parent
W = H = 256

INK = (0x24, 0x1A, 0x40)
GOLD = (0xFF, 0xB2, 0x3E)
SHINE = (0xFF, 0xE2, 0x8A)
BG_PREVIEW = (0x23, 0x1B, 0x3F)

FONT = ImageFont.truetype("C:/Windows/Fonts/impact.ttf", 26)
# (texto, y superior en el bmp) — pantalla visible: y 48..208, centro 128.
LINES = [("RHYTHM", 56), ("CAVERNS", 80)]
SHADOW = 2
SHINE_ROWS = 9


def word_mask(text):
    box = FONT.getbbox(text)
    img = Image.new("L", (box[2] - box[0] + 4, box[3] - box[1] + 4), 0)
    ImageDraw.Draw(img).text((2 - box[0], 2 - box[1]), text, font=FONT, fill=255)
    return img.point(lambda p: 255 if p > 100 else 0)


def main():
    glyphs = Image.new("L", (W, H), 0)
    tops = []
    for text, top in LINES:
        mask = word_mask(text)
        glyphs.paste(mask, ((W - mask.width) // 2, top), mask)
        tops.append(top)

    fat = glyphs.filter(ImageFilter.MaxFilter(3))
    shadow = fat.transform((W, H), Image.AFFINE, (1, 0, -SHADOW, 0, 1, -SHADOW))

    palette = [(255, 0, 255), INK, GOLD, SHINE]
    indices = bytearray(W * H)
    for y in range(H):
        line_top = max((t for t in tops if t <= y), default=0)
        for x in range(W):
            i = y * W + x
            if glyphs.getpixel((x, y)):
                indices[i] = 3 if y < line_top + SHINE_ROWS else 2
            elif fat.getpixel((x, y)) or shadow.getpixel((x, y)):
                indices[i] = 1

    # BMP 4bpp indexado (los BGs de 8bpp acaparan toda la paleta, ver
    # make_arena.py), filas de abajo hacia arriba.
    out = HERE.parent / "graphics" / "logo.bmp"
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
    print(f"{out} ({len(data)} bytes)")

    raw = b"".join(
        b"\x00" + bytes(c for x in range(W) for c in (palette[indices[y * W + x]] if indices[y * W + x] else BG_PREVIEW))
        for y in range(H)
    )
    png = b"\x89PNG\r\n\x1a\n"
    for tag, payload in ((b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)),
                         (b"IDAT", zlib.compress(raw)), (b"IEND", b"")):
        png += struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))
    (HERE / "logo_preview.png").write_bytes(png)
    print(HERE / "logo_preview.png")

    # Banner recortado para el README (solo la zona del texto).
    from PIL import Image as PILImage
    import io
    banner = PILImage.open(io.BytesIO(png)).crop((36, 44, 220, 122))
    banner = banner.resize((banner.width * 2, banner.height * 2), PILImage.NEAREST)
    banner.save(HERE / "logo_banner.png")
    print(HERE / "logo_banner.png")


if __name__ == "__main__":
    main()
