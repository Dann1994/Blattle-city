#include "battle.h"

// Port del motor de sonido de Battle City (bank_FF.asm, 00:EA51..00:ECFD).
// Mantiene la misma RAM que la ROM: banderas de efectos en $0300-$031B (una
// por efecto), un bloque de 8 bytes por efecto desde $031C (ram_se_data) y las
// variables de la pagina cero $F0-$FE. En lugar de escribir en la APU real,
// cada escritura a $4000-$4017 pasa por Battle::apuWrite.

namespace nes {

namespace {

constexpr uint16_t kRamSfxArrayIndex = 0x00F4;     // ram_sfx_array_index
constexpr uint16_t kRamSfxCheckLimit = 0x00F5;     // ram_sfx_check_limit
constexpr int kSfxCount = 0x1C;

inline u8 U8(int v) { return static_cast<u8>(v & 0xFF); }

} // namespace

void Battle::ApuWrite(uint16_t addr, u8 value) {
    if (apuWrite) {
        apuWrite(addr, value);
    }
}

// Byte de los datos de sonido de la ROM (bank_FF, desde $ECE6).
u8 Battle::SoundDataByte(uint16_t addr) const {
    const int i = static_cast<int>(addr) - snd_data_base;
    return (i >= 0 && i < static_cast<int>(sizeof(snd_data))) ? snd_data[i] : 0xFF;
}

// sub_EA51_clear_sound_engine_data (00:EA51)
void Battle::Sub_EA51_clear_sound_engine_data() {
    ApuWrite(0x4015, 0x0F);                           // activa pulso 1/2, triangulo y ruido
    ApuWrite(0x4017, 0xC0);                           // modo de 5 pasos, sin IRQ
    uint16_t ptr = ram_se_data;
    for (int x = 0; x < kSfxCount; ++x) {
        R(ptr + con_se_index_00) = 0;
        R(ram_sounds + x) = 0;
        ptr = static_cast<uint16_t>(ptr + 8);
    }
    R(ram_00F0_t01_se_data) = ptr & 0xFF;
    R(ram_00F0_t01_se_data + 1) = ptr >> 8;
}

// sub_ECBE_read_byte_from_sound_data (00:ECBE): lee con (F2),se[5] y avanza se[5].
u8 Battle::Sub_ECBE_read_byte_from_sound_data(uint16_t entry) {
    const u8 y = R(entry + con_se_index_05_data_pointer);
    const uint16_t dataPtr = static_cast<uint16_t>(R(0x00F2) | (R(0x00F3) << 8));
    const u8 value = SoundDataByte(static_cast<uint16_t>(dataPtr + y));
    R(entry + con_se_index_05_data_pointer) = U8(y + 1);
    return value;
}

// sub_ECAF_prepare_sound_engine_data_pointer (00:ECAF)
void Battle::Sub_ECAF_prepare_sound_engine_data_pointer() {
    const int x = R(kRamSfxArrayIndex) * 2;
    // tbl_ECFE_sfx_data: punteros absolutos de cada efecto, en el mismo bloque de la ROM
    R(0x00F2) = SoundDataByte(static_cast<uint16_t>(0xECFE + x));
    R(0x00F3) = SoundDataByte(static_cast<uint16_t>(0xECFE + x + 1));
}

// sub_EA7E_sound_driver (00:EA7E): se llama una vez por cuadro desde el NMI.
void Battle::Sub_EA7E_sound_driver() {
    // En pausa solo se atiende el efecto 0 (el de pausa).
    R(kRamSfxCheckLimit) = R(ram_pause_flag) != 0 ? 0x01 : 0x1C;
    for (int x = 3; x >= 0; --x) {
        R(ram_00F9_se + x) = 0;                       // canales ocupados en este cuadro
    }

    // ---- 1) arranca los efectos nuevos y vuelca sus registros ----
    R(kRamSfxArrayIndex) = 0;
    uint16_t entry = ram_se_data;
    do {
        const int idx = R(kRamSfxArrayIndex);
        if (R(ram_sounds + idx) != 0) {
            const u8 mark = R(entry + con_se_index_00);
            if (mark != 0) {
                if (mark >= 0x05) {
                    R(ram_00F9_se + (mark - 5)) = 1;  // ya sonando: reserva su canal
                } else {
                    const int ch = mark - 1;
                    if (R(ram_00F9_se + ch) == 0) {
                        R(ram_00F9_se + ch) = 1;
                        R(entry + con_se_index_00) = U8(ch + 5);
                        for (int i = 0; i < 4; ++i) {
                            ApuWrite(static_cast<uint16_t>(0x4000 + ch * 4 + i), R(entry + 1 + i));
                        }
                    }
                }
            }
        }
        entry = static_cast<uint16_t>(entry + 8);
        R(kRamSfxArrayIndex) = U8(R(kRamSfxArrayIndex) + 1);
    } while (R(kRamSfxArrayIndex) < R(kRamSfxCheckLimit));

    // ---- 2) silencia los canales sin efecto ----
    for (int x = 0; x < 4; ++x) {
        if (R(ram_00F9_se + x) == 0) {
            ApuWrite(static_cast<uint16_t>(0x4000 + x * 4), U8(((x * 8) & 0x10) ^ 0x10));
        }
    }

    // ---- 3) avanza la secuencia de cada efecto ----
    R(kRamSfxArrayIndex) = 0;
    entry = ram_se_data;
    do {
        const int idx = R(kRamSfxArrayIndex);
        const u8 flag = R(ram_sounds + idx);
        if (flag != 0) {
            if (flag == 0x01) {
                R(ram_sounds + idx) = 2;
                if (sfxStartHook) {
                    sfxStartHook(idx);
                }
                // loc_EB4F_initialize_sfx_and_read_data
                R(entry + con_se_index_05_data_pointer) = 0;
                Sub_ECAF_prepare_sound_engine_data_pointer();
                R(entry + con_se_index_00) = Sub_ECBE_read_byte_from_sound_data(entry);
                R(entry + con_se_index_01) = Sub_ECBE_read_byte_from_sound_data(entry);
                R(entry + con_se_index_02) = Sub_ECBE_read_byte_from_sound_data(entry);
                R(entry + con_se_index_04) = Sub_ECBE_read_byte_from_sound_data(entry);
                if (R(entry + con_se_index_00) == 0x04) {
                    R(entry + con_se_index_03) = Sub_ECBE_read_byte_from_sound_data(entry);   // explosiones y cuenta de puntos
                }
                SoundReadNextBytes(entry);
            } else {
                const u8 timer = U8(R(entry + con_se_index_07) - 1);
                R(entry + con_se_index_07) = timer;
                if (timer == 0) {
                    Sub_ECAF_prepare_sound_engine_data_pointer();   // bra_EB85_read_sfx_data
                    SoundReadNextBytes(entry);
                }
            }
        }
        entry = static_cast<uint16_t>(entry + 8);
        R(kRamSfxArrayIndex) = U8(R(kRamSfxArrayIndex) + 1);
    } while (R(kRamSfxArrayIndex) < R(kRamSfxCheckLimit));
}

// loc_EB88_read_next_byte (00:EB88): interpreta los bytes de la secuencia hasta
// llegar a una nota o una pausa (que fijan la duracion) o al final (E8).
void Battle::SoundReadNextBytes(uint16_t entry) {
    for (;;) {
        u8 b = Sub_ECBE_read_byte_from_sound_data(entry);
        if (b >= 0xE8) {
            const int cmd = b - 0xE8;
            switch (cmd) {
                case 0x00: {                          // E8 con_se_cb_stop
                    const int idx = R(kRamSfxArrayIndex);
                    R(ram_sounds + idx) = 0;
                    R(entry + con_se_index_00) = 0;
                    R(entry + con_se_index_05_data_pointer) = U8(R(entry + con_se_index_05_data_pointer) - 1);
                    return;
                }
                case 0x01: {                          // E9 (sin uso)
                    const u8 v = Sub_ECBE_read_byte_from_sound_data(entry);
                    R(entry + con_se_index_01) = (R(entry + con_se_index_01) & 0x3F) | v;
                    break;
                }
                case 0x02: {                          // EA: volumen / envolvente (bits bajos de $4000)
                    const u8 v = Sub_ECBE_read_byte_from_sound_data(entry);
                    R(entry + con_se_index_01) = (R(entry + con_se_index_01) & 0xC0) | v;
                    break;
                }
                case 0x03: {                          // EB (sin uso)
                    const u8 v = Sub_ECBE_read_byte_from_sound_data(entry);
                    R(entry + con_se_index_01) = (R(entry + con_se_index_01) & 0xF0) | v;
                    break;
                }
                case 0x04:                            // EC (sin uso)
                    R(entry + con_se_index_02) = Sub_ECBE_read_byte_from_sound_data(entry);
                    break;
                case 0x05:                            // ED (sin uso)
                    R(entry + con_se_index_04) = Sub_ECBE_read_byte_from_sound_data(entry);
                    break;
                case 0x06:                            // EE
                    R(entry + con_se_index_01) = Sub_ECBE_read_byte_from_sound_data(entry);
                    break;
                case 0x07:                            // EF con_se_cb_clear_loop_counters
                    for (int x = 2; x >= 0; --x) {
                        R(ram_00F6_se_loop_counter + x) = 0;
                    }
                    break;
                case 0x08: case 0x09: case 0x0A: {    // F0/F1/F2 bucles 1, 2 y 3
                    const int x = cmd - 0x08;
                    const u8 count = Sub_ECBE_read_byte_from_sound_data(entry);
                    const u8 counter = U8(R(ram_00F6_se_loop_counter + x) + 1);
                    R(ram_00F6_se_loop_counter + x) = counter;
                    if (counter != count) {
                        // bra_ECA5: salta al desplazamiento que sigue
                        R(entry + con_se_index_05_data_pointer) = Sub_ECBE_read_byte_from_sound_data(entry);
                    } else {
                        R(ram_00F6_se_loop_counter + x) = 0;
                        R(entry + con_se_index_05_data_pointer) = U8(R(entry + con_se_index_05_data_pointer) + 1);   // saltea el desplazamiento
                    }
                    break;
                }
                case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10:   // F3-F8 (sin uso)
                    R(entry + con_se_index_05_data_pointer) = U8(R(entry + con_se_index_05_data_pointer) + 1);
                    break;
                default:                              // F9 con_se_cb_main_loop (y cualquier otro)
                    R(entry + con_se_index_05_data_pointer) = Sub_ECBE_read_byte_from_sound_data(entry);
                    break;
            }
            continue;
        }
        if (b == con_se_cb_60) {                      // pausa con la duracion actual
            R(entry + con_se_index_07) = R(entry + con_se_index_06);
            return;
        }
        if (b > con_se_cb_60) {                       // 61-E7: fija la duracion de las notas
            R(entry + con_se_index_06) = U8(b - 0x60);
            continue;
        }
        // 00-5F: nota (octava en los bits 0-2, nombre en los bits 3-7)
        const int x = (b & 0xF8) >> 2;
        uint16_t period = static_cast<uint16_t>((SoundDataByte(static_cast<uint16_t>(0xECE6 + x)) << 8) | SoundDataByte(static_cast<uint16_t>(0xECE6 + x + 1)));
        period = static_cast<uint16_t>(period >> (b & 0x07));
        R(entry + con_se_index_04) = (R(entry + con_se_index_04) & 0xF8) | (period >> 8);
        R(entry + con_se_index_03) = period & 0xFF;
        const u8 mark = R(entry + con_se_index_00);
        if (mark >= 0x05) {
            R(entry + con_se_index_00) = U8(mark - 4);
        }
        R(entry + con_se_index_07) = R(entry + con_se_index_06);
        return;
    }
}

} // namespace nes
