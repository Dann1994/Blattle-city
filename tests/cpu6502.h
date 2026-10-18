#pragma once

// Interprete minimo del MOS 6502 (instrucciones oficiales, sin modo decimal,
// como el 2A03 de la NES) para ejecutar las rutinas reales de la ROM y usarlas
// como oraculo del port en C++. Solo para pruebas.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>

namespace oracle {

class Cpu6502 {
public:
    std::function<uint8_t(uint16_t)> read;
    std::function<void(uint16_t, uint8_t)> write;

    uint8_t a = 0, x = 0, y = 0, sp = 0xFF;
    uint16_t pc = 0;
    bool fc = false, fz = false, fi = true, fd = false, fv = false, fn = false;
    uint64_t cycles = 0;   // instrucciones ejecutadas
    uint16_t history[48] = {};   // ultimos PC ejecutados (para depurar)
    int historyPos = 0;

    uint8_t Flags(bool brk) const {
        return static_cast<uint8_t>((fc ? 1 : 0) | (fz ? 2 : 0) | (fi ? 4 : 0) | (fd ? 8 : 0) | (brk ? 0x10 : 0) | 0x20 | (fv ? 0x40 : 0) | (fn ? 0x80 : 0));
    }
    void SetFlags(uint8_t p) {
        fc = p & 1;
        fz = p & 2;
        fi = p & 4;
        fd = p & 8;
        fv = p & 0x40;
        fn = p & 0x80;
    }

    void Push(uint8_t v) { write(static_cast<uint16_t>(0x0100 | sp), v); --sp; }
    uint8_t Pop() { ++sp; return read(static_cast<uint16_t>(0x0100 | sp)); }

    // Simula "RTS" fuera de la instruccion (para interceptar rutinas).
    void DoRts() {
        const uint8_t lo = Pop();
        const uint8_t hi = Pop();
        pc = static_cast<uint16_t>(((hi << 8) | lo) + 1);
    }

    // Ejecuta una subrutina como si se la llamara con JSR desde afuera, hasta
    // que vuelve. hook(pc) se consulta antes de cada instruccion: si devuelve
    // true, la rutina fue interceptada y el hook ya movio el pc.
    void Call(uint16_t addr, const std::function<bool(Cpu6502&)>& hook = nullptr, uint64_t limit = 2000000) {
        constexpr uint16_t kSentinel = 0xFFFC;
        pc = addr;
        const uint8_t startSp = sp;
        Push((kSentinel - 1) >> 8);
        Push((kSentinel - 1) & 0xFF);
        uint64_t n = 0;
        while (pc != kSentinel) {
            if (hook && hook(*this)) {
                continue;
            }
            Step();
            if (++n > limit) {
                throw std::runtime_error("el oraculo 6502 no termina");
            }
        }
        sp = startSp;
    }

    void Step();

private:
    uint8_t Fetch() { return read(pc++); }
    uint16_t Fetch16() {
        const uint8_t lo = Fetch();
        const uint8_t hi = Fetch();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    void SetZN(uint8_t v) { fz = v == 0; fn = (v & 0x80) != 0; }
    void Compare(uint8_t reg, uint8_t m) {
        fc = reg >= m;
        SetZN(static_cast<uint8_t>(reg - m));
    }
    void Adc(uint8_t m) {
        const int sum = a + m + (fc ? 1 : 0);
        fv = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
        fc = sum > 0xFF;
        a = static_cast<uint8_t>(sum);
        SetZN(a);
    }
    void Branch(bool cond) {
        const int8_t off = static_cast<int8_t>(Fetch());
        if (cond) {
            pc = static_cast<uint16_t>(pc + off);
        }
    }
    uint16_t ZeroPageX() { return static_cast<uint8_t>(Fetch() + x); }
    uint16_t ZeroPageY() { return static_cast<uint8_t>(Fetch() + y); }
    uint16_t IndirectX() {
        const uint8_t zp = static_cast<uint8_t>(Fetch() + x);
        return static_cast<uint16_t>(read(zp) | (read(static_cast<uint8_t>(zp + 1)) << 8));
    }
    uint16_t IndirectY() {
        const uint8_t zp = Fetch();
        const uint16_t base = static_cast<uint16_t>(read(zp) | (read(static_cast<uint8_t>(zp + 1)) << 8));
        return static_cast<uint16_t>(base + y);
    }
    uint8_t Shift(int kind, uint8_t v) {
        switch (kind) {
            case 0: fc = v & 0x80; v = static_cast<uint8_t>(v << 1); break;                         // ASL
            case 1: { const bool c = fc; fc = v & 0x80; v = static_cast<uint8_t>((v << 1) | (c ? 1 : 0)); break; }  // ROL
            case 2: fc = v & 1; v = static_cast<uint8_t>(v >> 1); break;                            // LSR
            default: { const bool c = fc; fc = v & 1; v = static_cast<uint8_t>((v >> 1) | (c ? 0x80 : 0)); break; }  // ROR
        }
        SetZN(v);
        return v;
    }
};

inline void Cpu6502::Step() {
    ++cycles;
    const uint16_t opPc = pc;
    history[historyPos++ % 48] = opPc;
    const uint8_t op = Fetch();
    auto fail = [&](const char* what) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s: opcode $%02X en $%04X", what, op, opPc);
        throw std::runtime_error(buf);
    };

    // ---- ramas ----
    if ((op & 0x1F) == 0x10) {
        const bool want = (op & 0x20) != 0;
        switch (op >> 6) {
            case 0: Branch(fn == want); return;   // BPL / BMI
            case 1: Branch(fv == want); return;   // BVC / BVS
            case 2: Branch(fc == want); return;   // BCC / BCS
            default: Branch(fz == want); return;  // BNE / BEQ
        }
    }

    switch (op) {
        case 0x00: throw std::runtime_error("BRK en el oraculo");
        case 0x08: Push(Flags(true)); return;
        case 0x18: fc = false; return;
        case 0x20: { const uint16_t t = Fetch16(); const uint16_t r = static_cast<uint16_t>(pc - 1); Push(r >> 8); Push(r & 0xFF); pc = t; return; }
        case 0x28: SetFlags(Pop()); return;
        case 0x38: fc = true; return;
        case 0x40: SetFlags(Pop()); { const uint8_t lo = Pop(); const uint8_t hi = Pop(); pc = static_cast<uint16_t>(lo | (hi << 8)); } return;
        case 0x48: Push(a); return;
        case 0x4C: pc = Fetch16(); return;
        case 0x58: fi = false; return;
        case 0x60: DoRts(); return;
        case 0x68: a = Pop(); SetZN(a); return;
        case 0x6C: {
            const uint16_t p = Fetch16();
            const uint16_t hiAddr = static_cast<uint16_t>((p & 0xFF00) | ((p + 1) & 0x00FF));   // bug de la pagina del 6502
            pc = static_cast<uint16_t>(read(p) | (read(hiAddr) << 8));
            return;
        }
        case 0x78: fi = true; return;
        case 0x88: --y; SetZN(y); return;
        case 0x8A: a = x; SetZN(a); return;
        case 0x98: a = y; SetZN(a); return;
        case 0x9A: sp = x; return;
        case 0xA8: y = a; SetZN(y); return;
        case 0xAA: x = a; SetZN(x); return;
        case 0xB8: fv = false; return;
        case 0xBA: x = sp; SetZN(x); return;
        case 0xC8: ++y; SetZN(y); return;
        case 0xCA: --x; SetZN(x); return;
        case 0xD8: fd = false; return;
        case 0xE8: ++x; SetZN(x); return;
        case 0xEA: return;
        case 0xF8: fd = true; return;
        default: break;
    }

    const int cc = op & 3;
    const int bbb = (op >> 2) & 7;
    const int aaa = op >> 5;

    if (cc == 1) {
        uint16_t addr = 0;
        bool immediate = false;
        uint8_t imm = 0;
        switch (bbb) {
            case 0: addr = IndirectX(); break;
            case 1: addr = Fetch(); break;
            case 2: immediate = true; imm = Fetch(); break;
            case 3: addr = Fetch16(); break;
            case 4: addr = IndirectY(); break;
            case 5: addr = ZeroPageX(); break;
            case 6: addr = static_cast<uint16_t>(Fetch16() + y); break;
            default: addr = static_cast<uint16_t>(Fetch16() + x); break;
        }
        if (aaa == 4) {                                   // STA
            if (immediate) fail("STA inmediato");
            write(addr, a);
            return;
        }
        const uint8_t m = immediate ? imm : read(addr);
        switch (aaa) {
            case 0: a |= m; SetZN(a); break;
            case 1: a &= m; SetZN(a); break;
            case 2: a ^= m; SetZN(a); break;
            case 3: Adc(m); break;
            case 5: a = m; SetZN(a); break;
            case 6: Compare(a, m); break;
            default: Adc(static_cast<uint8_t>(~m)); break;   // SBC
        }
        return;
    }

    if (cc == 2) {
        const bool useY = (aaa == 4 || aaa == 5);             // STX/LDX indexan con Y
        if (bbb == 2) {                                       // acumulador (ASL/ROL/LSR/ROR A)
            a = Shift(aaa, a);
            return;
        }
        uint16_t addr = 0;
        bool immediate = false;
        uint8_t imm = 0;
        switch (bbb) {
            case 0: immediate = true; imm = Fetch(); break;
            case 1: addr = Fetch(); break;
            case 3: addr = Fetch16(); break;
            case 5: addr = useY ? ZeroPageY() : ZeroPageX(); break;
            case 7: addr = static_cast<uint16_t>(Fetch16() + (useY ? y : x)); break;
            default: fail("modo de direccionamiento invalido");
        }
        switch (aaa) {
            case 0: case 1: case 2: case 3: {                 // ASL ROL LSR ROR en memoria
                const uint8_t v = Shift(aaa, read(addr));
                write(addr, v);
                break;
            }
            case 4: write(addr, x); break;                    // STX
            case 5: x = immediate ? imm : read(addr); SetZN(x); break;   // LDX
            case 6: { const uint8_t v = static_cast<uint8_t>(read(addr) - 1); write(addr, v); SetZN(v); break; }   // DEC
            default: { const uint8_t v = static_cast<uint8_t>(read(addr) + 1); write(addr, v); SetZN(v); break; }  // INC
        }
        return;
    }

    if (cc == 0) {
        uint16_t addr = 0;
        bool immediate = false;
        uint8_t imm = 0;
        switch (bbb) {
            case 0: immediate = true; imm = Fetch(); break;
            case 1: addr = Fetch(); break;
            case 3: addr = Fetch16(); break;
            case 5: addr = ZeroPageX(); break;
            case 7: addr = static_cast<uint16_t>(Fetch16() + x); break;
            default: fail("opcode 6502 no soportado");
        }
        switch (aaa) {
            case 1: {                                         // BIT
                const uint8_t m = read(addr);
                fz = (a & m) == 0;
                fv = (m & 0x40) != 0;
                fn = (m & 0x80) != 0;
                break;
            }
            case 4: write(addr, y); break;                    // STY
            case 5: y = immediate ? imm : read(addr); SetZN(y); break;   // LDY
            case 6: Compare(y, immediate ? imm : read(addr)); break;     // CPY
            case 7: Compare(x, immediate ? imm : read(addr)); break;     // CPX
            default: fail("opcode 6502 no soportado");
        }
        return;
    }

    throw std::runtime_error("opcode ilegal en el oraculo: " + std::to_string(op));
}

} // namespace oracle
