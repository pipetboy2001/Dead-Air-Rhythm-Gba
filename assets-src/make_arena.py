#!/usr/bin/env python3
"""Genera los fondos pintados del juego:

- graphics/arena.bmp: piso circular + 4 carriles tenues + anillo de golpe
  con arcos por carril, paleta oscura del juego. Exterior transparente
  (indice 0) para que se vea el zone_color por cancion de main.cpp.
- graphics/menu_panel.bmp: tarjeta de menu con borde, sombra desplazada y
  barra de acento con los 4 colores de carril. Exterior transparente;
  en la pantalla de fin queda sobre la arena.

Correr desde esta carpeta: python make_arena.py
Deja *_preview.png al lado para mirar sin compilar.
"""
import math
import struct
import zlib
from pathlib import Path

W = H = 256
CX = CY = 128.0

BG = (0x19, 0x13, 0x2D)
FLOOR = (0x23, 0x1B, 0x3F)
INK = (0xF2, 0xED, 0xFF)
# (dx, dy, color) — colores de carril del tema oscuro del juego.
LANES = [
    (0, -1, (0x3F, 0xD3, 0xC3)),   # up: teal
    (0, 1, (0xB1, 0x8F, 0xFF)),    # down: violeta
    (-1, 0, (0xFF, 0x6A, 0x5C)),   # left: rojo
    (1, 0, (0xFF, 0xD0, 0x3A)),    # right: dorado
]
FLOOR_R = 70.0          # = lane_spawn_distance en main.cpp
RING_BAND = (38.5, 41.5)  # anillo en HIT_DISTANCE=40
ARC_BAND = (37.0, 43.0)
ARC_HALF_ANGLE = 0.32
STRIPE_HALF = 10.0
STRIPE_START = 43.5     # arranca pasado el arco para no multiplicar colores


def blend(base, top, alpha):
    return tuple(round(b + (t - b) * alpha) for b, t in zip(base, top))


def arena_pixel(x, y):
    """None = transparente (fuera del piso)."""
    dx, dy = x + 0.5 - CX, y + 0.5 - CY
    r = math.hypot(dx, dy)
    if r > FLOOR_R:
        return None
    color = FLOOR
    # Foco de luz en el centro: el héroe recorta contra un piso iluminado
    # en vez de perderse en lo oscuro cuando hay mucha acción.
    if r <= 15:
        color = blend(FLOOR, INK, 0.16 if r <= 10 else 0.08)
    for lx, ly, lane_color in LANES:
        along = dx * lx + dy * ly
        across = abs(dx * ly - dy * lx)
        if along >= STRIPE_START and across <= STRIPE_HALF:
            color = blend(FLOOR, lane_color, 0.18)
            break
    if RING_BAND[0] <= r <= RING_BAND[1]:
        color = blend(FLOOR, INK, 0.4)
    if ARC_BAND[0] <= r <= ARC_BAND[1]:
        angle = math.atan2(dy, dx)
        for lx, ly, lane_color in LANES:
            lane_angle = math.atan2(ly, lx)
            delta = abs(math.atan2(math.sin(angle - lane_angle), math.cos(angle - lane_angle)))
            if delta <= ARC_HALF_ANGLE:
                color = lane_color
                break
    return color


# Tarjeta: margenes respecto al area visible (pantalla 240x160 centrada
# en el bmp: x 8..248, y 48..208), esquinas redondeadas, borde de 2px.
CARD = (16, 54, 240, 202)
CARD_RADIUS = 10
SHADOW_OFFSET = 5
BORDER = 2
ACCENT_TOP = CARD[1] + BORDER + 2
ACCENT_HEIGHT = 4
ACCENT_GAP = 4


def in_rounded_rect(x, y, rect, radius):
    x0, y0, x1, y1 = rect
    px, py = x + 0.5, y + 0.5
    if not (x0 <= px <= x1 and y0 <= py <= y1):
        return False
    cx = max(x0 + radius, min(px, x1 - radius))
    cy = max(y0 + radius, min(py, y1 - radius))
    return math.hypot(px - cx, py - cy) <= radius or (x0 + radius <= px <= x1 - radius) or (y0 + radius <= py <= y1 - radius)


def panel_pixel(x, y):
    """None = transparente (fuera de la tarjeta y su sombra)."""
    x0, y0, x1, y1 = CARD
    color = None
    shadow = (x0 + SHADOW_OFFSET, y0 + SHADOW_OFFSET, x1 + SHADOW_OFFSET, y1 + SHADOW_OFFSET)
    if in_rounded_rect(x, y, shadow, CARD_RADIUS):
        color = INK
    if in_rounded_rect(x, y, CARD, CARD_RADIUS):
        inner = (x0 + BORDER, y0 + BORDER, x1 - BORDER, y1 - BORDER)
        color = FLOOR if in_rounded_rect(x, y, inner, CARD_RADIUS - BORDER) else INK
        if ACCENT_TOP <= y + 0.5 <= ACCENT_TOP + ACCENT_HEIGHT:
            left, right = x0 + CARD_RADIUS, x1 - CARD_RADIUS
            segment = (right - left - 3 * ACCENT_GAP) / 4
            for i, (_, _, lane_color) in enumerate(LANES):
                start = left + i * (segment + ACCENT_GAP)
                if start <= x + 0.5 <= start + segment:
                    color = lane_color
                    break
    return color


# Tarjeta de cursor estilo osu!: un resaltador de una fila que main.cpp
# desliza detrás de la opción seleccionada en los menús. Centrada en el
# bmp para que la posición del BG sea directamente la fila resaltada.
CURSOR = (43, 120, 213, 136)
CURSOR_RADIUS = 6
CURSOR_STRIP = 4  # borde izquierdo de color, como las tarjetas de osu


def cursor_pixel(x, y):
    if not in_rounded_rect(x, y, CURSOR, CURSOR_RADIUS):
        return None
    inner = (CURSOR[0] + 1, CURSOR[1] + 1, CURSOR[2] - 1, CURSOR[3] - 1)
    if not in_rounded_rect(x, y, inner, CURSOR_RADIUS - 1):
        return blend(FLOOR, INK, 0.55)
    if x + 0.5 <= CURSOR[0] + 1 + CURSOR_STRIP:
        return (0xFF, 0xB2, 0x3E)  # dorado héroe
    return blend(FLOOR, INK, 0.14)


def build(name, pixel_fn):
    palette = [(255, 0, 255)]  # indice 0 = transparente, nunca pintado
    indices = bytearray(W * H)
    lookup = {}
    for y in range(H):
        for x in range(W):
            color = pixel_fn(x, y)
            if color is None:
                continue
            if color not in lookup:
                lookup[color] = len(palette)
                palette.append(color)
            indices[y * W + x] = lookup[color]

    assert len(palette) <= 16, f"{name}: demasiados colores: {len(palette)}"

    # BMP 4bpp indexado (16 colores), filas de abajo hacia arriba. Tiene que
    # ser 4bpp: Butano importa un BMP de 8bpp como BG de 256 colores, que
    # acapara todos los bancos de paleta de fondos — dos BGs así no caben
    # y el juego muere al arrancar.
    out = Path(__file__).parent.parent / "graphics" / f"{name}.bmp"
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

    # Preview PNG (no se usa en el build, solo para mirar el resultado).
    preview = Path(__file__).parent / f"{name}_preview.png"
    raw = b"".join(
        b"\x00" + bytes(c for x in range(W) for c in (palette[indices[y * W + x]] if indices[y * W + x] else BG))
        for y in range(H)
    )
    png = b"\x89PNG\r\n\x1a\n"
    for tag, payload in ((b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)),
                         (b"IDAT", zlib.compress(raw)), (b"IEND", b"")):
        png += struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))
    preview.write_bytes(png)
    print(preview)


if __name__ == "__main__":
    build("arena", arena_pixel)
    build("menu_panel", panel_pixel)
    build("cursor_card", cursor_pixel)
