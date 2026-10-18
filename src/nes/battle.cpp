#include "battle.h"
#include <cstdio>

#include <algorithm>
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
    }
}

// sub_D6B3_fill_buffer_with_tiles (00:D6B3)
void Battle::Sub_D6B3_fill_buffer_with_tiles(int col, int row, const u8* tiles) {
    const uint16_t p = Sub_D5FB_calculate_pointer(col, row);
    for (int i = 0; tiles[i] != 0xFF; ++i) {
        M(p + i) = tiles[i];
        if (wide_ && p + i < static_cast<int>(sizeof(steelHp_))) {
            steelHp_[p + i] = 0;
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
    Sub_D635_calculate_nametable_attribute(tx, ty, tbl_DABB_nametable_attribute[tileId]);

    const uint16_t p = Sub_D5FB_calculate_pointer(tx & 0xFE, ty & 0xFE);
    const int x4 = tileId * 4;
    Sub_D784_write_tile(p, tbl_DACB_block_data[x4 + 0]);
    Sub_D784_write_tile(p + 1, tbl_DACB_block_data[x4 + 1]);
    Sub_D784_write_tile(p + Stride(), tbl_DACB_block_data[x4 + 2]);
    Sub_D784_write_tile(p + Stride() + 1, tbl_DACB_block_data[x4 + 3]);
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
    Sub_D635_calculate_nametable_attribute(12 + ShiftX() / 8 + quadX * 2, 24 + ShiftY() / 8 + quadY * 2, pal);
}

// sub_CAF5_draw_default_base (00:CAF5)
void Battle::Sub_CAF5_draw_default_base() {
    const int c = 12 + ShiftX() / 8;
    const int rs = ShiftY() / 8;
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
    const int c = 14 + ShiftX() / 8;
    const int rs = ShiftY() / 8;
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
    const int c = 12 + ShiftX() / 8;
    const int rs = ShiftY() / 8;
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
    const int c = 14 + ShiftX() / 8;
    const int rs = ShiftY() / 8;
    Sub_D6B3_fill_buffer_with_tiles(c, 26 + rs, tbl_D3AB_tiles___destroyed_eagle___line1);
    Sub_D6B3_fill_buffer_with_tiles(c, 27 + rs, tbl_D3AE_tiles___destroyed_eagle___line2);
}

// sub_D7CC_create_default_stage_field (00:D7CC) con los parametros de
// sub_C9B0: todo gris indestructible ($11) y el campo de 26x26 mosaicos vacio.
void Battle::Sub_D7CC_create_default_stage_field() {
    std::memset(steelHp_, 0, sizeof(steelHp_));
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
            Sub_D80B_write_block_tiles_and_attribute(blocks[row][col] & 0x0F, col * 0x10, 0x10 + row * 0x10);
        }
    }
    if (strip && kOpenTopStrip) {
        for (int col = 0; col < kWideBlocksX; ++col) {
            Sub_D80B_write_block_tiles_and_attribute(strip[col] & 0x0F, col * 0x10, 0x00);     // la franja de arriba (y = 0..15)
        }
    }
    if (IsVersus()) {                                    // batalla vs: una aguila por equipo, cada una con su fortaleza de hierro
        enemyEagleAlive_ = false;
        enemyEagleHp_ = 0;
        bossSpawned_ = 0;
        vsWinner_ = -1;
        for (int t = 0; t < 2; ++t) {
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
    nPlayers_ = wide_ ? (players < 1 ? 1 : (players > 4 ? 4 : players)) : 2;
    for (int i = 0; i < 4; ++i) present_[i] = i < nPlayers_;
    diffPlayers_ = nPlayers_;
    lastEnemy_ = wide_ ? nPlayers_ + kWideEnemySlots - 1 : con_max_tanks;
    allyBegin_ = lastEnemy_ + 1;
    lastTank_ = wide_ ? lastEnemy_ + kWideAllySlots : con_max_tanks;
    nBullets_ = wide_ ? 24 : 10;
    std::memset(allyOwner_, 0, sizeof(allyOwner_));
    {
        static const int kTotals[4] = {20, 20, 36, 48};          // 1 y 2 jugadores: los 20 de la ROM
        enemyTotal_ = wide_ ? kTotals[diffPlayers_ - 1] * (10 + tier_) / 10 : 20;   // +10% por escalon de dificultad
    }
    R(ram_game_mode) = players >= 2 ? 1 : 0;
    if (wide_) {
        SetEnemyLimitForStage();
        for (int p = 0; p < nPlayers_; ++p) {
            LIVES(p) = 3;
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
    std::memset(extraEnemy_, 0, sizeof(extraEnemy_));
    for (int& t : stealTarget_) t = -1;
    bomb_ = BombingState{};
    bomb_.countdown = (IsBossLevel() || IsVersus()) ? BombInterval() : 0;
    std::memset(vsTeam_, 0, sizeof(vsTeam_));
    vsAiTimer_[0] = vsAiTimer_[1] = 90;                  // batalla vs: los primeros tanques con IA salen al ratito
    vsSpawned_[0] = vsSpawned_[1] = 0;
    teamClock_[0] = teamClock_[1] = 0;
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
    for (int i = 0; i < (wide_ ? 16 : 8); ++i) {
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
    Nmi();
    skipRestOfScript_ = false;
    if (R(ram_pause_flag) == 0) {
        Sub_C2E6_main_battle_script();
    }
    Sub_E23B_display_bonus_on_screen();
    Sub_E0D8_bullets_status_handler();
    Sub_DEA6_tanks_handler();
    if (BTNPRESS(0) & con_btn_Start) {          // Start alterna la pausa
        R(ram_pause_flag) ^= 1;
        R(ram_sfx_pause) = R(ram_pause_flag);
    }
}

// sub_C2E6_main_battle_script (00:C2E6)
void Battle::Sub_C2E6_main_battle_script() {
    for (int p = 0; p < 4; ++p) {
        if (playerFreeze_[p] != 0) --playerFreeze_[p];     // cuenta el reloj enemigo, un cuadro por vez
    }
    Sub_E181_ice_detection();
    Sub_DB75_ice_movement();
    Sub_DBF1_tank_movement();
    Sub_E1FA_clear_tank_marks();
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
        for (int t = 0; t < 2; ++t) {
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
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        const uint16_t p = PointerPx(WX(TPOSX(x) - 8), U8(TPOSY(x) - 8));
        markPtr_[x] = p;
        TSTLO(x) = p & 0xFF;
        TSTHI(x) = (p >> 8) & 0x03;
        if (x < nPlayers_) {
            if (M(p + st + 1) == con_block_type + 0x21) {       // hielo
                PLRFLAGS(x) |= 0x80;
            } else {
                PLRFLAGS(x) &= 0x7F;
            }
        }
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
        if (!(f & 0x80) || f >= 0xE0) {
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
        if (wide_ && x < 4 && playerFreeze_[x] != 0) {
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
        if (run) {
            Sub_DC3D_tank_status_handler(x);
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
    if (x >= nPlayers_ && (TPOSX(x) & 0x07) == 0 && (TPOSY(x) & 0x07) == 0) {
        if ((Sub_D44D_generate_random_number() & 0x0F) == 0) {
            Sub_DE72_pick_goal(x);
            return;
        }
    }
    Loc_DC97_try_move(x);
}

// loc_DC97 (00:DC97): intento de avanzar 1 px con dos sondas en el frente.
void Battle::Loc_DC97_try_move(int x) {
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

    auto passable = [&](int px, int py) {
        const u8 t = M(PointerPx(px, py));
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
            return;
        }
    }
    Loc_DD11_blocked(x);
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
        const int opp = 1 - TeamOf(x);           // batalla vs: rumbo al aguila del otro equipo
        DESTX = spec_.vsEagleCol[opp] * 16 + 8;
        R(ram_enemy_destination_Y) = static_cast<u8>(spec_.vsEagleRow[opp] * 16 + 8);
    } else if (IsBossLevel()) {
        DESTX = guardX_[x];                      // niveles del aguila enemiga: se queda cerca de ella
        R(ram_enemy_destination_Y) = static_cast<u8>(guardY_[x]);
    } else {
        DESTX = 0x78 + ShiftX();                 // el aguila
        R(ram_enemy_destination_Y) = static_cast<u8>(0xD8 + ShiftY());
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
            if (TFLAGS(p) != 0) {
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
    LIVES(x)--;
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
    TTYPE(x) = 0;
    shieldHp_[x] = 0;
    if (x < nPlayers_) {
        // campo ancho: a los lados de la base, como en la version anterior (P1 a 2 casillas del aguila a la izquierda, P2 a la derecha, P3 y P4 una mas afuera)
        static const int kWidePlayerX[4] = {0x78 + 96 - 0x20, 0x78 + 96 + 0x20, 0x78 + 96 - 0x30, 0x78 + 96 + 0x30};
        TPOSX(x) = wide_ ? kWidePlayerX[x] : tbl_E47A_player_spawn_pos_X[x];
        TPOSY(x) = wide_ ? 0xD8 + ShiftY() : tbl_E47C_player_spawn_pos_Y[x];
        if (IsVersus()) {                                // batalla vs: al lado de la propia aguila, mirando al otro equipo
            const int t = playerTeam_[x];
            int k = 0;
            for (int p = 0; p < x; ++p) k += playerTeam_[p] == t ? 1 : 0;
            const int col = spec_.vsEagleCol[t] + (t == 0 ? 2 : -2);
            int row = spec_.vsEagleRow[t] + (k == 0 ? -1 : 1);
            row = row < 1 ? 1 : (row > 13 ? 13 : row);
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

// Aparecen tanques con IA de cada equipo, de a uno por punto y por turnos, hasta un maximo a la vez; la reposicion no se acaba.
void Battle::VersusSpawnHandler() {
    if (vsWinner_ >= 0) return;
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
                px = (t == 0 ? 4 : 22) * 16 + 8;
                py = ((vsSpawned_[t] & 1) ? 12 : 2) * 16 + 8;
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
        if (!(f & 0x80) || f >= 0xE0 || TeamOf(e) == t) continue;
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
            if (!(f & 0x80) || f >= 0xE0 || TeamOf(x) == shooterTeam) continue;
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
            }
            break;
        }
    }
}

// Pierde el equipo cuyo aguila cae o al que ya no le quedan jugadores con vidas.
void Battle::VersusCheckEnd() {
    if (vsWinner_ >= 0) return;
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

// Cantidad de puntos de aparicion de enemigos y su posicion (centro del bloque, en pixeles).
int Battle::SpawnPointCount() const {
    if (spec_.active && spec_.nSpawns > 0) return spec_.nSpawns;
    return IsBossLevel() ? (diffPlayers_ >= 3 ? 7 : 5) : ((wide_ && diffPlayers_ >= 3) ? 5 : 3);
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
        const int dir = TeamOf(x) == 0 ? 3 : 1;          // el equipo de la izquierda mira a la derecha y al reves
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
        static const int kAllyHp[4] = {2, 3, 4, 6};
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
        static const int kBase[4] = {1, 1, 2, 4};
        const int kind = (a >> 5) & 3;
        const int hp = kBase[kind] + (kind >= 2 ? tier_ / 2 : tier_ / 4);
        wd_.hp[x] = static_cast<u8>(hp);
        // los 2 bits bajos muestran el dano del blindado (color); el bit 2 es "parpadea con item"
        TTYPE(x) = static_cast<u8>((a & 0xFC) | (kind == 3 ? (hp - 1 < 3 ? hp - 1 : 3) : 0));
    }
    TWHEELS(x) = 0;
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
        if (!(f & 0x80) || f >= 0xE0) continue;
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
            if (AimsAtEagleAt(x, spec_.vsEagleCol[t], spec_.vsEagleRow[t], spec_.vsLayers)) continue;   // no le tira a su propia aguila
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
                bool broken = bulletLevel_[bi] >= 3;
                if (!broken) {
                    u8& hp = steelHp_[p];
                    if (hp == 0) hp = kSteelHealth;
                    const int dmg = bulletLevel_[bi] + 1;
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
        if (!(f & 0x80) || f >= 0xE0) {
            continue;
        }
        for (int b = lastEnemy_; b >= nPlayers_; --b) {
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (AbsDiffX(BPOSX(b), TPOSX(x)) >= 0x0A) continue;
            if (AbsDiff8(BPOSY(b), TPOSY(x)) >= 0x0A) continue;
            BSTATUS(b) = 0x33;
            if (wide_ && x < 4) ++playerHits_[x];                // vibracion: recibio un disparo
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
    // balas enemigas contra aliados (tienen 3 de salud; las de nivel 3 hacen doble dano)
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
                const int dmg = bulletLevel_[b] == 3 ? 2 : 1;
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
            if (wide_ && x < 4) ++playerHits_[x];                // vibracion: recibio un disparo
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
            } else if (wide_ && (IsAllyBullet(a) || IsAllyBullet(b)) && BulletOwner(a) >= 0 && BulletOwner(b) >= 0) {
                continue;                                                                   // las balas amigas no se anulan
            }
            if ((BSTATUS(b) & 0xF0) != 0x40) continue;
            if (AbsDiffX(BPOSX(b), BPOSX(a)) >= 6) continue;
            if (AbsDiff8(BPOSY(b), BPOSY(a)) >= 6) continue;
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
        static const u8 kWideBonus[8] = {0, 1, 2, 3, 4, 5, 7, 3};   // en el campo ancho una de las granadas repetidas es el item de aliados
        static const u8 kVsBonus[8] = {0, 1, 3, 4, 5, 6, 3, 0};    // batalla vs: sin pala ni aliados
        const u8 roll = Sub_D44D_generate_random_number() & 0x07;
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
        if (wide_ && x < 4) ++itemPickups_[x];
        BonusHandler(R(ram_bonus_id), x);
        return true;
    }
    if (wide_ && !IsVersus() && (R(ram_bonus_id) & 0x80) == 0 && true) {   // los enemigos tambien agarran items
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
    for (int p = 0; p < nPlayers_ && p < 4; ++p) n += present_[p] ? 1 : 0;
    return n < 1 ? 1 : n;
}

void Battle::SetPresentMask(int mask) {
    for (int p = 0; p < nPlayers_ && p < 4; ++p) {
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
    LIVES(p) = 3;
    TUPGRADE(p) = 0;
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
                    if (NearVsEagle(col, row)) bad = true;                       // ninguna bomba cerca de las dos aguilas
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

void Battle::SetEnemyKind(int x, int kind) {
    static const int kBase[4] = {1, 1, 2, 4};
    const int hp = kBase[kind] + (kind >= 2 ? tier_ / 2 : tier_ / 4);
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
        default:
            break;
    }
}

// Granada enemiga: destruye a la mitad de los jugadores vivos (redondeando hacia arriba), los mas cercanos al enemigo.
void Battle::EnemyGrenade(int x) {
    int alive[4], n = 0;
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
    const int c = 12 + ShiftX() / 8;
    const int r = 24 + ShiftY() / 8;
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
    switch (id) {
        case 0:                                          // casco
            HELMET(x) = 0x0A;
            if (wide_) shieldHp_[x] = 5;                 // aguanta 5 puntos de disparos
            break;
        case 1:                                          // reloj
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
            for (int y = lastEnemy_; y >= nPlayers_; --y) {
                const u8 f = TFLAGS(y);
                if (IsVersus() && vsTeam_[y] == playerTeam_[x]) continue;      // batalla vs: solo revienta a los tanques del otro equipo
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
        case 1: Sub_E328_hq_sprite(0xF1, 0x78 + ShiftX(), 0xD8 + ShiftY()); break;
        case 2: Sub_E328_hq_sprite(0xF5, 0x78 + ShiftX(), 0xD8 + ShiftY()); break;
        case 3: Sub_E328_hq_sprite(0xF9, 0x78 + ShiftX(), 0xD8 + ShiftY()); break;
        case 4:
        case 5: {
            R(ram_0069_00_or_10) = (a == 4) ? 0x00 : 0x10;
            Sub_E328_hq_sprite(0xD1, 0x70 + ShiftX(), 0xD0 + ShiftY());        // sub_E33E: 4 piezas
            Sub_E328_hq_sprite(0xD5, 0x80 + ShiftX(), 0xD0 + ShiftY());
            Sub_E328_hq_sprite(0xD9, 0x70 + ShiftX(), 0xE0 + ShiftY());
            Sub_E328_hq_sprite(0xDD, 0x80 + ShiftX(), 0xE0 + ShiftY());
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
    u8 palette;
    if (IsAlly(x)) {
        palette = PlayerPalette(allyOwner_[x]);          // sprite de enemigo, color del jugador que agarro el item
    } else if (x >= nPlayers_ && IsVersus()) {
        const u8 own = kColorAttr[kTeamColor[vsTeam_[x] & 1]];
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
                R(0x0004) = IsVersus() ? kColorAttr[kTeamColor[TeamOf(BulletShooter(x)) & 1]]
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

// Los aliados disparan cuando tienen un enemigo en la mira y de vez en cuando al azar.
void Battle::Sub_AllyFire() {
    if (!wide_) {
        return;
    }
    for (int x = allyBegin_; x <= lastTank_; ++x) {
        const u8 f = TFLAGS(x);
        if (!(f & 0x80) || f >= 0xE0) continue;
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
    const int offsets[4][2] = {{-32, 0}, {32, 0}, {0, -32}, {0, 32}};
    int placed = 0;
    for (int slot = allyBegin_; slot <= lastTank_ && placed < 2; ++slot) {
        if (TFLAGS(slot) != 0) continue;
        const int ox = offsets[placed][0], oy = offsets[placed][1];
        int x = cx + ox, y = cy + oy;
        x = (x + 4) & ~7;                                // alineados a 8 px: asi pueden elegir rumbo como los enemigos
        y = (y + 4) & ~7;
        x = x < 8 ? 8 : (x > 424 ? 424 : x);
        y = y < 24 ? 24 : (y > 0xE8 ? 0xE8 : y);
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
    mix(bulletLevel_, sizeof bulletLevel_);
    mix(shieldHp_, sizeof shieldHp_);
    mixInt(enemyTotal_);
    mixInt(diffPlayers_);
    for (int i = 0; i < 4; ++i) mixInt(present_[i] ? 1 : 0);
    for (int i = 0; i < 4; ++i) mixInt(playerColor_[i]);
    mixInt(level_);
    if (IsVersus()) {
        mixInt(vsWinner_ + 1);
        mixInt((vsEagleAlive_[0] ? 1 : 0) + (vsEagleAlive_[1] ? 2 : 0));
        for (int i = 0; i < 4; ++i) mixInt(playerTeam_[i]);
        mixInt(teamClock_[0]);
        mixInt(teamClock_[1]);
        for (int i = nPlayers_; i <= lastEnemy_; ++i) mixInt(vsTeam_[i]);
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
    for (int i = 0; i < 4; ++i) {
        mixInt(playerFreeze_[i]);
        mixInt(stealTarget_[i]);
    }
    return h;
}

int Battle::BulletOwner(int b) const {
    if (!wide_) {
        return (b & 0x06) == 0 ? (b & 1) : -1;
    }
    if (b < nPlayers_) return b;
    if (b >= 20 && b < 20 + nPlayers_) return b - 20;
    if (b >= allyBegin_ && b <= lastTank_) return allyOwner_[b];     // las balas de un aliado cuentan para su jugador
    return -1;
}

int Battle::BulletShooter(int b) const {
    if (!wide_) return b & 0x07;
    return b >= 20 ? b - 20 : b;
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
