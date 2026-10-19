# Genera src/app/heli_data.h a partir de Recursos/201012.png (hoja de sprites del helicoptero).
# Uso: python tools/gen_heli_sprites.py
import os, sys
from PIL import Image

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
im = Image.open(os.path.join(root, "Recursos", "201012.png")).convert("RGB")
W, H = im.size
px = im.load()
BORDER = (170, 0, 0)
MAGENTA = (255, 85, 255)
PURPLE = (168, 0, 168)
BLACK = (0, 0, 0)

def components(is_bg):
    seen = [[False] * W for _ in range(H)]
    out = []
    for y in range(H):
        for x in range(W):
            if seen[y][x] or is_bg(px[x, y]): continue
            st = [(x, y)]; seen[y][x] = True; pts = []
            while st:
                cx, cy = st.pop(); pts.append((cx, cy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] and not is_bg(px[nx, ny]):
                        seen[ny][nx] = True; st.append((nx, ny))
            out.append(pts)
    return out

cells = []
for pts in components(lambda c: c == BORDER):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    box = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
    n_mag = sum(1 for p in pts if px[p] == MAGENTA)
    n_pur = sum(1 for p in pts if px[p] == PURPLE)
    if (n_mag > 150 or n_pur > 400) and (box[2] - box[0]) > 20 and (box[3] - box[1]) > 20:
        cells.append(box)
cells.sort(key=lambda b: (b[1] // 20, b[0]))
# filas: aterrizaje (4), vuelo (6), fuego (6), invencible (6), explosion (4)
rows = {}
for b in cells:
    rows.setdefault(b[1] // 20 * 20, []).append(b)
keys = sorted(rows)
print("filas", [(k, len(rows[k])) for k in keys], file=sys.stderr)
fly, fire, inv, expl = rows[keys[1]], rows[keys[2]], rows[keys[3]], rows[keys[4]]

def extract(box):
    x0, y0, x1, y1 = box
    # los pixeles negros que no tocan nada coloreado son la sombra
    def is_trans(c): return c in (MAGENTA, PURPLE, BORDER)
    pts = {(x, y) for y in range(y0, y1) for x in range(x0, x1) if not is_trans(px[x, y])}
    black = {p for p in pts if px[p] == BLACK}
    shadow = set()
    seen = set()
    for p in black:
        if p in seen: continue
        st = [p]; comp = {p}; seen.add(p); touches = False
        while st:
            cx, cy = st.pop()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
                q = (cx + dx, cy + dy)
                if q in pts and px[q] != BLACK: touches = True
                if q in black and q not in seen:
                    seen.add(q); comp.add(q); st.append(q)
        if not touches: shadow |= comp
    body = pts - shadow
    return body, shadow

def sprite(box, with_shadow=True):
    body, shadow = extract(box)
    allp = body | (shadow if with_shadow else set())
    xs = [p[0] for p in allp]; ys = [p[1] for p in allp]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    bx = [p[0] for p in body]; by = [p[1] for p in body]
    ax = (min(bx) + max(bx) + 1) // 2 - x0       # ancla: el centro del cuerpo
    ay = (min(by) + max(by) + 1) // 2 - y0
    data = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if (x, y) in body: r, g, b = px[x, y]; data.append(0xFF000000 | (r << 16) | (g << 8) | b)
            elif (x, y) in shadow: data.append(0x70000000)
            else: data.append(0)
    return x1 - x0, y1 - y0, ax, ay, data

def explosion(box):
    x0, y0, x1, y1 = box
    data = []
    for y in range(y0, y1):
        for x in range(x0, x1):
            c = px[x, y]
            data.append(0 if c in (MAGENTA, PURPLE, BORDER) else 0xFF000000 | (c[0] << 16) | (c[1] << 8) | c[2])
    return x1 - x0, y1 - y0, (x1 - x0) // 2, (y1 - y0) // 2, data

out = ["// Generado por tools/gen_heli_sprites.py a partir de Recursos/201012.png: no editar a mano.",
       "#pragma once", "#include <cstdint>", "", "namespace app {", "",
       "struct HeliSprite { int w, h, ax, ay; const uint32_t* px; };   // ARGB (0 = transparente); (ax, ay) = centro del cuerpo", ""]
def emit(name, items, fn):
    out.append("// " + name)
    for i, b in enumerate(items):
        w, h, ax, ay, data = fn(b)
        out.append(f"inline constexpr uint32_t k{name}Px{i}[{len(data)}] = {{")
        for j in range(0, len(data), 12):
            out.append("    " + ", ".join("0x%08X" % v for v in data[j:j + 12]) + ",")
        out.append("};")
    out.append(f"inline constexpr HeliSprite k{name}[{len(items)}] = {{")
    for i, b in enumerate(items):
        w, h, ax, ay, data = fn(b)
        out.append(f"    {{{w}, {h}, {ax}, {ay}, k{name}Px{i}}},")
    out.append("};\n")
emit("HeliBlue", fly, lambda b: sprite(b, False))
emit("HeliRed", inv, lambda b: sprite(b, False))
emit("HeliBoom", expl, explosion)
out.append("} // namespace app\n")
open(os.path.join(root, "src", "app", "heli_data.h"), "w", newline="\n").write("\n".join(out))
print("ok", len(fly), len(inv), len(expl), file=sys.stderr)
