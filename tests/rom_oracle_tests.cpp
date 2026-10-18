// Prueba diferencial: la ROM real (interpretada con un 6502) contra el port en
// C++. Se ejecutan las mismas rutinas de un cuadro (script principal, dibujo de
// item, de balas y de tanques) con los mismos botones y el mismo flujo de
// numeros aleatorios, y se compara la RAM despues de cada cuadro.
//
// Necesita tu ROM (no esta en el repositorio): variable de entorno BCNES_ROM o
// la ruta por defecto de abajo. Sin ROM, la prueba se saltea.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../src/nes/battle.h"
#include "cpu6502.h"

using nes::u8;

namespace {

constexpr const char* kDefaultRom = "rom/Battle City (Japan).nes";

// generador sencillo y repetible (xorshift32)
struct Stream {
    uint32_t s;
    explicit Stream(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    u8 Byte() { return static_cast<u8>(Next() >> 8); }
    int Range(int lo, int hi) { return lo + static_cast<int>(Next() % static_cast<uint32_t>(hi - lo + 1)); }
};

// Bytes de RAM que se comparan. Se ignoran los temporales de la ROM (que el
// port no necesita), el buffer de la PPU, la pila y los datos del driver de sonido.
bool Compared(int a) {
    if (a <= 0x04) return false;                     // temporales
    if (a == 0x0C) return false;                     // ram_buffer_index (buffer PPU)
    if (a >= 0x11 && a <= 0x14) return false;        // punteros temporales
    if (a >= 0x35 && a <= 0x3C) return false;        // ram_com_dec_number: digitos de trabajo (los deja el HUD de vidas)
    if (a >= 0x47 && a <= 0x4A) return false;
    if (a >= 0x53 && a <= 0x59) return false;
    if (a == 0x5A || a == 0x5B) return false;
    if (a == 0x64 || a == 0x65) return false;
    if (a == 0x0F || a == 0x10) return false;        // azar: lo reemplaza el flujo compartido
    if (a >= 0x0400 && a < 0x07C0 && ((a - 0x0400) & 31) >= 28) return false;   // HUD: la ROM lo escribe por nametable, el renderer lo dibuja desde el estado
    if (a >= 0x0100 && a <= 0x010A) return true;     // variables de la pagina 1
    if (a >= 0x0100 && a < 0x0200) return false;     // pila y buffer PPU
    if (a >= 0xF0 && a <= 0xF3) return false;        // punteros temporales del driver de sonido
    if (a == 0xFD || a == 0xFE) return false;
    return true;
}

struct Rom {
    std::vector<u8> prg;
    bool Load(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        std::vector<u8> all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (all.size() < 16 + 16384 || all[0] != 'N') return false;
        prg.assign(all.begin() + 16, all.begin() + 16 + 16384);
        return true;
    }
};

class RomSide {
public:
    explicit RomSide(const Rom& rom, uint32_t seed) : rom_(rom), stream_(seed) {
        cpu_.sp = 0x7F;                              // la ROM arranca con SP=$7F (reset en 00:C070)
        cpu_.read = [this](uint16_t a) -> u8 {
            if (a < 0x2000) return ram_[a & 0x7FF];
            if (a >= 0x8000) return rom_.prg[(a - 0x8000) & 0x3FFF];
            return 0;
        };
        cpu_.write = [this](uint16_t a, u8 v) {
            if (a < 0x2000) ram_[a & 0x7FF] = v;
            else if (a >= 0x4000 && a <= 0x4017 && a != 0x4014 && a != 0x4016) apuWrites.push_back({a, v});
        };
    }

    u8 ram_[0x800] = {};
    int randoms = 0;
    std::vector<std::pair<uint16_t, u8>> apuWrites;   // escrituras a la APU de este cuadro

    void Call(uint16_t addr) {
        try {
            CallImpl(addr);
        } catch (const std::exception& e) {
            std::printf("  ORACULO: %s (llamando a $%04X). Ultimos PC:", e.what(), addr);
            for (int i = 0; i < 48; ++i) {
                std::printf(" %04X", cpu_.history[(cpu_.historyPos + i) % 48]);
            }
            std::printf("\n");
            throw;
        }
    }

    std::vector<std::array<int, 4>> trace;
    bool tracing = false;

    void CallImpl(uint16_t addr) {
        cpu_.Call(addr, [this](oracle::Cpu6502& c) {
            if (tracing && c.pc == 0xDA3B) {         // LDA (ram_0011),Y dentro de sub_DA2B
                const uint16_t ptr = static_cast<uint16_t>(ram_[0x11] | (ram_[0x12] << 8));
                trace.push_back({ram_[0x47], ram_[0x48], ram_[(ptr + c.y) & 0x7FF], ram_[0x04]});
            }
            if (c.pc != 0xD44D) return false;        // sub_D44D_generate_random_number
            const u8 v = stream_.Byte();
            ++randoms;
            ram_[nes::ram_random] = v;
            ram_[nes::ram_index_for_random] = static_cast<u8>(ram_[nes::ram_index_for_random] + 1);
            c.a = v;
            c.fz = v == 0;
            c.fn = (v & 0x80) != 0;
            c.DoRts();
            return true;
        });
    }

    // Un cuadro, igual que Battle::RunFrame pero ejecutando las rutinas de la ROM.
    void Frame(u8 pad0, u8 pad1) {
        const u8 pads[2] = {pad0, pad1};
        if (ram_[nes::ram_bg_palette_id] < 0x80) {
            ram_[nes::ram_bg_palette_id] = nes::con_bg_pal_FF;
        }
        ram_[nes::ram_buffer_index] = 0;             // sub_D8FD_write_buffer_to_ppu vacia el buffer en cada NMI
        for (int x = 1; x >= 0; --x) {               // sub_D689_read_joy_regs
            ram_[nes::ram_btn_press + x] = static_cast<u8>(~ram_[nes::ram_btn_hold + x]) & pads[x];
            ram_[nes::ram_btn_hold + x] = pads[x];
        }
        Call(0xDA93);                                // sub_DA93_hide_unused_sprites
        Call(0xEA7E);                                // sub_EA7E_sound_driver
        ram_[nes::ram_frm_cnt_lo] = static_cast<u8>(ram_[nes::ram_frm_cnt_lo] + 1);
        if ((ram_[nes::ram_frm_cnt_lo] & 0x3F) == 0) {
            ram_[nes::ram_frm_cnt_hi] = static_cast<u8>(ram_[nes::ram_frm_cnt_hi] + 1);
        }
        if (ram_[nes::ram_pause_flag] == 0) {
            Call(0xC2E6);                            // sub_C2E6_main_battle_script
        }
        Call(0xE23B);                                // sub_E23B_display_bonus_on_screen
        Call(0xE0D8);                                // sub_E0D8_bullets_status_handler
        Call(0xDEA6);                                // sub_DEA6_tanks_handler
        if (ram_[nes::ram_btn_press] & nes::con_btn_Start) {
            ram_[nes::ram_pause_flag] ^= 1;
            ram_[nes::ram_sfx_pause] = ram_[nes::ram_pause_flag];
        }
    }

private:
    const Rom& rom_;
    Stream stream_;
    oracle::Cpu6502 cpu_;
};

struct Result {
    int framesChecked = 0;
    int apuWrites = 0;
    bool ok = true;
};

Result RunScenario(const Rom& rom, uint32_t seed, int frames, bool verbose) {
    Result result;
    Stream gen(seed * 7919u + 13u);
    const int players = 1 + (gen.Next() & 1);
    const int stage = 1 + static_cast<int>(gen.Next() % 35u);

    nes::Battle cpp;
    RomSide romSide(rom, seed * 31u + 5u);
    Stream cppStream(seed * 31u + 5u);
    int cppRandoms = 0;
    cpp.randomHook = [&]() -> u8 {
        ++cppRandoms;
        return cppStream.Byte();
    };

    cpp.NewGame(players, stage);
    cpp.LoadOriginalStage(stage);
    cpp.StartStage();
    std::memcpy(romSide.ram_, cpp.RamData(), 0x800);

    u8 pads[2] = {0, 0};
    int hold[2] = {0, 0};
    for (int f = 0; f < frames; ++f) {
        // entrada aleatoria que cambia cada tanto
        for (int p = 0; p < 2; ++p) {
            if (--hold[p] <= 0) {
                hold[p] = gen.Range(4, 40);
                static const u8 dirs[5] = {0, nes::con_btn_Up, nes::con_btn_Down, nes::con_btn_Left, nes::con_btn_Right};
                pads[p] = dirs[gen.Next() % 5];
            }
            if ((gen.Next() & 7) == 0) pads[p] |= nes::con_btn_A; else pads[p] &= static_cast<u8>(~nes::con_btn_A);
        }
        // pokes espejados en ambos lados: items y mejoras de arma
        if (f % 300 == 150) {
            const u8 id = static_cast<u8>(gen.Range(0, 5));
            const u8 px = static_cast<u8>(0x30 * gen.Range(1, 4));
            const u8 py = static_cast<u8>(0x30 * gen.Range(1, 4));
            for (u8* ram : {cpp.RamData(), romSide.ram_}) {
                ram[nes::ram_bonus_id] = id;
                ram[nes::ram_bonus_pos_X] = px;
                ram[nes::ram_bonus_pos_Y] = py;
                ram[nes::ram_0062_bonus_timer] = 0;
            }
        }
        if (f % 700 == 350) {
            const u8 up = static_cast<u8>(0x20 * gen.Range(0, 3));
            for (u8* ram : {cpp.RamData(), romSide.ram_}) {
                ram[nes::ram_tank_upgrade] = up;
                if (ram[nes::ram_tank_flags] != 0 && (ram[nes::ram_tank_type] & 0xF0) < 0x80) ram[nes::ram_tank_type] = up;
            }
        }

        std::vector<std::array<int, 4>> cppTrace;
        cpp.spriteTraceHook = [&](int x, int y, u8 tile, u8 a4) { cppTrace.push_back({x, y, tile, a4}); };
        romSide.trace.clear();
        romSide.apuWrites.clear();
        std::vector<std::pair<uint16_t, u8>> cppApu;
        cpp.apuWrite = [&](uint16_t a, u8 v) { cppApu.push_back({a, v}); };
        romSide.tracing = true;
        cpp.SetButtons(0, pads[0]);
        cpp.SetButtons(1, pads[1]);
        cpp.RunFrame();
        romSide.Frame(pads[0], pads[1]);

        // comparacion
        if (cppApu != romSide.apuWrites) {
            std::printf("  DIFERENCIA seed=%u cuadro %d: escrituras a la APU distintas (port %zu, rom %zu)\n", seed, f, cppApu.size(), romSide.apuWrites.size());
            for (size_t i = 0; i < std::max(cppApu.size(), romSide.apuWrites.size()); ++i) {
                const auto c = i < cppApu.size() ? cppApu[i] : std::pair<uint16_t, u8>{0, 0};
                const auto r = i < romSide.apuWrites.size() ? romSide.apuWrites[i] : std::pair<uint16_t, u8>{0, 0};
                std::printf("    %s port $%04X=%02X | rom $%04X=%02X\n", c == r ? " " : "*", c.first, c.second, r.first, r.second);
            }
            result.ok = false;
            return result;
        }
        result.apuWrites += static_cast<int>(cppApu.size());
        const u8* a = cpp.RamData();
        const u8* b = romSide.ram_;
        for (int addr = 0; addr < 0x800; ++addr) {
            if (!Compared(addr)) continue;
            if (a[addr] != b[addr]) {
                std::printf("  DIFERENCIA seed=%u (etapa %d, %d jug.) cuadro %d: RAM[$%04X] port=$%02X rom=$%02X\n",
                            seed, stage, players, f, addr, a[addr], b[addr]);
                result.ok = false;
                if (addr >= 0x0200 && addr < 0x0300) {
                    const int base = addr & ~3;
                    std::printf("    sprite OAM[%02X]: port Y=%02X T=%02X A=%02X X=%02X | rom Y=%02X T=%02X A=%02X X=%02X\n", base & 0xFF,
                                a[base], a[base + 1], a[base + 2], a[base + 3], b[base], b[base + 1], b[base + 2], b[base + 3]);
                    const int tx = ((b[base + 3] + 3) & 0xFF) >> 3;
                    const int ty = (b[base] & 0xFF) >> 3;   // OAM Y = spr_Y - 8 (sub_DA2B)
                    std::printf("    mosaico bajo el sprite (col %d, fila %d): port=$%02X rom=$%02X; ram_priority_spr_A port=$%02X rom=$%02X\n", tx, ty,
                                a[0x0400 + ty * 32 + tx], b[0x0400 + ty * 32 + tx], a[nes::ram_priority_spr_A], b[nes::ram_priority_spr_A]);
                }
                for (size_t i = 0; i < std::max(cppTrace.size(), romSide.trace.size()); ++i) {
                    const auto c = i < cppTrace.size() ? cppTrace[i] : std::array<int, 4>{-1, -1, -1, -1};
                    const auto r = i < romSide.trace.size() ? romSide.trace[i] : std::array<int, 4>{-1, -1, -1, -1};
                    const bool same = c == r;
                    std::printf("    %s da2b #%zu port X=%02X Y=%02X tile=%02X a4=%02X | rom X=%02X Y=%02X tile=%02X a4=%02X\n", same ? " " : "*", i, c[0], c[1], c[2], c[3], r[0], r[1], r[2], r[3]);
                }
                if (verbose) {
                    std::printf("    botones J1=$%02X J2=$%02X, flags tanques:", pads[0], pads[1]);
                    for (int t = 0; t < 8; ++t) std::printf(" %02X/%02X", a[nes::ram_tank_flags + t], b[nes::ram_tank_flags + t]);
                    std::printf("\n");
                }
                return result;
            }
        }
        if (cppRandoms != romSide.randoms) {
            std::printf("  DIFERENCIA seed=%u cuadro %d: el port pidio %d numeros al azar y la ROM %d\n", seed, f, cppRandoms, romSide.randoms);
            result.ok = false;
            return result;
        }
        ++result.framesChecked;
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const char* envRom = std::getenv("BCNES_ROM");
    std::string path = envRom ? envRom : kDefaultRom;
    Rom rom;
    // la ROM va en rom/ dentro del proyecto: se busca subiendo desde el directorio actual (por ejemplo desde build/bin)
    for (int up = 0; !envRom && up < 5 && !rom.Load(path); ++up) path = "../" + path;
    if (!envRom && !rom.Load(path)) path = kDefaultRom;
    if (!rom.Load(path)) {
        std::printf("SALTEADA: no se encontro la ROM en %s (defini BCNES_ROM)\n", path.c_str());
        return 0;
    }
    int scenarios = 12;
    int frames = 2500;
    if (argc > 1) scenarios = std::atoi(argv[1]);
    if (argc > 2) frames = std::atoi(argv[2]);

    int failed = 0;
    long total = 0;
    long totalApu = 0;
    int badSound = 0;
    for (size_t i = 0; i < sizeof(nes::snd_data); ++i) {          // los datos de sonido generados == los de la ROM
        if (nes::snd_data[i] != rom.prg[(nes::snd_data_base + i - 0x8000) & 0x3FFF]) ++badSound;
    }
    std::printf("datos de sonido generados vs ROM: %d bytes distintos de %zu\n", badSound, sizeof(nes::snd_data));
    if (badSound) ++failed;
    for (int s = 1; s <= scenarios; ++s) {
        const Result r = RunScenario(rom, static_cast<uint32_t>(s), frames, true);
        total += r.framesChecked;
        totalApu += r.apuWrites;
        if (!r.ok) {
            ++failed;
        } else {
            std::printf("  seed %d: %d cuadros identicos a la ROM\n", s, r.framesChecked);
        }
    }
    std::printf("\n%d escenarios, %ld cuadros comparados (%ld escrituras a la APU iguales), %d con diferencias\n", scenarios, total, totalApu, failed);
    return failed == 0 ? 0 : 1;
}
