#!/usr/bin/env python3
"""Genera src/nes/nes_data.h a partir del desensamblado completo de Battle City
(cyneprepou4uk/NES-Games-Disassembly, carpeta "Battle City").

Convierte:
  - bank_ram.inc  -> direcciones de RAM con los nombres originales (ram_*)
  - bank_val.inc  -> constantes (con_*)
  - bank_FF.asm   -> tablas numericas (tbl_*) y los 36 niveles originales

El codigo C++ portado usa esos mismos nombres, asi cada rutina se puede
comparar linea por linea con el desensamblado.

Uso: python tools/gen_nes_data.py <carpeta "Battle City" del desensamblado>
"""
import os
import re
import sys

if len(sys.argv) != 2:
    sys.exit(__doc__)

src = sys.argv[1]
out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "nes", "nes_data.h")


def evaluate(expr, names):
    expr = expr.split(";")[0].strip()
    expr = re.sub(r"#", "", expr)
    expr = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), expr)
    expr = re.sub(r"<\s*", "(0xFF & ", expr) if False else expr
    tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expr)
    for t in tokens:
        if t not in names:
            raise KeyError(t)
        expr = re.sub(r"\b" + t + r"\b", str(names[t]), expr)
    if not re.fullmatch(r"[0-9+\-*()\s]*", expr):
        raise ValueError(expr)
    return eval(expr)


# ---------------------------------------------------------------- constantes
consts = {}
for line in open(os.path.join(src, "bank_val.inc"), encoding="utf-8", errors="replace"):
    m = re.match(r"^\s*(con_\w+)\s*=\s*([^;]+)", line)
    if m:
        try:
            consts[m.group(1)] = evaluate(m.group(2), consts)
        except Exception:
            pass

# ---------------------------------------------------------------------- RAM
ram = {}
pending = []
for line in open(os.path.join(src, "bank_ram.inc"), encoding="utf-8", errors="replace"):
    if line.lstrip().startswith(";"):
        continue
    m = re.match(r"^\s*(ram_\w+)\s*=\s*([^;]+)", line)
    if m:
        pending.append((m.group(1), m.group(2)))
for _ in range(4):
    still = []
    for name, expr in pending:
        try:
            ram[name] = evaluate(expr, ram)
        except KeyError:
            still.append((name, expr))
    pending = still

# ------------------------------------------------------------------ tablas
tables = {}
current = None
valid = True
names_all = dict(consts)
for line in open(os.path.join(src, "bank_FF.asm"), encoding="utf-8", errors="replace"):
    line = line.rstrip("\n")
    m = re.match(r"^([A-Za-z_]\w*):", line)
    if m:
        if current is not None and valid and tables.get(current):
            pass
        label = m.group(1)
        if label.startswith("tbl_"):
            current = label
            tables[current] = []
            valid = True
        else:
            current = None
        continue
    if current is None:
        continue
    m = re.search(r"\.(byte|word|incbin)\s+(.*)$", line)
    if not m:
        continue
    kind, operands = m.group(1), m.group(2)
    if kind != "byte" or '"' in operands:
        valid = False
        tables[current] = None
        current = None
        continue
    for operand in operands.split(";")[0].split(","):
        operand = operand.strip()
        if not operand:
            continue
        try:
            tables[current].append(evaluate(operand, names_all) & 0xFF)
        except Exception:
            valid = False
            tables[current] = None
            current = None
            break

tables = {k: v for k, v in tables.items() if v}


# ------------------------------------------------- datos del motor de sonido
# Imagen de memoria de la ROM entre tbl_ECE6 (tabla de notas) y el final de los
# datos de efectos/musica ($EF8F), con la misma direccion que en la NES: el
# driver original lee punteros absolutos ($ECFE..) y bytes con (puntero),Y.
SND_BASE = 0xECE6
SND_END = 0xF000
lines_all = open(os.path.join(src, "bank_FF.asm"), encoding="utf-8", errors="replace").read().split("\n")
start = next(i for i, l in enumerate(lines_all) if l.startswith("tbl_ECE6:"))
stop = next(i for i, l in enumerate(lines_all) if l.startswith("sub_F000_draw_stage:"))
region = lines_all[start:stop]


def operands_of(line):
    m = re.search(r"\.(byte|word)\s+(.*)$", line)
    if not m:
        return None
    ops = [o.strip() for o in m.group(2).split(";")[0].split(",") if o.strip()]
    return m.group(1), ops


labels = {}
pending_labels = []
for line in region:
    m = re.match(r"^([A-Za-z_]\w*):", line)
    if m:
        pending_labels.append(m.group(1))
        continue
    m = re.search(r"00:([0-9A-F]{4}):", line)
    if m and operands_of(line):
        for lab in pending_labels:
            labels[lab] = int(m.group(1), 16)
        pending_labels = []

snd_names = dict(consts)
snd_names.update(labels)
snd = [0xFF] * (SND_END - SND_BASE)
for line in region:
    m = re.search(r"00:([0-9A-F]{4}):", line)
    parsed = operands_of(line)
    if not (m and parsed):
        continue
    addr = int(m.group(1), 16)
    kind, ops = parsed
    for op in ops:
        value = evaluate(op, snd_names)
        if kind == "byte":
            snd[addr - SND_BASE] = value & 0xFF
            addr += 1
        else:
            snd[addr - SND_BASE] = value & 0xFF
            snd[addr + 1 - SND_BASE] = (value >> 8) & 0xFF
            addr += 2

# ----------------------------------------------------------------- niveles
stage_bytes = []
for i in list(range(1, 36)) + ["FF"]:
    name = "stage_%s.bin" % ("%02d" % i if isinstance(i, int) else i)
    stage_bytes += list(open(os.path.join(src, "incbin", "stages", name), "rb").read())
assert len(stage_bytes) == 36 * 91, len(stage_bytes)


def array(name, values, ctype="uint8_t"):
    rows = []
    for i in range(0, len(values), 16):
        rows.append("    " + ", ".join("0x%02X" % v for v in values[i:i + 16]) + ",")
    return "inline constexpr %s %s[%d] = {\n%s\n};\n" % (ctype, name, len(values), "\n".join(rows))


with open(out_path, "w", encoding="utf-8", newline="\n") as f:
    f.write("// GENERADO por tools/gen_nes_data.py desde el desensamblado de Battle City.\n")
    f.write("// No editar a mano: los nombres y valores son los del codigo original.\n")
    f.write("#pragma once\n\n#include <cstdint>\n\nnamespace nes {\n\n")
    f.write("// ---- RAM (direccion en la RAM de la NES) ----\n")
    for name in sorted(ram, key=lambda n: (ram[n], n)):
        f.write("inline constexpr uint16_t %s = 0x%04X;\n" % (name, ram[name]))
    f.write("\n// ---- Constantes ----\n")
    for name in sorted(consts, key=lambda n: (consts[n], n)):
        f.write("inline constexpr uint8_t %s = 0x%02X;\n" % (name, consts[name] & 0xFF))
    f.write("\n// ---- Tablas de datos de la ROM ----\n")
    for name in sorted(tables):
        f.write(array(name, tables[name]))
    f.write("\n// Los 36 niveles originales (35 + el vacio $FF), 91 bytes cada uno.\n")
    f.write(array("tbl_F07A_stage_data", stage_bytes))
    f.write("\n// Datos del motor de sonido: bytes de la ROM desde $%04X (tabla de notas, punteros y efectos).\n" % SND_BASE)
    f.write("inline constexpr uint16_t snd_data_base = 0x%04X;\n" % SND_BASE)
    f.write(array("snd_data", snd))
    f.write("\n} // namespace nes\n")

print("RAM:", len(ram), "constantes:", len(consts), "tablas:", len(tables), "->", os.path.normpath(out_path))
