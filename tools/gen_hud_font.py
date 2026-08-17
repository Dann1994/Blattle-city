"""Genera la hoja de fuente pixel-art (5x7, estilo bloque retro) usada por el
HUD del juego. No se copia de ninguna fuente existente: son formas de bloque
simples, disenadas a mano, en el mismo espiritu grafico que Tanques.png
(mayusculas, trazo grueso, sin antialiasing).

Ejecutar desde la raiz del repo: python tools/gen_hud_font.py
Genera:
  assets/sprites/hud_font.png      -> atlas final (5px por glifo, sin escalar)
  (temporal, no versionado) una hoja de muestra ampliada para revisar a ojo
"""

from PIL import Image

GLYPH_W = 5
GLYPH_H = 7

# Orden exacto en el que quedan los glifos en el atlas; debe coincidir con
# kHudFontChars en Game.cpp.
CHARS = " !%()-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ"

BITMAPS = {
    " ": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
    ],
    "!": [
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".....",
        "..#..",
    ],
    "%": [
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#....",
        "#...#",
    ],
    "(": [
        "...#.",
        "..#..",
        ".#...",
        ".#...",
        ".#...",
        "..#..",
        "...#.",
    ],
    ")": [
        ".#...",
        "..#..",
        "...#.",
        "...#.",
        "...#.",
        "..#..",
        ".#...",
    ],
    "-": [
        ".....",
        ".....",
        ".....",
        "#####",
        ".....",
        ".....",
        ".....",
    ],
    ".": [
        ".....",
        ".....",
        ".....",
        ".....",
        ".....",
        ".##..",
        ".##..",
    ],
    "0": [
        ".###.",
        "#...#",
        "#..##",
        "#.#.#",
        "##..#",
        "#...#",
        ".###.",
    ],
    "1": [
        "..#..",
        ".##..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        ".###.",
    ],
    "2": [
        ".###.",
        "#...#",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#####",
    ],
    "3": [
        ".###.",
        "#...#",
        "....#",
        "..##.",
        "....#",
        "#...#",
        ".###.",
    ],
    "4": [
        "...#.",
        "..##.",
        ".#.#.",
        "#..#.",
        "#####",
        "...#.",
        "...#.",
    ],
    "5": [
        "#####",
        "#....",
        "####.",
        "....#",
        "....#",
        "#...#",
        ".###.",
    ],
    "6": [
        "..##.",
        ".#...",
        "#....",
        "####.",
        "#...#",
        "#...#",
        ".###.",
    ],
    "7": [
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        ".#...",
        ".#...",
    ],
    "8": [
        ".###.",
        "#...#",
        "#...#",
        ".###.",
        "#...#",
        "#...#",
        ".###.",
    ],
    "9": [
        ".###.",
        "#...#",
        "#...#",
        ".####",
        "....#",
        "...#.",
        ".##..",
    ],
    ":": [
        ".....",
        "..##.",
        "..##.",
        ".....",
        "..##.",
        "..##.",
        ".....",
    ],
    "A": [
        "..#..",
        ".#.#.",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
    ],
    "B": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#...#",
        "#...#",
        "####.",
    ],
    "C": [
        ".####",
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        ".####",
    ],
    "D": [
        "####.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "####.",
    ],
    "E": [
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#####",
    ],
    "F": [
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#....",
    ],
    "G": [
        ".####",
        "#....",
        "#....",
        "#.###",
        "#...#",
        "#...#",
        ".####",
    ],
    "H": [
        "#...#",
        "#...#",
        "#...#",
        "#####",
        "#...#",
        "#...#",
        "#...#",
    ],
    "I": [
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "#####",
    ],
    "J": [
        "..###",
        "...#.",
        "...#.",
        "...#.",
        "...#.",
        "#..#.",
        ".##..",
    ],
    "K": [
        "#...#",
        "#..#.",
        "#.#..",
        "##...",
        "#.#..",
        "#..#.",
        "#...#",
    ],
    "L": [
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#....",
        "#####",
    ],
    "M": [
        "#...#",
        "##.##",
        "#.#.#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
    ],
    "N": [
        "#...#",
        "##..#",
        "#.#.#",
        "#..##",
        "#...#",
        "#...#",
        "#...#",
    ],
    "O": [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ],
    "P": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#....",
        "#....",
        "#....",
    ],
    "Q": [
        ".###.",
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#..#.",
        ".##.#",
    ],
    "R": [
        "####.",
        "#...#",
        "#...#",
        "####.",
        "#.#..",
        "#..#.",
        "#...#",
    ],
    "S": [
        ".####",
        "#....",
        "#....",
        ".###.",
        "....#",
        "....#",
        "####.",
    ],
    "T": [
        "#####",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
    ],
    "U": [
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".###.",
    ],
    "V": [
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".#.#.",
        "..#..",
    ],
    "W": [
        "#...#",
        "#...#",
        "#...#",
        "#.#.#",
        "#.#.#",
        "##.##",
        "#...#",
    ],
    "X": [
        "#...#",
        ".#.#.",
        "..#..",
        "..#..",
        "..#..",
        ".#.#.",
        "#...#",
    ],
    "Y": [
        "#...#",
        ".#.#.",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
        "..#..",
    ],
    "Z": [
        "#####",
        "....#",
        "...#.",
        "..#..",
        ".#...",
        "#....",
        "#####",
    ],
}

missing = [c for c in CHARS if c not in BITMAPS]
assert not missing, f"faltan bitmaps para: {missing}"

atlas = Image.new("RGBA", (GLYPH_W * len(CHARS), GLYPH_H), (0, 0, 0, 0))
apx = atlas.load()

for i, ch in enumerate(CHARS):
    rows = BITMAPS[ch]
    assert len(rows) == GLYPH_H, ch
    for row, line in enumerate(rows):
        assert len(line) == GLYPH_W, ch
        for col, mark in enumerate(line):
            if mark == "#":
                apx[i * GLYPH_W + col, row] = (255, 255, 255, 255)

atlas.save("assets/sprites/hud_font.png")
print("atlas guardado:", atlas.size, "-", len(CHARS), "glifos")

# Hoja de muestra ampliada (solo para revisar a ojo, no se versiona).
scale = 8
sheet_cols = 15
sheet_rows = (len(CHARS) + sheet_cols - 1) // sheet_cols
cell_w = (GLYPH_W + 1) * scale
cell_h = (GLYPH_H + 1) * scale
sheet = Image.new("RGBA", (sheet_cols * cell_w, sheet_rows * cell_h), (30, 30, 30, 255))
for i, ch in enumerate(CHARS):
    rows = BITMAPS[ch]
    cx = (i % sheet_cols) * cell_w
    cy = (i // sheet_cols) * cell_h
    for row, line in enumerate(rows):
        for col, mark in enumerate(line):
            if mark == "#":
                x0 = cx + col * scale
                y0 = cy + row * scale
                for yy in range(scale):
                    for xx in range(scale):
                        sheet.putpixel((x0 + xx, y0 + yy), (255, 255, 255, 255))

sheet.save(r"C:\Users\dvergara\AppData\Local\Temp\claude\C--Users-dvergara-Documents\fd0a5e42-b768-4d5e-9ad2-91374e134aa4\scratchpad\hud_font_specimen.png")
print("specimen guardado")
