#include "battle.h"
#include <cstdio>

#include <algorithm>
#include <queue>
#include <utility>

#include <cstdlib>

// Port de bank_FF.asm (Battle City, Famicom). Cada funcion lleva en su
// comentario la direccion original. Los registros A/X/Y se reemplazan por
// variables locales; los indices de tanque y de bala son el "X" de la ROM.

// Estado por tanque / bala / jugador: en modo clasico vive en la RAM de la ROM; en el
// campo ancho (hasta 4 jugadores) en wd_. Ambas ramas son referencias al mismo tipo.
#define SLOT(ramBase, field, i) (wide_ ? wd_.field[(i)] : R((ramBase) + (i)))
#define TFLAGS(i) SLOT(ram_tank_flags, tflags, i)
#define TTYPE(i) SLOT(ram_tank_type, ttype, i)
#define TPOSX(i) XRef{wide_ ? wd_.tposX[(i)] : R(ram_tank_pos_X + (i)), tankXHi_[(i)], wide_}
#define TPOSY(i) SLOT(ram_tank_pos_Y, tposY, i)
#define TWHEELS(i) SLOT(ram_tank_wheels, twheels, i)
#define TUPGRADE(i) SLOT(ram_tank_upgrade, upgrade, i)
#define BSTATUS(i) SLOT(ram_bullet_status, bstatus, i)
#define BPOSX(i) XRef{wide_ ? wd_.bposX[(i)] : R(ram_bullet_pos_X + (i)), bulletXHi_[(i)], wide_}
#define BPOSY(i) SLOT(ram_bullet_pos_Y, bposY, i)
#define BPROP(i) SLOT(ram_bullet_property, bprop, i)
#define PLRFLAGS(i) SLOT(ram_0103_plr_flags, plrflags, i)
#define LIVES(i) SLOT(ram_lives, lives, i)
#define HELMET(i) SLOT(ram_helmet_timer, helmet, i)
#define STUN(i) SLOT(ram_plr_stun_timer, stun, i)
#define BTNHOLD(i) SLOT(ram_btn_hold, btnHold, i)
#define BTNPRESS(i) SLOT(ram_btn_press, btnPress, i)
#define TSTLO(i) SLOT(ram_tank_stage_pos_lo, tstLo, i)
#define TSTHI(i) SLOT(ram_tank_stage_pos_hi, tstHi, i)
#define EXTRALIFE(i) SLOT(ram_p1_extra_life, extra, i)
#define SCOREB(i) SLOT(ram_score, score, i)
#define KILLCNT(i) SLOT(ram_enemy_type_kill_cnt, kill, i)
#define BONUSX XRef{R(ram_bonus_pos_X), bonusXHi_, wide_}
#define MSGX XRef{R(ram_game_over_msg_pos_X), msgXHi_, wide_}
#define DESTX XRef{R(ram_enemy_destination_X), destXHi_, wide_}

namespace nes {

namespace {

inline u8 U8(int v) { return static_cast<u8>(v & 0xFF); }
inline int S8(u8 v) { return static_cast<int8_t>(v); }

// "SEC; SBC; BPL; EOR #$FF; CLC; ADC #$01": valor absoluto de la resta como byte con signo.
inline int AbsDiff8(u8 a, u8 b) {
    u8 d = U8(a - b);
    if (d & 0x80) {
        d = U8(~d + 1);
    }
    return d;
}

} // namespace

int Battle::AbsDiffX(int a, int b) const {
    if (!wide_) {
        return AbsDiff8(U8(a), U8(b));
    }
    return a > b ? a - b : b - a;
}

void Battle::SetWide(bool wide) {
    wide_ = wide;
}

Battle::Battle() {
    std::memset(ram_, 0, sizeof(ram_));
    for (int& t : debugForcedType_) {
        t = -1;
    }
}

// ===========================================================================
// Utilidades generales
// ===========================================================================

// sub_D44D_generate_random_number (00:D44D)
u8 Battle::Sub_D44D_generate_random_number() {
    if (randomHook) {
        const u8 v = randomHook();
        R(ram_random) = v;
        R(ram_index_for_random) = U8(R(ram_index_for_random) + 1);
        return v;
    }
    u8 a = R(ram_random);
    a = U8(a << 3);
    a = U8(a - R(ram_random));
    int sum = a + R(ram_frm_cnt_hi);            // CLC; ADC
    const int carry = sum > 0xFF ? 1 : 0;
    a = U8(sum);
    R(ram_index_for_random) = U8(R(ram_index_for_random) + 1);
    const u8 idx = R(ram_index_for_random);
    sum = a + ram_[idx] + carry;                // ADC $00,X (pagina cero)
    a = U8(sum);
    R(ram_random) = a;
    return a;
}

// sub_D5FB_calculate_pointer (00:D5FB): direccion de la RAM sombra de la
// nametable para el mosaico (col, fila) = 0400 + fila * 20 + col.
uint16_t Battle::Sub_D5FB_calculate_pointer(int col, int row) const {
    return static_cast<uint16_t>(row * Stride() + col);      // indice en el mapa (0400 + ... en la RAM de la NES)
}

// sub_D706_divide_by_08_and_calculate_pointer (00:D706)
uint16_t Battle::PointerPx(int px, int py) const {
    int col = WX(px) >> 3;
    if (col >= Stride()) {
        col = Stride() - 1;
    }
    return Sub_D5FB_calculate_pointer(col, (py & 0xFF) >> 3);
}

// sub_D725 (00:D725): mascara del cuarto de 4x4 px dentro de un mosaico de 8x8.
u8 Battle::Sub_D725_tile_mask(int px, int py) const {
    u8 mask = 1;
    if (py & 0x04) {
        mask = U8(mask << 2);
    }
    if (px & 0x04) {
        mask = U8(mask << 1);
    }
    return mask;
}

// sub_D784_write_tile_to_buffer (00:D784): la ROM tambien encola la escritura
// a la PPU; aca el renderer lee directo de la RAM sombra.
void Battle::Sub_D784_write_tile(uint16_t addr, u8 value) {
    M(addr) = value;
    if (wide_ && addr < sizeof(steelHp_)) {
        steelHp_[addr] = 0;
        rock_[addr] = 0;
    }
}

// sub_D6B3_fill_buffer_with_tiles (00:D6B3)
void Battle::Sub_D6B3_fill_buffer_with_tiles(int col, int row, const u8* tiles) {
    const uint16_t p = Sub_D5FB_calculate_pointer(col, row);
    for (int i = 0; tiles[i] != 0xFF; ++i) {
        M(p + i) = tiles[i];
        if (wide_ && p + i < static_cast<int>(sizeof(steelHp_))) {
            steelHp_[p + i] = 0;
            rock_[p + i] = 0;
        }
    }
}

// sub_D635_calculate_nametable_attribute (00:D635)
void Battle::Sub_D635_calculate_nametable_attribute(int tileX, int tileY, u8 attr) {
    u8 a = attr;
    for (int i = 0; i < 3; ++i) {
        a = U8((a << 2) | attr);                // sub_D684
    }
    u8 mask;
    if ((tileY & 0x02) == 0) {
        mask = ((tileX & 0x02) != 0) ? 0xF3 : 0xFC;
    } else {
        mask = ((tileX & 0x02) != 0) ? 0x3F : 0xCF;
    }
    const u8 keep = mask;
    const u8 fresh = U8(~mask) & a;
    u8& cell = AttrCellRef(tileX, tileY);
    cell = (cell & keep) | fresh;
}

// sub_D80B_write_block_tiles_and_attribute_to_buffer (00:D80B)
void Battle::Sub_D80B_write_block_tiles_and_attribute(u8 tileId, int px, int py) {
    const int tx = WX(px) >> 3;
    const int ty = (py & 0xFF) >> 3;
    const bool corner = wide_ && tileId >= kCornerBrickBase && tileId < kCornerBrickBase + 8;     // un solo mosaico de ladrillo o de hierro en una punta del bloque
    const int idx = corner ? (tileId < kCornerSteelBase ? 4 : 9) : (tileId < 16 ? tileId : 0);   // (las tablas de la ROM llegan al id 15)
    Sub_D635_calculate_nametable_attribute(tx, ty, tbl_DABB_nametable_attribute[idx]);

    const uint16_t p = Sub_D5FB_calculate_pointer(tx & 0xFE, ty & 0xFE);
    const int x4 = idx * 4;
    const bool sand = wide_ && tileId == kSandBlockId;                    // arena (solo campo ancho): cuatro mosaicos propios
    const bool lava = wide_ && tileId == kLavaBlockId;                    // lava: lo mismo
    const u8 own = lava ? kLavaTile : kSandTile;
    const bool special = sand || lava;
    const int cornerTile = corner ? (tileId < kCornerSteelBase ? 0x0F : 0x10) : 0;
    const int cornerQ = corner ? (tileId - kCornerBrickBase) & 3 : -1;                // 0 arriba izquierda, 1 arriba derecha, 2 abajo izquierda, 3 abajo derecha
    auto tileOf = [&](int k) -> u8 {
        if (special) return own;
        if (corner) return static_cast<u8>(k == cornerQ ? cornerTile : 0);
        return tbl_DACB_block_data[x4 + k];
    };
    Sub_D784_write_tile(p, tileOf(0));
    Sub_D784_write_tile(p + 1, tileOf(1));
    Sub_D784_write_tile(p + Stride(), tileOf(2));
    Sub_D784_write_tile(p + Stride() + 1, tileOf(3));
}

// sub_D9E1_calculate_decimal_number (00:D9E1): A (BCD de 2 digitos) a ram_com_dec_number.
void Battle::Sub_D9E1_calculate_decimal_number(u8 a) {
    for (int i = 0; i < 7; ++i) {
        R(ram_com_dec_number + i) = 0;          // sub_D9FE_clear_bcd_number
    }
    R(ram_com_dec_number + 7) = 0xFF;
    if (a == 0) {
        R(ram_com_dec_num_thousands) = 1;
        return;
    }
    R(ram_com_dec_num_tens) = a & 0x0F;
    R(ram_com_dec_num_hundreds) = a >> 4;
}

// sub_D9BE_add_score (00:D9BE)
void Battle::Sub_D9BE_add_score(int x) {
    int idx = x * 8 + 6;
    int carry = 0;
    for (int y = 6; y >= 0; --y, --idx) {
        int a = R(ram_com_dec_number + y) + SCOREB(idx) + carry;
        if (a >= 10) {
            a -= 10;
            carry = 1;
        } else {
            carry = 0;
        }
        SCOREB(idx) = U8(a);
    }
}

// sub_D138_gain_extra_life_for_20000_pts (00:D138)
void Battle::Sub_D138_gain_extra_life_for_20000_pts() {
    if (R(ram_game_over_flag) != con_not_game_over) {
        return;
    }
    if (wide_) {
        bool gained = false;
        for (int p = 0; p < nPlayers_; ++p) {
            if (EXTRALIFE(p) == 0 && SCOREB(p * 8 + 2) >= 2) {
                LIVES(p)++;
                EXTRALIFE(p)++;
                gained = true;
            }
        }
        if (gained) {
            R(ram_sfx_gain_life_1) = 1;
            R(ram_sfx_gain_life_2) = 1;
        }
        return;
    }
    if (R(ram_p1_extra_life) == 0 && R(ram_p1_score + 2) >= 2) {
        LIVES(0)++;
        R(ram_p1_extra_life)++;
    } else {
        if (R(ram_game_mode) == 0) {
            return;
        }
        if (R(ram_p2_extra_life) != 0 || R(ram_p2_score + 2) < 2) {
            return;
        }
        LIVES(1)++;
        R(ram_p2_extra_life)++;
    }
    R(ram_sfx_gain_life_1) = 1;
    R(ram_sfx_gain_life_2) = 1;
}

// sub_E451_convert_Dpad_buttons (00:E451): prioridad Der > Izq > Abajo > Arriba.
u8 Battle::Sub_E451_convert_Dpad_buttons(u8 a) const {
    if (a & con_btn_Right) return 3;
    if (a & con_btn_Left) return 1;
    if (a & con_btn_Down) return 2;
    if (a & con_btn_Up) return 0;
    return 0xFF;
}

// sub_E420_change_tank_status (00:E420)
void Battle::Sub_E420_change_tank_status(int x, u8 status) {
    TFLAGS(x) = (TFLAGS(x) & 0x0F) | status;
}

// ===========================================================================
// Base (aguila) y campo
// ===========================================================================

// Paleta de los cuadrantes 2x2 de la zona de la base en el campo ancho (en la
// ROM son bytes de atributos sueltos: $07F3 y $07F4). col0 = primera columna de la base.
void Battle::SetBaseQuadrantPalette(int quadX, int quadY, u8 pal) {
    Sub_D635_calculate_nametable_attribute(12 + BaseShiftX() / 8 + quadX * 2, 24 + BaseShiftY() / 8 + quadY * 2, pal);
}

// sub_CAF5_draw_default_base (00:CAF5)
void Battle::Sub_CAF5_draw_default_base() {
    const int c = 12 + BaseShiftX() / 8;
    const int rs = BaseShiftY() / 8;
    Sub_D6B3_fill_buffer_with_tiles(c, 24 + rs, tbl_D36D_tiles___default_base___line1);
    Sub_D6B3_fill_buffer_with_tiles(c, 25 + rs, tbl_D374_tiles___default_base___line2);
    Sub_D6B3_fill_buffer_with_tiles(c, 26 + rs, tbl_D37B_tiles___default_base___line3);
    Sub_D6B3_fill_buffer_with_tiles(c, 27 + rs, tbl_D382_tiles___default_base___line4);
    if (wide_) {
        for (int qy = 0; qy < 2; ++qy) {
            for (int qx = 0; qx < 3; ++qx) {
                SetBaseQuadrantPalette(qx, qy, 0);
            }
        }
        return;
    }
    R(ram_nmt_attr_buffer + 0x33) = 0x00;
    R(ram_nmt_attr_buffer + 0x34) &= 0xCC;
}

// sub_CB5D_draw_default_eagle (00:CB5D)
void Battle::Sub_CB5D_draw_default_eagle() {
    const int c = 14 + BaseShiftX() / 8;
    const int rs = BaseShiftY() / 8;
    Sub_D6B3_fill_buffer_with_tiles(c, 26 + rs, tbl_D3A5_tiles___default_eagle___line1);
    Sub_D6B3_fill_buffer_with_tiles(c, 27 + rs, tbl_D3A8_tiles___default_eagle___line2);
    if (wide_) {
        SetBaseQuadrantPalette(1, 1, 0);
        return;
    }
    R(ram_nmt_attr_buffer + 0x33) &= 0x3F;
}

// sub_CB9E_draw_protected_base (00:CB9E): base con el muro de acero (pala).
void Battle::Sub_CB9E_draw_protected_base() {
    const int c = 12 + BaseShiftX() / 8;
    const int rs = BaseShiftY() / 8;
    Sub_D6B3_fill_buffer_with_tiles(c, 24 + rs, tbl_D389_tiles___protected_base___line1);
    Sub_D6B3_fill_buffer_with_tiles(c, 25 + rs, tbl_D390_tiles___protected_base___line2);
    Sub_D6B3_fill_buffer_with_tiles(c, 26 + rs, tbl_D397_tiles___protected_base___line3);
    Sub_D6B3_fill_buffer_with_tiles(c, 27 + rs, tbl_D39E_tiles___protected_base___line4);
    if (wide_) {
        SetBaseQuadrantPalette(0, 0, 3);
        SetBaseQuadrantPalette(1, 0, 3);
        SetBaseQuadrantPalette(0, 1, 3);
        SetBaseQuadrantPalette(1, 1, 0);
        SetBaseQuadrantPalette(2, 0, 3);
        SetBaseQuadrantPalette(2, 1, 3);
        return;
    }
    R(ram_nmt_attr_buffer + 0x33) = 0x3F;
    R(ram_nmt_attr_buffer + 0x34) = (R(ram_nmt_attr_buffer + 0x34) & 0xCC) | 0x33;
}

// sub_CC08_draw_destroyed_eagle (00:CC08)
void Battle::Sub_CC08_draw_destroyed_eagle() {
    const int c = 14 + BaseShiftX() / 8;
    const int rs = BaseShiftY() / 8;
    Sub_D6B3_fill_buffer_with_tiles(c, 26 + rs, tbl_D3AB_tiles___destroyed_eagle___line1);
    Sub_D6B3_fill_buffer_with_tiles(c, 27 + rs, tbl_D3AE_tiles___destroyed_eagle___line2);
}

// sub_D7CC_create_default_stage_field (00:D7CC) con los parametros de
// sub_C9B0: todo gris indestructible ($11) y el campo de 26x26 mosaicos vacio.
void Battle::Sub_D7CC_create_default_stage_field() {
    std::memset(steelHp_, 0, sizeof(steelHp_));
    std::memset(rock_, 0, sizeof(rock_));
    const int total = wide_ ? kWideMapSize : 0x400;
    for (int i = 0; i < total; ++i) {
        M(i) = 0x11;
    }
    if (wide_) {
        std::memset(wideAttr_, 0, sizeof(wideAttr_));
    } else {
        for (int i = 0; i < 0x40; ++i) {
            R(ram_nmt_attr_buffer + i) = 0;
        }
    }
    const int firstRow = (wide_ && kOpenTopStrip) ? 0 : 2;      // campo ancho: la franja de arriba tambien se juega
    const int firstCol = wide_ ? 0 : 2;                         // el campo ancho no tiene bordes laterales
    const int lastCol = wide_ ? kWideBlocksX * 2 : 28;          // exclusivo
    const int lastRow = wide_ ? 30 : 28;                        // exclusivo
    for (int row = firstRow; row < lastRow; ++row) {
        for (int col = firstCol; col < lastCol; ++col) {
            M(Sub_D5FB_calculate_pointer(col, row)) = 0x00;
        }
    }
}

// sub_F000_draw_stage (00:F000) sobre 91 bytes de nivel: 13 filas de 7 bytes
// (13 nibbles de datos + 1 de relleno), un bloque 2x2 por nibble.
void Battle::LoadStageData(const u8* data91) {
    Sub_D7CC_create_default_stage_field();
    int nibble = 0;
    for (int by = 0x10; by != 0xE0; by += 0x10) {
        for (int bx = 0x10; bx != 0xE0; bx += 0x10) {
            const u8 byte = data91[nibble >> 1];
            const u8 block = (nibble & 1) ? (byte & 0x0F) : (byte >> 4);
            Sub_D80B_write_block_tiles_and_attribute(block, bx, by);
            ++nibble;
        }
        ++nibble;   // nibble de relleno al final de cada fila
    }
    Sub_CAF5_draw_default_base();
    Sub_CB5D_draw_default_eagle();
}

void Battle::LoadOriginalStage(int stage) {
    int s = stage;
    if (s >= 0x24) {
        s -= 0x23;
    }
    if (s < 1) {
        s = 1;
    }
    LoadStageData(&tbl_F07A_stage_data[(s - 1) * 91]);
}

// Como Sub_F000_draw_stage pero con 25 bloques por fila (campo panoramico).
void Battle::LoadWideStage(const u8 blocks[kWideBlocksY][kWideBlocksX], const u8* strip) {
    Sub_D7CC_create_default_stage_field();
    for (int row = 0; row < kWideBlocksY; ++row) {
        for (int col = 0; col < kWideBlocksX; ++col) {
            Sub_D80B_write_block_tiles_and_attribute(blocks[row][col] & 0x1F, col * 0x10, 0x10 + row * 0x10);
        }
    }
    if (strip && kOpenTopStrip) {
        for (int col = 0; col < kWideBlocksX; ++col) {
            Sub_D80B_write_block_tiles_and_attribute(strip[col] & 0x1F, col * 0x10, 0x00);     // la franja de arriba (y = 0..15)
        }
    }
    nLava_ = 0;                                          // las fuentes de lava de este mapa
    std::memset(lavaTick_, 0, sizeof(lavaTick_));
    auto addLava = [&](int col, int row) {
        if (nLava_ >= kMaxLavaSources) return;
        lava_[nLava_] = LavaSource{};
        lava_[nLava_].col = col;
        lava_[nLava_].row = row;
        lava_[nLava_].timer = 120;
        ++nLava_;
    };
    for (int row = 0; row < kWideBlocksY; ++row)
        for (int col = 0; col < kWideBlocksX; ++col)
            if ((blocks[row][col] & 0x1F) == kLavaBlockId) addLava(col, row + 1);
    if (strip && kOpenTopStrip)
        for (int col = 0; col < kWideBlocksX; ++col)
            if ((strip[col] & 0x1F) == kLavaBlockId) addLava(col, 0);
    if (IsVersus()) {                                    // batalla vs: una aguila por equipo, cada una con su fortaleza de hierro
        enemyEagleAlive_ = false;
        enemyEagleHp_ = 0;
        bossSpawned_ = 0;
        vsWinner_ = -1;
        for (int t = 0; t < 2 && !IsRoyale(); ++t) {         // (el battle royale no tiene aguilas)
            vsEagleAlive_[t] = true;
            const int ec = 2 * spec_.vsEagleCol[t], er = 2 * spec_.vsEagleRow[t];
            DrawEagleAt(ec, er, false, static_cast<u8>(t));
            DrawFortressAt(ec, er, spec_.vsLayers);
        }
        return;
    }
    if (!IsBossLevel()) {                                // en los niveles del aguila enemiga los jugadores no tienen aguila
        Sub_CAF5_draw_default_base();
        Sub_CB5D_draw_default_eagle();
    }
    enemyEagleAlive_ = IsBossLevel();
    enemyEagleHp_ = enemyEagleAlive_ ? 10 * Rank() : 0;             // salud: 10, 20, 30 y 40 segun la fuerza
    bossSpawned_ = 0;
    if (enemyEagleAlive_) {
        DrawEnemyEagle(false);
        DrawEagleFortress();
    }
}

// Capas de hierro alrededor del aguila enemiga, de 1 mosaico (la unidad minima, 8 px) de espesor cada una.
void Battle::DrawEagleFortress() {
    DrawFortressAt(2 * EagleCol(), 2 * EagleRow(), EagleLayers());
}

void Battle::DrawFortressAt(int ec, int er, int n) {                          // mosaico de arriba a la izquierda del aguila
    for (int k = 1; k <= n; ++k) {
        const int topRow = kOpenTopStrip ? er - k : er;                     // con la franja de arriba abierta la capa tambien la cubre
        for (int row = topRow < 0 ? 0 : topRow; row <= er + 1 + k && row < 30; ++row) {
            for (int col = ec - k < 0 ? 0 : ec - k; col <= ec + 1 + k && col < kWideMapCols; ++col) {
                if (row != er + 1 + k && row != topRow && col != ec - k && col != ec + 1 + k) continue;   // solo el contorno de la capa k
                const uint16_t p = Sub_D5FB_calculate_pointer(col, row);
                Sub_D784_write_tile(p, tbl_DACB_block_data[9 * 4 + (row & 1) * 2 + (col & 1)]);   // el mosaico de hierro
                Sub_D635_calculate_nametable_attribute(col, row, tbl_DABB_nametable_attribute[9]);   // y su paleta (gris)
            }
        }
    }
}

// Aguila enemiga de los niveles 10/20/30/40: arriba y al centro (bloque fila 0, columna 13), con la paleta roja.
void Battle::DrawEnemyEagle(bool destroyed) {
    DrawEagleAt(2 * EagleCol(), 2 * EagleRow(), destroyed, 1);
}

void Battle::DrawEagleAt(int ec, int er, bool destroyed, u8 attr) {
    Sub_D6B3_fill_buffer_with_tiles(ec, er, destroyed ? tbl_D3AB_tiles___destroyed_eagle___line1 : tbl_D3A5_tiles___default_eagle___line1);
    Sub_D6B3_fill_buffer_with_tiles(ec, er + 1, destroyed ? tbl_D3AE_tiles___destroyed_eagle___line2 : tbl_D3A8_tiles___default_eagle___line2);
    Sub_D635_calculate_nametable_attribute(ec, er, destroyed ? 0 : attr);
}

// Cae el aguila enemiga: explota y los tanques enemigos que quedaban desaparecen (el nivel esta ganado).
void Battle::DestroyEnemyEagle(int owner) {
    enemyEagleAlive_ = false;
    enemyEagleHp_ = 0;
    ++eaglesDestroyed_;
    if (owner >= 0 && owner < nPlayers_) {
        for (int i = 0; i < 5 * Rank(); ++i) {             // plus para quien la destruyo: 5000 por cada punto de fuerza
            Sub_D9E1_calculate_decimal_number(0);
            Sub_D9BE_add_score(owner);
        }
        R(ram_sfx_bonus_1000) = 1;
        Sub_D138_gain_extra_life_for_20000_pts();
    }
    DrawEnemyEagle(true);
    R(ram_sfx_explosion_hq) = 1;
    R(ram_sfx_explosion_player) = 1;
    BonusHandler(4, 0);
}

// ===========================================================================
// Partida
// ===========================================================================

// sub_C2B3 / sub_C2BD / sub_C331 / sub_E42B: preparacion de partida y etapa.
void Battle::NewGame(int players, int stage) {
    std::memset(ram_, 0, sizeof(ram_));
    std::memset(wideMap_, 0, sizeof(wideMap_));
    std::memset(wideAttr_, 0, sizeof(wideAttr_));
    std::memset(tankXHi_, 0, sizeof(tankXHi_));
    std::memset(bulletXHi_, 0, sizeof(bulletXHi_));
    std::memset(oamX_, 0, sizeof(oamX_));
    std::memset(bulletLevel_, 0, sizeof(bulletLevel_));
    std::memset(bulletPierced_, 0, sizeof(bulletPierced_));
    std::memset(steelHp_, 0, sizeof(steelHp_));
    std::memset(rock_, 0, sizeof(rock_));
    bonusXHi_ = msgXHi_ = destXHi_ = 0;
    R(ram_random) = 0x5A;
    R(ram_offset_oam_index) = 4;
    R(ram_priority_spr_A) = 0x20;
    R(ram_bg_palette_id) = con_bg_pal_FF;
    bgPalette_ = 0;
    Sub_DA93_hide_unused_sprites();

    std::memset(&wd_, 0, sizeof(wd_));
    std::memset(chaseTarget_, 0, sizeof(chaseTarget_));
    std::memset(shieldHp_, 0, sizeof(shieldHp_));
    std::memset(extraEnemy_, 0, sizeof(extraEnemy_));
    for (int& t : stealTarget_) t = -1;
    nPlayers_ = wide_ ? (players < 1 ? 1 : (players > kMaxPlayers ? kMaxPlayers : players)) : 2;
    for (int i = 0; i < kMaxPlayers; ++i) present_[i] = i < nPlayers_;
    diffPlayers_ = nPlayers_ > 4 ? 4 : nPlayers_;
    lastEnemy_ = wide_ ? nPlayers_ + kWideEnemySlots - 1 : con_max_tanks;
    allyBegin_ = lastEnemy_ + 1;
    lastTank_ = wide_ ? lastEnemy_ + kWideAllySlots : con_max_tanks;
    nBullets_ = wide_ ? kMaxBullets : 10;
    std::memset(allyOwner_, 0, sizeof(allyOwner_));
    {
        static const int kTotals[4] = {20, 20, 36, 48};          // 1 y 2 jugadores: los 20 de la ROM
        enemyTotal_ = wide_ ? kTotals[diffPlayers_ - 1] * (10 + tier_) / 10 : 20;   // +10% por escalon de dificultad
    }
    R(ram_game_mode) = players >= 2 ? 1 : 0;
    if (wide_) {
        SetEnemyLimitForStage();
        for (int p = 0; p < nPlayers_; ++p) {
            LIVES(p) = static_cast<u8>(startLives_);
        }
    } else {
        R(ram_enemy_limit) = players >= 2 ? con_max_tanks : con_max_tanks - 2;   // CA6F / CA74
        LIVES(0) = 3;                                                       // sub_C2BD
        LIVES(1) = (R(ram_game_mode) == 0) ? 0 : 3;
    }
    R(ram_stage) = U8(wide_ && stage > 35 ? 35 : stage);       // las tablas de la ROM llegan a la etapa 35
    R(ram_2nd_loop_flag) = 0;
    R(ram_hi_score + 2) = 2;
    R(ram_priority_spr_A) = 0x20;
    Sub_EA51_clear_sound_engine_data();              // loc_C159
}

// loc_C1E5..C1F6 (00:C1E5): arranque de la etapa una vez dibujado el campo.
void Battle::StartStage() {
    // bra_C1C5: la musica de arranque de etapa (tres voces)
    R(ram_sfx_stage_load_1) = 1;
    R(ram_sfx_stage_load_2) = 1;
    R(ram_sfx_stage_load_3) = 1;
    R(ram_bg_palette_id) = con_bg_pal_00;
    Sub_C331_prepare_tanks_and_spawn_players();
}

// sub_C331_prepare_tanks_addresses_and_spawn_players_before_stage (00:C331)
void Battle::Sub_C331_prepare_tanks_and_spawn_players() {
    inputLocked_ = false;
    std::memset(shieldHp_, 0, sizeof(shieldHp_));
    std::memset(playerFreeze_, 0, sizeof(playerFreeze_));
    std::memset(lavaTick_, 0, sizeof(lavaTick_));
    std::memset(extraEnemy_, 0, sizeof(extraEnemy_));
    for (int& t : stealTarget_) t = -1;
    bomb_ = BombingState{};
    bomb_.countdown = (IsBossLevel() || IsVersus()) ? BombInterval() : 0;
    std::memset(vsTeam_, 0, sizeof(vsTeam_));
    vsAiTimer_[0] = vsAiTimer_[1] = 90;                  // batalla vs: los primeros tanques con IA salen al ratito
    vsSpawned_[0] = vsSpawned_[1] = 0;
    std::memset(teamClock_, 0, sizeof(teamClock_));
    for (auto& h : heli_) h = Heli{};
    for (Cannon& cn : cannons_) cn = Cannon{};
    for (CannonShot& sh : cshots_) sh = CannonShot{};
    for (auto& m : mines_) m = Mine{};
    portal_ = Portal{};
    for (auto& b : booms_) b = Boom{};
    std::memset(ghostT_, 0, sizeof(ghostT_));
    std::memset(speedT_, 0, sizeof(speedT_));
    std::memset(portalCd_, 0, sizeof(portalCd_));
    enemyGhostT_ = 0;
    mineSeq_ = 0;
    std::memset(royaleKills_, 0, sizeof(royaleKills_));
    royaleFrames_ = spec_.royaleMinutes * 3600;
    royaleBonusT_ = 1200;
    for (auto& it : rItems_) it = RoyaleItem{};
    vsWinner_ = -1;
    shake_ = 0;
    Sub_E409_clear_bullet_status();
    Sub_E413_clear_some_tank_addresses();
    R(ram_game_over_msg_pos_Y) = 0xF0;
    R(ram_game_over_msg_timer) = 0;
    for (int p = 0; p < nPlayers_; ++p) {
        if (LIVES(p) != 0) {
            Sub_E363_tank_spawn_handler(p);
        }
    }
    R(ram_enemy_spawn_cnt) = static_cast<u8>((IsBossLevel() || IsVersus()) ? 0 : enemyTotal_);     // en los niveles del aguila no hay cuota: salen sin parar
    R(ram_enemies_left_cnt) = static_cast<u8>(enemyTotal_);
    R(ram_enemy_type_offset) = 0;
    R(ram_frm_cnt_hi) = 0;
    R(ram_constr_usage_cnt) = 0;
    R(ram_shovel_timer) = 0;
    for (int p = 0; p < nPlayers_; ++p) {
        STUN(p) = 0;
        HELMET(p) = 0;
    }
    R(ram_enemy_timer_before_spawn) = 0;
    BONUSX = 0;
    R(ram_clock_timer) = 0;
    R(ram_enemy_spawn_pos_index) = 0;
    for (int i = 0; i < (wide_ ? kMaxPlayers * 4 : 8); ++i) {
        KILLCNT(i) = 0;                          // sub_C71E_clear_kill_counters
    }
    Sub_E42B_prepare_enemy_tanks_for_stage();
    R(ram_game_over_flag) = con_not_game_over;
    R(ram_sfx_movement_enemy) = 1;
    R(ram_004C_flag) = 1;

    const int stage = (R(ram_2nd_loop_flag) == 1) ? 0x23 : R(ram_stage);
    int interval = 0xBE - U8(stage << 2);
    if (R(ram_game_mode) != 0) {
        interval -= 0x14;
    }
    if (wide_) {
        // mismo calculo pero con el numero de nivel (hasta 40), sin pasarse de rapido
        const int lvl = spec_.active ? tier_ * 5 + 3 : (level_ > 0 ? level_ : stage);     // nivel hecho a mano: el ritmo sale de su dificultad
        interval = 0xBE - 4 * lvl;
        if (interval < 0x28) interval = 0x28;
        if (diffPlayers_ >= 2) interval -= 0x14;
        if (diffPlayers_ > 2) interval -= 0x0A * (diffPlayers_ - 2);
        if (interval < 0x1E) interval = 0x1E;
    }
    R(ram_enemy_spawn_interval) = U8(interval);
}

// sub_E42B_prepare_enemy_tanks_for_stage (00:E42B)
void Battle::Sub_E42B_prepare_enemy_tanks_for_stage() {
    const int stage = (R(ram_2nd_loop_flag) != 0) ? 0x23 : R(ram_stage);
    const int y = U8(stage - 1) * 4;
    if (wide_) {
        // campo ancho: la mezcla de tipos depende solo del escalon de dificultad (basico/rapido/power/blindado)
        static const int w0[4] = {70, 20, 8, 2};            // escalon 0
        static const int w7[4] = {30, 25, 25, 20};          // escalon 7
        int sum = 0;
        for (int i = 0; i < 4; ++i) {
            const int w = w0[i] + (w7[i] - w0[i]) * tier_ / 7;
            const int n = enemyTotal_ * w / 100;
            R(ram_enemy_type_stage_cnt + i) = static_cast<u8>(n);
            sum += n;
        }
        R(ram_enemy_type_stage_cnt) = static_cast<u8>(R(ram_enemy_type_stage_cnt) + enemyTotal_ - sum);
        return;
    }
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        int n = tbl_E578_stage_enemies_type_counter[y + i];
        if (enemyTotal_ != 20) {
            n = n * enemyTotal_ / 20;                    // mas enemigos con 3 o 4 jugadores, en la misma proporcion de tipos
        }
        R(ram_enemy_type_stage_cnt + i) = static_cast<u8>(n);
        sum += n;
    }
    if (enemyTotal_ != 20 && sum < enemyTotal_) {
        R(ram_enemy_type_stage_cnt) = static_cast<u8>(R(ram_enemy_type_stage_cnt) + enemyTotal_ - sum);   // el resto, tanques basicos
    }
}

// sub_E409_clear_bullet_status (00:E409)
void Battle::Sub_E409_clear_bullet_status() {
    for (int i = nBullets_ - 1; i >= 0; --i) {
        BSTATUS(i) = 0;
    }
}

// sub_E413_clear_some_tank_addresses (00:E413)
void Battle::Sub_E413_clear_some_tank_addresses() {
    for (int i = lastTank_; i >= 0; --i) {
        TFLAGS(i) = 0;
        PLRFLAGS(i) = 0;
    }
}

// ===========================================================================
// Un cuadro: NMI + cuerpo del bucle de la partida (00:C1F9)
// ===========================================================================

// vec_D400_NMI (00:D400)
void Battle::Nmi() {
    if (R(ram_bg_palette_id) < 0x80) {
        bgPalette_ = R(ram_bg_palette_id);
        R(ram_bg_palette_id) = con_bg_pal_FF;
    }
    for (int x = nPlayers_ - 1; x >= 0; --x) {                  // sub_D689_read_joy_regs
        const u8 now = inputLocked_ ? 0 : pad_[x];                  // con el GAME OVER en marcha los jugadores no responden
        BTNPRESS(x) = U8(~BTNHOLD(x)) & now;
        BTNHOLD(x) = now;
    }
    Sub_DA93_hide_unused_sprites();
    Sub_EA7E_sound_driver();
    R(ram_frm_cnt_lo) = U8(R(ram_frm_cnt_lo) + 1);
    if ((R(ram_frm_cnt_lo) & 0x3F) == 0) {
        R(ram_frm_cnt_hi) = U8(R(ram_frm_cnt_hi) + 1);
    }
}

void Battle::RunFrame() {
    if (IsVersus()) VersusAiInputs();                    // los jugadores con IA aprietan sus botones antes de leer los controles
    Nmi();
    skipRestOfScript_ = false;
    if (R(ram_pause_flag) == 0) {
        Sub_C2E6_main_battle_script();
    }
    Sub_E23B_display_bonus_on_screen();
    if (IsRoyale() || (wide_ && !IsVersus())) RoyaleItemsDraw();
    Sub_E0D8_bullets_status_handler();
    Sub_DEA6_tanks_handler();
    if (BTNPRESS(0) & con_btn_Start) {          // Start alterna la pausa
        R(ram_pause_flag) ^= 1;
        R(ram_sfx_pause) = R(ram_pause_flag);
    }
}

// sub_C2E6_main_battle_script (00:C2E6)
void Battle::Sub_C2E6_main_battle_script() {
    for (int p = 0; p < kMaxPlayers; ++p) {
        if (playerFreeze_[p] != 0) --playerFreeze_[p];     // cuenta el reloj enemigo, un cuadro por vez
    }
    Sub_E181_ice_detection();
    Sub_DB75_ice_movement();
    Sub_DBF1_tank_movement();
    Sub_E1FA_clear_tank_marks();
    Sub_Lava();
    Sub_Extras();
    CampaignItemsStep();
    Sub_E02E_bullets_status_handler();
    Sub_E2A9_HQ_handler();
    Sub_E27C_players_invincibility_sprites();
    Sub_E122_players_fire();
    Sub_E162_enemies_fire();
    Sub_AllyFire();
    Sub_StealLives();
    Sub_Bombing();
    Sub_DB48_enemy_spawn_handler();
    Sub_E604_bullets_movement();
    Sub_E910_check_bullets_collision_with_bullets();
    Sub_E70C_check_bullets_collision_with_tanks();
    if (IsVersus()) {
        for (int t = 0; t < 8; ++t) {
            if (teamClock_[t] != 0 && (R(ram_frm_cnt_lo) & 0x3F) == 0) --teamClock_[t];
        }
        VersusCheckEnd();
    }
    // sub_E972: al recoger un item, la ROM reemplaza su direccion de retorno
    // (PLA PLA + JMP), asi que el resto del cuadro (texto de game over, sonido
    // de movimiento, vidas y paleta del agua) se saltea.
    if (Sub_E972_try_to_pick_up_bonus()) {
        return;
    }
    Sub_C972_game_over_text_handler();
    Sub_DB0B_player_movement_sfx_handler();
    // sub_C7C8 (vidas del HUD): el renderer lo dibuja desde el estado.
    Sub_C31D_water_palette_swap_handler();
}

// sub_C972_game_over_text_handler (00:C972): el cartel GAME OVER que sube o cruza el campo.
void Battle::Sub_C972_game_over_text_handler() {
    if (R(ram_game_over_msg_timer) == 0) {
        return;
    }
    if (R(ram_2nd_loop_flag) == con_flag_demo) {
        return;
    }
    if ((R(ram_frm_cnt_lo) & 0x0F) == 0) {
        R(ram_game_over_msg_timer)--;                    // cada 16 cuadros
        if (R(ram_game_over_msg_timer) == 0) {
            R(ram_game_over_msg_pos_Y) = 0xF0;
        }
    }
    if (R(ram_game_over_msg_timer) >= 0x0A) {
        const int dir = R(ram_game_over_msg_mov_type);
        MSGX = WX(MSGX + S8(tbl_D3D5_game_over_message_spd_X[dir]));
        R(ram_game_over_msg_pos_Y) = U8(R(ram_game_over_msg_pos_Y) + tbl_D3D9_game_over_message_spd_Y[dir]);
    }
    Sub_C947_display_game_over_text_with_sprites();
}

// sub_C947_display_game_over_text_with_sprites (00:C947)
void Battle::Sub_C947_display_game_over_text_with_sprites() {
    R(0x0004) = 3;
    R(ram_priority_spr_A) = 0;
    R(ram_0053_t01_spr_T) = 0x79;
    Sub_DA7B_display_2_sprites(MSGX, R(ram_game_over_msg_pos_Y));
    R(ram_0053_t01_spr_T) = 0x7D;
    Sub_DA7B_display_2_sprites(WX(MSGX + 0x10), R(ram_game_over_msg_pos_Y));
    R(ram_priority_spr_A) = 0x20;
}

// sub_DB38_check_if_player_is_moving (00:DB38)
bool Battle::IsPlayerMoving(int x) {
    return (BTNHOLD(x) & con_btns_Dpad) != 0 && TFLAGS(x) != 0;
}

// sub_DB0B_player_movement_sfx_handler (00:DB0B): solo mantiene el flag de sonido.
void Battle::Sub_DB0B_player_movement_sfx_handler() {
    bool any = false;
    for (int p = 0; p < nPlayers_; ++p) {
        any = any || IsPlayerMoving(p);
    }
    if (R(ram_sfx_movement_player) != 0) {
        if (!any) {
            R(ram_sfx_movement_player) = 0;
        }
    } else if (any) {
        R(ram_sfx_movement_player) = 1;
    }
}

// sub_C31D_water_palette_swap_handler (00:C31D): cada 32 cuadros.
void Battle::Sub_C31D_water_palette_swap_handler() {
    const u8 a = R(ram_frm_cnt_lo) & 0x3F;
    if (a == 0) {
        R(ram_bg_palette_id) = con_bg_pal_02;
    } else if (a == 0x20) {
        R(ram_bg_palette_id) = con_bg_pal_01;
    }
}

// ===========================================================================
// Tanques
// ===========================================================================

// sub_E181_ice_detection (00:E181): marca con el bit 7 los mosaicos que ocupa
// cada tanque (asi se bloquean entre si) y detecta si un jugador pisa hielo.
void Battle::Sub_E181_ice_detection() {
    const int st = Stride();
    for (int x = lastTank_; x >= 0; --x) {
        onSand_[x] = false;
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        const uint16_t p = PointerPx(WX(TPOSX(x) - 8), U8(TPOSY(x) - 8));
        onSand_[x] = wide_ && (M(p + st + 1) & 0x7F) == kSandTile;        // el mosaico del centro del tanque es arena
        markPtr_[x] = p;
        noMark_[x] = ghostT_[x] > 0;
        TSTLO(x) = p & 0xFF;
        TSTHI(x) = (p >> 8) & 0x03;
        if (x < nPlayers_) {
            if (M(p + st + 1) == con_block_type + 0x21) {       // hielo
                PLRFLAGS(x) |= 0x80;
            } else {
                PLRFLAGS(x) &= 0x7F;
            }
        }
        if (noMark_[x]) continue;
        M(p + st + 1) |= 0x80;
        if ((TPOSX(x) & 0x07) == 0) {
            TSTHI(x) |= 0x80;
            M(p + st) |= 0x80;
        }
        if ((TPOSY(x) & 0x07) == 0) {
            TSTHI(x) |= 0x40;
            M(p + 1) |= 0x80;
        }
    }
}

// sub_E1FA (00:E1FA): quita las marcas puestas por sub_E181.
void Battle::Sub_E1FA_clear_tank_marks() {
    const int st = Stride();
    for (int x = lastTank_; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0 || noMark_[x]) {
            continue;
        }
        const int p = markPtr_[x];
        M(p + st + 1) &= 0x7F;
        if (TSTHI(x) & 0x80) {
            M(p + st) &= 0x7F;
        }
        if (TSTHI(x) & 0x40) {
            M(p + 1) &= 0x7F;
        }
    }
}

// sub_DB75_ice_movement (00:DB75): lectura de la cruceta de los jugadores,
// giros alineados a la grilla de 8 px y deslizamiento sobre hielo.
void Battle::Sub_DB75_ice_movement() {
    const u8 lo = R(ram_frm_cnt_lo);
    if ((lo & 0x01) == 0 && (lo & 0x03) != 0) {
        return;
    }
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;                                   // explotando o apareciendo
        }
        u8 dpad = 0;
        bool forceBump = false;
        if (wide_ && x < kMaxPlayers && playerFreeze_[x] != 0) {
                                                    // reloj enemigo: paralizado
            forceBump = true;
        } else if (STUN(x) != 0) {
            STUN(x)--;
            forceBump = true;                           // loc_DBA6
        } else {
            const u8 pf = PLRFLAGS(x);
            if ((pf & 0x80) && (pf & 0x10)) {
                forceBump = true;                       // BNE bra_DBA6
            } else {
                dpad = Sub_E451_convert_Dpad_buttons(BTNHOLD(x));
                if (dpad & 0x80) {
                    forceBump = true;                   // sin cruceta: estado $80
                }
            }
        }
        if (forceBump) {
            Sub_E420_change_tank_status(x, con_tank_flag_80);
            TFLAGS(x) |= 0x08;
            continue;
        }
        if ((PLRFLAGS(x) & 0x80) && (PLRFLAGS(x) & 0x1F) == 0) {
            PLRFLAGS(x) = 0x9C;
            R(ram_sfx_movement_ice) = 1;
        }
        const u8 cur = TFLAGS(x) & 0x03;
        if (cur != dpad && U8(cur ^ 0x02) != dpad) {
            TPOSX(x) = WX(TPOSX(x) + 4) & ~0x07;        // giro perpendicular: alinea a 8 px
            TPOSY(x) = U8(TPOSY(x) + 4) & 0xF8;
        }
        TFLAGS(x) = dpad | con_tank_flag_A0;
    }
}

// sub_DBF1_tank_movement (00:DBF1)
void Battle::Sub_DBF1_tank_movement() {
    if (R(ram_clock_timer) != 0 && (R(ram_frm_cnt_lo) & 0x3F) == 0) {
        R(ram_clock_timer)--;
    }
    for (int x = lastTank_; x >= 0; --x) {
        bool run;
        if (x < nPlayers_) {
            const u8 lo = R(ram_frm_cnt_lo);
            run = (lo & 0x01) != 0 || (lo & 0x03) == 0;
        } else {
            const u8 f = TFLAGS(x);
            if ((IsVersus() ? teamClock_[vsTeam_[x]] != 0 : R(ram_clock_timer) != 0) && (f & 0x80) && f < 0xE0 && !IsAlly(x)) {
                run = false;                            // congelado por el reloj
            } else if ((TTYPE(x) & 0xF0) == 0xA0) {
                run = true;                             // el rapido se mueve todos los cuadros
            } else {
                run = ((x ^ R(ram_frm_cnt_lo)) & 0x01) != 0;
            }
        }
        if (run && onSand_[x]) {                         // arena: el tanque se queda con la mitad de sus pasos (va a la mitad de velocidad)
            const int lo = R(ram_frm_cnt_lo);
            const int m = lo & 7;
            const bool allowed = x < nPlayers_ ? (m == 1 || m == 4 || m == 7) : ((lo & 3) < 2);
            if (!allowed) run = false;
        }
        if (run) {
            Sub_DC3D_tank_status_handler(x);
            if (speedT_[x] > 0 && (TFLAGS(x) & 0x80) && TFLAGS(x) < 0xE0) Sub_DC3D_tank_status_handler(x);     // velocidad: el doble de pasos
        }
    }
}

// sub_DC3D_tank_status_handler (00:DC3D): despacho por la nibble alta de ram_tank_flags.
void Battle::Sub_DC3D_tank_status_handler(int x) {
    switch (TFLAGS(x) >> 4) {
        case 0x0: break;
        case 0x1: case 0x2: case 0x3: case 0x4: case 0x5: case 0x6: case 0x7:
            Status_Explosion(x);
            break;
        case 0x8: Status_80(x); break;
        case 0x9: Status_90(x); break;
        case 0xA: Status_A0(x); break;
        case 0xB: Status_FollowTarget(x, con_tank_flag_follow_HQ); break;   // en los niveles del aguila: el punto de guardia
        case 0xC: Status_FollowTarget(x, con_tank_flag_follow_p2); break;
        case 0xD: Status_FollowTarget(x, con_tank_flag_follow_p1); break;
        case 0xE: Status_E0(x); break;
        case 0xF: Status_F0(x); break;
    }
}

// ofs_000_DC52_80 (00:DC52): el tanque choco; espera unos cuadros ("rebote").
void Battle::Status_80(int x) {
    if (x < nPlayers_) {
        const u8 pf = PLRFLAGS(x);
        if ((pf & 0x80) && (pf & 0x7F) != 0) {
            PLRFLAGS(x)--;                              // desliza sobre hielo
            TWHEELS(x) ^= 0x04;
            Loc_DC97_try_move(x);
            return;
        }
    }
    TFLAGS(x) = U8(TFLAGS(x) - 4);
    if ((TFLAGS(x) & 0x0C) == 0) {
        Sub_E420_change_tank_status(x, con_tank_flag_A0);
    }
}

// ofs_000_DD48_90 (00:DD48): tras un choque con media vuelta, elige rumbo.
void Battle::Status_90(int x) {
    if ((Sub_D44D_generate_random_number() & 0x01) == 0) {
        Sub_DE72_pick_goal(x);
        return;
    }
    u8 a;
    if ((Sub_D44D_generate_random_number() & 0x01) != 0) {
        a = U8(TFLAGS(x) + 1);
    } else {
        a = U8(TFLAGS(x) - 1);
    }
    TFLAGS(x) = (a & 0x03) | con_tank_flag_A0;
}

// ofs_000_DC7C_A0 (00:DC7C): tanque activo; los enemigos alineados a 8 px
// pueden cambiar de objetivo con probabilidad 1/16.
void Battle::Status_A0(int x) {
    if (wide_ && !IsVersus() && IsAlly(x)) { AllyDrive(x); return; }          // los aliados juegan con la IA de los combatientes del royale
    if (x >= nPlayers_ && (TPOSX(x) & 0x07) == 0 && (TPOSY(x) & 0x07) == 0) {
        int ix, iy;
        const bool seeking = wide_ && !IsVersus() && !IsAlly(x) && ((TTYPE(x) >> 5) & 3) == 0 && NearestItem(TankX(x), TankY(x), ix, iy);
        if ((Sub_D44D_generate_random_number() & (seeking ? 0x03 : 0x0F)) == 0) {      // (el enemigo de tipo 1 que va por un item corrige el rumbo mucho mas seguido)
            Sub_DE72_pick_goal(x);
            return;
        }
    }
    Loc_DC97_try_move(x);
}

// loc_DC97 (00:DC97): intento de avanzar 1 px con dos sondas en el frente.
void Battle::Loc_DC97_try_move(int x) {
    if (!StepForward(x)) Loc_DD11_blocked(x);
}

bool Battle::StepForward(int x) {
    const int dir = TFLAGS(x) & 0x03;
    const int dy = S8(tbl_E470[dir]);
    const int dx = S8(tbl_E46C[dir]);
    const int t59 = dy * 8;
    const int t57 = U8(dy + TPOSY(x));
    const int t58 = dx * 8;
    const int t56 = WX(dx + TPOSX(x));

    // sub_DD6E / sub_DD76: si el valor >= la posicion, resta 1.
    auto adjustX = [&](int value, int pos) { return value >= pos ? WX(value - 1) : value; };
    auto adjustY = [](int value, int pos) { return value >= pos ? U8(value - 1) : value; };

    const bool ghost = x < nPlayers_ && ghostT_[x] > 0;      // invisible: atraviesa ladrillos y tanques (no el hierro, el agua ni el aguila)
    auto passable = [&](int px, int py) {
        u8 t = M(PointerPx(px, py));
        if (ghost) {
            if (t >= 0xC8) return false;
            t &= 0x7F;
            if (t <= 0x0F) return true;
        }
        if (t & 0x80) return false;             // marcado por otro tanque (o aguila: $C8+)
        if (t == 0) return true;
        return t >= con_block_type + 0x20;      // hielo / arbustos
    };

    const int x1 = adjustX(WX(t56 + t58 + t59), t56);
    const int y1 = adjustY(U8(t57 + t58 + t59), t57);
    if (passable(x1, y1)) {
        const int x2 = adjustX(WX(t56 + t58 - t59), t56);
        const int y2 = adjustY(U8(t57 + t59 - t58), t57);
        if (passable(x2, y2)) {
            TPOSX(x) = t56;
            TPOSY(x) = t57;
            TWHEELS(x) ^= 0x04;                  // loc_DD29
            return true;
        }
    }
    return false;
}

// bra_DD11 (00:DD11): el paso esta bloqueado.
void Battle::Loc_DD11_blocked(int x) {
    if (x < nPlayers_) {
        TWHEELS(x) ^= 0x04;
        return;
    }
    if ((Sub_D44D_generate_random_number() & 0x03) != 0) {
        Sub_E420_change_tank_status(x, con_tank_flag_80);   // rebote (25% no: 75% si)
        TFLAGS(x) |= 0x08;
        TWHEELS(x) ^= 0x04;
        return;
    }
    // bra_DD30: media vuelta
    if ((TPOSX(x) & 0x07) == 0 && (TPOSY(x) & 0x07) == 0) {
        Sub_E420_change_tank_status(x, con_tank_flag_90);
    }
    TFLAGS(x) ^= 0x02;
}

// ofs_000_DD7E / DD89 / DD94 (00:DD7E): apunta hacia el jugador 1, el jugador 2 o el aguila.
void Battle::Status_FollowTarget(int x, u8 status) {
    if (status == con_tank_flag_follow_p1 || status == con_tank_flag_follow_p2) {
        const int p = wide_ ? chaseTarget_[x] : (status == con_tank_flag_follow_p1 ? 0 : 1);
        DESTX = TPOSX(p);
        R(ram_enemy_destination_Y) = TPOSY(p);
    } else if (IsVersus()) {
        const int opp = IsRoyale() ? 0 : 1 - TeamOf(x);           // batalla vs: rumbo al aguila del otro equipo
        DESTX = IsRoyale() ? 13 * 16 + 8 : spec_.vsEagleCol[opp] * 16 + 8;       // (battle royale: al centro del mapa)
        R(ram_enemy_destination_Y) = static_cast<u8>(IsRoyale() ? 7 * 16 + 8 : spec_.vsEagleRow[opp] * 16 + 8);
    } else if (wide_ && x >= nPlayers_ && !IsAlly(x) && ((TTYPE(x) >> 5) & 3) == 0 && [&] { int ix, iy; if (!NearestItem(TankX(x), TankY(x), ix, iy)) return false; DESTX = ix; R(ram_enemy_destination_Y) = static_cast<u8>(iy); return true; }()) {
        // enemigo de tipo 1: hay un item en el campo, va tras el
    } else if (IsBossLevel()) {
        DESTX = guardX_[x];                      // niveles del aguila enemiga: se queda cerca de ella
        R(ram_enemy_destination_Y) = static_cast<u8>(guardY_[x]);
    } else {
        DESTX = 0x78 + BaseShiftX();             // el aguila
        R(ram_enemy_destination_Y) = static_cast<u8>(0xD8 + BaseShiftY());
    }
    TFLAGS(x) = Sub_DDA2_direction_to_target(x);
}

// sub_DDA2 (00:DDA2): rumbo hacia el destino segun el signo de la distancia.
u8 Battle::Sub_DDA2_direction_to_target(int x) {
    auto sign = [](int target, int pos) -> int {
        if (target == pos) return 0;
        return target > pos ? 1 : -1;
    };
    const int sx = sign(DESTX, TPOSX(x)) + 1;
    const int sy = sign(R(ram_enemy_destination_Y), TPOSY(x)) + 1;
    const int index = sy * 3 + sx;
    bool second;
    if (x < nPlayers_) {
        second = (((x << 1) ^ R(ram_frm_cnt_hi)) & 0x02) != 0;
    } else {
        second = (Sub_D44D_generate_random_number() & 0x01) != 0;
    }
    return tbl_E486_tank_flags[(second ? 9 : 0) + index];
}

// sub_DE72 (00:DE72): selector de objetivo segun el tiempo de la etapa.
void Battle::Sub_DE72_pick_goal(int x) {
    if (IsVersus()) {
        VersusPickGoal(x);
        return;
    }
    if (wide_ && x >= nPlayers_ && !IsAlly(x) && ((TTYPE(x) >> 5) & 3) == 0) {          // enemigo de tipo 1: va por el item
        int ix, iy;
        if (NearestItem(TankX(x), TankY(x), ix, iy)) {
            Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
            return;
        }
    }
    if (IsAlly(x)) {
        // aliado: se comporta como un enemigo (rumbo al azar o persecucion) pero su objetivo son los
        // enemigos y nunca la base
        const u8 r = Sub_D44D_generate_random_number();
        const int e = NearestEnemy(x);
        if ((r & 3) == 0 || e < 0) {                     // 1 de cada 4 elecciones: rumbo al azar
            TFLAGS(x) = static_cast<u8>((Sub_D44D_generate_random_number() & 0x03) | con_tank_flag_A0);
            return;
        }
        chaseTarget_[x] = e;
        Sub_E420_change_tank_status(x, con_tank_flag_follow_p1);
        return;
    }
    if (IsBossLevel()) {
        // Niveles del aguila enemiga: los enemigos defienden su aguila (patrullan cerca de ella) y pasan al ataque, yendo por
        // el jugador mas cercano, cuando algun jugador se acerca a su aguila o a ellos.
        int best = -1;
        const bool aggro = BossAggro(x, best);
        hunting_[x] = aggro && best >= 0;
        if (aggro && best >= 0) {
            if ((Sub_D44D_generate_random_number() & 7) == 0) {
                TFLAGS(x) = (Sub_D44D_generate_random_number() & 0x03) | con_tank_flag_A0;
                return;
            }
            chaseTarget_[x] = best;
            Sub_E420_change_tank_status(x, con_tank_flag_follow_p1);
            return;
        }
        // defensa: un punto al azar de la zona de guardia (13 columnas por 5 filas alrededor del aguila, fuera de la fortaleza)
        const int ex = EagleCol() * 16 + 8, ey = EagleRow() * 16 + 8;
        int gx = ex + (static_cast<int>(Sub_D44D_generate_random_number() % 13) - 6) * 16;
        int gy = ey + static_cast<int>(Sub_D44D_generate_random_number() % 5) * 16;
        if (gx >= ex - 48 && gx < ex + 48 && gy < ey + 32) gy = ey + 48;
        gx = gx < 8 ? 8 : (gx > 424 ? 424 : gx);
        gy = gy > 216 ? 216 : gy;
        guardX_[x] = gx;
        guardY_[x] = gy;
        Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
        return;
    }
    const u8 interval = R(ram_enemy_spawn_interval);
    const u8 hi = R(ram_frm_cnt_hi);
    u8 a = interval >> 2;
    if (a < hi) {
        Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
        return;
    }
    a >>= 1;
    if (a >= hi) {
        TFLAGS(x) = (Sub_D44D_generate_random_number() & 0x03) | con_tank_flag_A0;
        return;
    }
    u8 target;
    if (wide_) {
        int cand[4];
        int n = 0;
        for (int p = 0; p < nPlayers_; ++p) {
            if (TFLAGS(p) != 0 && ghostT_[p] == 0) {
                cand[n++] = p;
            }
        }
        if (n == 0) {
            Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
            return;
        }
        chaseTarget_[x] = cand[x % n];                  // reparte a los enemigos entre los jugadores vivos
        Sub_E420_change_tank_status(x, con_tank_flag_follow_p1);
        return;
    }
    if (TFLAGS(0) != 0 && ((x & 1) == 0 || TFLAGS(1) == 0)) {
        target = con_tank_flag_follow_p1;
    } else {
        target = con_tank_flag_follow_p2;
    }
    if (TFLAGS(0) == 0) {
        target = con_tank_flag_follow_p2;
    }
    Sub_E420_change_tank_status(x, target);
}

// ofs_000_DDEA_10..70 (00:DDEA): cuenta regresiva de la explosion y muerte.
void Battle::Status_Explosion(int x) {
    TFLAGS(x) = U8(TFLAGS(x) - 1);
    if ((TFLAGS(x) & 0x0F) != 0) {
        return;
    }
    u8 a = U8(TFLAGS(x) - 0x10);
    if (a != 0) {
        a |= (a == 0x10) ? 0x06 : 0x03;     // el cartel de puntos dura un poco mas
        TFLAGS(x) = a;
        return;
    }
    TFLAGS(x) = 0;                          // el tanque murio
    if (x >= nPlayers_) {
        if (!IsAlly(x)) {
            R(ram_enemies_left_cnt)--;
        }
        return;
    }
    if (!RoyaleTimed()) LIVES(x)--;                       // (battle royale por tiempo: reaparece siempre)
    if (LIVES(x) != 0) {
        Sub_E363_tank_spawn_handler(x);
        return;
    }
    if (R(ram_game_over_flag) != con_not_game_over) {
        return;
    }
    // se queda sin vidas: mensaje de GAME OVER desde su lado
    if (IsVersus()) return;                  // batalla vs: el final lo decide VersusCheckEnd
    if (wide_) {
        bool anyOther = false;
        for (int p = 0; p < nPlayers_; ++p) {
            if (p != x && LIVES(p) != 0) anyOther = true;
        }
        if (!anyOther) return;
        if ((x & 1) == 0) {
            R(ram_game_over_msg_mov_type) = 3;
            MSGX = 0x20 + ShiftX();
        } else {
            R(ram_game_over_msg_mov_type) = 1;
            MSGX = 0xC0 + ShiftX();
        }
    } else if (x == 0) {
        if (LIVES(1) == 0) return;
        R(ram_game_over_msg_mov_type) = 3;
        MSGX = 0x20;
    } else {
        if (LIVES(0) == 0) return;
        R(ram_game_over_msg_mov_type) = 1;
        MSGX = 0xC0;
    }
    R(ram_game_over_msg_timer) = 0x0D;      // sub_DE46_set_message_init_data
    R(ram_game_over_msg_pos_Y) = static_cast<u8>(0xD8 + ShiftY());
    R(ram_frm_cnt_lo) = 0;
}

// ofs_000_DE55_F0_respawn (00:DE55)
void Battle::Status_F0(int x) {
    TFLAGS(x) = U8(TFLAGS(x) + 1);
    if ((TFLAGS(x) & 0x0F) == 0x0E) {
        TFLAGS(x) = con_tank_flag_E0;
    }
}

// ofs_000_DE64_E0 (00:DE64)
void Battle::Status_E0(int x) {
    TFLAGS(x) = U8(TFLAGS(x) + 1);
    if ((TFLAGS(x) & 0x0F) == 0x0E) {
        Sub_E3B8_finish_respawn(x);
    }
}

// sub_E363_tank_spawn_handler (00:E363)
void Battle::Sub_E363_tank_spawn_handler(int x) {
    ghostT_[x] = speedT_[x] = portalCd_[x] = 0;
    TTYPE(x) = 0;
    shieldHp_[x] = 0;
    if (x < nPlayers_) {
        // campo ancho: a los lados de la base, como en la version anterior (P1 a 2 casillas del aguila a la izquierda, P2 a la derecha, P3 y P4 una mas afuera)
        static const int kWidePlayerX[4] = {0x78 + 96 - 0x20, 0x78 + 96 + 0x20, 0x78 + 96 - 0x30, 0x78 + 96 + 0x30};
        TPOSX(x) = wide_ ? kWidePlayerX[x & 3] : tbl_E47A_player_spawn_pos_X[x];
        TPOSY(x) = wide_ ? 0xD8 + ShiftY() : tbl_E47C_player_spawn_pos_Y[x];
        if (wide_ && spec_.active && !spec_.versus && x < 4) {          // nivel hecho a mano: los lugares que puso el editor
            TPOSX(x) = spec_.playerCol[x] * 16 + 8;
            TPOSY(x) = spec_.playerRow[x] * 16 + 8;
        }
        if (IsRoyale()) {                                // battle royale: cada uno en su punto de salida
            int col, row;
            RoyaleSpawnBlock(x, col, row);
            TPOSX(x) = col * 16 + 8;
            TPOSY(x) = row * 16 + 8;
        } else if (IsVersus()) {                         // batalla vs: al lado de la propia aguila, mirando al otro equipo
            int col, row;
            VsSideBlock(spec_.vsEagleCol[playerTeam_[x]], spec_.vsEagleRow[playerTeam_[x]], VsMemberIndex(x), col, row);
            TPOSX(x) = col * 16 + 8;
            TPOSY(x) = row * 16 + 8;
        }
        STUN(x) = 0;
    } else {
        R(ram_enemy_spawn_pos_index)++;
        int y = R(ram_enemy_spawn_pos_index);
        // con 3 o 4 jugadores hay 2 puntos mas, en los laterales a mitad del mapa
        // (en los niveles del aguila: a los lados del aguila, enfrente, las dos puntas de arriba y, con 3+, los laterales)
        const int spawnPoints = SpawnPointCount();
        if (y == spawnPoints) {
            R(ram_enemy_spawn_pos_index) = 0;
            y = 0;
        }
        if (wide_) {
            int sx, sy;
            SpawnPointPx(y, sx, sy);
            TPOSX(x) = sx;
            TPOSY(x) = sy;
        } else {
            TPOSX(x) = tbl_E474_enemy_spawn_pos_X[y];
            TPOSY(x) = tbl_E477_enemy_spawn_pos_Y[y];
        }
        const u8 cnt = IsBossLevel() ? static_cast<u8>(++bossSpawned_ % 7) : R(ram_enemy_spawn_cnt);
        if (wide_ ? (cnt % 7) == 3 : (cnt == 0x03 || cnt == 0x0A || cnt == 0x11)) {
            TTYPE(x) = 0x04;                    // tanque con bonus (parpadea)
            BONUSX = 0;                         // y esconde el item anterior
        }
    }
    TFLAGS(x) = con_tank_flag_respawn;
    Sub_D80B_write_block_tiles_and_attribute(0x0F, TPOSX(x), TPOSY(x));   // limpia el lugar
}

// ---------------------------------------------------------------------------
// MODO BATALLA VS
// ---------------------------------------------------------------------------

int Battle::VsEagleAt(int px, int py) const {
    for (int t = 0; t < 2; ++t) {
        const int x0 = spec_.vsEagleCol[t] * 16, y0 = spec_.vsEagleRow[t] * 16;
        if (px >= x0 && px < x0 + 16 && py >= y0 && py < y0 + 16) return t;
    }
    return -1;
}

bool Battle::NearVsEagle(int col, int row) const {
    for (int t = 0; t < 2; ++t) {
        if (std::abs(col - spec_.vsEagleCol[t]) <= 1 && std::abs(row - (spec_.vsEagleRow[t] - 1)) <= 1) return true;
    }
    return false;
}

void Battle::DestroyVsEagle(int t) {
    vsEagleAlive_[t] = false;
    ++eaglesDestroyed_;
    R(ram_sfx_explosion_hq) = 1;
    R(ram_sfx_explosion_player) = 1;
    DrawEagleAt(2 * spec_.vsEagleCol[t], 2 * spec_.vsEagleRow[t], true, 0);
}

// ---- battle royale ----
void Battle::RoyaleSpawnBlock(int id, int& col, int& row) {
    static const int kSpawn[8][2] = {{1, 13}, {25, 1}, {1, 1}, {25, 13}, {1, 7}, {25, 7}, {13, 13}, {13, 1}};     // esquinas opuestas de a pares
    col = kSpawn[id & 7][0];
    row = kSpawn[id & 7][1];
}

int Battle::RoyaleColor(int id) const { return playerColor_[id & 7]; }

int Battle::RoyaleLives(int id) const { return id >= 0 && id < nPlayers_ ? Lives(id) : 0; }

bool Battle::RoyaleAlive(int id) const { return id >= 0 && id < nPlayers_ && present_[id] && (Lives(id) != 0 || TankFlags(id) != 0); }

u8 Battle::VsTankAttr(int tank) const {
    if (tank >= 0 && tank < nPlayers_) return kColorAttr[playerColor_[tank] & 7];
    return kColorAttr[kTeamColor[TeamOf(tank) & 1]];
}

// Lugares donde aparecen los miembros de un equipo: pegados a la fortaleza de su aguila, del lado del centro del mapa.
void Battle::VsSideBlock(int ec, int er, int k, int& col, int& row) {
    const int sx = ec < 13 ? 1 : -1, sy = er < 7 ? 1 : -1;
    static const int kOff[8][2] = {{2, 0}, {0, 2}, {2, 2}, {4, 0}, {0, 4}, {4, 2}, {2, 4}, {4, 4}};
    int n = 0;
    for (const auto& o : kOff) {
        const int c = ec + o[0] * sx, r = er + o[1] * sy;
        if (c < 1 || c > 25 || r < 1 || r > 13) continue;
        if (n++ == k) { col = c; row = r; return; }
    }
    col = ec + 2 * sx < 1 ? 1 : (ec + 2 * sx > 25 ? 25 : ec + 2 * sx);
    row = er < 1 ? 1 : (er > 13 ? 13 : er);
}

// Lugar (0..3) del jugador en su equipo (por orden).
int Battle::VsMemberIndex(int x) const {
    const int t = TeamOf(x);
    int k = 0;
    for (int p = 0; p < x && p < nPlayers_; ++p) k += (playerTeam_[p] == t) ? 1 : 0;
    return k > 3 ? 3 : k;
}

int Battle::VsBotCount(int t) const {
    int n = 0;
    for (int p = 0; p < nPlayers_; ++p) n += (aiPlayer_[p] && playerTeam_[p] == (t & 1)) ? 1 : 0;
    return n;
}

int Battle::VsBotLives(int t) const {
    int n = 0;
    for (int p = 0; p < nPlayers_; ++p) if (aiPlayer_[p] && playerTeam_[p] == (t & 1)) n += Lives(p);
    return n;
}

// Los lugares que no ocupa una persona los juega la IA (siguen siendo jugadores completos).
void Battle::SetHumanMask(int mask) {
    for (int p = 0; p < nPlayers_ && p < kMaxPlayers; ++p) {
        present_[p] = true;
        aiPlayer_[p] = IsVersus() && !((mask >> p) & 1);
        LIVES(p) = static_cast<u8>(startLives_);
    }
}

void Battle::VersusAiInputs() {
    for (int p = 0; p < nPlayers_; ++p) if (aiPlayer_[p]) pad_[p] = AiButtons(p);
}

// Cuanto cuesta que el tanque pase por el bloque (col, fila): vacio 1, arena 2, ladrillo 4 (hay que romperlo), lava 9, hierro / agua / borde / aguila: no.
int Battle::AiBlockCost(int col, int row) const {
    if (col < 0 || col >= kWideBlocksX || row < 0 || row > kWideBlocksY) return 255;
    const int p = Sub_D5FB_calculate_pointer(2 * col, 2 * row);
    const int st = Stride();
    const int idx[4] = {p, p + 1, p + st, p + st + 1};
    int cost = 1;
    for (int i : idx) {
        const int t = M(i) & 0x7F;
        int c;
        if (t == 0 || t == 0x21 || t == 0x22) c = 1;
        else if (t == 0x23) c = 2;
        else if (t == kLavaTile) c = 9;
        else if (t == 0x10) c = (i < static_cast<int>(sizeof(rock_)) && rock_[i]) ? 6 : 255;
        else if (t >= 0x11) c = 255;
        else c = 4;
        cost = std::max(cost, c);
    }
    return cost;
}

// Primer paso (0 arriba, 1 izquierda, 2 abajo, 3 derecha) del camino mas barato entre dos bloques; nextCost = lo que cuesta ese bloque.
int Battle::AiPathDir(int sc, int sr, int gc, int gr, int& nextCost) {
    static const int kDc[4] = {0, -1, 0, 1}, kDr[4] = {-1, 0, 1, 0};
    constexpr int kRows = kWideBlocksY + 1;
    int cost[kRows][kWideBlocksX];
    for (int r = 0; r < kRows; ++r) for (int c = 0; c < kWideBlocksX; ++c) cost[r][c] = AiBlockCost(c, r);
    int dist[kRows][kWideBlocksX];
    for (auto& row : dist) for (int& d : row) d = 1 << 20;
    typedef std::pair<int, int> Node;                            // (distancia, fila * 64 + columna)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> q;
    if (cost[gr][gc] >= 255) return -1;
    dist[gr][gc] = 0;
    q.push({0, gr * 64 + gc});
    while (!q.empty()) {
        const Node n = q.top();
        q.pop();
        const int r = n.second / 64, c = n.second % 64;
        if (n.first > dist[r][c]) continue;
        for (int k = 0; k < 4; ++k) {
            const int nr = r + kDr[k], nc = c + kDc[k];
            if (nr < 0 || nr >= kRows || nc < 0 || nc >= kWideBlocksX || cost[nr][nc] >= 255) continue;
            const int nd = n.first + cost[r][c];                 // (entrar a (r, c) desde un vecino cuesta lo que (r, c))
            if (nd < dist[nr][nc]) { dist[nr][nc] = nd; q.push({nd, nr * 64 + nc}); }
        }
    }
    if (sr == gr && sc == gc) return -1;
    int bestDir = -1, bestTotal = 1 << 20;
    for (int k = 0; k < 4; ++k) {
        const int nr = sr + kDr[k], nc = sc + kDc[k];
        if (nr < 0 || nr >= kRows || nc < 0 || nc >= kWideBlocksX || cost[nr][nc] >= 255 || dist[nr][nc] >= (1 << 20)) continue;
        const int total = cost[nr][nc] + dist[nr][nc];           // pisar ese bloque + lo que falta desde ahi
        if (total < bestTotal) { bestTotal = total; bestDir = k; nextCost = cost[nr][nc]; }
    }
    return bestDir;
}

// Los botones que aprieta la IA de un jugador en este cuadro. Anda con calma: elige un destino (el rival cercano, un item, o un punto
// cualquiera del mapa para recorrerlo), busca el camino por los bloques libres (los ladrillos los rompe a tiros) y lo sigue sin cambiar
// de idea a cada rato; esquiva las balas que le vienen de frente y dispara solo cuando apunta a algo, con pausas entre tiro y tiro.
u8 Battle::AiButtons(int p) {
    const u8 fl = TFLAGS(p);
    if (!(fl & 0x80) || fl >= 0xE0 || vsWinner_ >= 0 || R(ram_pause_flag) != 0) { aiStuck_[p] = 0; aiTimer_[p] = 0; aiGoalT_[p] = 0; return 0; }
    const int x = TankX(p), y = TankY(p);
    const int myTeam = TeamOf(p);
    const int facing = fl & 3;
    const int sc = std::max(0, std::min(kWideBlocksX - 1, x >> 4)), sr = std::max(0, std::min(kWideBlocksY, y >> 4));
    if (x == aiLastX_[p] && y == aiLastY_[p]) ++aiStuck_[p]; else aiStuck_[p] = 0;
    aiLastX_[p] = x;
    aiLastY_[p] = y;
    if (aiTimer_[p] > 0) --aiTimer_[p];
    if (aiGoalT_[p] > 0) --aiGoalT_[p];
    if (aiFireCool_[p] > 0) --aiFireCool_[p];
    if (aiDodgeT_[p] > 0) --aiDodgeT_[p];
    if (aiWanderT_[p] > 0) --aiWanderT_[p];
    const bool wander = aiWanderT_[p] > 0;                       // despues de trabarse: un rato a recorrer, sin ir tras nadie
    auto hostile = [&](int e) {
        const u8 f = TFLAGS(e);
        return e != p && (f & 0x80) && f < 0xE0 && TeamOf(e) != myTeam && !(e < nPlayers_ && ghostT_[e] > 0);
    };
    auto rnd = [&]() { return static_cast<int>(Sub_D44D_generate_random_number()); };
    // 1) esquivar una bala rival que viene de frente (de cerca, y una sola vez por tanda)
    if (aiDodgeT_[p] <= 0) {
        for (int b = 0; b < nBullets_; ++b) {
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            const int sh = BulletShooter(b);
            if (sh == p || sh > lastTank_ || TeamOf(sh) == myTeam) continue;
            const int bx = wd_.bposX[b] | (bulletXHi_[b] << 8), by = wd_.bposY[b];
            const int bd = BSTATUS(b) & 3;
            int dodge = -1;
            if (bd == 1 || bd == 3) {
                const bool toward = bd == 3 ? bx < x : bx > x;
                if (toward && std::abs(by - y) < 11 && std::abs(bx - x) < 56) dodge = by < y ? 2 : 0;
            } else {
                const bool toward = bd == 2 ? by < y : by > y;
                if (toward && std::abs(bx - x) < 11 && std::abs(by - y) < 56) dodge = bx < x ? 3 : 1;
            }
            if (dodge >= 0) { aiDir_[p] = dodge; aiDodgeT_[p] = 12; aiGoalT_[p] = 0; break; }
        }
    }
    // 2) destino
    int best = -1, bestDist = 1 << 20;
    for (int e = 0; e <= lastEnemy_; ++e) {
        if (!hostile(e)) continue;
        const int d = std::abs(TankX(e) - x) + std::abs(TankY(e) - y);
        if (d < bestDist) { bestDist = d; best = e; }
    }
    const bool royale = IsRoyale();
    int ox = -1, oy = -1;
    if (!royale && vsEagleAlive_[1 - myTeam]) { ox = spec_.vsEagleCol[1 - myTeam] * 16 + 8; oy = spec_.vsEagleRow[1 - myTeam] * 16 + 8; }
    const bool atGoal = sc == aiGoalC_[p] && sr == aiGoalR_[p];
    if (aiGoalT_[p] <= 0 || atGoal || aiStuck_[p] > 24) {
        bool picked = false;
        if (!royale && !wander && vsEagleAlive_[myTeam]) {      // defiende su aguila si un rival se le acerca
            const int ex = spec_.vsEagleCol[myTeam] * 16 + 8, ey = spec_.vsEagleRow[myTeam] * 16 + 8;
            int threat = -1, td = 1 << 20;
            for (int e = 0; e <= lastEnemy_; ++e) {
                if (!hostile(e)) continue;
                const int d = std::abs(TankX(e) - ex) + std::abs(TankY(e) - ey);
                if (d < td) { td = d; threat = e; }
            }
            if (threat >= 0 && td < 160) { aiGoalC_[p] = TankX(threat) >> 4; aiGoalR_[p] = TankY(threat) >> 4; aiGoalT_[p] = 20; picked = true; }
        }
        if (!picked && !wander && best >= 0 && bestDist < 105) {   // un rival cerca: va a su encuentro
            aiGoalC_[p] = TankX(best) >> 4;
            aiGoalR_[p] = TankY(best) >> 4;
            aiGoalT_[p] = 20;
            picked = true;
        }
        if (!picked && !wander) {                                // un item que este cerca
            if (royale) {
                int bi = -1, bd = 260;
                for (int i = 0; i < 3; ++i) {
                    if (!rItems_[i].on) continue;
                    const int d = std::abs(rItems_[i].x - x) + std::abs(rItems_[i].y - y);
                    if (d < bd) { bd = d; bi = i; }
                }
                if (bi >= 0) { aiGoalC_[p] = rItems_[bi].x >> 4; aiGoalR_[p] = rItems_[bi].y >> 4; aiGoalT_[p] = 30; picked = true; }
            } else if (BonusActive()) {
                const int ix = R(ram_bonus_pos_X) | (bonusXHi_ << 8), iy = R(ram_bonus_pos_Y);
                if (std::abs(ix - x) + std::abs(iy - y) < 200) { aiGoalC_[p] = ix >> 4; aiGoalR_[p] = iy >> 4; aiGoalT_[p] = 30; picked = true; }
            }
        }
        if (!picked) {                                           // a recorrer el mapa: un punto cualquiera (en equipos, a veces el aguila rival)
            if (!royale && ox >= 0 && (rnd() % 4) == 0) { aiGoalC_[p] = ox >> 4; aiGoalR_[p] = oy >> 4; }
            else {
                for (int tries = 0; tries < 12; ++tries) {
                    const int c = 1 + rnd() % (kWideBlocksX - 2), r = 1 + rnd() % (kWideBlocksY - 1);
                    if (AiBlockCost(c, r) < 255 && (std::abs(c - sc) + std::abs(r - sr)) >= 4) { aiGoalC_[p] = c; aiGoalR_[p] = r; break; }
                }
            }
            aiGoalT_[p] = 90 + rnd() % 90;                       // y lo sigue un buen rato
        }
        aiStuck_[p] = aiStuck_[p] > 24 ? 0 : aiStuck_[p];
    }
    // trabado (contra una pared o contra otro tanque): primero prueba a tiros, despues se va por otro lado un buen rato
    bool breakWall = false;
    if (aiStuck_[p] > 6) breakWall = true;
    if (aiStuck_[p] > 14) {
        static const int kDc[4] = {0, -1, 0, 1}, kDr[4] = {-1, 0, 1, 0};
        int free[4], nf = 0;
        for (int k = 0; k < 4; ++k) if (AiBlockCost(sc + kDc[k], sr + kDr[k]) < 255 && k != facing) free[nf++] = k;
        if (nf == 0) free[nf++] = (facing + 2) & 3;
        aiDir_[p] = free[rnd() % nf];
        aiTimer_[p] = 22 + rnd() % 20;
        aiWanderT_[p] = 120;
        aiGoalT_[p] = 0;
        aiStuck_[p] = 0;
        breakWall = false;
    }
    // 3) rumbo
    int dir = facing, nextCost = 1;
    if (aiDodgeT_[p] > 0) {
        dir = aiDir_[p];
    } else if (aiTimer_[p] > 0) {
        dir = aiDir_[p];                                         // sigue el rumbo elegido un ratito: no cambia de idea a cada cuadro
    } else {
        int d = AiPathDir(sc, sr, aiGoalC_[p], aiGoalR_[p], nextCost);
        if (d < 0) {                                             // en el bloque de destino (o sin camino): hacia el punto exacto, o a otro lado
            const int gx = aiGoalC_[p] * 16 + 8, gy = aiGoalR_[p] * 16 + 8;
            if (std::abs(gx - x) >= 4 || std::abs(gy - y) >= 4) d = std::abs(gx - x) >= std::abs(gy - y) ? (gx > x ? 3 : 1) : (gy > y ? 2 : 0);
            else { d = facing; aiGoalT_[p] = 0; }
        } else {
            // alinearse con el centro del carril antes de avanzar (si no, se choca con las esquinas)
            const int cx = sc * 16 + 8, cy = sr * 16 + 8;
            if ((d == 1 || d == 3) && std::abs(cy - y) >= 5) d = cy > y ? 2 : 0;
            else if ((d == 0 || d == 2) && std::abs(cx - x) >= 5) d = cx > x ? 3 : 1;
        }
        dir = d;
        aiDir_[p] = d;
        aiTimer_[p] = 6;                                         // un minimo de 6 cuadros por rumbo
    }
    // 4) disparo: con calma
    bool fire = breakWall;
    if (aiFireCool_[p] <= 0 && !fire) {
        for (int e = 0; e <= lastEnemy_ && !fire; ++e) if (hostile(e) && LineOfFirePoint(p, TankX(e), TankY(e), 120)) fire = (rnd() % 2) == 0;       // si lo tiene en la mira, dispara enseguida
        if (!fire && ox >= 0 && LineOfFirePoint(p, ox, oy, 100)) fire = (rnd() % 3) == 0;
        if (!fire && (rnd() % 130) == 0) fire = true;
    }
    if (!royale && AimsAtEagleAt(p, spec_.vsEagleCol[myTeam], spec_.vsEagleRow[myTeam], spec_.vsLayers)) fire = false;   // no le tira a su aguila
    static const u8 kDirBtn[4] = {con_btn_Up, con_btn_Left, con_btn_Down, con_btn_Right};
    u8 out = kDirBtn[dir & 3];
    if (fire && (R(ram_frm_cnt_lo) & 1)) { out = static_cast<u8>(out | con_btn_A); aiFireCool_[p] = breakWall ? 12 : 36; }
    return out;
}

// ---- items del battle royale: tres a la vez, cada 20 segundos (a muerte: puede haber vidas; por tiempo, no) ----
void Battle::RoyaleItemsStep() {
    // se agarran
    for (int i = 0; i < 3; ++i) {
        RoyaleItem& it = rItems_[i];
        if (!it.on) continue;
        if (--it.life <= 0) { it.on = false; continue; }
        for (int p = nPlayers_ - 1; p >= 0; --p) {
            const u8 f = TFLAGS(p);
            if (!(f & 0x80) || f >= 0xE0) continue;
            if (std::abs(TankX(p) - it.x) >= 12 || std::abs(TankY(p) - it.y) >= 12) continue;
            it.on = false;
            Sub_D9E1_calculate_decimal_number(0x50);             // 500 puntos
            Sub_D9BE_add_score(p);
            R(ram_sfx_bonus_pickup) = 1;
            ++itemPickups_[p];
            BonusHandler(it.id, p);
            break;
        }
    }
    // los 20 segundos del proximo grupo empiezan a contar cuando no queda ninguno
    if (RoyaleItemCount() > 0) { royaleBonusT_ = 1200; return; }
    if (--royaleBonusT_ > 0) return;
    royaleBonusT_ = 1200;
    static const int kAll[12] = {0, 1, 3, 3, 4, 5, 6, 8, 9, 10, 11, 12};   // casco, reloj, estrella, granada, vida, pistola y los items nuevos
    static const int kTimed[11] = {0, 1, 3, 3, 4, 6, 8, 9, 10, 11, 12};   // por tiempo: sin vidas
    int n = 0;
    for (int tries = 0; tries < 60 && n < 3; ++tries) {
        const int c = 1 + Sub_D44D_generate_random_number() % (kWideBlocksX - 2), r = 1 + Sub_D44D_generate_random_number() % (kWideBlocksY - 1);
        if (!BlockEmptyAt(c, r)) continue;
        bool near = false;
        for (int i = 0; i < n; ++i) near = near || (std::abs(rItems_[i].x - (c * 16 + 8)) + std::abs(rItems_[i].y - (r * 16 + 8)) < 64);
        if (near) continue;
        RoyaleItem& it = rItems_[n++];
        it.on = true;
        it.x = c * 16 + 8;
        it.y = r * 16 + 8;
        const int roll = Sub_D44D_generate_random_number();
        it.id = spec_.royaleTimed ? kTimed[roll % 11] : kAll[roll % 12];
        it.life = 1800;
    }
    if (n > 0) R(ram_sfx_bonus_appear) = 1;
}

void Battle::RoyaleItemsDraw() {
    if ((R(ram_frm_cnt_lo) & 0x08) == 0) return;                // parpadean como los de siempre
    for (const RoyaleItem& it : rItems_) {
        if (!it.on || it.id >= 8) continue;                       // (los items nuevos los dibuja el renderer)
        R(0x0004) = 2;
        R(ram_0053_t01_spr_T) = U8((it.id << 2) + 0x81);
        R(ram_priority_spr_A) = 0;
        Sub_DA7B_display_2_sprites(it.x, it.y);
        R(ram_priority_spr_A) = 0x20;
    }
}

// El tanque x mira de frente a (px, py) a no mas de `range` pixeles y casi en linea.
bool Battle::LineOfFirePoint(int x, int px, int py, int range) {
    const int tx = TankX(x), ty = TankY(x);
    switch (TFLAGS(x) & 0x03) {
        case 0: return std::abs(px - tx) <= 10 && ty - py >= 0 && ty - py <= range;     // arriba
        case 1: return std::abs(py - ty) <= 10 && tx - px >= 0 && tx - px <= range;     // izquierda
        case 2: return std::abs(px - tx) <= 10 && py - ty >= 0 && py - ty <= range;     // abajo
        default: return std::abs(py - ty) <= 10 && px - tx >= 0 && px - tx <= range;    // derecha
    }
}

// Aparecen tanques con IA de cada equipo, de a uno por punto y por turnos, hasta un maximo a la vez; la reposicion no se acaba.
void Battle::VersusSpawnHandler() {
    if (vsWinner_ >= 0 || IsRoyale()) return;               // (el battle royale no tiene tanques con IA aparte de los combatientes)
    const int maxAi = 2 + tier_ / 3;
    for (int t = 0; t < 2; ++t) {
        if (vsAiTimer_[t] > 0) { --vsAiTimer_[t]; continue; }
        int alive = 0;
        for (int x = nPlayers_; x <= lastEnemy_; ++x) alive += (TFLAGS(x) != 0 && vsTeam_[x] == t) ? 1 : 0;
        if (alive >= maxAi) continue;
        for (int x = nPlayers_; x <= lastEnemy_; ++x) {
            if (TFLAGS(x) != 0) continue;
            const int n = spec_.nSpawns >= 4 ? 2 : 0;
            int px, py;
            if (n == 2) {
                const int i = t * 2 + (vsSpawned_[t] & 1);
                px = spec_.spawnCol[i] * 16 + 8;
                py = spec_.spawnRow[i] * 16 + 8;
            } else {                                         // sin puntos propios: arriba y abajo del lado del equipo
                const bool second = (vsSpawned_[t] & 1) != 0;          // medio a un lado y medio arriba/abajo, del lado de la aguila
                px = (second ? 13 : (t == 0 ? 1 : 25)) * 16 + 8;
                py = (second ? (t == 0 ? 13 : 1) : 7) * 16 + 8;
            }
            TTYPE(x) = (vsSpawned_[t] % 4 == 3) ? 0x04 : 0;   // uno de cada cuatro lleva un item (parpadea)
            shieldHp_[x] = 0;
            vsTeam_[x] = t;
            TPOSX(x) = px;
            TPOSY(x) = py;
            TWHEELS(x) = 0;
            TFLAGS(x) = con_tank_flag_respawn;
            Sub_D80B_write_block_tiles_and_attribute(0x0F, px, py);     // limpia el lugar
            ++vsSpawned_[t];
            const int interval = 330 - tier_ * 22;
            vsAiTimer_[t] = interval < 120 ? 120 : interval;
            break;
        }
    }
}

// Los tanques con IA van tras el tanque rival mas cercano si esta cerca y, si no, hacia la aguila del otro equipo.
void Battle::VersusPickGoal(int x) {
    const int t = TeamOf(x);
    const u8 r = Sub_D44D_generate_random_number();
    int best = -1, bestDist = 1 << 20;
    for (int e = 0; e <= lastEnemy_; ++e) {
        const u8 f = TFLAGS(e);
        if (!(f & 0x80) || f >= 0xE0 || TeamOf(e) == t || (e < nPlayers_ && ghostT_[e] > 0)) continue;
        const int d = std::abs(TankX(e) - TankX(x)) + std::abs(TankY(e) - TankY(x));
        if (d < bestDist) { bestDist = d; best = e; }
    }
    if ((r & 7) == 0) {                                  // de vez en cuando, rumbo al azar
        TFLAGS(x) = static_cast<u8>((Sub_D44D_generate_random_number() & 0x03) | con_tank_flag_A0);
        return;
    }
    if (best >= 0 && bestDist < 120 && (r & 1)) {
        chaseTarget_[x] = best;
        Sub_E420_change_tank_status(x, con_tank_flag_follow_p1);
        return;
    }
    Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
}

// Las balas dan a los tanques del otro equipo (los del propio equipo las dejan pasar).
void Battle::VersusCollisions() {
    for (int b = 0; b < nBullets_; ++b) {
        if ((BSTATUS(b) & 0xF0) != 0x40) continue;
        const int shooterTeam = TeamOf(BulletShooter(b));
        for (int x = 0; x <= lastEnemy_; ++x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0 || TeamOf(x) == shooterTeam || (x < nPlayers_ && ghostT_[x] > 0)) continue;
            if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
            if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
            BSTATUS(b) = 0x33;
            const int lvl = bulletLevel_[b];
            if (x < nPlayers_) {
                ++playerHits_[x];
                if (HELMET(x) != 0) {
                    BSTATUS(b) = 0;
                    ShieldHit(x, lvl == 3 ? 2 : 1);
                    break;
                }
                if (TUPGRADE(x) != 0) {                  // un impacto quita un nivel de arma (3 -> 2); a nivel 0 destruye
                    const int drop = lvl == 3 ? 2 : 1;
                    TUPGRADE(x) = static_cast<u8>(TUPGRADE(x) > 0x20 * drop ? TUPGRADE(x) - 0x20 * drop : 0);
                    TTYPE(x) = TUPGRADE(x);
                    R(ram_sfx_bullet_hit_tank) = 1;
                    break;
                }
                TFLAGS(x) = con_tank_flag_explosion + 0x03;
                R(ram_sfx_explosion_player) = 1;
                TUPGRADE(x) = 0;
                TTYPE(x) = 0;
                if (IsRoyale()) ++royaleKills_[shooterTeam & 7];
                break;
            }
            if (TTYPE(x) & 0x04) {                       // el tanque con item lo suelta al ser golpeado
                Sub_E8BE_spawn_bonus();
                TTYPE(x) = static_cast<u8>(TTYPE(x) & 0xFB);
            }
            const int dmg = lvl == 3 ? 2 : 1;
            if (wd_.hp[x] > dmg) {
                wd_.hp[x] = static_cast<u8>(wd_.hp[x] - dmg);
                if (((TTYPE(x) >> 5) & 3) == 3) TTYPE(x) = static_cast<u8>((TTYPE(x) & 0xFC) | (wd_.hp[x] - 1 < 3 ? wd_.hp[x] - 1 : 3));
                R(ram_sfx_bullet_hit_tank) = 1;
            } else {
                TFLAGS(x) = con_tank_flag_explosion + 0x03;
                R(ram_sfx_explosion_enemy) = 1;
                TTYPE(x) = 0;
                if (IsRoyale()) ++royaleKills_[shooterTeam & 7];
            }
            break;
        }
    }
}

// Pierde el equipo cuyo aguila cae o al que ya no le quedan jugadores con vidas.
void Battle::VersusCheckEnd() {
    if (vsWinner_ >= 0) return;
    if (IsRoyale()) {
        if (spec_.royaleTimed) {                                // por tiempo: gana el que mas destruyo
            if (royaleFrames_ > 0) --royaleFrames_;
            if (royaleFrames_ == 0) {
                int best = -1, bestKills = -1;
                bool tie = false;
                for (int id = 0; id < 8; ++id) {
                    if (royaleKills_[id] > bestKills) { bestKills = royaleKills_[id]; best = id; tie = false; }
                    else if (royaleKills_[id] == bestKills) tie = true;
                }
                vsWinner_ = tie ? 2 : 10 + best;
                return;
            }
        } else {                                                // a muerte: queda uno solo
            int alive = 0, last = -1;
            for (int id = 0; id < 8; ++id) if (RoyaleAlive(id)) { ++alive; last = id; }
            if (alive <= 1) { vsWinner_ = alive == 1 ? 10 + last : 2; return; }
        }
        RoyaleItemsStep();
        return;
    }
    bool lost[2] = {!vsEagleAlive_[0], !vsEagleAlive_[1]};
    for (int t = 0; t < 2; ++t) {
        if (lost[t]) continue;
        bool any = false;
        for (int p = 0; p < nPlayers_; ++p) {
            if (playerTeam_[p] == t && present_[p] && (LIVES(p) != 0 || TFLAGS(p) != 0)) any = true;
        }
        if (!any) lost[t] = true;
    }
    if (lost[0] && lost[1]) vsWinner_ = 2;
    else if (lost[0]) vsWinner_ = 1;
    else if (lost[1]) vsWinner_ = 0;
}

// ---------------------------------------------------------------------------
// Lava
// ---------------------------------------------------------------------------

// El bloque (col, row) -- 2x2 mosaicos -- tiene el mosaico `tile` (all: en los cuatro; si no, en alguno).
bool Battle::BlockTilesAre(int col, int row, u8 tile, bool all) const {
    if (col < 0 || col >= kWideBlocksX || row < 0 || row >= kWideBlocksY + 1) return false;
    const int p = Sub_D5FB_calculate_pointer(2 * col, 2 * row);
    const int st = Stride();
    const int idx[4] = {p, p + 1, p + st, p + st + 1};
    int hits = 0;
    for (int i : idx) hits += (M(i) & 0x7F) == tile ? 1 : 0;
    return all ? hits == 4 : hits > 0;
}

bool Battle::BlockEmptyAt(int col, int row) const { return BlockTilesAre(col, row, 0, true); }

void Battle::SetBlockTiles(int col, int row, u8 tile) {
    const int p = Sub_D5FB_calculate_pointer(2 * col, 2 * row);
    const int st = Stride();
    Sub_D784_write_tile(static_cast<uint16_t>(p), tile);
    Sub_D784_write_tile(static_cast<uint16_t>(p + 1), tile);
    Sub_D784_write_tile(static_cast<uint16_t>(p + st), tile);
    Sub_D784_write_tile(static_cast<uint16_t>(p + st + 1), tile);
}

int Battle::TileAt(int tx, int ty) const {
    if (tx < 0 || ty < 0 || tx >= MapCols() || ty >= 34) return 0;
    return M(Sub_D5FB_calculate_pointer(tx, ty)) & 0x7F;
}

bool Battle::RockAt(int tx, int ty) const {
    if (!wide_ || tx < 0 || ty < 0 || tx >= MapCols() || ty >= 34) return false;
    const int p = Sub_D5FB_calculate_pointer(tx, ty);
    return p < static_cast<int>(sizeof(rock_)) && rock_[p] != 0 && M(p) == con_block_type + 0x10;
}

bool Battle::RockBlockAt(int col, int row) const { return RockAt(2 * col, 2 * row) && RockAt(2 * col + 1, 2 * row + 1); }

// Algun tanque vivo pisa alguno de los mosaicos del bloque.
bool Battle::BlockOccupied(int col, int row) {
    const int st = Stride();
    const int q = Sub_D5FB_calculate_pointer(2 * col, 2 * row);
    const int blk[4] = {q, q + 1, q + st, q + st + 1};
    for (int x = 0; x <= lastTank_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) continue;
        const int p = PointerPx(WX(TPOSX(x) - 8), U8(TPOSY(x) - 8));
        const int tk[4] = {p, p + 1, p + st, p + st + 1};
        for (int a : tk) for (int b : blk) if (a == b) return true;
    }
    return false;
}

void Battle::SetRockBlock(int col, int row) {
    SetBlockTiles(col, row, con_block_type + 0x10);        // para el motor es hierro: cuatro mosaicos...
    const int p = Sub_D5FB_calculate_pointer(2 * col, 2 * row);
    const int st = Stride();
    rock_[p] = rock_[p + 1] = rock_[p + st] = rock_[p + st + 1] = 1;   // ...pero con 3 de salud cada uno
}

int Battle::LavaFlowCells() const {
    int n = 0;
    for (int i = 0; i < nLava_; ++i) n += lava_[i].n;
    return n;
}

bool Battle::LavaAtBlock(int col, int row) const { return BlockTilesAre(col, row, kLavaTile, true); }

// Donde la lava no se esparce: alrededor de las aguilas.
bool Battle::LavaForbidden(int col, int row) const {
    if (row < 1 || row > kWideBlocksY || col < 0 || col >= kWideBlocksX) return true;
    if (IsRoyale()) {
        return false;
    } else if (IsVersus()) {
        for (int t = 0; t < 2; ++t) if (std::abs(col - spec_.vsEagleCol[t]) <= 1 && std::abs(row - spec_.vsEagleRow[t]) <= 1) return true;
    } else if (IsBossLevel()) {
        if (std::abs(col - EagleCol()) <= 2 && std::abs(row - EagleRow()) <= 2) return true;
    } else if (row >= BaseRow() - 1 && std::abs(col - BaseCol()) <= 3) {
        return true;                                                 // la base de los jugadores
    }
    return false;
}

void Battle::LavaHit(int x) {
    if (x < nPlayers_) {
        if (HELMET(x) != 0) {                                        // el casco protege (con su salud, si la tiene)
            if (shieldHp_[x] > 0) ShieldHit(x, 1);
            return;
        }
        ++playerHits_[x];
        if (TUPGRADE(x) != 0) {                                      // como un impacto: pierde un nivel de arma; a nivel 0 lo destruye
            TUPGRADE(x) = static_cast<u8>(TUPGRADE(x) > 0x20 ? TUPGRADE(x) - 0x20 : 0);
            TTYPE(x) = TUPGRADE(x);
            R(ram_sfx_bullet_hit_tank) = 1;
        } else {
            TFLAGS(x) = con_tank_flag_explosion + 0x03;
            R(ram_sfx_explosion_player) = 1;
            TUPGRADE(x) = 0;
            TTYPE(x) = 0;
        }
        return;
    }
    if (wd_.hp[x] > 1) {                                             // enemigos y aliados: un punto de salud
        wd_.hp[x] = static_cast<u8>(wd_.hp[x] - 1);
        if (!IsAlly(x) && ((TTYPE(x) >> 5) & 3) == 3) TTYPE(x) = static_cast<u8>((TTYPE(x) & 0xFC) | (wd_.hp[x] - 1 < 3 ? wd_.hp[x] - 1 : 3));
        R(ram_sfx_bullet_hit_tank) = 1;
    } else {
        TFLAGS(x) = con_tank_flag_explosion + 0x03;
        R(ram_sfx_explosion_enemy) = 1;
        TTYPE(x) = 0;
    }
}

void Battle::Sub_Lava() {
    if (!wide_) return;
    static const int kDir[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    // quema los arbustos que toca: los bloques de arbusto vecinos de la lava desaparecen
    auto burn = [&](int col, int row) {
        for (const auto& d : kDir) {
            const int nc = col + d[0], nr = row + d[1];
            if (nc < 0 || nc >= kWideBlocksX || nr < 0 || nr > kWideBlocksY) continue;
            if (!BlockTilesAre(nc, nr, 0x22, false)) continue;
            const int p = Sub_D5FB_calculate_pointer(2 * nc, 2 * nr);
            const int st = Stride();
            const int idx[4] = {p, p + 1, p + st, p + st + 1};
            for (int i : idx) if ((M(i) & 0x7F) == 0x22) Sub_D784_write_tile(static_cast<uint16_t>(i), 0);
        }
    };
    for (int i = 0; i < nLava_; ++i) {
        LavaSource& s = lava_[i];
        if (!BlockTilesAre(s.col, s.row, kLavaTile, false)) {          // la fuente ya no esta (una bomba la borro)
            for (int k = 0; k < s.n; ++k) if (BlockTilesAre(s.flow[k].col, s.flow[k].row, kLavaTile, false)) SetBlockTiles(s.flow[k].col, s.flow[k].row, 0);
            s.n = 0;
            s.phase = -1;
            continue;
        }
        // las celdas esparcidas se enfrian al cabo de 15 segundos
        for (int k = 0; k < s.n;) {
            LavaCell& c = s.flow[k];
            if (!BlockTilesAre(c.col, c.row, kLavaTile, false)) {
                s.flow[k] = s.flow[--s.n];
            } else if (--c.timer <= 0) {
                if (BlockOccupied(c.col, c.row)) { c.timer = 1; ++k; continue; }   // un tanque encima: espera a que salga
                SetRockBlock(c.col, c.row);                                // al secarse queda piedra
                s.flow[k] = s.flow[--s.n];
            } else {
                ++k;
            }
        }
        burn(s.col, s.row);
        for (int k = 0; k < s.n; ++k) burn(s.flow[k].col, s.flow[k].row);
        if (s.phase == 0) {                                            // descanso
            if (--s.timer <= 0) { s.phase = 1; s.timer = kLavaSpreadFrames; s.placed = 0; }
        } else if (s.phase == 1) {                                     // esparciendo: una celda nueva cada tanto, en un bloque vacio al azar
            if (--s.timer <= 0) {
                int cand[4 * (kLavaMaxFlow + 1)][2];
                int nc = 0;
                auto consider = [&](int col, int row) {
                    for (const auto& d : kDir) {
                        const int c2 = col + d[0], r2 = row + d[1];
                        if (LavaForbidden(c2, r2) || !BlockEmptyAt(c2, r2)) continue;
                        bool dup = false;
                        for (int q = 0; q < nc; ++q) dup = dup || (cand[q][0] == c2 && cand[q][1] == r2);
                        if (!dup && nc < 4 * (kLavaMaxFlow + 1)) { cand[nc][0] = c2; cand[nc][1] = r2; ++nc; }
                    }
                };
                consider(s.col, s.row);
                for (int k = 0; k < s.n; ++k) consider(s.flow[k].col, s.flow[k].row);
                if (nc == 0) {                                         // sin salida: se vuelve a mirar en un rato (puede que se abra una)
                    s.timer = 60;
                } else {
                    const int pick = Sub_D44D_generate_random_number() % nc;
                    SetBlockTiles(cand[pick][0], cand[pick][1], kLavaTile);
                    s.flow[s.n].col = cand[pick][0];
                    s.flow[s.n].row = cand[pick][1];
                    s.flow[s.n].timer = kLavaFlowLife;
                    ++s.n;
                    ++s.placed;
                    s.timer = kLavaSpreadFrames;
                    if (s.placed >= kLavaMaxFlow) s.phase = 2;
                }
            }
        } else if (s.phase == 2) {                                     // espera a que se enfrie toda
            if (s.n == 0 && !BlockOccupied(s.col, s.row)) { SetRockBlock(s.col, s.row); s.phase = -1; }   // se enfrio todo: tambien el punto inicial se vuelve piedra
        }
    }
    // dano: quien pisa lava recibe 1, y 1 mas por cada segundo que siga encima
    for (int x = 0; x <= lastTank_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) { lavaTick_[x] = 0; continue; }
        const int st = Stride();
        const uint16_t p = PointerPx(WX(TPOSX(x) - 8), U8(TPOSY(x) - 8));
        const int idx[4] = {p, p + 1, p + st, p + st + 1};
        bool on = false;
        for (int i : idx) on = on || (M(i) & 0x7F) == kLavaTile;
        if (!on) { lavaTick_[x] = 0; continue; }
        if (lavaTick_[x] > 0) { --lavaTick_[x]; continue; }
        lavaTick_[x] = kLavaDamageFrames - 1;
        LavaHit(x);
    }
}

// ---------------------------------------------------------------------------
// Items nuevos: invisible, helicoptero aliado, minas terrestres, portal y velocidad
// ---------------------------------------------------------------------------

// El tanque `tank` es un enemigo de lo que puso `owner` (un jugador, o -1 = los enemigos).
bool Battle::ExtraHostile(int owner, int tank) {
    if (tank < 0 || tank > lastTank_) return false;
    const u8 f = TFLAGS(tank);
    if (!(f & 0x80) || f >= 0xE0) return false;
    if (owner < 0) return tank < nPlayers_ ? ghostT_[tank] == 0 : IsAlly(tank);        // los enemigos atacan a los jugadores (que no sean invisibles) y a sus aliados
    if (IsVersus()) return TeamOf(tank) != TeamOf(owner) && !(tank < nPlayers_ && ghostT_[tank] > 0);
    return tank >= nPlayers_ && tank <= lastEnemy_;                                    // los jugadores atacan a los enemigos
}

// Destruye un tanque con un item: el casco aguanta una vez; el que destruye (si es un jugador) se lleva los puntos.
void Battle::ExtraKill(int x, int owner) {
    const u8 f = TFLAGS(x);
    if (!(f & 0x80) || f >= 0xE0) return;
    if (x < nPlayers_) {
        if (HELMET(x) != 0) { ShieldHit(x, 3); R(ram_sfx_bullet_hit_tank) = 1; return; }
        ++playerHits_[x];
        TFLAGS(x) = con_tank_flag_explosion + 0x03;
        TTYPE(x) = 0;
        TUPGRADE(x) = 0;
        R(ram_sfx_explosion_player) = 1;
        if (IsRoyale() && owner >= 0 && owner < 8) ++royaleKills_[owner];
        return;
    }
    TFLAGS(x) = con_tank_flag_explosion + 0x03;
    R(ram_sfx_explosion_enemy) = 1;
    if (IsAlly(x)) { TTYPE(x) = 0; return; }
    const int kind = ((TTYPE(x) >> 5) - 4) & 3;
    TTYPE(x) = 0;
    if (owner >= 0 && owner < nPlayers_ && !IsVersus()) {
        KILLCNT(owner * 4 + kind)++;
        if (R(ram_2nd_loop_flag) != con_flag_demo) {
            Sub_D9E1_calculate_decimal_number(tbl_E8BA_points_for_killing_enemy[kind]);
            Sub_D9BE_add_score(owner);
            Sub_D138_gain_extra_life_for_20000_pts();
        }
    }
}

void Battle::AddBoom(int x, int y) {
    int slot = 0;
    for (int i = 0; i < kMaxBooms; ++i) {
        if (booms_[i].t <= 0) { slot = i; break; }
        if (booms_[i].t < booms_[slot].t) slot = i;
    }
    booms_[slot] = Boom{x, y, 16};
}

// Un bloque donde se puede dejar una mina o una puerta: vacio, lejos de las bases, sin tanques y sin otra cosa encima.
bool Battle::FreeBlockForExtra(int col, int row) {
    if (col < 1 || col > kWideBlocksX - 2 || row < 1 || row > kWideBlocksY - 1) return false;
    if (!BlockEmptyAt(col, row) || LavaForbidden(col, row) || BlockOccupied(col, row)) return false;
    for (const Mine& m : mines_) if (m.on && m.col == col && m.row == row) return false;
    if (portal_.on) for (int i = 0; i < 2; ++i) if (portal_.col[i] == col && portal_.row[i] == row) return false;
    return true;
}

void Battle::SpawnHeli(int owner) {
    if (!wide_) return;
    for (Heli& h : heli_) {
        if (h.on) continue;
        h = Heli{};
        h.on = true;
        h.owner = owner;
        const bool left = (Sub_D44D_generate_random_number() & 1) != 0;
        h.x = left ? -24 : MapCols() * 8 + 24;                     // entra volando por un costado
        h.y = 24 + Sub_D44D_generate_random_number() % 192;
        h.dir = left ? 2 : 6;                                      // mira hacia donde entra
        h.life = 1200;                                             // 20 segundos y se va
        R(ram_sfx_bonus_appear) = 1;
        return;
    }
}

void Battle::PlaceMines(int owner, int count) {
    if (!wide_) return;
    int active = 0;
    for (const Mine& m : mines_) active += m.on ? 1 : 0;
    for (int excess = active + count - kMaxMines; excess > 0; --excess) {      // con 9, se sacan las mas viejas
        int old = -1;
        for (int i = 0; i < kMaxMines; ++i) if (mines_[i].on && (old < 0 || mines_[i].seq < mines_[old].seq)) old = i;
        if (old >= 0) mines_[old].on = false;
    }
    for (int k = 0; k < count; ++k) {
        for (int tries = 0; tries < 60; ++tries) {
            const int c = 1 + Sub_D44D_generate_random_number() % (kWideBlocksX - 2), r = 1 + Sub_D44D_generate_random_number() % (kWideBlocksY - 1);
            if (!FreeBlockForExtra(c, r)) continue;
            for (Mine& m : mines_) {
                if (m.on) continue;
                m = Mine{true, owner, c, r, 0, ++mineSeq_};
                break;
            }
            break;
        }
    }
}

void Battle::MakePortal() {
    if (!wide_) return;
    portal_.on = false;
    int cA = -1, rA = -1, cB = -1, rB = -1;
    for (int tries = 0; tries < 80 && rA < 0; ++tries) {                       // una de la mitad de abajo
        const int c = 1 + Sub_D44D_generate_random_number() % (kWideBlocksX - 2), r = 8 + Sub_D44D_generate_random_number() % 6;
        if (FreeBlockForExtra(c, r)) { cA = c; rA = r; }
    }
    for (int tries = 0; tries < 80 && rB < 0; ++tries) {                       // y otra de la mitad de arriba
        const int c = 1 + Sub_D44D_generate_random_number() % (kWideBlocksX - 2), r = 1 + Sub_D44D_generate_random_number() % 6;
        if (FreeBlockForExtra(c, r) && !(c == cA && r == rA)) { cB = c; rB = r; }
    }
    if (rA < 0 || rB < 0) return;
    portal_.on = true;
    portal_.col[0] = cA; portal_.row[0] = rA;
    portal_.col[1] = cB; portal_.row[1] = rB;
    portal_.uses = 3;
}

int Battle::ExtraIcons(ExtraIcon* out, int max) const {
    int n = 0;
    if ((R(ram_frm_cnt_lo) & 0x08) == 0) return 0;                  // parpadean como los demas items
    if (BonusActive() && R(ram_0062_bonus_timer) == 0 && (R(ram_bonus_id) & 0x80) == 0 && R(ram_bonus_id) >= 8 && n < max)
        out[n++] = ExtraIcon{R(ram_bonus_pos_X) | (bonusXHi_ << 8), R(ram_bonus_pos_Y), R(ram_bonus_id)};
    for (const RoyaleItem& it : rItems_) if (it.on && it.id >= 8 && n < max) out[n++] = ExtraIcon{it.x, it.y, it.id};
    return n;
}

// ---------------------------------------------------------------------------
// Canones
// ---------------------------------------------------------------------------

// Cubre el aguila de ladrillos y pone un canon pegado a cada lado del aguila (en lugar de los ladrillos laterales del muro; asi
// no tapan los lugares donde aparecen los jugadores). Si ya habia canones, los repara.
void Battle::SpawnCannons() {
    if (!wide_ || IsVersus()) return;
    if (R(ram_game_over_flag) & 0x80) {
        R(ram_shovel_timer) = 0;
        Sub_CAF5_draw_default_base();                    // los ladrillos de siempre, enteros
    }
    const int kCol[kMaxCannons] = {BaseCol() - 1, BaseCol() + 1};
    for (int i = 0; i < kMaxCannons; ++i) {
        Cannon& cn = cannons_[i];
        cn = Cannon{true, kCol[i], BaseRow(), i == 0 ? -1 : 1, kCannonHp, 0, 0};
        const int x0 = cn.col * 16, y0 = cn.row * 16;
        for (int a = 0; a < 2; ++a)
            for (int b = 0; b < 2; ++b) Sub_D784_write_tile(PointerPx(x0 + a * 8, y0 + b * 8), con_block_type + 0x10);
        AddBoom(x0 + 8, y0 + 8);
    }
}

void Battle::CannonKill(int i) {
    Cannon& cn = cannons_[i];
    const int x0 = cn.col * 16, y0 = cn.row * 16;
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b) Sub_D784_write_tile(PointerPx(x0 + a * 8, y0 + b * 8), 0);
    AddBoom(x0 + 8, y0 + 8);
    R(ram_sfx_explosion_enemy) = 1;
    cn.on = false;
}

bool Battle::CannonHit(int px, int py, int bi) {
    for (int i = 0; i < kMaxCannons; ++i) {
        Cannon& cn = cannons_[i];
        if (!cn.on || px < cn.col * 16 || px >= cn.col * 16 + 16 || py < cn.row * 16 || py >= cn.row * 16 + 16) continue;
        const bool live = (BSTATUS(bi) & 0xF0) == 0x40;                   // (la sonda puede pasar dos veces por la misma bala)
        BSTATUS(bi) = 0x33;
        if (live && BulletOwner(bi) < 0) {                       // solo danan los disparos enemigos (las balas de los jugadores se apagan)
            cn.flash = 8;
            if (--cn.hp <= 0) CannonKill(i);
            else R(ram_sfx_bullet_hit_tank) = 1;
        }
        return true;
    }
    return false;
}

// Un disparo de canon le pega a un enemigo: como una bala comun (casco, item que suelta, salud).
void Battle::CannonDamageEnemy(int e) {
    ++cannonHits_;
    if (shieldHp_[e] > 0) { ShieldHit(e, 1); R(ram_sfx_bullet_hit_tank) = 1; return; }
    if (TTYPE(e) & 0x04) {
        Sub_E8BE_spawn_bonus();
        TTYPE(e) = static_cast<u8>(TTYPE(e) & 0xFB);
    }
    if (wd_.hp[e] > 1) {
        wd_.hp[e] = static_cast<u8>(wd_.hp[e] - 1);
        if (((TTYPE(e) >> 5) & 3) == 3) TTYPE(e) = static_cast<u8>((TTYPE(e) & 0xFC) | (wd_.hp[e] - 1 < 3 ? wd_.hp[e] - 1 : 3));
        R(ram_sfx_bullet_hit_tank) = 1;
    } else {
        TFLAGS(e) = con_tank_flag_explosion + 0x03;
        R(ram_sfx_explosion_enemy) = 1;
        TTYPE(e) = 0;
    }
}

void Battle::CannonsStep() {
    for (Cannon& cn : cannons_) {
        if (!cn.on) continue;
        if (cn.flash > 0) --cn.flash;
        if (cn.cd > 0) { --cn.cd; continue; }
        const int cx = cn.col * 16 + 8, cy = cn.row * 16 + 8;
        int best = 1 << 20, bdx = 0, bdy = 0;
        for (int e = nPlayers_; e <= lastEnemy_; ++e) {                 // el enemigo mas cercano en la linea hacia afuera (solo a los costados)
            const u8 f = TFLAGS(e);
            if (!(f & 0x80) || f >= 0xE0 || IsAlly(e)) continue;
            const int dx = TankX(e) - cx, dy = TankY(e) - cy;
            if (dx * cn.side > 0 && dx * cn.side <= 240 && std::abs(dy) <= 48 && dx * cn.side < best) { best = dx * cn.side; bdx = cn.side; bdy = 0; }
        }
        if (best == (1 << 20)) continue;
        for (CannonShot& sh : cshots_) {
            if (sh.on) continue;
            sh = CannonShot{true, cx + bdx * 10, cy + bdy * 10, bdx, bdy};
            cn.cd = kCannonReload;
            R(ram_sfx_shot) = 1;
            break;
        }
    }
    for (CannonShot& sh : cshots_) {
        if (!sh.on) continue;
        for (int k = 0; k < 4 && sh.on; ++k) {                          // 4 pixeles por cuadro, de a uno
            sh.x += sh.dx;
            sh.y += sh.dy;
            if (sh.x < 0 || sh.x >= MapCols() * 8 || sh.y < 8 || sh.y >= 240) { sh.on = false; break; }
            if (sh.x >= 192 + BaseDX() && sh.x < 240 + BaseDX() && sh.y >= 208 + BaseDY() && sh.y < 240 + BaseDY()) continue;         // pasa sobre los ladrillos de la base sin romperlos (el tiro hacia arriba sale por encima del muro)
            const uint16_t p = PointerPx(sh.x, sh.y);
            const u8 t = M(p) & 0x7F;
            if ((t & 0xFC) == 0xC8) { sh.on = false; break; }            // el aguila: nunca la toca
            if (t == 0x10) { sh.on = false; break; }                     // acero, piedra o canon
            if (t >= 1 && t <= 0x0F) {
                const u8 mask = Sub_D725_tile_mask(sh.x, sh.y);
                if (t & mask) {
                    Sub_D784_write_tile(p, static_cast<u8>(M(p) & ~mask));
                    R(ram_sfx_bullet_hit_brick) = 1;
                    sh.on = false;
                    break;
                }
            }
            for (int e = nPlayers_; e <= lastEnemy_; ++e) {
                const u8 f = TFLAGS(e);
                if (!(f & 0x80) || f >= 0xE0 || IsAlly(e)) continue;
                if (std::abs(TankX(e) - sh.x) >= 10 || std::abs(TankY(e) - sh.y) >= 10) continue;
                CannonDamageEnemy(e);
                sh.on = false;
                break;
            }
        }
    }
}

void Battle::GhostClearBricks(int x) {
    const int px0 = WX(TPOSX(x) - 8), py0 = U8(TPOSY(x) - 8);
    const int offs[3] = {0, 8, 15};
    for (int a : offs) {
        for (int b : offs) {
            const uint16_t p = PointerPx(WX(px0 + a), U8(py0 + b));
            const u8 t = M(p) & 0x7F;
            if (t >= 1 && t <= 0x0F) Sub_D784_write_tile(p, static_cast<u8>(M(p) & 0x80));
        }
    }
}

void Battle::Sub_Extras() {
    if (!wide_) return;
    for (int x = 0; x <= lastTank_ && x < kMaxTanks; ++x) {
        if (ghostT_[x] > 0 && --ghostT_[x] == 0 && x < nPlayers_) GhostClearBricks(x);
        if (speedT_[x] > 0) --speedT_[x];
        if (portalCd_[x] > 0) --portalCd_[x];
    }
    if (enemyGhostT_ > 0) --enemyGhostT_;
    if (!IsVersus()) CannonsStep();
    for (Boom& b : booms_) if (b.t > 0) --b.t;
    // helicopteros: vuelan sobre todo, van tras el enemigo mas cercano (despacio, para que se pueda escapar), se posan y sueltan una bomba
    const int worldW = MapCols() * 8;
    auto faceTo = [](Heli& h, int dx, int dy) {                                  // hacia donde mira: 8 direcciones
        const int ax = std::abs(dx), ay = std::abs(dy);
        if (ax < 2 && ay < 2) return;
        int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0), sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
        if (ay * 5 < ax * 2) sy = 0;                                             // casi horizontal
        else if (ax * 5 < ay * 2) sx = 0;                                        // casi vertical
        static const int kDir[3][3] = {{7, 0, 1}, {6, 0, 2}, {5, 4, 3}};         // [sy + 1][sx + 1]
        h.dir = kDir[sy + 1][sx + 1];
    };
    for (Heli& h : heli_) {
        if (!h.on) continue;
        ++h.frame;
        if (h.state != 3 && --h.life <= 0) h.state = 3;                          // se acabo el tiempo: se va
        if (h.state == 3) {
            const bool left = h.x < worldW / 2;
            h.dir = left ? 6 : 2;
            h.x += left ? -1 : 1;
            if (h.x < -40 || h.x > worldW + 40) h.on = false;
            continue;
        }
        if (h.state == 2) { if (--h.timer <= 0) { h.state = 0; h.target = -1; } continue; }
        if (h.state == 1) {                                                      // posado: se queda quieto hasta soltar la bomba (el tanque puede irse)
            if (--h.timer <= 0) {
                AddBoom(h.x, h.y);
                R(ram_sfx_explosion_hq) = 1;
                for (int e = 0; e <= lastTank_; ++e) {
                    if (!ExtraHostile(h.owner, e)) continue;
                    if (std::abs(TankX(e) - h.x) < 22 && std::abs(TankY(e) - h.y) < 22) ExtraKill(e, h.owner);
                }
                h.state = 2;
                h.timer = 24;
            }
            continue;
        }
        if (h.target >= 0 && !ExtraHostile(h.owner, h.target)) h.target = -1;
        if (h.target < 0) {
            int best = -1, bd = 1 << 20;
            for (int e = 0; e <= lastTank_; ++e) {
                if (!ExtraHostile(h.owner, e)) continue;
                const int d = std::abs(TankX(e) - h.x) + std::abs(TankY(e) - h.y);
                if (d < bd) { bd = d; best = e; }
            }
            h.target = best;
        }
        if (h.target < 0) continue;                                              // nadie a quien atacar: sobrevuela
        const int dx = TankX(h.target) - h.x, dy = TankY(h.target) - h.y;
        faceTo(h, dx, dy);
        const int dist = std::abs(dx) + std::abs(dy);
        h.acc += dist > 120 ? 16 : (dist > 50 ? 10 : 5);                         // lejos viene rapido (2 px por cuadro); cerca se acerca a unos 0,6 px por cuadro (el tanque camina a 0,75)
        const int move = h.acc >> 3;
        h.acc &= 7;
        h.x += std::max(-move, std::min(move, dx));
        h.y += std::max(-move, std::min(move, dy));
        if (std::abs(TankX(h.target) - h.x) <= 4 && std::abs(TankY(h.target) - h.y) <= 4) { h.state = 1; h.timer = 40; }
    }
    // minas: solo las pisan los enemigos de quien las puso
    for (Mine& m : mines_) {
        if (!m.on) continue;
        ++m.age;
        const int mx = m.col * 16 + 8, my = m.row * 16 + 8;
        for (int e = 0; e <= lastTank_; ++e) {
            if (!ExtraHostile(m.owner, e)) continue;
            if (std::abs(TankX(e) - mx) >= 10 || std::abs(TankY(e) - my) >= 10) continue;
            AddBoom(mx, my);
            R(ram_sfx_explosion_hq) = 1;
            ExtraKill(e, m.owner);
            m.on = false;
            break;
        }
    }
    // portal: el tanque que toca una puerta sale por la otra (3 usos)
    if (portal_.on) {
        for (int x = 0; x <= lastTank_ && portal_.on; ++x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0) continue;
            for (int side = 0; side < 2; ++side) {
                if (std::abs(TankX(x) - (portal_.col[side] * 16 + 8)) >= 8 || std::abs(TankY(x) - (portal_.row[side] * 16 + 8)) >= 8) continue;
                if (portalCd_[x] > 0) { portalCd_[x] = 2; break; }                 // sigue sobre una puerta: no vuelve a viajar hasta que salga
                const int o = 1 - side;
                TPOSX(x) = portal_.col[o] * 16 + 8;
                TPOSY(x) = portal_.row[o] * 16 + 8;
                portalCd_[x] = 90;
                R(ram_sfx_bonus_appear) = 1;
                if (--portal_.uses <= 0) portal_.on = false;
                break;
            }
        }
    }
}

// El item mas cercano: el principal (si esta en el campo) o el segundo (campania con 3 o 4 jugadores).
bool Battle::NearestItem(int x, int y, int& ix, int& iy) const {
    if (rules_.ai < 2) return false;                      // solo en dificil los enemigos buscan los items
    int best = 1 << 20;
    bool found = false;
    if (BonusActive() && R(ram_0062_bonus_timer) == 0 && (R(ram_bonus_id) & 0x80) == 0) {
        ix = R(ram_bonus_pos_X) | (bonusXHi_ << 8);
        iy = R(ram_bonus_pos_Y);
        best = std::abs(ix - x) + std::abs(iy - y);
        found = true;
    }
    if (!IsVersus()) {
        for (const RoyaleItem& it : rItems_) {
            if (!it.on) continue;
            const int d = std::abs(it.x - x) + std::abs(it.y - y);
            if (!found || d < best) { ix = it.x; iy = it.y; best = d; found = true; }
        }
    }
    return found;
}

// Aparecio un item: los enemigos de tipo 1 que estan en juego van tras el.
void Battle::EnemiesGoForItem() {
    if (!wide_ || IsVersus()) return;
    for (int x = nPlayers_; x <= lastEnemy_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0 || IsAlly(x) || ((TTYPE(x) >> 5) & 3) != 0) continue;
        Sub_E420_change_tank_status(x, con_tank_flag_follow_HQ);
    }
}

// El segundo item de la campania con 3 o 4 jugadores: lo agarra quien lo toque (jugadores o enemigos).
void Battle::CampaignItemsStep() {
    if (!wide_ || IsVersus()) return;
    for (RoyaleItem& it : rItems_) {
        if (!it.on) continue;
        bool taken = false;
        for (int p = nPlayers_ - 1; p >= 0 && !taken; --p) {
            const u8 f = TFLAGS(p);
            if (!(f & 0x80) || f >= 0xE0) continue;
            if (std::abs(TankX(p) - it.x) >= 12 || std::abs(TankY(p) - it.y) >= 12) continue;
            it.on = false;
            Sub_D9E1_calculate_decimal_number(0x50);                 // 500 puntos
            Sub_D9BE_add_score(p);
            Sub_D138_gain_extra_life_for_20000_pts();
            R(ram_sfx_bonus_pickup) = 1;
            ++itemPickups_[p];
            BonusHandler(it.id, p);
            taken = true;
        }
        for (int x = nPlayers_; x <= lastEnemy_ && rules_.ai >= 1 && !taken; ++x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0) continue;
            if (std::abs(TankX(x) - it.x) >= 12 || std::abs(TankY(x) - it.y) >= 12) continue;
            const int id = it.id;
            it.on = false;
            R(ram_sfx_bonus_pickup) = 1;
            EnemyBonus(id, x);
            taken = true;
        }
    }
}

// Cantidad de puntos de aparicion de enemigos y su posicion (centro del bloque, en pixeles).
int Battle::SpawnPointCount() const {
    if (spec_.active && spec_.nSpawns > 0) return spec_.nSpawns;
    return IsBossLevel() ? (diffPlayers_ >= 4 ? 7 : 5) : ((wide_ && diffPlayers_ >= 4) ? 5 : 3);      // los dos puntos de los costados, solo con 4 jugadores
}

void Battle::SpawnPointPx(int y, int& sx, int& sy) const {
    if (spec_.active && spec_.nSpawns > 0) {
        const int i = y < 0 ? 0 : (y >= spec_.nSpawns ? spec_.nSpawns - 1 : y);
        sx = spec_.spawnCol[i] * 16 + 8;
        sy = spec_.spawnRow[i] * 16 + 8;
        return;
    }
    static const int kWideSpawnX[5] = {0x08, 0x78 + 96, 0xD8 + 192 + 0x10, 0x08, 0xD8 + 192 + 0x10};
    // los de arriba salen en la franja de arriba del todo (y = 8) cuando esta abierta; si no, en la primera fila
    static const int kTop = kOpenTopStrip ? 0x08 : 0x18;
    static const int kWideSpawnY[5] = {kTop, kTop, kTop, 0x78, 0x78};
    // niveles del aguila: a los lados y enfrente de la fortaleza, justo afuera de sus capas
    const int gap = EagleSpawnGap();
    const int bossX[7] = {216 - gap, 216, 216 + gap, 0x08, 0xD8 + 192 + 0x10, 0x08, 0xD8 + 192 + 0x10};
    const int bossY[7] = {0x18, 0x18 + gap, 0x18, kTop, kTop, 0x78, 0x78};     // junto al aguila: a su altura; las puntas, arriba del todo
    sx = IsBossLevel() ? bossX[y] : kWideSpawnX[y];
    sy = IsBossLevel() ? bossY[y] : kWideSpawnY[y];
}

// sub_E3B8 (00:E3B8): termina la animacion de aparicion.
void Battle::Sub_E3B8_finish_respawn(int x) {
    if (IsVersus()) {
        int dir = TeamOf(x) == 0 ? 3 : 1;                // el equipo de la izquierda mira a la derecha y al reves
        if (IsRoyale()) {                                // battle royale: mirando al centro del mapa
            const int cx = TPOSX(x), cy = TPOSY(x);
            dir = cx < 13 * 16 ? 3 : (cx > 14 * 16 ? 1 : (cy > 7 * 16 ? 0 : 2));
        }
        if (x < nPlayers_) {
            TFLAGS(x) = static_cast<u8>(0xA0 | dir);
            HELMET(x) = 3;
            TTYPE(x) = static_cast<u8>(TUPGRADE(x) | TTYPE(x));
            TWHEELS(x) = 0;
        } else {
            TFLAGS(x) = static_cast<u8>(0xA0 | dir);
            SetEnemyKind(x, RandomEnemyKind());          // salud y tipo segun la dificultad
            TWHEELS(x) = 0;
        }
        return;
    }
    if (IsAlly(x)) {
        static const int kAllyHp[4] = {1, 2, 3, 4};     // tipo 1 muere de 1 tiro, tipo 2 de 2, tipo 3 de 3 y tipo 4 de 4
        const int kind = allyKind_[x];
        TFLAGS(x) = 0xA2;
        TTYPE(x) = static_cast<u8>(0x80 + (kind << 5));  // basico / rapido / power / blindado segun el nivel de arma del jugador
        TWHEELS(x) = 0;
        wd_.hp[x] = static_cast<u8>(kAllyHp[kind]);
        return;
    }
    TFLAGS(x) = wide_ ? (x < nPlayers_ ? 0xA0 : 0xA2) : tbl_E47E[x];
    u8 a;
    if (x < nPlayers_) {
        HELMET(x) = 3;
        a = TUPGRADE(x);
    } else if (extraEnemy_[x]) {                         // refuerzo de un item de aliados enemigo: no gasta la cuota de la etapa
        extraEnemy_[x] = false;
        a = static_cast<u8>(0x80 + (RandomEnemyKind() << 5));
    } else {
        if (debugForcedType_[x] >= 0) {                 // [prueba] fuerza el tipo pedido
            R(ram_enemy_type_offset) = static_cast<u8>(debugForcedType_[x]);
            R(ram_enemy_type_stage_cnt + debugForcedType_[x]) = 1;
            debugForcedType_[x] = -1;
        }
        if (IsBossLevel()) {
            // sin cuota: cuando la mezcla de tipos se agota se vuelve a llenar
            int tot = 0;
            for (int i = R(ram_enemy_type_offset); i < 4; ++i) tot += R(ram_enemy_type_stage_cnt + i);
            if (tot == 0) {
                Sub_E42B_prepare_enemy_tanks_for_stage();
                R(ram_enemy_type_offset) = 0;
            }
        }
        int y = R(ram_enemy_type_offset);
        while (R(ram_enemy_type_stage_cnt + y) == 0) {
            R(ram_enemy_type_offset)++;
            y = R(ram_enemy_type_offset);
        }
        R(ram_enemy_type_stage_cnt + y)--;
        const int stage = (R(ram_2nd_loop_flag) != 0) ? 0x23 : R(ram_stage);
        const int idx = U8(stage - 1) * 4 + R(ram_enemy_type_offset);
        a = wide_ ? static_cast<u8>(0x80 + (R(ram_enemy_type_offset) << 5))   // campo ancho: el tipo es el de la mezcla por escalon
                  : tbl_E4EC_stage_enemies[idx];
        if (a == 0xE0) {
            a |= 0x03;
        }
    }
    a |= TTYPE(x);
    if (!wide_ && a == 0xE7) {
        a = 0xE4;
    }
    TTYPE(x) = a;
    if (wide_ && x >= nPlayers_) {
        // salud: basico 1, rapido 1, power 2, blindado 4; +1 cada 2 escalones a power/blindado y cada 4 a los otros
        const int kind = (a >> 5) & 3;
        const int hp = EnemyHp(kind);
        wd_.hp[x] = static_cast<u8>(hp);
        // los 2 bits bajos muestran el dano del blindado (color); el bit 2 es "parpadea con item"
        TTYPE(x) = static_cast<u8>((a & 0xFC) | (kind == 3 ? (hp - 1 < 3 ? hp - 1 : 3) : 0));
    }
    TWHEELS(x) = 0;
    if (wide_ && !IsVersus() && x >= nPlayers_ && !IsAlly(x) && ((TTYPE(x) >> 5) & 3) == 0) {     // enemigo de tipo 1 que sale con un item en el campo: va por el
        int ix, iy;
        if (NearestItem(TankX(x), TankY(x), ix, iy)) TFLAGS(x) = con_tank_flag_follow_HQ | 0x02;
    }
}

// sub_DB48_enemy_spawn_handler (00:DB48)
void Battle::Sub_DB48_enemy_spawn_handler() {
    if (IsVersus()) {
        VersusSpawnHandler();
        return;
    }
    if (R(ram_enemy_timer_before_spawn) != 0) {
        R(ram_enemy_timer_before_spawn)--;
        return;
    }
    const bool boss = IsBossLevel();
    if (boss ? !enemyEagleAlive_ : R(ram_enemy_spawn_cnt) == 0) {
        return;
    }
    for (int x = R(ram_enemy_limit); x >= nPlayers_; --x) {
        if (TFLAGS(x) == 0) {
            R(ram_enemy_timer_before_spawn) = R(ram_enemy_spawn_interval);
            Sub_E363_tank_spawn_handler(x);
            if (!boss) R(ram_enemy_spawn_cnt)--;
            return;
        }
    }
}

// ===========================================================================
// Balas
// ===========================================================================

// sub_E02E_bullets_status_handler (00:E02E): mueve las balas y cuenta el impacto.
void Battle::Sub_E02E_bullets_status_handler() {
    for (int x = nBullets_ - 1; x >= 0; --x) {
        const u8 st = BSTATUS(x);
        switch (st >> 4) {
            case 0x4: {                                  // ofs_002_E051_40
                const int dir = st & 0x03;
                auto step = [&]() {
                    BPOSX(x) = WX(BPOSX(x) + S8(tbl_E46C[dir]) * 2);
                    BPOSY(x) = U8(BPOSY(x) + U8(tbl_E470[dir] << 1));
                };
                step();
                if (BPROP(x) & 0x01) {
                    step();
                }
                break;
            }
            case 0x1: case 0x2: case 0x3: {              // ofs_002_E076
                BSTATUS(x) = U8(BSTATUS(x) - 1);
                if ((BSTATUS(x) & 0x0F) == 0) {
                    u8 a = U8((BSTATUS(x) & 0xF0) - 0x10);
                    if (a != 0) {
                        a |= 0x03;
                    }
                    BSTATUS(x) = a;
                }
                break;
            }
            default:
                break;
        }
    }
}

// sub_E08C_bullets (00:E08C): dispara la bala del tanque x.
void Battle::Sub_E08C_fire_bullet(int x) {
    if (BSTATUS(x) != 0) {
        return;
    }
    if (x < nPlayers_) {
        R(ram_sfx_shot) = 1;
    }
    const int dir = TFLAGS(x) & 0x03;
    BSTATUS(x) = dir | 0x40;
    BPOSX(x) = WX(S8(tbl_E46C[dir]) * 8 + TPOSX(x));
    BPOSY(x) = U8(U8(tbl_E470[dir] << 3) + TPOSY(x));
    BPROP(x) = 0;
    const u8 t = TTYPE(x) & 0xF0;
    if (t != 0) {
        if (t == 0xC0) {
            BPROP(x) = 1;                                // tanque power: bala veloz
        } else if (t == 0x60) {
            BPROP(x) = 3;                                // 3 estrellas: veloz y rompe acero
        } else if ((t & 0x80) == 0) {
            BPROP(x) = 1;                                // 1 o 2 estrellas: veloz
        }
    }
    if (wide_) {
        // jugador: su nivel de arma; enemigo: tanque 1 (basico) a 4 (blindado). Nivel 3 rompe todo.
        bulletLevel_[x] = static_cast<u8>((TTYPE(x) >> 5) & 3);
        bulletPierced_[x] = 0;
        if (bulletLevel_[x] == 3) {
            BPROP(x) |= 0x02;
        }
    }
}

// sub_E122 (00:E122): disparo de los jugadores (hasta 2 balas desde 2 estrellas).
void Battle::Sub_E122_players_fire() {
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        if ((BTNPRESS(x) & con_btns_AB) == 0 || (wide_ && playerFreeze_[x] != 0)) {
            continue;                                    // sin disparo mientras el reloj enemigo los paraliza
        }
        if ((TTYPE(x) & 0xC0) == 0x40 && BSTATUS(x) != 0) {
            const int sb = SecondBullet(x);
            if (BSTATUS(sb) != 0) {
                continue;
            }
            BSTATUS(sb) = BSTATUS(x);                    // la primera pasa al 2do slot
            BPOSX(sb) = BPOSX(x);
            BPOSY(sb) = BPOSY(x);
            BPROP(sb) = BPROP(x);
            bulletLevel_[sb] = bulletLevel_[x];
            bulletPierced_[sb] = bulletPierced_[x];
            BSTATUS(x) = 0;
        }
        Sub_E08C_fire_bullet(x);
    }
}

// sub_E162 (00:E162): los enemigos disparan con probabilidad 1/32 por cuadro.
bool Battle::BossAggro(int x, int& nearest) {
    int bestDist = 1 << 20;
    bool aggro = false;
    nearest = -1;
    for (int p = 0; p < nPlayers_; ++p) {
        const u8 f = TFLAGS(p);
        if (!(f & 0x80) || f >= 0xE0 || ghostT_[p] > 0) continue;
        const int dTank = AbsDiffX(TPOSX(p), TPOSX(x)) + std::abs(static_cast<int>(TPOSY(p)) - static_cast<int>(TPOSY(x)));   // (AbsDiff8 da la vuelta a los 8 bits)
        const int dEagle = AbsDiffX(TPOSX(p), EagleCol() * 16 + 8) + std::abs(static_cast<int>(TPOSY(p)) - (EagleRow() * 16 + 8));
        if (dTank < bestDist) { bestDist = dTank; nearest = p; }
        if (dEagle < EagleGuardRadius() || dTank < 96) aggro = true;
    }
    return aggro;
}

// true si el tanque x apunta hacia la fortaleza de su aguila (bloques columnas 12-14, filas 0-1) a menos de 6 bloques.
bool Battle::AimsAtEnemyEagle(int x) {
    return AimsAtEagleAt(x, EagleCol(), EagleRow(), EagleLayers());
}

bool Battle::AimsAtEagleAt(int x, int col, int row, int n) {
    const int tx = TPOSX(x), ty = TPOSY(x);
    const int xl = col * 16 - 8 * n, xr = (col + 1) * 16 + 8 * n;      // la fortaleza: x en [xl, xr), y en [yt, yb)
    const int yt = row * 16 - 8 * n, yb = (row + 1) * 16 + 8 * n;
    const int yLow = yt - 8 > 8 ? yt - 8 : 8;
    switch (TFLAGS(x) & 0x03) {
        case 0: return tx >= xl - 8 && tx < xr + 8 && ty >= yb && ty - yb <= 96;     // hacia arriba
        case 1: return ty >= yLow && ty < yb + 8 && tx >= xr && tx - xr <= 96;       // hacia la izquierda
        case 2: return tx >= xl - 8 && tx < xr + 8 && ty < yt && yt - ty <= 96;      // hacia abajo
        case 3: return ty >= yLow && ty < yb + 8 && tx < xl && xl - tx <= 96;        // hacia la derecha
        default: return false;
    }
}

void Battle::Sub_E162_enemies_fire() {
    if (!IsVersus() && R(ram_clock_timer) != 0) {
        return;
    }
    for (int x = lastEnemy_; x >= nPlayers_; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        if (IsVersus()) {
            if (teamClock_[vsTeam_[x]] != 0) continue;                                    // congelado por el reloj
            const int t = vsTeam_[x];
            if (!IsRoyale() && AimsAtEagleAt(x, spec_.vsEagleCol[t], spec_.vsEagleRow[t], spec_.vsLayers)) continue;   // no le tira a su propia aguila
        }
        if (IsBossLevel() && enemyEagleAlive_ && AimsAtEnemyEagle(x)) continue;     // no le tiran a su propia aguila
        const u8 roll = Sub_D44D_generate_random_number();
        // 1/32 por cuadro en la ROM; en el campo ancho sube hasta 1/12 con el escalon de dificultad
        if (wide_ ? (roll % (32 - 20 * tier_ / 7)) == 0 : (roll & 0x1F) == 0) {
            Sub_E08C_fire_bullet(x);
        }
    }
}

// sub_E604_bullets_movement (00:E604): colision de las balas con el terreno
// (las 4 sondas: posicion, +4 perpendicular, -1 y -5 perpendicular).
void Battle::Sub_E604_bullets_movement() {
    for (int x = nBullets_ - 1; x >= 0; --x) {
        if ((BSTATUS(x) & 0xF0) != 0x40) {
            continue;
        }
        if (BPROP(x) == 0 && ((x ^ R(ram_frm_cnt_lo)) & 0x01) == 0) {
            continue;                                    // la bala lenta revisa un cuadro si, uno no
        }
        const int dir = BSTATUS(x) & 0x03;
        auto absByte = [](u8 v) -> int { return (v & 0x80) ? U8(~v + 1) : v; };
        const int t54 = absByte(tbl_EA49[dir]);
        const int t64 = t54 * 4;
        const int t55 = absByte(tbl_EA4D[dir]);
        const int t65 = t55 * 4;

        if (Sub_E693_bullet_probe(BPOSX(x), BPOSY(x), x) != 0) {
            Sub_E693_bullet_probe(WX(BPOSX(x) + t65), U8(BPOSY(x) + t64), x);
        }
        if (Sub_E693_bullet_probe(WX(BPOSX(x) - t55), U8(BPOSY(x) - t54), x) != 0) {
            Sub_E693_bullet_probe(WX(BPOSX(x) - t65 - t55), U8(BPOSY(x) - t64 - t54), x);
        }
    }
}

// sub_E693 / sub_E69A (00:E693): una sonda de la bala contra el mosaico.
// Devuelve 1 si rompio un cuarto de ladrillo (habilita la segunda sonda).
int Battle::Sub_E693_bullet_probe(int px, int py, int bi) {
    if (wide_ && !IsVersus() && CannonHit(px, py, bi)) return 0;
    const uint16_t p = PointerPx(px, py);
    const u8 mask = Sub_D725_tile_mask(px, py);
    const u8 tile = M(p);
    if ((tile & (mask | 0xF0)) == 0) {
        return 0;                                        // nada en ese cuarto
    }
    if (IsVersus() && (tile & 0xFC) == 0xC8) {
        const int e = VsEagleAt(px, py);
        if (e >= 0 && vsEagleAlive_[e] && e != TeamOf(BulletShooter(bi))) DestroyVsEagle(e);   // solo la destruye el otro equipo
        BSTATUS(bi) = 0x33;                              // la bala del propio equipo se apaga contra su aguila
        return 0;
    }
    if (IsBossLevel() && enemyEagleAlive_ && BulletOwner(bi) < 0 &&
        px >= EagleCol() * 16 - 8 * EagleLayers() && px < (EagleCol() + 1) * 16 + 8 * EagleLayers() &&
        py >= EagleRow() * 16 - 8 * EagleLayers() && py < (EagleRow() + 1) * 16 + 8 * EagleLayers()) {
        BSTATUS(bi) = 0x33;                              // las balas enemigas no rompen la fortaleza de su aguila
        return 0;
    }
    if ((tile & 0xFC) == 0xC8 && IsBossLevel() && px >= EagleCol() * 16 && px < (EagleCol() + 1) * 16 &&
        py >= EagleRow() * 16 && py < (EagleRow() + 1) * 16) {   // el aguila enemiga
        if (enemyEagleAlive_ && BulletOwner(bi) >= 0) {  // solo la danan las balas de los jugadores y sus aliados
            const int lvl = bulletLevel_[bi];
            const int dmg = (lvl + 1) * (lvl >= 3 ? 2 : 1);   // como a los tanques: el nivel 3 hace doble dano
            enemyEagleHp_ -= dmg;
            if (enemyEagleHp_ <= 0) {
                DestroyEnemyEagle(BulletOwner(bi));
            } else {
                R(ram_sfx_bullet_hit_wall) = 1;
            }
        }
        BSTATUS(bi) = 0x33;
        return 0;
    }
    if ((tile & 0xFC) == 0xC8 && R(ram_game_over_flag) != 0 && IsAllyBullet(bi)) {
        BSTATUS(bi) = 0x33;                              // la bala de un aliado se apaga contra el aguila de los jugadores: no le hace nada
        return 0;
    }
    if ((tile & 0xFC) == 0xC8 && R(ram_game_over_flag) != 0) {   // el aguila
        R(ram_game_over_flag) = 0x27;
        ++eaglesDestroyed_;
        R(ram_sfx_explosion_hq) = 1;
        R(ram_sfx_explosion_player) = 1;
        Sub_CC08_draw_destroyed_eagle();
        BSTATUS(bi) = 0x33;
        return 0;
    }
    if (M(p) >= con_block_type + 0x12) {
        return 0;                                        // agua, hielo, arbustos, tanque
    }
    BSTATUS(bi) = 0x33;                                  // la bala se detiene
    const u8 t = M(p);
    if (t != con_block_type + 0x11) {
        if (wide_ && t == con_block_type + 0x10) {
            // campo ancho: el hierro tiene 20 de salud; las balas bajan nivel + 1 (nivel 3 lo rompe de una)
            {
                const bool rock = rock_[p] != 0;
                bool broken = !rock && bulletLevel_[bi] >= 3;
                if (!broken) {
                    u8& hp = steelHp_[p];
                    if (hp == 0) hp = rock ? kRockHealth : kSteelHealth;
                    const int dmg = rock ? (bulletLevel_[bi] >= 3 ? 2 : 1) : bulletLevel_[bi] + 1;   // la piedra: 1 por tiro (2 con nivel 3)
                    if (hp <= dmg) {
                        broken = true;
                    } else {
                        hp = static_cast<u8>(hp - dmg);
                    }
                }
                if (broken) {
                    Sub_D784_write_tile(p, 0);
                    R(ram_sfx_bullet_hit_brick) = 1;
                } else if (BulletOwner(bi) >= 0) {
                    R(ram_sfx_bullet_hit_wall) = 1;
                }
            }
            return 0;
        }
        if (BPROP(bi) & 0x02) {                          // rompe todo el mosaico (3 estrellas)
            Sub_D784_write_tile(p, 0);
            R(ram_sfx_bullet_hit_brick) = 1;
            return 0;
        }
        if (t != con_block_type + 0x10) {                // ladrillo
            if (wide_ ? BulletOwner(bi) >= 0 : bi < 2) {
                R(ram_sfx_bullet_hit_brick) = 1;
            }
            Sub_D784_write_tile(p, t & U8(~mask));       // sub_D743
            return 1;
        }
    }
    if (wide_ ? BulletOwner(bi) >= 0 : bi < 2) {
        R(ram_sfx_bullet_hit_wall) = 1;                  // acero o borde: la bala rebota
    }
    return 0;
}

// sub_E70C (00:E70C): balas contra tanques.
void Battle::Sub_E70C_check_bullets_collision_with_tanks() {
    if (IsVersus()) {
        VersusCollisions();
        return;
    }
    // balas enemigas contra jugadores
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0 || ghostT_[x] > 0) {          // (el invisible: las balas enemigas lo atraviesan)
            continue;
        }
        for (int b = lastEnemy_; b >= nPlayers_; --b) {
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
            if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
            BSTATUS(b) = 0x33;
            if (wide_ && x < kMaxPlayers) ++playerHits_[x];                // vibracion: recibio un disparo
            if (HELMET(x) != 0) {
                BSTATUS(b) = 0;
                if (wide_) ShieldHit(x, bulletLevel_[b] == 3 ? 2 : 1);   // el casco aguanta 5 puntos                          // el casco la absorbe
                continue;
            }
            if (wide_ && TUPGRADE(x) != 0) {
                // campo ancho: un impacto enemigo quita un nivel de arma (3 -> 2); a nivel 0 destruye
                const int drop = bulletLevel_[b] == 3 ? 2 : 1;
                TUPGRADE(x) = static_cast<u8>(TUPGRADE(x) > 0x20 * drop ? TUPGRADE(x) - 0x20 * drop : 0);
                TTYPE(x) = TUPGRADE(x);
                R(ram_sfx_bullet_hit_tank) = 1;
                break;
            }
            TFLAGS(x) = con_tank_flag_explosion + 0x03;
            R(ram_sfx_explosion_player) = 1;
            TUPGRADE(x) = 0;
            TTYPE(x) = 0;
            break;
        }
    }
    // balas enemigas contra aliados (aguantan tantos tiros como el tipo: 1, 2, 3 o 4)
    if (wide_) {
        for (int x = allyBegin_; x <= lastTank_; ++x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0) {
                continue;
            }
            for (int b = lastEnemy_; b >= nPlayers_; --b) {
                if ((BSTATUS(b) & 0xF0) != 0x40) continue;
                if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
                if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
                BSTATUS(b) = 0x33;
                const int dmg = 1;                           // cada tiro cuenta uno, sea cual sea el arma del enemigo
                if (wd_.hp[x] > dmg) {
                    wd_.hp[x] = static_cast<u8>(wd_.hp[x] - dmg);
                    R(ram_sfx_bullet_hit_tank) = 1;
                } else {
                    TFLAGS(x) = con_tank_flag_explosion + 0x03;
                    R(ram_sfx_explosion_enemy) = 1;
                    TTYPE(x) = 0;
                }
                break;
            }
        }
    }
    // balas de jugador contra enemigos
    for (int x = lastEnemy_; x >= nPlayers_; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        for (int b = nBullets_ - 1; b >= 0; --b) {
            if (BulletOwner(b) < 0) continue;
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
            if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
            const u8 prevStatus = BSTATUS(b);
            const bool lvl3 = wide_ && bulletLevel_[b] == 3;
            const bool alreadyPierced = wide_ && bulletPierced_[b] != 0;
            BSTATUS(b) = 0x33;
            if (wide_ && shieldHp_[x] > 0) {             // el casco del enemigo se come el disparo (5 puntos de salud)
                ShieldHit(x, lvl3 ? 2 : 1);
                R(ram_sfx_bullet_hit_tank) = 1;
                continue;
            }
            if (TTYPE(x) & 0x04) {                       // tanque con bonus: suelta el item
                Sub_E8BE_spawn_bonus();
                if (wide_) {
                    TTYPE(x) = static_cast<u8>(TTYPE(x) & 0xFB);
                } else if (TTYPE(x) == 0xE4) {
                    TTYPE(x)--;
                }
            }
            const int hits = TTYPE(x) & 0x03;
            const int dmg = lvl3 ? 2 : 1;                // las balas de nivel 3 hacen doble dano
            if (wide_) {
                if (wd_.hp[x] > dmg) {                   // el enemigo aguanta: baja su salud
                    wd_.hp[x] = static_cast<u8>(wd_.hp[x] - dmg);
                    if (((TTYPE(x) >> 5) & 3) == 3) {
                        TTYPE(x) = static_cast<u8>((TTYPE(x) & 0xFC) | (wd_.hp[x] - 1 < 3 ? wd_.hp[x] - 1 : 3));
                    }
                    R(ram_sfx_bullet_hit_tank) = 1;
                    continue;
                }
            } else if (hits != 0) {                      // le quedan impactos (blindado)
                TTYPE(x)--;
                R(ram_sfx_bullet_hit_tank) = 1;
                continue;
            }
            TFLAGS(x) = con_tank_flag_explosion + 0x03;
            R(ram_sfx_explosion_enemy) = 1;
            const int kind = (TTYPE(x) >> 5) - 4;
            const int player = BulletOwner(b);
            KILLCNT(player * 4 + kind)++;
            if (R(ram_2nd_loop_flag) != con_flag_demo) {
                Sub_D9E1_calculate_decimal_number(tbl_E8BA_points_for_killing_enemy[kind]);
                Sub_D9BE_add_score(player);
                Sub_D138_gain_extra_life_for_20000_pts();
            }
            if (lvl3 && !alreadyPierced && kind != 3) {
                BSTATUS(b) = prevStatus;                 // nivel 3: mata a un enemigo (que no sea el tanque 4) y sigue de largo
                bulletPierced_[b] = 1;                   // con un segundo enemigo si se destruye
            }
            break;
        }
    }
    // balas de los aliados contra los jugadores: impactan y se apagan, pero no les hacen dano salvo con el dano amigo activado
    if (wide_) {
        for (int x = nPlayers_ - 1; x >= 0; --x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0) continue;
            for (int b = nBullets_ - 1; b >= 0; --b) {
                if (!IsAllyBullet(b) || (BSTATUS(b) & 0xF0) != 0x40) continue;
                if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
                if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
                BSTATUS(b) = 0x33;
                if (friendlyFire_ != FriendlyFire::Damage) break;                      // sin dano amigo: nada
                ++playerHits_[x];
                if (HELMET(x) != 0) {
                    BSTATUS(b) = 0;
                    ShieldHit(x, bulletLevel_[b] == 3 ? 2 : 1);
                    break;
                }
                if (TUPGRADE(x) != 0) {                                                // quita un nivel de arma; a nivel 0 destruye el tanque
                    const int drop = bulletLevel_[b] == 3 ? 2 : 1;
                    TUPGRADE(x) = static_cast<u8>(TUPGRADE(x) > 0x20 * drop ? TUPGRADE(x) - 0x20 * drop : 0);
                    TTYPE(x) = TUPGRADE(x);
                } else {
                    TFLAGS(x) = con_tank_flag_explosion + 0x03;
                    R(ram_sfx_explosion_player) = 1;
                    TTYPE(x) = 0;
                }
                break;
            }
        }
    }
    // balas de jugador contra el otro jugador (lo paraliza)
    if (friendlyFire_ == FriendlyFire::Off) {
        return;                                          // las balas de jugador atraviesan a los aliados
    }
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        for (int b = nBullets_ - 1; b >= 0; --b) {
            if (BulletOwner(b) < 0) continue;
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (BulletOwner(b) == x || IsAllyBullet(b)) continue;   // su propia bala; las de los aliados no danan
            if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
            if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
            BSTATUS(b) = 0x33;
            if (wide_ && x < kMaxPlayers) ++playerHits_[x];                // vibracion: recibio un disparo
            if (HELMET(x) != 0) {
                BSTATUS(b) = 0;
                if (wide_) ShieldHit(x, bulletLevel_[b] == 3 ? 2 : 1);   // el casco aguanta 5 puntos
                continue;
            }
            if (R(ram_2nd_loop_flag) == con_flag_demo) continue;
            if (friendlyFire_ == FriendlyFire::Damage) {
                // el impacto aliado quita un nivel de arma (3 -> 2); a nivel 0 destruye el tanque
                if (TUPGRADE(x) != 0) {
                    const int drop = bulletLevel_[b] == 3 ? 2 : 1;
                    TUPGRADE(x) = static_cast<u8>(TUPGRADE(x) > 0x20 * drop ? TUPGRADE(x) - 0x20 * drop : 0);
                    TTYPE(x) = TUPGRADE(x);
                } else {
                    TFLAGS(x) = con_tank_flag_explosion + 0x03;
                    R(ram_sfx_explosion_player) = 1;
                    TTYPE(x) = 0;
                }
                break;
            }
            if (STUN(x) != 0) continue;
            STUN(x) = 0xC8;
            break;
        }
    }
}

// sub_E910 (00:E910): las balas de jugador anulan a cualquier otra bala que toquen.
void Battle::Sub_E910_check_bullets_collision_with_bullets() {
    const bool vs = IsVersus();
    for (int a = nBullets_ - 1; a >= 0; --a) {
        if (!vs && BulletOwner(a) < 0) continue;
        if ((BSTATUS(a) & 0xF0) != 0x40) continue;
        for (int b = nBullets_ - 1; b >= 0; --b) {
            if (BulletShooter(a) == BulletShooter(b)) continue;      // la misma bala o su segunda
            if (vs) {
                if (TeamOf(BulletShooter(a)) == TeamOf(BulletShooter(b))) continue;       // las balas del mismo equipo no se anulan
            } else if (wide_ && IsAllyBullet(a) && IsAllyBullet(b)) {
                continue;                                                                   // las balas de los aliados entre si no se anulan
            }
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (AbsDiffX(BPOSX(b), BPOSX(a)) >= 6) continue;
            if (AbsDiff8(BPOSY(b), BPOSY(a)) >= 6) continue;
            if (wide_ && !vs && rules_.ai < 2 && BulletOwner(b) < 0) {
                // facil y normal: cualquier bala aliada frena a cualquier bala enemiga. En facil la bala aliada sigue; en normal se gasta
                // salvo que sea de nivel mayor.
                BSTATUS(b) = 0;
                if (rules_.ai == 1 && bulletLevel_[a] <= bulletLevel_[b]) { BSTATUS(a) = 0; break; }
                continue;
            }
            if (wide_ && bulletLevel_[a] != bulletLevel_[b]) {
                // la bala de nivel mayor sigue; solo se destruye la de nivel inferior
                BSTATUS(bulletLevel_[a] > bulletLevel_[b] ? b : a) = 0;
                if (bulletLevel_[a] < bulletLevel_[b]) break;
                continue;
            }
            BSTATUS(a) = 0;
            BSTATUS(b) = 0;
        }
    }
}

// ===========================================================================
// Items (bonus) y base
// ===========================================================================

// sub_E8BE_spawn_bonus (00:E8BE)
void Battle::Sub_E8BE_spawn_bonus() {
    int freeSlot = -1;                                                   // lugares de items extra libres (hasta MaxItems() - 1)
    for (int i = 0; i < MaxItems() - 1 && i < 3; ++i) if (!rItems_[i].on) { freeSlot = i; break; }
    if (SecondItemsAllowed() && BONUSX != 0 && freeSlot >= 0) {          // ya hay un item: el nuevo va a un lugar aparte y los demas se quedan
        const int sx = R(ram_bonus_pos_X), shi = bonusXHi_, sy = R(ram_bonus_pos_Y), sid = R(ram_bonus_id), st = R(ram_0062_bonus_timer);
        for (int tries = 0; tries < 12; ++tries) {
            SpawnBonusCore();
            const int nx = R(ram_bonus_pos_X) | (bonusXHi_ << 8), ny = R(ram_bonus_pos_Y);
            bool apart = std::abs(nx - (sx | (shi << 8))) >= 16 || std::abs(ny - sy) >= 16;
            for (const RoyaleItem& o : rItems_) if (o.on && std::abs(nx - o.x) < 16 && std::abs(ny - o.y) < 16) apart = false;
            if (apart) break;                                                                                      // en otro lugar
        }
        rItems_[freeSlot] = RoyaleItem{true, R(ram_bonus_pos_X) | (bonusXHi_ << 8), R(ram_bonus_pos_Y), R(ram_bonus_id), 1 << 30};
        R(ram_bonus_pos_X) = static_cast<u8>(sx);
        bonusXHi_ = static_cast<u8>(shi);
        R(ram_bonus_pos_Y) = static_cast<u8>(sy);
        R(ram_bonus_id) = static_cast<u8>(sid);
        R(ram_0062_bonus_timer) = static_cast<u8>(st);
    } else {
        SpawnBonusCore();
    }
    EnemiesGoForItem();
}

void Battle::SpawnBonusCore() {
    R(ram_sfx_bonus_appear) = 1;
    do {
        auto toPos = [](u8 n) { return U8(((n * 3 * 2) + 6) << 3); };   // 0..3 -> 30, 60, 90, C0
        if (wide_) {
            BONUSX = 0x30 + (Sub_D44D_generate_random_number() & 0x07) * 0x30;    // 8 columnas a lo ancho del campo
        } else {
            BONUSX = toPos(Sub_D44D_generate_random_number() & 0x03);
        }
        R(ram_bonus_pos_Y) = toPos(Sub_D44D_generate_random_number() & 0x03);
        R(ram_bonus_id) = 0xFF;
        R(ram_0062_bonus_timer) = 0;
        Sub_E972_try_to_pick_up_bonus();                  // solo para evitar que caiga encima de un jugador
    } while (R(ram_0062_bonus_timer) != 0);
    {
        static const u8 kWideBonus[16] = {0, 1, 2, 3, 4, 5, 7, 3, 8, 9, 10, 11, 12, 0, 3, 13};   // en el campo ancho una de las granadas repetidas es el item de aliados; 8-12 = items nuevos
        static const u8 kVsBonus[16] = {0, 1, 3, 4, 5, 6, 3, 0, 8, 9, 10, 11, 12, 6, 3, 8};     // batalla vs: sin pala ni aliados
        const u8 roll = Sub_D44D_generate_random_number() & (wide_ ? 0x0F : 0x07);
        R(ram_bonus_id) = IsVersus() ? kVsBonus[roll] : (wide_ ? kWideBonus[roll] : tbl_E8FA_bonus[roll]);
    }
    R(ram_0062_bonus_timer) = 0;
}

// sub_E972_try_to_pick_up_bonus (00:E972). Devuelve true si ejecuto un
// efecto de item (la ROM descarta ahi su direccion de retorno).
bool Battle::Sub_E972_try_to_pick_up_bonus() {
    if (BONUSX == 0 || R(ram_0062_bonus_timer) != 0) {
        return false;
    }
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) continue;
        if (AbsDiffX(TPOSX(x), BONUSX) >= 0x0C) continue;
        if (AbsDiff8(TPOSY(x), R(ram_bonus_pos_Y)) >= 0x0C) continue;
        R(ram_0062_bonus_timer) = 0x32;
        if (R(ram_bonus_id) & 0x80) {
            return false;                                // sondeo de sub_E8BE: no hay item todavia
        }
        if (R(ram_2nd_loop_flag) != con_flag_demo) {
            Sub_D9E1_calculate_decimal_number(0x50);     // 500 puntos
            Sub_D9BE_add_score(x);
            Sub_D138_gain_extra_life_for_20000_pts();
            R(ram_sfx_bonus_pickup) = 1;
        }
        if (wide_ && x < kMaxPlayers) ++itemPickups_[x];
        BonusHandler(R(ram_bonus_id), x);
        return true;
    }
    if (wide_ && !IsVersus() && rules_.ai >= 1 && (R(ram_bonus_id) & 0x80) == 0) {   // los enemigos tambien agarran items (salvo en facil)
        for (int x = nPlayers_; x <= lastEnemy_; ++x) {
            const u8 f = TFLAGS(x);
            if (!(f & 0x80) || f >= 0xE0) continue;
            if (AbsDiffX(TPOSX(x), BONUSX) >= 0x0C) continue;
            if (AbsDiff8(TPOSY(x), R(ram_bonus_pos_Y)) >= 0x0C) continue;
            const int id = R(ram_bonus_id);
            BONUSX = 0;                                  // el item desaparece
            R(ram_sfx_bonus_pickup) = 1;
            EnemyBonus(id, x);
            break;
        }
    }
    return false;
}

int Battle::ActivePlayers() const {
    int n = 0;
    for (int p = 0; p < nPlayers_ && p < kMaxPlayers; ++p) n += present_[p] ? 1 : 0;
    return n < 1 ? 1 : (n > 4 ? 4 : n);        // la dificultad cuenta hasta 4 jugadores
}

void Battle::SetPresentMask(int mask) {
    for (int p = 0; p < nPlayers_ && p < kMaxPlayers; ++p) {
        present_[p] = (mask >> p) & 1;
        if (!present_[p]) LIVES(p) = 0;
    }
    diffPlayers_ = ActivePlayers();
    static const int kTotals[4] = {20, 20, 36, 48};
    enemyTotal_ = kTotals[diffPlayers_ - 1] * (10 + tier_) / 10;
    SetEnemyLimitForStage();
}

void Battle::MarkPlayerGone(int p) {
    if (p < 0 || p >= nPlayers_ || !present_[p]) return;
    if (IsVersus()) { aiPlayer_[p] = true; return; }         // batalla vs / royale: su tanque sigue, ahora lo juega la IA
    present_[p] = false;
    LIVES(p) = 0;
    TFLAGS(p) = 0;
    TTYPE(p) = 0;
    TUPGRADE(p) = 0;
    HELMET(p) = 0;
    STUN(p) = 0;
    shieldHp_[p] = 0;
    playerFreeze_[p] = 0;
    stealTarget_[p] = -1;
}

void Battle::AddPlayerLate(int p, int color) {
    if (p < 0 || p >= nPlayers_ || present_[p]) return;
    present_[p] = true;
    SetPlayerColor(p, color);
    LIVES(p) = static_cast<u8>(startLives_);
    TUPGRADE(p) = 0;
    aiPlayer_[p] = false;
    Sub_E363_tank_spawn_handler(p);                      // aparece con la estrella de siempre
}

int Battle::ShakeX() const {
    if (shake_ <= 0) return 0;
    const int amp = 1 + shake_ / 8;
    return (shake_ & 1) ? amp : -amp;
}

int Battle::ShakeY() const {
    if (shake_ <= 0) return 0;
    const int amp = 1 + shake_ / 10;
    return ((shake_ >> 1) & 1) ? amp : -amp;
}

// Bombardeo: espera 2 minutos, alerta, y tres aviones recorren tres columnas de arriba a abajo. Cada bomba marca su
// cuadro en rojo y a 1,5 s explota (3x3 cuadros). Nunca se apunta cerca del aguila enemiga.
void Battle::Sub_Bombing() {
    if (shake_ > 0) --shake_;
    if (!BombingActive()) {
        bomb_.phase = 0;
        for (auto& p : bomb_.plane) p.active = false;
        for (auto& t : bomb_.target) t.fuse = 0;
        return;
    }
    // las explosiones en curso solo avanzan su animacion
    for (auto& bl : bomb_.blast) {
        if (bl.age >= 0 && ++bl.age >= kBombBlastFrames) bl.age = -1;
    }
    if (bomb_.phase == 0) {
        if (bomb_.countdown > 0 && --bomb_.countdown == 0) {
            bomb_.phase = 1;
            bomb_.alert = kBombAlertFrames;
        }
        return;
    }
    if (bomb_.phase == 1) {
        if (bomb_.alert % 30 == 0) R(ram_sfx_bonus_appear) = 1;       // la sirena
        if (--bomb_.alert <= 0) StartRaid();
        return;
    }
    // fase 2: aviones, marcas y explosiones
    ++bomb_.raidT;
    static constexpr int kPlaneGap = 45;                              // un avion detras de otro
    static constexpr int kPlaneSpeed = 3;                             // pixeles por cuadro
    const int fieldW = kWideBlocksX * 16;
    bool planesBusy = false;
    for (int i = 0; i < bomb_.planes; ++i) {
        auto& pl = bomb_.plane[i];
        const int t = bomb_.raidT - i * kPlaneGap;
        if (t < 0) { planesBusy = true; continue; }
        if (t == 0) ++bomberSounds_;                                   // entra el avion
        const int travel = -24 + kPlaneSpeed * t;
        if (pl.dir == 0) {                                             // vertical: de arriba hacia abajo
            pl.x = bomb_.lane[i] * 16 + 8;
            pl.y = travel;
            pl.active = pl.y < 264 + 24;
        } else {                                                       // horizontal: de lado a lado de la pantalla
            pl.y = 0x18 + bomb_.lane[i] * 16;
            pl.x = pl.dir == 2 ? travel : fieldW - travel;
            pl.active = pl.dir == 2 ? pl.x < fieldW + 24 : pl.x > -24;
        }
        if (pl.active) planesBusy = true;
        for (int k = 0; k < 3; ++k) {
            if (bomb_.dropped[i][k]) continue;
            int cx, cy;
            bool reached;
            if (pl.dir == 0) {
                cx = bomb_.lane[i] * 16 + 8;
                cy = 0x18 + bomb_.bombAt[i][k] * 16;
                reached = pl.y >= cy;
            } else {
                cx = bomb_.bombAt[i][k] * 16 + 8;
                cy = 0x18 + bomb_.lane[i] * 16;
                reached = pl.dir == 2 ? pl.x >= cx : pl.x <= cx;
            }
            if (!reached) continue;
            bomb_.dropped[i][k] = true;
            for (auto& tg : bomb_.target) {
                if (tg.fuse == 0) {
                    tg.cx = cx;
                    tg.cy = cy;
                    tg.fuse = kBombFuse;
                    break;
                }
            }
        }
    }
    bool pending = planesBusy;
    for (auto& tg : bomb_.target) {
        if (tg.fuse == 0) continue;
        pending = true;
        if (--tg.fuse == 0) DetonateBomb(tg.cx, tg.cy);
    }
    for (const auto& bl : bomb_.blast) {
        if (bl.age >= 0) pending = true;
    }
    if (!pending) {
        bomb_.phase = 0;
        bomb_.countdown = BombInterval();
    }
}

// Elige aviones (mas con la dificultad: 3 a 6), su orientacion (vertical u horizontal) y donde cae cada bomba.
// Ninguna bomba apunta a un cuadro cuya explosion pueda tocar el aguila enemiga (bloque fila 0, columna 13: su zona de
// peligro son las filas 0-1 y las columnas 12-14).
void Battle::StartRaid() {
    bomb_.phase = 2;
    bomb_.raidT = -1;                                 // el primer cuadro del bombardeo es el 0
    bomb_.planes = 2 + (tier_ + 1) / 2;               // nivel 10: 3, 20: 4, 30: 5, 40: 6
    if (bomb_.planes > kMaxPlanes) bomb_.planes = kMaxPlanes;
    bool usedCol[kWideBlocksX] = {};
    bool usedRow[kWideBlocksY] = {};
    for (int i = 0; i < bomb_.planes; ++i) {
        const bool horizontal = (Sub_D44D_generate_random_number() & 1) != 0;
        const int lanes = horizontal ? kWideBlocksY : kWideBlocksX;
        int lane;
        do {
            lane = Sub_D44D_generate_random_number() % lanes;
        } while (horizontal ? usedRow[lane] : usedCol[lane]);
        (horizontal ? usedRow : usedCol)[lane] = true;
        bomb_.lane[i] = lane;
        // posiciones de las tres bombas a lo largo del carril
        const int span = horizontal ? kWideBlocksX : kWideBlocksY;
        int pos[3];
        for (int k = 0; k < 3; ++k) {
            bool bad;
            do {
                pos[k] = Sub_D44D_generate_random_number() % span;
                bad = false;
                for (int j = 0; j < k; ++j) bad = bad || pos[j] == pos[k];
                const int col = horizontal ? pos[k] : lane;
                const int row = horizontal ? lane : pos[k];
                if (IsVersus()) {
                    if (!IsRoyale() && NearVsEagle(col, row)) bad = true;                       // ninguna bomba cerca de las dos aguilas
                } else if (col >= EagleCol() - 1 && col <= EagleCol() + 1 && row >= EagleRow() - 2 && row <= EagleRow()) {
                    bad = true;
                }
            } while (bad);
        }
        std::sort(pos, pos + 3);
        for (int k = 0; k < 3; ++k) {
            bomb_.bombAt[i][k] = pos[k];
            bomb_.dropped[i][k] = false;
        }
        bomb_.plane[i] = BombingState::Plane{};
        bomb_.plane[i].dir = horizontal ? ((Sub_D44D_generate_random_number() & 1) ? 2 : 3) : 0;
    }
    for (int i = bomb_.planes; i < kMaxPlanes; ++i) bomb_.plane[i] = BombingState::Plane{};
}

// Una bomba explota: 3x3 cuadros. Destruye todo tipo de bloques, tanques (jugadores, enemigos y aliados) y balas.
void Battle::DetonateBomb(int cx, int cy) {
    shake_ = 24;
    ++blastCount_;
    for (auto& bl : bomb_.blast) {
        if (bl.age < 0) {
            bl.cx = cx;
            bl.cy = cy;
            bl.age = 0;
            break;
        }
    }
    R(ram_sfx_explosion_hq) = 1;
    // terreno
    const int col0 = (cx - 24) / 8, row0 = (cy - 24) / 8;
    bool eagleHit = false;
    for (int row = row0; row < row0 + 6; ++row) {
        for (int col = col0; col < col0 + 6; ++col) {
            if (col < 0 || col >= kWideBlocksX * 2 || row < (kOpenTopStrip ? 0 : 2) || row > 29) continue;
            const uint16_t p = Sub_D5FB_calculate_pointer(col, row);
            const u8 tile = M(p);
            if ((tile & 0xFC) == 0xC8) {
                if (row >= 8) eagleHit = true;                  // el aguila de los jugadores (el enemigo no entra nunca en la zona)
                continue;
            }
            M(p) = 0x00;
            steelHp_[p] = 0;
            rock_[p] = 0;
        }
    }
    if (eagleHit && !IsBossLevel() && !IsVersus() && R(ram_game_over_flag) != 0 && (R(ram_game_over_flag) & 0x80)) {
        R(ram_game_over_flag) = 0x27;
        ++eaglesDestroyed_;
        R(ram_sfx_explosion_player) = 1;
        Sub_CC08_draw_destroyed_eagle();
    }
    // tanques
    for (int x = 0; x <= lastTank_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) continue;
        if (AbsDiffX(TPOSX(x), cx) >= 32 || AbsDiff8(TPOSY(x), cy) >= 32) continue;
        TFLAGS(x) = con_tank_flag_explosion + 0x03;
        TTYPE(x) = 0;
        if (x < nPlayers_) {
            TUPGRADE(x) = 0;
            R(ram_sfx_explosion_player) = 1;
        } else {
            R(ram_sfx_explosion_enemy) = 1;
        }
    }
    // balas e item
    for (int b = 0; b < nBullets_; ++b) {
        if ((BSTATUS(b) & 0xF0) != 0x40) continue;
        if (AbsDiffX(BPOSX(b), cx) < 28 && AbsDiff8(BPOSY(b), cy) < 28) BSTATUS(b) = 0;
    }
    if (BONUSX != 0 && AbsDiffX(BONUSX, cx) < 28 && AbsDiff8(R(ram_bonus_pos_Y), cy) < 28) BONUSX = 0;
}

// Enemigos a la vez en pantalla: 1 jugador 4, 2: 6 (como la ROM), 3: 10, 4: 12; en los niveles del aguila, ademas +1 cada
// 2 escalones de dificultad (nivel 10: +0, 20: +1, 30: +2, 40: +3).
void Battle::SetEnemyLimitForStage() {
    static const int kEnemiesOnScreen[5] = {4, 4, 6, 10, 12};
    int limit = nPlayers_ + kEnemiesOnScreen[diffPlayers_] - 1 + (IsBossLevel() ? tier_ / 2 : 0);
    if (limit > lastEnemy_) limit = lastEnemy_;
    R(ram_enemy_limit) = static_cast<u8>(limit);
}

// Disparos que aguanta un enemigo de ese tipo (0 basico, 1 rapido, 2 power, 3 blindado) segun la dificultad de la IA.
int Battle::EnemyHp(int kind) const {
    static const int kEasy[4] = {1, 1, 1, 3}, kNormal[4] = {1, 1, 2, 4};
    if (IsVersus()) return kNormal[kind] + (kind >= 2 ? tier_ / 2 : tier_ / 4);
    if (rules_.ai == 0) return kEasy[kind];
    if (rules_.ai == 1) return kNormal[kind];
    return kNormal[kind] + (kind >= 2 ? tier_ / 2 : tier_ / 4);                      // dificil: crece con los niveles
}

void Battle::ReviveForContinue() {
    for (int p = 0; p < nPlayers_; ++p) if (present_[p]) LIVES(p) = static_cast<u8>(startLives_);
    R(ram_game_over_flag) = con_not_game_over;
}

void Battle::SetEnemyKind(int x, int kind) {
    const int hp = EnemyHp(kind);
    wd_.hp[x] = static_cast<u8>(hp);
    TTYPE(x) = static_cast<u8>((0x80 + (kind << 5)) | (TTYPE(x) & 0x04) | (kind == 3 ? (hp - 1 < 3 ? hp - 1 : 3) : 0));
}

int Battle::RandomEnemyKind() {
    static const int w0[4] = {70, 20, 8, 2};
    static const int w7[4] = {30, 25, 25, 20};
    int roll = Sub_D44D_generate_random_number() % 100;
    for (int i = 0; i < 4; ++i) {
        const int w = w0[i] + (w7[i] - w0[i]) * tier_ / 7;
        if (roll < w) return i;
        roll -= w;
    }
    return 0;
}

void Battle::EnemyBonus(int id, int x) {
    switch (id) {
        case 0:                                          // casco: 5 puntos de salud
            shieldHp_[x] = 5;
            break;
        case 1:                                          // reloj: paraliza a los jugadores la mitad que a los enemigos (0x0A * 64 cuadros)
            for (int p = 0; p < nPlayers_; ++p) playerFreeze_[p] = (0x0A * 64) / 2;
            break;
        case 2:                                          // pala: se queda sin defensas el aguila de los jugadores
            if ((R(ram_game_over_flag) & 0x80) && !IsBossLevel()) {
                R(ram_shovel_timer) = 0;
                DrawBareBase();
            }
            break;
        case 3: {                                        // estrella: el tipo siguiente
            const int kind = (TTYPE(x) >> 5) & 3;
            if (kind < 3) SetEnemyKind(x, kind + 1);
            break;
        }
        case 4:
            EnemyGrenade(x);
            break;
        case 5:                                          // vida: un enemigo mas en la lista de la etapa
            if (!IsBossLevel() && R(ram_enemy_spawn_cnt) < 250) R(ram_enemy_spawn_cnt)++;
            break;
        case 6:                                          // pistola: el tanque grande
            SetEnemyKind(x, 3);
            break;
        case 7:
            SpawnExtraEnemies(x);
            break;
        case kBonusGhost:                                // los enemigos se vuelven invisibles (solo se ven sus disparos)
            enemyGhostT_ = kItemFrames;
            break;
        case kBonusHeli:                                 // helicoptero enemigo: ataca a los jugadores
            SpawnHeli(-1);
            break;
        case kBonusMine:                                 // minas enemigas: parpadean 3 segundos y se vuelven invisibles
            PlaceMines(-1, 3);
            break;
        case kBonusPortal:
            MakePortal();
            break;
        case kBonusSpeed:
            speedT_[x] = kItemFrames;
            break;
        default:
            break;
    }
}

// Granada enemiga: destruye a la mitad de los jugadores vivos (redondeando hacia arriba), los mas cercanos al enemigo.
void Battle::EnemyGrenade(int x) {
    int alive[kMaxPlayers], n = 0;
    for (int p = 0; p < nPlayers_; ++p) {
        const u8 f = TFLAGS(p);
        if ((f & 0x80) && f < 0xE0) alive[n++] = p;
    }
    auto dist = [&](int p) { return AbsDiffX(TPOSX(p), TPOSX(x)) + std::abs(static_cast<int>(TPOSY(p)) - static_cast<int>(TPOSY(x))); };
    for (int i = 1; i < n; ++i) {                        // ordena por cercania
        for (int j = i; j > 0 && dist(alive[j]) < dist(alive[j - 1]); --j) std::swap(alive[j], alive[j - 1]);
    }
    R(ram_sfx_explosion_enemy) = 1;
    for (int i = 0; i < (n + 1) / 2; ++i) {
        const int p = alive[i];
        TFLAGS(p) = con_tank_flag_explosion + 0x03;
        R(ram_sfx_explosion_player) = 1;
        TUPGRADE(p) = 0;
        TTYPE(p) = 0;
    }
}

// Item de aliados en manos enemigas: dos tanques enemigos mas al lado del que lo agarro.
void Battle::SpawnExtraEnemies(int x) {
    static const int offsets[2][2] = {{-16, 0}, {16, 0}};
    int placed = 0;
    for (int slot = nPlayers_; slot <= lastEnemy_ && placed < 2; ++slot) {
        if (TFLAGS(slot) != 0) continue;
        int px = (TPOSX(x) + offsets[placed][0] + 4) & ~7;
        int py = (TPOSY(x) + offsets[placed][1] + 4) & ~7;
        px = px < 8 ? 8 : (px > 424 ? 424 : px);
        py = py < 24 ? 24 : (py > 0xE8 ? 0xE8 : py);
        TPOSX(slot) = px;
        TPOSY(slot) = py;
        TTYPE(slot) = 0;
        TWHEELS(slot) = 0;
        extraEnemy_[slot] = true;
        shieldHp_[slot] = 0;
        TFLAGS(slot) = con_tank_flag_respawn;
        Sub_D80B_write_block_tiles_and_attribute(0x0F, px, py);   // limpia el lugar
        R(ram_enemies_left_cnt)++;
        ++placed;
    }
}

// El aguila de los jugadores sin ningun muro alrededor (pala enemiga).
void Battle::DrawBareBase() {
    const int c = 12 + BaseShiftX() / 8;
    const int r = 24 + BaseShiftY() / 8;
    for (int row = r; row < r + 4; ++row) {
        for (int col = c; col < c + 6; ++col) {
            M(Sub_D5FB_calculate_pointer(col, row)) = 0x00;
        }
    }
    Sub_CB5D_draw_default_eagle();
}

// Un impacto contra el casco (5 puntos). Devuelve true si lo rompio.
bool Battle::ShieldHit(int x, int dmg) {
    if (shieldHp_[x] == 0) return false;                 // sin salud propia: proteccion de aparicion, no se gasta
    if (shieldHp_[x] > dmg) {
        shieldHp_[x] = static_cast<u8>(shieldHp_[x] - dmg);
        return false;
    }
    shieldHp_[x] = 0;
    if (x < nPlayers_) HELMET(x) = 0;
    return true;
}

// Robo de vidas: el jugador sin vidas elige (izquierda/derecha) a quien robarle una y la roba con el disparo.
void Battle::Sub_StealLives() {
    if (!wide_ || IsVersus()) return;
    const bool baseOk = R(ram_game_over_flag) == con_not_game_over;
    for (int p = 0; p < nPlayers_; ++p) {
        auto eligible = [&](int d) { return d != p && d >= 0 && d < nPlayers_ && present_[d] && LIVES(d) >= 2; };   // al donante le queda al menos una
        if (!present_[p] || LIVES(p) != 0 || TFLAGS(p) != 0 || !baseOk) {
            stealTarget_[p] = -1;
            continue;
        }
        if (!eligible(stealTarget_[p])) {
            stealTarget_[p] = -1;
            for (int d = 0; d < nPlayers_; ++d) {
                if (eligible(d)) { stealTarget_[p] = d; break; }
            }
        }
        if (stealTarget_[p] < 0) continue;
        const u8 press = BTNPRESS(p);
        const int dir = (press & (con_btn_Right | con_btn_Down)) ? 1 : ((press & (con_btn_Left | con_btn_Up)) ? -1 : 0);
        if (dir != 0) {
            for (int i = 1; i <= nPlayers_; ++i) {
                const int d = ((stealTarget_[p] + dir * i) % nPlayers_ + nPlayers_) % nPlayers_;
                if (eligible(d)) { stealTarget_[p] = d; break; }
            }
        }
        if (press & con_btn_A) {
            const int d = stealTarget_[p];
            LIVES(d)--;
            LIVES(p) = 1;
            TUPGRADE(p) = 0;
            TTYPE(p) = 0;
            R(ram_sfx_gain_life_1) = 1;
            R(ram_sfx_gain_life_2) = 1;
            Sub_E363_tank_spawn_handler(p);
            stealTarget_[p] = -1;
        }
    }
}

// tbl_E9E2_bonus_pickup_handler (00:E9E2)
void Battle::BonusHandler(int id, int x) {
    if (rules_.itemsShared && wide_ && !IsVersus() && !sharing_ && x < nPlayers_ &&
        (id == 0 || id == 3 || id == 6 || id == kBonusGhost || id == kBonusSpeed)) {      // casco, estrella, pistola, invisible y velocidad: para todos
        sharing_ = true;
        for (int p = 0; p < nPlayers_; ++p) {
            const u8 f = TFLAGS(p);
            if (p != x && present_[p] && (f & 0x80) && f < 0xE0) BonusHandler(id, p);
        }
        sharing_ = false;
    }
    switch (id) {
        case 0:                                          // casco
            HELMET(x) = 0x0A;
            if (wide_) shieldHp_[x] = 5;                 // aguanta 5 puntos de disparos
            break;
        case 1:                                          // reloj
            if (IsRoyale()) {                            // battle royale: congela a todos los demas
                for (int p = 0; p < nPlayers_; ++p) if (p != x) playerFreeze_[p] = (0x0A * 64) / 2;
                for (int id = 0; id < 8; ++id) if (id != x) teamClock_[id] = 0x0A;
                break;
            }
            if (IsVersus()) {                            // congela al otro equipo (sus tanques con IA y a sus jugadores)
                const int opp = 1 - playerTeam_[x];
                teamClock_[opp] = 0x0A;
                for (int p = 0; p < nPlayers_; ++p) if (playerTeam_[p] == opp) playerFreeze_[p] = (0x0A * 64) / 2;
                break;
            }
            R(ram_clock_timer) = 0x0A;
            break;
        case 2:                                          // pala
            if (IsVersus()) break;
            if ((R(ram_game_over_flag) & 0x80) && !IsBossLevel()) {
                Sub_CB9E_draw_protected_base();
                R(ram_shovel_timer) = 0x14;
            }
            break;
        case 3:                                          // estrella
            if (TUPGRADE(x) != 0x60) {
                TUPGRADE(x) = U8(TUPGRADE(x) + 0x20);
                TTYPE(x) = TUPGRADE(x);
            }
            break;
        case 4:                                          // granada
            R(ram_sfx_explosion_enemy) = 1;
            if (IsRoyale()) {                            // battle royale: revienta a todos los jugadores contrarios (cuentan como bajas de quien la agarro)
                for (int p = 0; p < nPlayers_; ++p) {
                    const u8 f = TFLAGS(p);
                    if (p == x || !present_[p] || !(f & 0x80) || f >= 0xE0) continue;
                    TFLAGS(p) = con_tank_flag_explosion + 0x03;
                    TUPGRADE(p) = 0;
                    TTYPE(p) = 0;
                    R(ram_sfx_explosion_player) = 1;
                    ++royaleKills_[x & 7];
                }
            }
            for (int y = lastEnemy_; y >= nPlayers_; --y) {
                const u8 f = TFLAGS(y);
                if (IsVersus() && vsTeam_[y] == (IsRoyale() ? x : playerTeam_[x])) continue;      // batalla vs: solo revienta a los tanques del otro equipo
                if ((f & 0x80) && f < 0xE0) {
                    TFLAGS(y) = con_tank_flag_explosion + 0x03;
                    TTYPE(y) = 0;
                }
            }
            break;
        case 5:                                          // tanque: vida extra
            LIVES(x)++;
            R(ram_sfx_gain_life_1) = 1;
            R(ram_sfx_gain_life_2) = 1;
            break;
        case 6:
            // Pistola. En la ROM este id existe (grafico en el CHR y entrada
            // en tbl_E9E2 apuntando a un RTS) pero nunca aparece ni hace nada.
            // Extension nuestra, como en la version anterior: sube el tanque
            // directo al nivel maximo (3 estrellas, $60).
            TUPGRADE(x) = 0x60;
            TTYPE(x) = 0x60;
            break;
        case 7:
            if (!IsVersus()) SpawnAllies(x);             // aliados con IA (extension del campo ancho)
            break;
        case kBonusGhost:                                // invisible: los enemigos no te atacan
            ghostT_[x] = kItemFrames;
            break;
        case kBonusHeli:                                 // helicoptero aliado: ataca a los enemigos 10 segundos
            SpawnHeli(x);
            break;
        case kBonusMine:                                 // 3 minas terrestres al azar (hasta 9)
            PlaceMines(x, 3);
            break;
        case kBonusPortal:                               // dos puertas conectadas, 3 usos
            MakePortal();
            break;
        case kBonusSpeed:                                // el doble de velocidad
            speedT_[x] = kItemFrames;
            break;
        case kBonusCannon:                               // canones: el aguila se cubre de ladrillos y dos canones la cuidan
            if (!IsVersus()) SpawnCannons();
            break;
        default:
            break;
    }
}

// sub_E2A9_HQ_handler (00:E2A9): temporizador de la pala y secuencia de game over de la base.
void Battle::Sub_E2A9_HQ_handler() {
    if (R(ram_shovel_timer) != 0 && (R(ram_frm_cnt_lo) & 0x0F) == 0) {
        bool restoreDefault = false;
        if ((R(ram_frm_cnt_lo) & 0x3F) == 0) {
            R(ram_shovel_timer)--;
            if (R(ram_shovel_timer) == 0) {
                restoreDefault = true;
            }
        }
        if (restoreDefault) {
            Sub_CAF5_draw_default_base();
        } else if (R(ram_shovel_timer) < 4) {            // parpadea antes de terminar
            if (R(ram_frm_cnt_lo) & 0x10) {
                Sub_CB9E_draw_protected_base();
            } else {
                Sub_CAF5_draw_default_base();
            }
        }
    }
    const u8 flag = R(ram_game_over_flag);
    if (flag == 0 || (flag & 0x80)) {
        return;
    }
    R(0x0004) = 3;
    R(ram_game_over_flag)--;
    int a = R(ram_game_over_flag) >> 2;
    a -= 5;
    if (a < 0) a = -a;
    a -= 5;
    if (a < 0) a = -a;
    switch (a) {
        case 1: Sub_E328_hq_sprite(0xF1, 0x78 + BaseShiftX(), 0xD8 + BaseShiftY()); break;
        case 2: Sub_E328_hq_sprite(0xF5, 0x78 + BaseShiftX(), 0xD8 + BaseShiftY()); break;
        case 3: Sub_E328_hq_sprite(0xF9, 0x78 + BaseShiftX(), 0xD8 + BaseShiftY()); break;
        case 4:
        case 5: {
            R(ram_0069_00_or_10) = (a == 4) ? 0x00 : 0x10;
            Sub_E328_hq_sprite(0xD1, 0x70 + BaseShiftX(), 0xD0 + BaseShiftY());        // sub_E33E: 4 piezas
            Sub_E328_hq_sprite(0xD5, 0x80 + BaseShiftX(), 0xD0 + BaseShiftY());
            Sub_E328_hq_sprite(0xD9, 0x70 + BaseShiftX(), 0xE0 + BaseShiftY());
            Sub_E328_hq_sprite(0xDD, 0x80 + BaseShiftX(), 0xE0 + BaseShiftY());
            break;
        }
        default:
            break;
    }
}

// sub_E328 / loc_E322 (00:E322): dos sprites en (X, Y) con tile A (+ram_0069 en las piezas grandes).
void Battle::Sub_E328_hq_sprite(u8 tile, int sprX, int sprY) {
    u8 t = tile;
    if (tile >= 0xD1 && tile <= 0xDD) {
        t = U8(tile + R(ram_0069_00_or_10));
    }
    R(ram_0053_t01_spr_T) = t;
    Sub_DA7B_display_2_sprites(sprX, sprY);
}

// sub_E27C_players_invincibility_handler (00:E27C)
void Battle::Sub_E27C_players_invincibility_sprites() {
    for (int x = nPlayers_ - 1; x >= 0; --x) {
        if (HELMET(x) == 0) {
            continue;
        }
        if ((R(ram_frm_cnt_lo) & 0x3F) == 0) {
            HELMET(x)--;
        }
        R(0x0004) = 2;
        R(ram_0053_t01_spr_T) = U8(((R(ram_frm_cnt_lo) & 0x02) << 1) + 0x29);
        Sub_DA7B_display_2_sprites(TPOSX(x), TPOSY(x));
    }
    if (!wide_) return;
    for (int x = nPlayers_; x <= lastEnemy_; ++x) {      // enemigos con casco
        const u8 f = TFLAGS(x);
        if (shieldHp_[x] == 0 || !(f & 0x80) || f >= 0xE0) continue;
        R(0x0004) = 2;
        R(ram_0053_t01_spr_T) = U8(((R(ram_frm_cnt_lo) & 0x02) << 1) + 0x29);
        Sub_DA7B_display_2_sprites(TPOSX(x), TPOSY(x));
    }
}

// ===========================================================================
// Sprites (OAM)
// ===========================================================================

// sub_DA2B_display_sprite (00:DA2B): un sprite 8x16. Los arbustos tapan al sprite.
void Battle::Sub_DA2B_display_sprite(int sprX, int sprY) {
    const int x47 = WX(sprX);
    const u8 y48 = U8(sprY - 8);
    // La ROM resta 8 solo para el OAM; el registro Y que entra a sub_D706 sigue siendo spr_Y.
    const uint16_t p = PointerPx(WX(sprX + 3), U8(sprY));
    if (spriteTraceHook) {
        spriteTraceHook(x47 & 0xFF, y48, M(p), R(0x0004));
    }
    if (M(p) == con_block_type + 0x22) {
        R(0x0004) |= R(ram_priority_spr_A);
    }
    const u8 idx = R(ram_index_oam);
    R(ram_oam + idx + 0) = y48;
    R(ram_oam + idx + 1) = R(ram_0053_t01_spr_T);
    R(ram_oam + idx + 2) = R(0x0004);
    R(ram_oam + idx + 3) = U8(x47);
    oamX_[idx >> 2] = static_cast<uint16_t>(x47);
    R(ram_index_oam) = U8(idx + R(ram_offset_oam_index));
}

// sub_DA64 (00:DA64): una bala (8x16 centrado).
void Battle::Sub_DA64_display_bullet_sprite(u8 dir, int sprX, int sprY) {
    R(ram_0053_t01_spr_T) = U8(R(ram_0053_t01_spr_T) + (dir << 1));
    Sub_DA2B_display_sprite(WX(sprX - 5), sprY);
}

// sub_DA73 (00:DA73): 2 sprites con tile base + dir * 8.
void Battle::Sub_DA73_display_2_sprites_dir(u8 dir, int sprX, int sprY) {
    R(ram_0053_t01_spr_T) = U8(R(ram_0053_t01_spr_T) + (dir << 3));
    Sub_DA7B_display_2_sprites(sprX, sprY);
}

// sub_DA7B (00:DA7B): mitad izquierda (tile T) y derecha (tile T + 2).
void Battle::Sub_DA7B_display_2_sprites(int sprX, int sprY) {
    Sub_DA2B_display_sprite(WX(sprX - 8), sprY);
    R(ram_0053_t01_spr_T) = U8(R(ram_0053_t01_spr_T) + 2);
    Sub_DA2B_display_sprite(sprX, sprY);
}

// sub_DA93_hide_unused_sprites (00:DA93)
void Battle::Sub_DA93_hide_unused_sprites() {
    u8 x = R(ram_index_oam);
    R(ram_offset_oam_index) = U8(~R(ram_offset_oam_index) + 1);
    do {
        x = U8(x + R(ram_offset_oam_index));
        R(ram_oam + x) = 0xF0;
    } while (x != 0x04);
    R(ram_index_oam) = x;
}

// sub_DEA6_tanks_handler (00:DEA6)
void Battle::Sub_DEA6_tanks_handler() {
    for (int x = 0; x <= lastTank_; ++x) {
        Sub_DEB8_tank_handler(x);
    }
}

// sub_DEE2_draw_bullet_explosion (00:DEE2)
void Battle::Sub_DEE2_draw_bullet_explosion(u8 a, int sprX, int sprY) {
    int n = (a >> 4) - 7;
    n = U8(~n + 1);
    Sub_DEF0(U8(n << 2), sprX, sprY);
}

// sub_DEF0 (00:DEF0)
void Battle::Sub_DEF0(u8 a, int sprX, int sprY) {
    R(ram_0053_t01_spr_T) = U8(a + 0xF1);
    R(0x0004) = 3;
    Sub_DA7B_display_2_sprites(sprX, sprY);
}

// sub_DEB8_tank_handler (00:DEB8): dibuja el tanque x segun su estado.
void Battle::Sub_DEB8_tank_handler(int x) {
    const u8 f = TFLAGS(x);
    const int hi = f >> 4;
    switch (hi) {
        case 0x0:
            return;
        case 0x1: {                                      // ofs_001_DEFD: cartel de puntos
            R(ram_priority_spr_A) = 0;
            if (TTYPE(x) != 0) {
                R(ram_0053_t01_spr_T) = U8((((TTYPE(x) >> 3) & 0xFC) - 0x10) + 0xB9);
                R(0x0004) = 3;
                Sub_DA7B_display_2_sprites(TPOSX(x), TPOSY(x));
            } else {
                Sub_DEF0(0, TPOSX(x), TPOSY(x));
            }
            R(ram_priority_spr_A) = 0x20;
            return;
        }
        case 0x2:                                        // ofs_001_DF33
            R(ram_priority_spr_A) = 0;
            Sub_DEF0(0x08, TPOSX(x), TPOSY(x));
            R(ram_priority_spr_A) = 0x20;
            return;
        case 0x3:
        case 0x4: {                                      // ofs_001_DF46: explosion grande (4 piezas)
            R(0x0004) = 3;
            R(ram_priority_spr_A) = 0;
            auto piece = [&](int n, int dx, int dy) {
                // sub_DF99
                const u8 t8 = U8((n << 2) + 0xD1);
                R(ram_0053_t01_spr_T) = U8(((TFLAGS(x) & 0xF0) - 0x30 ^ 0x10) + t8);
                Sub_DA7B_display_2_sprites(WX(TPOSX(x) + dx), U8(TPOSY(x) + dy));
            };
            piece(0, -8, -8);
            piece(1, 8, -8);
            piece(2, -8, 8);
            piece(3, 8, 8);
            R(ram_priority_spr_A) = 0x20;
            return;
        }
        case 0x5: case 0x6: case 0x7:                    // ofs_001_DECD: explosion chica
            R(ram_priority_spr_A) = 0;
            Sub_DEE2_draw_bullet_explosion(f, TPOSX(x), TPOSY(x));
            R(ram_priority_spr_A) = 0x20;
            return;
        case 0xE: case 0xF: {                            // ofs_001_E00B: estrella de aparicion
            int n = (f & 0x0F) - 7;
            if (n < 0) n = -n;
            R(ram_0053_t01_spr_T) = U8(((n << 1) & 0xFC) + 0xA1);
            R(0x0004) = 3;
            Sub_DA7B_display_2_sprites(TPOSX(x), TPOSY(x));
            return;
        }
        default:
            break;
    }
    // ofs_001_DFB6 (estados $80..$D0): el tanque
    if (enemyGhostT_ > 0 && x >= nPlayers_ && !IsAlly(x) && !IsVersus()) return;    // enemigos invisibles: solo se ven sus disparos
    if (x < nPlayers_ && ghostT_[x] > 0 && ((R(ram_frm_cnt_lo) >> 1) & 1)) return;  // el invisible parpadea
    u8 palette;
    if (IsAlly(x)) {
        palette = PlayerPalette(allyOwner_[x]);          // sprite de enemigo, color del jugador que agarro el item
    } else if (x >= nPlayers_ && IsVersus()) {
        const u8 own = VsTankAttr(x);
        palette = ((TTYPE(x) & 0x04) && ((R(ram_frm_cnt_lo) >> 3) & 0x01)) ? u8(3) : own;      // el que lleva un item parpadea
    } else if (x >= nPlayers_) {
        if (TTYPE(x) & 0x04) {
            palette = U8(((R(ram_frm_cnt_lo) >> 3) & 0x01) + 2);
        } else {
            palette = tbl_E003_spr_A_palette[U8((R(ram_frm_cnt_lo) << 2) + TTYPE(x)) & 0x07];
        }
    } else {
        if ((STUN(x) != 0 || (wide_ && playerFreeze_[x] != 0)) && (R(ram_frm_cnt_lo) & 0x08)) {
            return;                                      // paralizado: parpadea
        }
        palette = PlayerPalette(x);
    }
    R(0x0004) = palette;
    R(ram_0053_t01_spr_T) = U8((TTYPE(x) & 0xF0) + TWHEELS(x));
    Sub_DA73_display_2_sprites_dir(TFLAGS(x) & 0x03, TPOSX(x), TPOSY(x));
}

// sub_E0D8_bullets_status_handler (00:E0D8): dibuja las balas.
void Battle::Sub_E0D8_bullets_status_handler() {
    for (int x = nBullets_ - 1; x >= 0; --x) {
        const u8 st = BSTATUS(x);
        switch (st >> 4) {
            case 0x4:                                    // ofs_003_E0FB
                R(0x0004) = IsVersus() ? VsTankAttr(BulletShooter(x))
                                       : ((wide_ && BulletOwner(x) >= 0) ? PlayerPalette(BulletOwner(x)) : 2);   // campo ancho: la bala lleva el color de su jugador
                R(ram_0053_t01_spr_T) = 0xB1;
                Sub_DA64_display_bullet_sprite(st & 0x03, BPOSX(x), BPOSY(x));
                break;
            case 0x1: case 0x2: case 0x3:                // ofs_003_E112: explosion de impacto
                Sub_DEE2_draw_bullet_explosion(U8(st + 0x40), BPOSX(x), BPOSY(x));
                break;
            default:
                break;
        }
    }
}

// sub_E23B_display_bonus_on_screen (00:E23B)
void Battle::Sub_E23B_display_bonus_on_screen() {
    if (BONUSX == 0) {
        return;
    }
    if (R(ram_0062_bonus_timer) != 0) {
        R(ram_0062_bonus_timer)--;
        if (R(ram_0062_bonus_timer) == 0) {
            BONUSX = 0;
            return;
        }
        R(0x0004) = 2;
        R(ram_0053_t01_spr_T) = 0x3B;                    // cartel "500"
    } else {
        if ((R(ram_frm_cnt_lo) & 0x08) == 0) {
            return;                                      // parpadea
        }
        if (wide_ && R(ram_bonus_id) >= 8 && (R(ram_bonus_id) & 0x80) == 0) return;      // items nuevos: los dibuja el renderer
        R(0x0004) = 2;
        R(ram_0053_t01_spr_T) = (wide_ && R(ram_bonus_id) == 7) ? 0x00 : U8((R(ram_bonus_id) << 2) + 0x81);
        if (wide_ && R(ram_bonus_id) == 7) {
            R(0x0004) = 2 | 0x08;                        // item de aliados: grafico propio (banco 2 del renderer), paleta de los items
        }
    }
    R(ram_priority_spr_A) = 0;
    Sub_DA7B_display_2_sprites(BONUSX, R(ram_bonus_pos_Y));
    R(ram_priority_spr_A) = 0x20;
}

// ===========================================================================
// Herramientas de prueba
// ===========================================================================

int Battle::DebugSpawnEnemy(int type, bool flashing) {
    for (int x = R(ram_enemy_limit); x >= nPlayers_; --x) {
        if (TFLAGS(x) == 0) {
            Sub_E363_tank_spawn_handler(x);
            TTYPE(x) = flashing ? 0x04 : 0x00;
            debugForcedType_[x] = type & 3;
            return x;
        }
    }
    return -1;
}

void Battle::DebugSpawnBonus(int bonusId) {
    Sub_E8BE_spawn_bonus();
    R(ram_bonus_id) = U8(bonusId);
}

void Battle::DebugPlaySound(int which) {
    static const uint16_t kFlags[3][3] = {
        {ram_sfx_stage_load_1, ram_sfx_stage_load_2, ram_sfx_stage_load_3},
        {ram_sfx_hiscore_1, ram_sfx_hiscore_2, ram_sfx_hiscore_3},
        {ram_sfx_game_over_1, ram_sfx_game_over_2, ram_sfx_game_over_3},
    };
    for (uint16_t flag : kFlags[which % 3]) {
        R(flag) = 1;
    }
}

int Battle::NearestEnemy(int x) const {
    int best = -1;
    int bestDist = 1 << 30;
    for (int e = nPlayers_; e <= lastEnemy_; ++e) {
        const u8 f = wide_ ? wd_.tflags[e] : R(ram_tank_flags + e);
        if (!(f & 0x80) || f >= 0xE0) continue;
        const int d = std::abs(TankX(e) - TankX(x)) + std::abs(TankY(e) - TankY(x));
        if (d < bestDist) {
            bestDist = d;
            best = e;
        }
    }
    return best;
}

// Un aliado de la campania: pide botones a la IA y los aplica (gira alineado a la grilla de 8 px, o media vuelta en cualquier momento).
void Battle::AllyDrive(int x) {
    const u8 btn = AllyAiButtons(x);
    static const u8 kDirBtn[4] = {con_btn_Up, con_btn_Left, con_btn_Down, con_btn_Right};
    int want = TFLAGS(x) & 3;
    bool moves = false;
    for (int d = 0; d < 4; ++d) if (btn & kDirBtn[d]) { want = d; moves = true; }
    const int cur = TFLAGS(x) & 3;
    const bool aligned = (TPOSX(x) & 7) == 0 && (TPOSY(x) & 7) == 0;
    if (want != cur && (aligned || want == ((cur + 2) & 3))) TFLAGS(x) = static_cast<u8>((TFLAGS(x) & ~3) | want);
    if (moves) StepForward(x);
    if ((btn & con_btn_A) && allyAutoFire_) Sub_E08C_fire_bullet(x);
}

// Los botones de la IA de un aliado: el mismo esquema que AiButtons (los combatientes del royale), con otros objetivos.
u8 Battle::AllyAiButtons(int p) {
    const u8 fl = TFLAGS(p);
    if (!(fl & 0x80) || fl >= 0xE0 || R(ram_pause_flag) != 0) { aiStuck_[p] = 0; aiTimer_[p] = 0; aiGoalT_[p] = 0; return 0; }
    const int x = TankX(p), y = TankY(p);
    const int facing = fl & 3;
    const int sc = std::max(0, std::min(kWideBlocksX - 1, x >> 4)), sr = std::max(0, std::min(kWideBlocksY, y >> 4));
    if (x == aiLastX_[p] && y == aiLastY_[p]) ++aiStuck_[p]; else aiStuck_[p] = 0;
    aiLastX_[p] = x;
    aiLastY_[p] = y;
    if (aiTimer_[p] > 0) --aiTimer_[p];
    if (aiGoalT_[p] > 0) --aiGoalT_[p];
    if (aiFireCool_[p] > 0) --aiFireCool_[p];
    if (aiDodgeT_[p] > 0) --aiDodgeT_[p];
    if (aiWanderT_[p] > 0) --aiWanderT_[p];
    const bool wander = aiWanderT_[p] > 0;
    auto hostile = [&](int e) {
        const u8 f = TFLAGS(e);
        return e >= nPlayers_ && e <= lastEnemy_ && (f & 0x80) && f < 0xE0;
    };
    auto rnd = [&]() { return static_cast<int>(Sub_D44D_generate_random_number()); };
    const int owner = allyOwner_[p];
    const bool ownerAlive = owner >= 0 && owner < nPlayers_ && (TFLAGS(owner) & 0x80) && TFLAGS(owner) < 0xE0;
    // 1) esquivar un disparo enemigo que viene de frente
    if (aiDodgeT_[p] <= 0) {
        for (int b = 0; b < nBullets_; ++b) {
            if ((BSTATUS(b) & 0xF0) != 0x40 || BulletOwner(b) >= 0) continue;     // solo las balas enemigas
            const int bx = wd_.bposX[b] | (bulletXHi_[b] << 8), by = wd_.bposY[b];
            const int bd = BSTATUS(b) & 3;
            int dodge = -1;
            if (bd == 1 || bd == 3) {
                const bool toward = bd == 3 ? bx < x : bx > x;
                if (toward && std::abs(by - y) < 11 && std::abs(bx - x) < 56) dodge = by < y ? 2 : 0;
            } else {
                const bool toward = bd == 2 ? by < y : by > y;
                if (toward && std::abs(bx - x) < 11 && std::abs(by - y) < 56) dodge = bx < x ? 3 : 1;
            }
            if (dodge >= 0) { aiDir_[p] = dodge; aiDodgeT_[p] = 12; aiGoalT_[p] = 0; break; }
        }
    }
    // 2) destino
    int best = -1, bestDist = 1 << 20;
    for (int e = nPlayers_; e <= lastEnemy_; ++e) {
        if (!hostile(e)) continue;
        const int d = std::abs(TankX(e) - x) + std::abs(TankY(e) - y);
        if (d < bestDist) { bestDist = d; best = e; }
    }
    int ox = -1, oy = -1;                                        // el aguila enemiga de los niveles especiales
    if (IsBossLevel() && enemyEagleAlive_) { ox = EagleCol() * 16 + 8; oy = EagleRow() * 16 + 8; }
    const bool atGoal = sc == aiGoalC_[p] && sr == aiGoalR_[p];
    if (aiGoalT_[p] <= 0 || atGoal || aiStuck_[p] > 24) {
        bool picked = false;
        if (!wander && !IsBossLevel() && (R(ram_game_over_flag) & 0x80)) {       // defiende el aguila si un enemigo se le acerca
            int threat = -1, td = 1 << 20;
            for (int e = nPlayers_; e <= lastEnemy_; ++e) {
                if (!hostile(e)) continue;
                const int d = std::abs(TankX(e) - (216 + BaseDX())) + std::abs(TankY(e) - (232 + BaseDY()));
                if (d < td) { td = d; threat = e; }
            }
            if (threat >= 0 && td < 150) { aiGoalC_[p] = TankX(threat) >> 4; aiGoalR_[p] = TankY(threat) >> 4; aiGoalT_[p] = 20; picked = true; }
        }
        if (!picked && !wander && best >= 0 && bestDist < 130) {  // un enemigo cerca: va a su encuentro
            aiGoalC_[p] = TankX(best) >> 4;
            aiGoalR_[p] = TankY(best) >> 4;
            aiGoalT_[p] = 20;
            picked = true;
        }
        if (!picked && ownerAlive) {                             // acompana a quien lo trajo: lo alcanza y se queda cerca
            const int oxp = TankX(owner), oyp = TankY(owner);
            if (std::abs(oxp - x) + std::abs(oyp - y) > 90) {
                aiGoalC_[p] = oxp >> 4;
                aiGoalR_[p] = oyp >> 4;
                aiGoalT_[p] = 25;
            } else {
                for (int tries = 0; tries < 10; ++tries) {       // cerca del dueno: patrulla en un radio de 3 bloques
                    const int c = (oxp >> 4) + rnd() % 7 - 3, r = (oyp >> 4) + rnd() % 7 - 3;
                    if (c >= 1 && c < kWideBlocksX - 1 && r >= 1 && r <= kWideBlocksY - 1 && AiBlockCost(c, r) < 255) { aiGoalC_[p] = c; aiGoalR_[p] = r; break; }
                }
                aiGoalT_[p] = 60 + rnd() % 60;
            }
            picked = true;
        }
        if (!picked) {                                           // sin dueno: a recorrer el mapa (a veces, a por el aguila enemiga)
            if (ox >= 0 && (rnd() % 3) == 0) { aiGoalC_[p] = ox >> 4; aiGoalR_[p] = oy >> 4; }
            else {
                for (int tries = 0; tries < 12; ++tries) {
                    const int c = 1 + rnd() % (kWideBlocksX - 2), r = 1 + rnd() % (kWideBlocksY - 1);
                    if (AiBlockCost(c, r) < 255 && (std::abs(c - sc) + std::abs(r - sr)) >= 4) { aiGoalC_[p] = c; aiGoalR_[p] = r; break; }
                }
            }
            aiGoalT_[p] = 90 + rnd() % 90;
        }
        aiStuck_[p] = aiStuck_[p] > 24 ? 0 : aiStuck_[p];
    }
    // trabado: primero a tiros, despues por otro lado
    bool breakWall = false;
    if (aiStuck_[p] > 6) breakWall = true;
    if (aiStuck_[p] > 14) {
        static const int kDc[4] = {0, -1, 0, 1}, kDr[4] = {-1, 0, 1, 0};
        int free[4], nf = 0;
        for (int k = 0; k < 4; ++k) if (AiBlockCost(sc + kDc[k], sr + kDr[k]) < 255 && k != facing) free[nf++] = k;
        if (nf == 0) free[nf++] = (facing + 2) & 3;
        aiDir_[p] = free[rnd() % nf];
        aiTimer_[p] = 22 + rnd() % 20;
        aiWanderT_[p] = 120;
        aiGoalT_[p] = 0;
        aiStuck_[p] = 0;
        breakWall = false;
    }
    // 3) rumbo
    int dir = facing, nextCost = 1;
    if (aiDodgeT_[p] > 0) {
        dir = aiDir_[p];
    } else if (aiTimer_[p] > 0) {
        dir = aiDir_[p];
    } else {
        int d = AiPathDir(sc, sr, aiGoalC_[p], aiGoalR_[p], nextCost);
        if (d < 0) {
            const int gx = aiGoalC_[p] * 16 + 8, gy = aiGoalR_[p] * 16 + 8;
            if (std::abs(gx - x) >= 4 || std::abs(gy - y) >= 4) d = std::abs(gx - x) >= std::abs(gy - y) ? (gx > x ? 3 : 1) : (gy > y ? 2 : 0);
            else { d = facing; aiGoalT_[p] = 0; }
        } else {
            const int cx = sc * 16 + 8, cy = sr * 16 + 8;
            if ((d == 1 || d == 3) && std::abs(cy - y) >= 5) d = cy > y ? 2 : 0;
            else if ((d == 0 || d == 2) && std::abs(cx - x) >= 5) d = cx > x ? 3 : 1;
        }
        dir = d;
        aiDir_[p] = d;
        aiTimer_[p] = 6;
    }
    // quieto cuando ya esta cerca de su dueno y no hay nada que hacer
    bool hold = false;
    if (ownerAlive && !wander && best < 0 && aiDodgeT_[p] <= 0 && std::abs(TankX(owner) - x) + std::abs(TankY(owner) - y) < 40) hold = true;
    // 4) disparo: apunta antes de tirar y nunca hacia el aguila propia ni hacia el jugador que lo trajo
    bool fire = breakWall;
    if (aiFireCool_[p] <= 0 && !fire) {
        for (int e = nPlayers_; e <= lastEnemy_ && !fire; ++e) if (hostile(e) && LineOfFirePoint(p, TankX(e), TankY(e), 140)) fire = (rnd() % 2) == 0;
        if (!fire && ox >= 0 && LineOfFirePoint(p, ox, oy, 100)) fire = (rnd() % 3) == 0;
        if (!fire && (rnd() % 160) == 0) fire = true;
    }
    if (!IsBossLevel() && AimsAtEagleAt(p, BaseCol(), BaseRow(), 1)) fire = false;
    if (fire && ownerAlive && LineOfFirePoint(p, TankX(owner), TankY(owner), 140)) fire = breakWall;       // no le tira a su dueno
    static const u8 kDirBtn[4] = {con_btn_Up, con_btn_Left, con_btn_Down, con_btn_Right};
    u8 out = hold ? 0 : kDirBtn[dir & 3];                       // (quieto: sin botones de rumbo)
    if (fire && (R(ram_frm_cnt_lo) & 1)) { out = static_cast<u8>(out | con_btn_A); aiFireCool_[p] = breakWall ? 12 : 36; }
    return out;
}

// Los aliados disparan cuando tienen un enemigo en la mira y de vez en cuando al azar.
void Battle::Sub_AllyFire() {
    if (!wide_ || !allyAutoFire_ || !IsVersus()) {                // en la campania disparan desde AllyDrive (IA nueva)
        return;
    }
    for (int x = allyBegin_; x <= lastTank_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) continue;
        if (!IsBossLevel() && !IsVersus() && AimsAtEagleAt(x, BaseCol(), BaseRow(), 1)) continue;      // no le tiran a su propia aguila (ni a sus muros)
        const u8 roll = Sub_D44D_generate_random_number();
        bool inLine = false;
        const int e = NearestEnemy(x);
        if (e >= 0) {
            const int dx = TankX(e) - static_cast<int>(TPOSX(x));
            const int dy = TankY(e) - static_cast<int>(TPOSY(x));
            switch (f & 0x03) {
                case 0: inLine = dy < 0 && std::abs(dx) < 10; break;
                case 1: inLine = dx < 0 && std::abs(dy) < 10; break;
                case 2: inLine = dy > 0 && std::abs(dx) < 10; break;
                default: inLine = dx > 0 && std::abs(dy) < 10; break;
            }
        }
        if ((inLine && roll % 3 == 0) || roll % 24 == 0) {
            Sub_E08C_fire_bullet(x);
        }
    }
}

void Battle::SpawnAllies(int owner) {
    const int cx = TPOSX(owner), cy = TPOSY(owner);
    // lugares alrededor del jugador, en orden; se saltan los que caerian sobre el aguila de los jugadores (al aparecer, un aliado
    // borra lo que haya debajo y se llevaria puesta el aguila)
    static const int offsets[10][2] = {{-32, 0}, {32, 0}, {0, -32}, {0, 32}, {-32, -32}, {32, -32}, {-64, 0}, {64, 0}, {0, -64}, {-64, -32}};
    int placed = 0, tried = 0;
    for (int slot = allyBegin_; slot <= lastTank_ && placed < 2; ++slot) {
        if (TFLAGS(slot) != 0) continue;
        int x = 0, y = 0;
        bool found = false;
        while (tried < 10 && !found) {
            const int ox = offsets[tried][0], oy = offsets[tried][1];
            ++tried;
            x = (cx + ox + 4) & ~7;                      // alineados a 8 px: asi pueden elegir rumbo como los enemigos
            y = (cy + oy + 4) & ~7;
            x = x < 8 ? 8 : (x > 424 ? 424 : x);
            y = y < 24 ? 24 : (y > 0xE8 ? 0xE8 : y);
            const bool onBase = !IsBossLevel() && !IsVersus() && std::abs(x - (216 + BaseDX())) < 24 && y > 200 + BaseDY();
            found = !onBase;
        }
        if (!found) break;
        TPOSX(slot) = x;
        TPOSY(slot) = y;
        TTYPE(slot) = 0xA0;
        TWHEELS(slot) = 0;
        allyOwner_[slot] = owner;
        allyKind_[slot] = (TUPGRADE(owner) >> 5) & 3;
        TFLAGS(slot) = con_tank_flag_respawn;
        Sub_D80B_write_block_tiles_and_attribute(0x0F, x, y);   // limpia el lugar
        ++placed;
    }
}

bool Battle::StageCleared() const {
    if (IsVersus()) return false;
    if (IsBossLevel()) {
        return !enemyEagleAlive_ && R(ram_game_over_flag) == con_not_game_over;   // solo se gana destruyendo el aguila enemiga
    }
    if (!wide_ || level_ <= 0 || R(ram_enemy_spawn_cnt) != 0 || R(ram_game_over_flag) != con_not_game_over) {
        return false;
    }
    for (int e = nPlayers_; e <= lastEnemy_; ++e) {
        if (wd_.tflags[e] != 0) return false;
    }
    return true;
}

void Battle::NextStage(int level, const u8 blocks[kWideBlocksY][kWideBlocksX], const u8* strip) {
    SetLevel(level);
    diffPlayers_ = ActivePlayers();                  // la dificultad sigue a los jugadores presentes al empezar el nivel
    static const int kTotals[4] = {20, 20, 36, 48};
    enemyTotal_ = kTotals[diffPlayers_ - 1] * (10 + tier_) / 10;
    R(ram_stage) = U8(level > 35 ? 35 : level);
    SetEnemyLimitForStage();
    LoadWideStage(blocks, strip);
    std::memset(wd_.hp, 0, sizeof(wd_.hp));
    std::memset(allyOwner_, 0, sizeof(allyOwner_));
    std::memset(bulletLevel_, 0, sizeof(bulletLevel_));
    std::memset(bulletPierced_, 0, sizeof(bulletPierced_));
    R(ram_sfx_stage_load_1) = 1;
    R(ram_sfx_stage_load_2) = 1;
    R(ram_sfx_stage_load_3) = 1;
    R(ram_bg_palette_id) = con_bg_pal_00;
    Sub_C331_prepare_tanks_and_spawn_players();      // conserva vidas, puntajes y nivel de arma
}

int Battle::Score(int p) const {
    int v = 0;
    for (int y = 0; y < 7; ++y) {
        v = v * 10 + (wide_ ? wd_.score[p * 8 + y] : R(ram_score + p * 8 + y));
    }
    return v;
}

int Battle::KillCount(int p, int kind) const {
    return wide_ ? wd_.kill[p * 4 + kind] : R(ram_enemy_type_kill_cnt + p * 4 + kind);
}

void Battle::AddBonusPoints(int p, u8 hundreds) {
    Sub_D9E1_calculate_decimal_number(hundreds);
    Sub_D9BE_add_score(p);
    R(ram_sfx_bonus_1000) = 1;
    Sub_D138_gain_extra_life_for_20000_pts();
}

bool Battle::GameOver() const {
    if (IsVersus()) return false;                       // la batalla vs termina con VersusWinner
    if (R(ram_game_over_flag) == 0) {
        return true;                                    // la base se destruyo y termino la cuenta regresiva
    }
    for (int p = 0; p < nPlayers_; ++p) {
        const u8 lives = wide_ ? wd_.lives[p] : R(ram_lives + p);
        const u8 flags = wide_ ? wd_.tflags[p] : R(ram_tank_flags + p);
        if (lives != 0 || flags != 0) {
            return false;
        }
    }
    return true;
}

void Battle::StartGameOverMessage() {
    MSGX = 0x70 + ShiftX();                              // sub_C737: X = $70 (centro del campo), Y = $F0 (abajo), sube
    R(ram_game_over_msg_pos_Y) = 0xF0;
    R(ram_game_over_msg_mov_type) = 0;
    R(ram_game_over_msg_timer) = 0x11;
    R(ram_frm_cnt_lo) = 0;
    inputLocked_ = true;
}

void Battle::RunSoundFrame() {
    Sub_EA7E_sound_driver();
}

void Battle::DebugSetKills(int p, int kind, int n) {
    KILLCNT(p * 4 + kind) = static_cast<u8>(n);
}

void Battle::PlayScoreTick() {
    R(ram_sfx_score_count_1) = 1;
    R(ram_sfx_score_count_2) = 1;
}

void Battle::SetEnemyLimitMax() {
    R(ram_enemy_limit) = static_cast<u8>(lastEnemy_);
}

u8 Battle::TankFlags(int i) const { return wide_ ? wd_.tflags[i] : R(ram_tank_flags + i); }
u8 Battle::TankType(int i) const { return wide_ ? wd_.ttype[i] : R(ram_tank_type + i); }
int Battle::TankX(int i) const {
    return (wide_ ? wd_.tposX[i] : R(ram_tank_pos_X + i)) | (wide_ ? tankXHi_[i] << 8 : 0);
}
int Battle::TankY(int i) const { return wide_ ? wd_.tposY[i] : R(ram_tank_pos_Y + i); }
void Battle::SetTankPos(int i, int x, int y) {
    TPOSX(i) = x;
    TPOSY(i) = y;
}
u8 Battle::Lives(int p) const { return wide_ ? wd_.lives[p] : R(ram_lives + p); }
void Battle::SetLives(int p, u8 v) { LIVES(p) = v; }
u8 Battle::Upgrade(int p) const { return wide_ ? wd_.upgrade[p] : R(ram_tank_upgrade + p); }
u8 Battle::Helmet(int p) const { return wide_ ? wd_.helmet[p] : R(ram_helmet_timer + p); }
void Battle::SetHelmet(int p, u8 v) { HELMET(p) = v; }
void Battle::SetUpgrade(int p, u8 v) {
    TUPGRADE(p) = v;
    TTYPE(p) = v;
}

void Battle::SetBonus(int x, int y, int id) {
    BONUSX = x;
    R(ram_bonus_pos_Y) = static_cast<u8>(y);
    R(ram_bonus_id) = static_cast<u8>(id);
    R(ram_0062_bonus_timer) = 0;
}

uint32_t Battle::StateHash() const {
    uint32_t h = 2166136261u;
    auto mix = [&h](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    };
    auto mixInt = [&mix](int v) { mix(&v, sizeof v); };
    mix(ram_, sizeof ram_);
    mix(&wd_, sizeof wd_);
    mix(wideMap_, sizeof wideMap_);
    mix(tankXHi_, sizeof tankXHi_);
    mix(bulletXHi_, sizeof bulletXHi_);
    mix(steelHp_, sizeof steelHp_);
    mix(rock_, sizeof rock_);
    mix(bulletLevel_, sizeof bulletLevel_);
    mix(shieldHp_, sizeof shieldHp_);
    mixInt(enemyTotal_);
    mixInt(diffPlayers_);
    for (int i = 0; i < kMaxPlayers; ++i) mixInt(present_[i] ? 1 : 0);
    for (int i = 0; i < kMaxPlayers; ++i) mixInt(playerColor_[i]);
    mixInt(level_);
    if (IsVersus()) {
        mixInt(vsWinner_ + 1);
        mixInt((vsEagleAlive_[0] ? 1 : 0) + (vsEagleAlive_[1] ? 2 : 0));
        for (int i = 0; i < kMaxPlayers; ++i) mixInt(playerTeam_[i]);
        for (int t = 0; t < 8; ++t) mixInt(teamClock_[t]);
        for (int t = 0; t < 8; ++t) mixInt(royaleKills_[t]);
        mixInt(royaleFrames_);
        mixInt(royaleBonusT_);
        for (const RoyaleItem& it : rItems_) { mixInt(it.on ? 1 : 0); mixInt(it.x); mixInt(it.y); mixInt(it.id); mixInt(it.life); }
        for (int i = 0; i <= lastTank_ && i < kMaxTanks; ++i) { mixInt(aiGoalC_[i]); mixInt(aiGoalR_[i]); mixInt(aiGoalT_[i]); mixInt(aiFireCool_[i]); mixInt(aiDodgeT_[i]); mixInt(aiWanderT_[i]); }
        for (int i = 0; i <= lastTank_ && i < kMaxTanks; ++i) { mixInt(i < kMaxPlayers && aiPlayer_[i] ? 1 : 0); mixInt(aiDir_[i]); mixInt(aiTimer_[i]); mixInt(aiStuck_[i]); mixInt(aiLastX_[i]); mixInt(aiLastY_[i]); }
        for (int i = nPlayers_; i <= lastEnemy_; ++i) mixInt(vsTeam_[i]);
        mixInt(vsSize_);
    }
    if (spec_.active) {                                     // niveles hechos a mano: que todos tengan la misma ficha
        mixInt(spec_.boss ? 1 : 0);
        mixInt(spec_.tier);
        mixInt(spec_.rank);
        mixInt(spec_.eagleCol * 64 + spec_.eagleRow);
        mixInt(spec_.nSpawns);
        for (int i = 0; i < spec_.nSpawns; ++i) mixInt(spec_.spawnCol[i] * 64 + spec_.spawnRow[i]);
        mixInt(spec_.versus ? 1 + spec_.vsLayers : 0);
        for (int t = 0; t < 2; ++t) mixInt(spec_.vsEagleCol[t] * 64 + spec_.vsEagleRow[t]);
    }
    mixInt(enemyEagleAlive_ ? enemyEagleHp_ + 1 : 0);
    mixInt(bomb_.phase);
    mixInt(bomb_.countdown);
    mixInt(bomb_.raidT);
    mixInt(shake_);
    for (int i = 0; i < kMaxPlayers; ++i) {
        mixInt(playerFreeze_[i]);
        mixInt(stealTarget_[i]);
    }
    for (int i = 0; i <= lastTank_ && i < kMaxTanks; ++i) { mixInt(ghostT_[i]); mixInt(speedT_[i]); mixInt(portalCd_[i]); }
    mixInt(enemyGhostT_);
    for (const RoyaleItem& it : rItems_) { mixInt(it.on ? 1 : 0); mixInt(it.x); mixInt(it.y); mixInt(it.id); }
    mixInt(mineSeq_);
    for (const Heli& h : heli_) { mixInt(h.on ? 1 : 0); mixInt(h.owner); mixInt(h.x); mixInt(h.y); mixInt(h.state); mixInt(h.timer); mixInt(h.life); mixInt(h.target); mixInt(h.dir); mixInt(h.acc); }
    for (const Mine& m : mines_) { mixInt(m.on ? 1 : 0); mixInt(m.owner); mixInt(m.col); mixInt(m.row); mixInt(m.age); mixInt(m.seq); }
    for (const Cannon& cn : cannons_) { mixInt(cn.on ? 1 : 0); mixInt(cn.col); mixInt(cn.row); mixInt(cn.hp); mixInt(cn.cd); }
    for (const CannonShot& sh : cshots_) { mixInt(sh.on ? 1 : 0); mixInt(sh.x); mixInt(sh.y); mixInt(sh.dx); mixInt(sh.dy); }
    mixInt(portal_.on ? 1 : 0); mixInt(portal_.uses);
    for (int i = 0; i < 2; ++i) { mixInt(portal_.col[i]); mixInt(portal_.row[i]); }
    for (const Boom& b : booms_) { mixInt(b.t); mixInt(b.x); mixInt(b.y); }
    for (int i = 0; i < nLava_; ++i) {                    // la lava (fuentes, celdas y relojes) tambien tiene que ser igual en todos
        mixInt(lava_[i].phase);
        mixInt(lava_[i].timer);
        mixInt(lava_[i].placed);
        mixInt(lava_[i].n);
        for (int k = 0; k < lava_[i].n; ++k) mixInt(lava_[i].flow[k].col * 4096 + lava_[i].flow[k].row * 64 + lava_[i].flow[k].timer);
    }
    for (int i = 0; i <= lastTank_ && i < kMaxTanks; ++i) mixInt(lavaTick_[i]);
    return h;
}

int Battle::BulletOwner(int b) const {
    if (!wide_) {
        return (b & 0x06) == 0 ? (b & 1) : -1;
    }
    if (b < nPlayers_) return b;
    if (b >= kSecondBulletBase && b < kSecondBulletBase + nPlayers_) return b - kSecondBulletBase;
    if (b >= allyBegin_ && b <= lastTank_) return allyOwner_[b];     // las balas de un aliado cuentan para su jugador
    return -1;
}

int Battle::BulletShooter(int b) const {
    if (!wide_) return b & 0x07;
    return b >= kSecondBulletBase ? b - kSecondBulletBase : b;
}

bool Battle::BulletActive(int b) const { return (BSTATUS(b) & 0xF0) == 0x40; }

void Battle::DebugFire(int tank) {
    Sub_E08C_fire_bullet(tank);
}

void Battle::DebugBareEagle() {
    R(ram_shovel_timer) = 0;
    DrawBareBase();
}

void Battle::DebugAllyScenario() {
    DebugBareEagle();
    SpawnAllies(0);
    DebugClearEnemies();
    for (int i = 0; i < 3; ++i) {
        const int e = DebugSpawnEnemy(i % 2, false);
        if (e >= 0) SetTankPos(e, 80 + i * 90, 40 + i * 12);
    }
    R(ram_clock_timer) = 0x0A;                             // los enemigos quedan quietos un rato (los aliados no se congelan)
}

void Battle::DebugKillAllEnemies() {
    BonusHandler(4, 0);
}

void Battle::DebugRespawnPlayer(int player) {
    if (LIVES(player) == 0) {
        LIVES(player) = 1;
    }
    Sub_E363_tank_spawn_handler(player);
}

void Battle::DebugSetTankFlags(int i, u8 flags) {
    TFLAGS(i) = flags;
}

void Battle::DebugClearEnemies() {
    for (int x = nPlayers_; x <= lastEnemy_; ++x) {
        TFLAGS(x) = 0;
        TTYPE(x) = 0;
    }
}

} // namespace nes
