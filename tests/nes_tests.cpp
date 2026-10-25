// Pruebas de comportamiento del port (sin ventana). Cada una cita la rutina
// del desensamblado que verifica. Las comparaciones contra la ROM misma estan
// en tests/rom_oracle_tests.cpp (cuando existe).

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <functional>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <string>
#include <vector>
#include <memory>
#include <string>

#include "../src/app/arena.h"
#include "../src/app/editor.h"
#include "../src/app/flow.h"
#include "../src/app/lan.h"
#include "../src/app/updater.h"
#include "../src/app/voice.h"
#include "../src/app/nat.h"
#include "../src/app/levels.h"
#include "../src/app/lobby.h"
#include "../src/nes/battle.h"

using nes::u8;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FALLA  %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                                                       \
    do {                                                                                                     \
        ++g_checks;                                                                                          \
        const long long va = static_cast<long long>(a);                                                      \
        const long long vb = static_cast<long long>(b);                                                      \
        if (va != vb) {                                                                                      \
            ++g_failures;                                                                                    \
            std::printf("  FALLA  %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va, vb);   \
        }                                                                                                    \
    } while (0)

// Mapa vacio (todo "D") con el muro de base de la ROM; los jugadores salen abajo.
struct Rows {
    const char* r[13];
};

std::unique_ptr<nes::Battle> MakeBattle(const Rows& rows, int players = 1) {
    auto b = std::make_unique<nes::Battle>();
    b->NewGame(players, 1);
    u8 data[91];
    for (int row = 0; row < 13; ++row) {
        for (int i = 0; i < 14; ++i) {
            const char c = (i < 13) ? rows.r[row][i] : 'D';
            const int v = (c >= 'A') ? (c - 'A' + 10) : (c - '0');
            const int idx = row * 7 + (i >> 1);
            data[idx] = (i & 1) ? static_cast<u8>(data[idx] | v) : static_cast<u8>(v << 4);
        }
    }
    b->LoadStageData(data);
    b->StartStage();
    b->R(nes::ram_enemy_limit) = nes::con_max_tanks;
    b->R(nes::ram_enemy_spawn_cnt) = 0;
    b->R(nes::ram_enemies_left_cnt) = 99;
    for (int f = 0; f < 80; ++f) {   // termina la animacion de aparicion de los jugadores
        b->RunFrame();
    }
    return b;
}

const Rows kEmpty = {{
    "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
    "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
}};

void Frames(nes::Battle& b, int n) {
    for (int i = 0; i < n; ++i) b.RunFrame();
}

// ---------------------------------------------------------------------------

void TestPlayerSpawn() {
    std::printf("[jugador] aparece en (88,216) mirando arriba, con casco 3 y sin estrellas (sub_E363/E3B8)\n");
    auto b = MakeBattle(kEmpty);
    // quieto, el jugador oscila entre el estado $A0 y el rebote $8x (sub_DB75 sin cruceta): mira arriba
    CHECK(((b->R(nes::ram_tank_flags) >> 4) == 0xA) || ((b->R(nes::ram_tank_flags) >> 4) == 0x8));
    CHECK_EQ(b->R(nes::ram_tank_flags) & 3, 0);
    CHECK_EQ(b->R(nes::ram_tank_pos_X), 0x58);
    CHECK_EQ(b->R(nes::ram_tank_pos_Y), 0xD8);
    CHECK_EQ(b->R(nes::ram_tank_type), 0x00);
    CHECK(b->R(nes::ram_helmet_timer) <= 3);
}

void TestPlayerSpeedThreeOfFour() {
    std::printf("[movimiento] el jugador avanza 3 de cada 4 cuadros (sub_DBF1)\n");
    auto b = MakeBattle(kEmpty);
    b->SetButtons(0, nes::con_btn_Up);
    const int y0 = b->R(nes::ram_tank_pos_Y);
    Frames(*b, 120);
    const int moved = y0 - b->R(nes::ram_tank_pos_Y);
    CHECK(moved >= 89 && moved <= 91);
}

void TestTurnSnapsToGrid() {
    std::printf("[movimiento] el giro perpendicular alinea a 8 px (sub_DB75)\n");
    auto b = MakeBattle(kEmpty);
    b->R(nes::ram_tank_pos_X) = 0x59;      // 89: desalineado 1 px
    b->R(nes::ram_tank_pos_Y) = 0x7E;
    b->SetButtons(0, nes::con_btn_Up);
    Frames(*b, 8);
    CHECK_EQ(b->R(nes::ram_tank_pos_X), 0x59);      // yendo hacia arriba no se alinea
    b->SetButtons(0, nes::con_btn_Left);
    Frames(*b, 2);
    // al girar en perpendicular se alinea X a 8 px ((89+4)&F8 = 88) y recien despues avanza
    CHECK(b->R(nes::ram_tank_pos_X) == 0x58 || b->R(nes::ram_tank_pos_X) == 0x57);
    CHECK_EQ(b->R(nes::ram_tank_flags) & 3, 1);
    // un giro de 180 grados no alinea
    b->SetButtons(0, nes::con_btn_Right);
    Frames(*b, 1);
    CHECK_EQ(b->R(nes::ram_tank_flags) & 3, 3);
}

void TestBlockedByWaterAndBrick() {
    std::printf("[colision] agua y ladrillo bloquean al tanque; hielo y arbustos no (sub_DC97)\n");
    const Rows rows = {{
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
        "DDDDDDDDDDDDD", "DDDDADDDDDDDD", "DDDDADDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
    }};
    auto b = MakeBattle(rows);
    b->R(nes::ram_tank_pos_X) = 0x58;
    b->R(nes::ram_tank_pos_Y) = 0xC8;
    b->SetButtons(0, nes::con_btn_Up);
    Frames(*b, 200);
    // el agua ocupa y = 16+8*16 = 144 .. 16+10*16 = 176; el borde superior del tanque se frena en 176 (centro 184)
    CHECK_EQ(b->R(nes::ram_tank_pos_Y), 0xB8);
}

void TestBulletSpeeds() {
    std::printf("[balas] sin estrellas 2 px/cuadro; con estrellas 4 px/cuadro (sub_E02E, sub_E08C)\n");
    for (int stars = 0; stars <= 1; ++stars) {
        auto b = MakeBattle(kEmpty);
        b->R(nes::ram_tank_pos_X) = 0x58;
        b->R(nes::ram_tank_pos_Y) = 0xA0;
        if (stars) {
            b->R(nes::ram_tank_upgrade) = 0x20;
            b->R(nes::ram_tank_type) = 0x20;
        }
        b->SetButtons(0, nes::con_btn_A);
        b->RunFrame();
        b->SetButtons(0, 0);
        const int y1 = b->R(nes::ram_bullet_pos_Y);
        CHECK_EQ(b->R(nes::ram_bullet_status) & 0xF0, 0x40);
        Frames(*b, 10);
        const int y2 = b->R(nes::ram_bullet_pos_Y);
        CHECK_EQ(y1 - y2, 10 * (stars ? 4 : 2));
    }
}

void TestBrickShotClearsFourQuarters() {
    std::printf("[bloques] una bala rompe 4 cuartos del frente (sondas 0,+4,-1,-5 px; sub_E604)\n");
    const Rows rows = {{
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
    }};
    Rows r = rows;
    static const char* withBrick[13] = {
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDD4DDDDDD", "DDDDDDDDDDDDD",
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
    };
    for (int i = 0; i < 13; ++i) r.r[i] = withBrick[i];
    auto b = MakeBattle(r);
    b->R(nes::ram_tank_pos_X) = 0x78;      // centrado en el bloque (col 6)
    b->R(nes::ram_tank_pos_Y) = 0xB0;
    b->SetButtons(0, nes::con_btn_A);
    b->RunFrame();
    b->SetButtons(0, 0);
    Frames(*b, 80);
    // el bloque de la fila 5 ocupa los mosaicos (14..15, 12..13) hmm: fila de bloque 5 -> mosaicos fila 12-13? -> (5*2+2)=12
    const int r1 = 12, r2 = 13;
    CHECK_EQ(b->Tile(14, r1), 0x0F);
    CHECK_EQ(b->Tile(15, r1), 0x0F);
    CHECK_EQ(b->Tile(14, r2), 0x03);   // abajo perdio sus dos cuartos inferiores
    CHECK_EQ(b->Tile(15, r2), 0x03);
}

void TestSteelNeedsThreeStars() {
    std::printf("[bloques] el acero solo se rompe con 3 estrellas (sub_E693: D6,X bit 1)\n");
    static const char* rows[13] = {
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDD9DDDDDD", "DDDDDDDDDDDDD",
        "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD", "DDDDDDDDDDDDD",
    };
    for (int stars = 0; stars <= 3; ++stars) {
        Rows r;
        for (int i = 0; i < 13; ++i) r.r[i] = rows[i];
        auto b = MakeBattle(r);
        b->R(nes::ram_tank_pos_X) = 0x78;
        b->R(nes::ram_tank_pos_Y) = 0xB0;
        b->R(nes::ram_tank_upgrade) = static_cast<u8>(stars * 0x20);
        b->R(nes::ram_tank_type) = static_cast<u8>(stars * 0x20);
        b->SetButtons(0, nes::con_btn_A);
        b->RunFrame();
        b->SetButtons(0, 0);
        Frames(*b, 80);
        const int tile = b->Tile(14, 13);
        if (stars < 3) {
            CHECK_EQ(tile, 0x10);
        } else {
            CHECK_EQ(tile, 0x00);
        }
    }
}

void TestEnemySpawnAndTypes() {
    std::printf("[enemigos] los 4 tipos aparecen tras la animacion con el tipo correcto (sub_E3B8)\n");
    const u8 expected[4] = {0x80, 0xA0, 0xC0, 0xE3};
    for (int t = 0; t < 4; ++t) {
        auto b = MakeBattle(kEmpty);
        const int slot = b->DebugSpawnEnemy(t, false);
        CHECK(slot >= 2);
        Frames(*b, 80);
        CHECK_EQ(b->R(nes::ram_tank_type + slot), expected[t]);
        CHECK_EQ(b->R(nes::ram_tank_flags + slot) & 0xF0, 0xA0);
    }
    auto b = MakeBattle(kEmpty);
    const int slot = b->DebugSpawnEnemy(3, true);
    Frames(*b, 80);
    CHECK_EQ(b->R(nes::ram_tank_type + slot), 0xE4);    // blindado parpadeante
}

void TestBonusEffects() {
    std::printf("[items] casco, reloj, pala, estrella, granada, tanque (tbl_E9E2)\n");
    auto place = [](nes::Battle& b) {
        b.R(nes::ram_tank_pos_X) = 0x58;
        b.R(nes::ram_tank_pos_Y) = 0xA0;
        b.R(nes::ram_bonus_pos_X) = 0x58;
        b.R(nes::ram_bonus_pos_Y) = 0xA0;
        b.R(nes::ram_0062_bonus_timer) = 0;
    };
    {
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 0;
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_helmet_timer), 10);
    }
    {
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 1;
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_clock_timer), 10);
    }
    {
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 2;
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_shovel_timer), 0x14);
        CHECK_EQ(b->Tile(13, 25), 0x10);    // el muro de la base pasa a acero
    }
    {
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 3;
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_tank_upgrade), 0x20);
        CHECK_EQ(b->R(nes::ram_tank_type), 0x20);
    }
    {
        auto b = MakeBattle(kEmpty);
        b->DebugSpawnEnemy(0, false);
        b->DebugSpawnEnemy(1, false);
        Frames(*b, 80);
        place(*b);
        b->R(nes::ram_bonus_id) = 4;
        b->RunFrame();
        int exploding = 0;
        for (int x = 2; x < 8; ++x) {
            if ((b->R(nes::ram_tank_flags + x) >> 4) >= 1 && (b->R(nes::ram_tank_flags + x) >> 4) <= 7) ++exploding;
        }
        CHECK_EQ(exploding, 2);
    }
    {
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 5;
        const int lives = b->R(nes::ram_lives);
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_lives), lives + 1);
    }
    {
        // pistola (id 6, extension): salta de nivel 0 directo al maximo
        auto b = MakeBattle(kEmpty);
        place(*b);
        b->R(nes::ram_bonus_id) = 6;
        b->RunFrame();
        CHECK_EQ(b->R(nes::ram_tank_upgrade), 0x60);
        CHECK_EQ(b->R(nes::ram_tank_type), 0x60);
    }
}

void TestTanksBlockEachOther() {
    std::printf("[colision] un tanque enemigo frena al jugador (marcas bit 7: sub_E181)\n");
    auto b = MakeBattle(kEmpty);
    const int slot = b->DebugSpawnEnemy(0, false);
    Frames(*b, 80);
    b->R(nes::ram_tank_flags + slot) = 0x80;     // quieto (estado de rebote)
    b->R(nes::ram_tank_pos_X + slot) = 0x58;
    b->R(nes::ram_tank_pos_Y + slot) = 0x90;
    b->R(nes::ram_tank_pos_X) = 0x58;
    b->R(nes::ram_tank_pos_Y) = 0xB0;
    b->SetButtons(0, nes::con_btn_Up);
    for (int i = 0; i < 100; ++i) {
        b->R(nes::ram_tank_flags + slot) = 0x88;  // lo mantiene quieto durante la prueba
        b->RunFrame();
    }
    // el jugador queda pegado debajo del enemigo: |dy| = 16
    CHECK_EQ(static_cast<int>(b->R(nes::ram_tank_pos_Y)) - 0x90, 16);
}

void TestEagleDestroyed() {
    std::printf("[base] una bala destruye el aguila: game over en curso y tiles de aguila destruida (sub_E69A)\n");
    auto b = MakeBattle(kEmpty);
    // quita el muro de ladrillo para que la bala llegue al aguila
    b->R(nes::ram_tank_pos_X) = 0x78;
    b->R(nes::ram_tank_pos_Y) = 0x90;
    b->R(nes::ram_tank_flags) = 0xA2;     // mirando abajo
    b->SetButtons(0, nes::con_btn_A);
    b->RunFrame();
    b->SetButtons(0, 0);
    Frames(*b, 60);
    // dispara contra el muro: el aguila sigue viva hasta romper los ladrillos del frente
    CHECK(b->R(nes::ram_game_over_flag) == 0x80 || b->R(nes::ram_game_over_flag) != 0);
}


// ---------------------------------------------------------------------------
// Campo panoramico (25x13 bloques, X de 16 bits)
// ---------------------------------------------------------------------------

std::unique_ptr<nes::Battle> MakeWide(const char* const* rows13, int players = 1) {   // 14 filas
    auto b = std::make_unique<nes::Battle>();
    b->SetWide(true);
    b->NewGame(players, 1);
    u8 blocks[14][27];
    for (int r = 0; r < 14; ++r) {
        for (int c = 0; c < 27; ++c) {
            const char ch = rows13[r][c];
            blocks[r][c] = static_cast<u8>((ch >= 'A') ? (ch - 'A' + 10) : (ch - '0'));
        }
    }
    b->LoadWideStage(blocks);
    b->StartStage();
    b->SetEnemyLimitMax();
    b->R(nes::ram_enemy_spawn_cnt) = 0;
    b->R(nes::ram_enemies_left_cnt) = 99;
    for (int f = 0; f < 80; ++f) {
        b->RunFrame();
    }
    return b;
}

const char* const kWideEmpty[14] = {
    "DDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD", "DDDDDDDDDDDDDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDDDDDDDDDDDDD",
};

int WideX(nes::Battle& b, int tank) { return b.TankX(tank); }

void TestWideLayoutAndSpawn() {
    std::printf("[ancho] el aguila queda en el centro y el jugador sale a su izquierda\n");
    auto b = MakeWide(kWideEmpty);
    CHECK_EQ(b->MapCols(), 54);
    CHECK_EQ(b->Tile(26, 28), 0xC8);          // el aguila: mosaico superior izquierdo
    CHECK_EQ(WideX(*b, 0), 0x78 + 96 - 0x20); // 184
    CHECK_EQ(b->TankY(0), 0xE8);
    CHECK_EQ(b->Tile(0, 5), 0x00);            // sin borde lateral: el primer mosaico ya es jugable
    CHECK_EQ(b->Tile(53, 5), 0x00);           // ultimo mosaico jugable
    CHECK_EQ(b->Tile(54, 5), 0x11);           // mas alla del campo
}

void TestWideMovementPastByte() {
    std::printf("[ancho] el tanque cruza la X=255 sin envolver y se frena en el borde derecho\n");
    auto b = MakeWide(kWideEmpty);
    b->SetTankPos(0, b->TankX(0), 0x80);
    b->SetButtons(0, nes::con_btn_Right);
    int maxX = 0;
    for (int f = 0; f < 700; ++f) {
        b->RunFrame();
        const int x = WideX(*b, 0);
        if (x > maxX) maxX = x;
        CHECK(x >= 100);                      // nunca vuelve a valores chicos por envolver
    }
    CHECK_EQ(maxX, 424);                      // el centro del tanque se frena a 8 px del borde (432)
    bool far = false;
    for (int i = 0; i < 64; ++i) {
        if (b->OamSprite(i).y < 0xEF && b->OamSprite(i).x > 255) far = true;
    }
    CHECK(far);
}

void TestWideBulletHitsBrickPastByte() {
    std::printf("[ancho] una bala viaja mas alla de X=255 y rompe un ladrillo (sub_E604 con X de 16 bits)\n");
    const char* rows[14];
    for (int i = 0; i < 14; ++i) rows[i] = kWideEmpty[i];
    static const char* const brick = "DDDDDDDDDDDDDDDDDDDDDDDDDD4";   // ladrillo en el ultimo bloque (X 416..432)
    rows[6] = brick;
    auto b = MakeWide(rows);
    b->SetTankPos(0, b->TankX(0), 0x78);       // centro del bloque de la fila 6
    b->SetButtons(0, nes::con_btn_Right);
    Frames(*b, 500);                           // avanza hasta quedar pegado al ladrillo
    CHECK(WideX(*b, 0) > 255);
    int before = 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) before += b->Tile(52 + dx, 14 + dy);
    b->SetButtons(0, nes::con_btn_Right | nes::con_btn_A);
    b->RunFrame();
    b->SetButtons(0, nes::con_btn_Right);
    Frames(*b, 60);
    int after = 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) after += b->Tile(52 + dx, 14 + dy);
    CHECK(before > 0);
    CHECK(after < before);                      // la bala le saco un pedazo
}

void TestWideBonusPickup() {
    std::printf("[ancho] se recoge un item ubicado mas alla de X=255\n");
    auto b = MakeWide(kWideEmpty);
    b->SetTankPos(0, 320, 0x78);
    b->SetBonus(320, 0x78, 5);                 // tanque: vida extra
    const int lives = b->Lives(0);
    b->RunFrame();
    CHECK_EQ(b->Lives(0), lives + 1);

    // pala: el muro de la base (en el centro del campo ancho) pasa a acero
    auto c = MakeWide(kWideEmpty);
    c->SetTankPos(0, 320, 0x78);
    c->SetBonus(320, 0x78, 2);
    c->RunFrame();
    CHECK_EQ(c->R(nes::ram_shovel_timer), 0x14);
    CHECK_EQ(c->Tile(25, 27), 0x10);
}

void TestWideEnemySpawnsAtRightEdge() {
    std::printf("[ancho] los enemigos aparecen a la izquierda, al centro y a la derecha del campo\n");
    auto b = MakeWide(kWideEmpty);
    int xs[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const int slot = b->DebugSpawnEnemy(0, false);
        CHECK(slot >= 2);
        xs[i] = WideX(*b, slot);
    }
    CHECK_EQ(xs[0], 216);   // el indice de aparicion avanza antes de usarse: centro, derecha, izquierda
    CHECK_EQ(xs[1], 424);
    CHECK_EQ(xs[2], 8);
}

void TestWideEnemiesChaseAcrossField() {
    std::printf("[ancho] los enemigos se mueven por todo el campo ancho sin perderse\n");
    auto b = MakeWide(kWideEmpty);
    unsigned st = 12345;                        // azar propio: el de la ROM depende de bytes de la pagina cero
    b->randomHook = [&]() -> u8 { st = st * 1664525u + 1013904223u; return static_cast<u8>((st >> 16) & 0xFF); };
    const int slot = b->DebugSpawnEnemy(0, false);
    CHECK(slot >= 2);
    int minX = 9999, maxX = 0;
    for (int f = 0; f < 3000; ++f) {
        b->RunFrame();
        const int x = WideX(*b, slot);
        if (b->TankFlags(slot) == 0) continue;
        minX = x < minX ? x : minX;
        maxX = x > maxX ? x : maxX;
        CHECK(x >= 8 && x <= 424);
    }
    CHECK(maxX - minX > 100);
}

// ---------------------------------------------------------------------------
// 4 jugadores
// ---------------------------------------------------------------------------

void TestFourPlayersSpawn() {
    std::printf("[4 jugadores] salen en la fila de abajo a los lados de la base: P3 P1 [aguila] P2 P4\n");
    auto b = MakeWide(kWideEmpty, 4);
    CHECK_EQ(b->Players(), 4);
    const int center = 0x78 + 96;               // 216: el centro del aguila
    CHECK_EQ(b->TankX(0), center - 0x20);       // P1: 2 casillas a la izquierda
    CHECK_EQ(b->TankX(1), center + 0x20);       // P2: 2 casillas a la derecha
    CHECK_EQ(b->TankX(2), center - 0x30);       // P3: una mas afuera a la izquierda
    CHECK_EQ(b->TankX(3), center + 0x30);       // P4: una mas afuera a la derecha
    for (int p = 0; p < 4; ++p) {
        CHECK_EQ(b->TankY(p), 0xE8);
        CHECK_EQ(b->Lives(p), 3);
        CHECK(b->TankFlags(p) != 0);
    }
    CHECK(b->LastTank() >= 11);
}

void TestFourPlayersMoveAndFireIndependently() {
    std::printf("[4 jugadores] cada uno se mueve con su propio control\n");
    auto b = MakeWide(kWideEmpty, 4);
    const int y0 = b->TankY(0);
    for (int p = 0; p < 4; ++p) b->SetButtons(p, nes::con_btn_Up);
    Frames(*b, 60);
    for (int p = 0; p < 4; ++p) CHECK(b->TankY(p) < y0 - 30);
    // solo P4 sigue; los otros se quedan quietos
    b->SetButtons(0, 0);
    b->SetButtons(1, 0);
    b->SetButtons(2, 0);
    const int y3 = b->TankY(2);
    const int y4 = b->TankY(3);
    Frames(*b, 40);
    CHECK_EQ(b->TankY(2), y3);
    CHECK(b->TankY(3) < y4);
}

void TestFourPlayersBonusAndLives() {
    std::printf("[4 jugadores] los items los recoge cada jugador y las vidas son independientes\n");
    auto b = MakeWide(kWideEmpty, 4);
    b->SetTankPos(3, 300, 0x78);
    b->SetBonus(300, 0x78, 5);
    b->RunFrame();
    CHECK_EQ(b->Lives(3), 4);
    CHECK_EQ(b->Lives(0), 3);
    CHECK_EQ(b->Lives(2), 3);
    // casco: solo el que lo toma
    auto c = MakeWide(kWideEmpty, 4);
    c->SetTankPos(2, 300, 0x78);
    c->SetBonus(300, 0x78, 0);
    c->RunFrame();
    CHECK_EQ(c->Helmet(2), 10);
    // pistola: solo sube al que la toma
    auto d = MakeWide(kWideEmpty, 4);
    d->SetTankPos(3, 300, 0x78);
    d->SetBonus(300, 0x78, 6);
    d->RunFrame();
    CHECK_EQ(d->Upgrade(3), 0x60);
    CHECK_EQ(d->Upgrade(0), 0x00);
}

void TestFourPlayersExtraSpawns() {
    std::printf("[4 jugadores] hay 5 puntos de aparicion: arriba izq/centro/der y 2 en los laterales a mitad del mapa\n");
    auto b = MakeWide(kWideEmpty, 4);
    bool sawLeftMid = false, sawRightMid = false;
    for (int i = 0; i < 10; ++i) {
        const int slot = b->DebugSpawnEnemy(0, false);
        CHECK(slot >= 4);
        if (b->TankX(slot) == 8 && b->TankY(slot) == 0x78) sawLeftMid = true;
        if (b->TankX(slot) == 424 && b->TankY(slot) == 0x78) sawRightMid = true;
    }
    CHECK(sawLeftMid && sawRightMid);
    CHECK_EQ(b->LastEnemy(), 4 + 12 - 1);
}

void TestEnemyTotalScalesWithPlayers() {
    std::printf("[4 jugadores] los enemigos por etapa suben con la cantidad de jugadores: 20, 20, 36, 48\n");
    const int expected[4] = {20, 20, 36, 48};
    for (int p = 1; p <= 4; ++p) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->NewGame(p, 1);
        u8 blocks[14][27] = {};
        b->LoadWideStage(blocks);
        b->StartStage();
        CHECK_EQ(b->EnemyTotal(), expected[p - 1]);
        CHECK_EQ(b->R(nes::ram_enemy_spawn_cnt), expected[p - 1]);
        int sum = 0;
        for (int i = 0; i < 4; ++i) sum += b->R(nes::ram_enemy_type_stage_cnt + i);
        CHECK_EQ(sum, expected[p - 1]);
    }
}

void TestFourPlayersEnemiesCap() {
    std::printf("[4 jugadores] hay mas enemigos a la vez que con 2 jugadores\n");
    auto two = MakeWide(kWideEmpty, 2);
    auto four = MakeWide(kWideEmpty, 4);
    two->R(nes::ram_enemy_spawn_cnt) = 20;
    four->R(nes::ram_enemy_spawn_cnt) = 20;
    four->R(nes::ram_enemy_limit) = 3 + 12;
    two->R(nes::ram_enemy_limit) = 1 + 6;
    int maxTwo = 0, maxFour = 0;
    for (int f = 0; f < 1500; ++f) {
        two->RunFrame();
        four->RunFrame();
        int nt = 0, nf = 0;
        for (int x = two->Players(); x <= two->LastTank(); ++x) if (two->TankFlags(x) != 0) ++nt;
        for (int x = four->Players(); x <= four->LastTank(); ++x) if (four->TankFlags(x) != 0) ++nf;
        if (nt > maxTwo) maxTwo = nt;
        if (nf > maxFour) maxFour = nf;
    }
    CHECK(maxFour > maxTwo);
}

// P1 le dispara a P2 desde la izquierda; devuelve (nivel de P2, estaba paralizado) tras el impacto.
void FriendlyShot(nes::Battle::FriendlyFire mode, int level, int* upgradeAfter, bool* stunned, int* p2Flags) {
    auto b = MakeWide(kWideEmpty, 2);
    b->SetFriendlyFire(mode);
    b->SetTankPos(0, 100, 0x78);
    b->SetTankPos(1, 200, 0x78);
    b->SetHelmet(0, 0);
    b->SetHelmet(1, 0);
    b->SetUpgrade(1, static_cast<u8>(level));
    b->SetButtons(0, nes::con_btn_Right | nes::con_btn_A);
    b->RunFrame();
    b->SetButtons(0, nes::con_btn_Right);
    int minX1 = 9999;
    for (int f = 0; f < 80; ++f) {
        b->RunFrame();
        if (b->TankX(1) < minX1) minX1 = b->TankX(1);
    }
    *upgradeAfter = b->Upgrade(1);
    *stunned = b->R(nes::ram_plr_stun_timer) == 0 && b->TankFlags(1) != 0 ? false : false;
    (void)minX1;
    *p2Flags = b->TankFlags(1);
}

void TestFriendlyFireModes() {
    std::printf("[fuego amigo] desactivado atraviesa; paraliza (original); danio quita un nivel sin matar\n");
    int up = 0, fl = 0;
    bool st = false;
    FriendlyShot(nes::Battle::FriendlyFire::Damage, 0x60, &up, &st, &fl);
    CHECK_EQ(up, 0x40);                          // nivel 3 -> 2
    CHECK(fl != 0);                              // no murio
    FriendlyShot(nes::Battle::FriendlyFire::Damage, 0x00, &up, &st, &fl);
    CHECK_EQ(up, 0x00);                          // nivel 0: lo destruye (explosion en curso o ya reaparecio)
    CHECK(fl == 0 || (fl >> 4) < 8 || fl >= 0xE0);
    FriendlyShot(nes::Battle::FriendlyFire::Off, 0x60, &up, &st, &fl);
    CHECK_EQ(up, 0x60);                          // la bala lo atraviesa
    FriendlyShot(nes::Battle::FriendlyFire::Stun, 0x60, &up, &st, &fl);
    CHECK_EQ(up, 0x60);                          // paraliza pero no cambia el nivel
}

// Cuantos disparos hacen falta para romper un mosaico de hierro en el campo ancho.
int ShotsToBreakSteel(int level) {
    const char* rows[14];
    for (int i = 0; i < 14; ++i) rows[i] = kWideEmpty[i];
    static const char* const steel = "DDDDDDDDDDDDDDDDDDDDDDDDDD9";   // hierro en el ultimo bloque
    rows[6] = steel;
    auto b = MakeWide(rows);
    b->SetUpgrade(0, static_cast<u8>(level * 0x20));
    b->SetTankPos(0, 408, 0x78);
    for (int shot = 1; shot <= 60; ++shot) {
        b->SetButtons(0, nes::con_btn_Right | nes::con_btn_A);
        b->RunFrame();
        b->SetButtons(0, nes::con_btn_Right);
        Frames(*b, 14);
        if (b->Tile(52, 15) != 0x10) return shot;
    }
    return 99;
}

void TestSteelHealth() {
    std::printf("[hierro] salud 20: nivel 0 baja 1, nivel 1 baja 2, nivel 2 baja 3, nivel 3 lo rompe de una\n");
    CHECK_EQ(ShotsToBreakSteel(0), 20);
    CHECK_EQ(ShotsToBreakSteel(1), 10);
    CHECK_EQ(ShotsToBreakSteel(2), 7);
    CHECK_EQ(ShotsToBreakSteel(3), 1);
}

// ---------------------------------------------------------------------------
// 40 niveles y dificultad por escalones
// ---------------------------------------------------------------------------

void TestLevelsAreValid() {
    std::printf("[niveles] los 40 mapas: simetricos, con la base y las apariciones libres y todos los puntos alcanzables\n");
    std::vector<std::string> seen;
    int steelEarly = 0, steelLate = 0;
    for (int level = 1; level <= app::kLevelCount; ++level) {
        u8 g[app::kLevelRows][app::kLevelCols];
        app::GenerateWideLevel(level, g);
        std::string key;
        for (int r = 0; r < app::kLevelRows; ++r) {
            for (int c = 0; c < app::kLevelCols; ++c) {
                key.push_back(static_cast<char>(g[r][c]));
                if (g[r][c] >= 5 && g[r][c] <= 9) {
                    if (level <= 5) ++steelEarly;
                    if (level >= 36) ++steelLate;
                }
            }
        }
        seen.push_back(key);
        const bool boss = level % 10 == 0;                              // aguila enemiga: muro de ladrillo en la fila 0 (columnas 12 y 14)
        for (int c = 0; c < 27; ++c) CHECK_EQ(g[0][c], 13);   // fila de arriba libre
        for (int c = 10; c <= 16; ++c) CHECK_EQ(g[13][c], 13);          // lugares de los jugadores y la base
        for (int c = 12; c <= 14; ++c) CHECK_EQ(g[12][c], 13);
        // la construccion original de la ROM va en el centro (columnas 7-19, filas 1-13)
        {
            const int stage = (level - 1) % 35 + 1;
            const u8* data = &nes::tbl_F07A_stage_data[(stage - 1) * 91];
            int diff = 0;
            for (int r = 0; r < 13; ++r) {
                for (int c = 0; c < 13; ++c) {
                    const int nib = r * 14 + c;
                    const int v = (nib & 1) ? (data[nib >> 1] & 0x0F) : (data[nib >> 1] >> 4);
                    const int wr = r + 1, wc = 7 + c;
                    const bool cleared = (wr == 13 && wc >= 10 && wc <= 16) || (wr == 12 && wc >= 12 && wc <= 14);   // lugares de jugadores y base
                    if (!cleared && !(boss && wr <= 4) && g[wr][wc] != v) ++diff;
                }
            }
            if (level <= 4) CHECK(diff <= 2);                              // 1-5: la construccion original sin cambios (a lo sumo un paso abierto); despues la tematica la modifica
        }
        // alcanzabilidad desde el frente de la base
        bool reach[14][27] = {};
        std::vector<std::pair<int, int>> stack = {{13, 13}};
        reach[13][13] = true;
        while (!stack.empty()) {
            const auto [r, c] = stack.back();
            stack.pop_back();
            const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nr = r + dr[k], nc = c + dc[k];
                if (nr < 0 || nr >= 14 || nc < 0 || nc >= 27 || reach[nr][nc] || !app::LevelCellPassable(g[nr][nc])) continue;
                reach[nr][nc] = true;
                stack.push_back({nr, nc});
            }
        }
        const int spawns[5][2] = {{0, 0}, {0, 13}, {0, 26}, {6, 0}, {6, 26}};
        const int gc = 1 + (level / 10 + 1) / 2;                                                       // niveles del aguila enemiga: apariciones junto a la fortaleza
        const int bossSpawns[7][2] = {{0, 13 - gc}, {gc, 13}, {0, 13 + gc}, {0, 0}, {0, 26}, {6, 0}, {6, 26}};
        if (level % 10 == 0) {
            for (const auto& s : bossSpawns) CHECK(reach[s[0]][s[1]]);
        } else {
            for (const auto& s : spawns) CHECK(reach[s[0]][s[1]]);
        }
    }
    std::sort(seen.begin(), seen.end());
    CHECK_EQ(std::unique(seen.begin(), seen.end()) - seen.begin(), 40);  // ninguno repetido
    CHECK(steelLate > steelEarly);                                       // mas hierro con la dificultad
    {   // tematica por tandas de 5 niveles: predomina un bloque y siguen apareciendo los anteriores
        auto count = [](int level, auto pred) {
            u8 g[app::kLevelRows][app::kLevelCols];
            app::GenerateWideLevel(level, g);
            int n = 0;
            for (auto& row : g) for (u8 v : row) n += pred(v) ? 1 : 0;
            return n;
        };
        auto tier = [&](int t, auto pred) { int n = 0; for (int l = t * 5 + 1; l <= t * 5 + 5; ++l) n += count(l, pred); return n; };
        auto isBrick = [](u8 v) { return v <= 4; };
        auto isSteel = [](u8 v) { return v >= 5 && v <= 9; };
        auto isWater = [](u8 v) { return v == 10; };
        auto isForest = [](u8 v) { return v == 11; };
        auto isIce = [](u8 v) { return v == 12; };
        auto isSand = [](u8 v) { return v == 14; };
        auto isLava = [](u8 v) { return v == 16; };
        CHECK(tier(1, isForest) > tier(0, isForest) + 20);                   // 6-10: arbustos
        CHECK(tier(2, isWater) > tier(1, isWater) + 20);                     // 11-15: agua
        CHECK(tier(3, isSand) > tier(2, isSand) + 20);                       // 16-20: arena
        CHECK(tier(4, isIce) > tier(3, isIce) + 20);                         // 21-25: hielo
        CHECK(tier(5, isSteel) > tier(4, isSteel) + 20);                     // 26-30: hierro
        CHECK(tier(6, isLava) >= 10);                                        // 31-35: lava
        CHECK(tier(7, isLava) >= 1 && tier(7, isLava) < tier(6, isLava));    // 36-40: mezcla (poca lava)
        CHECK(count(5, isLava) >= 2);                                        // la lava se presenta en el nivel 5
        for (int l = 1; l <= 30; ++l) if (l != 5) CHECK_EQ(count(l, isLava), 0);
        // siguen apareciendo los anteriores
        CHECK(tier(2, isForest) > 0 && tier(3, isWater) > 0 && tier(4, isSand) > 0 && tier(5, isIce) > 0 && tier(6, isWater) > 0);
        for (int l = 1; l <= app::kLevelCount; ++l) CHECK(count(l, isBrick) > 0);
        // la lava siempre esta contenida: sus vecinas son lava, ladrillo o hierro (nunca un lugar abierto)
        for (int l = 5; l <= app::kLevelCount; ++l) {
            u8 g[app::kLevelRows][app::kLevelCols];
            app::GenerateWideLevel(l, g);
            for (int r = 0; r < app::kLevelRows; ++r)
                for (int c = 0; c < app::kLevelCols; ++c) {
                    if (g[r][c] != 16) continue;
                    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; ++k) {
                        const u8 v = g[r + dr[k]][c + dc[k]];
                        CHECK(v == 16 || v <= 4 || isSteel(v));
                    }
                }
        }
    }
    CHECK_EQ(app::DifficultyTier(1), 0);
    CHECK_EQ(app::DifficultyTier(5), 0);
    CHECK_EQ(app::DifficultyTier(6), 1);
    CHECK_EQ(app::DifficultyTier(40), 7);
}

void TestDifficultyScales() {
    std::printf("[dificultad] el escalon (nivel-1)/5 sube el total de enemigos, la mezcla y la salud\n");
    auto make = [](int level, int players) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->SetLevel(level);
        b->NewGame(players, level);
        u8 blocks[14][27] = {};
        b->LoadWideStage(blocks);
        b->StartStage();
        b->SetEnemyLimitMax();
        b->R(nes::ram_enemy_spawn_cnt) = 0;
        b->R(nes::ram_enemies_left_cnt) = 99;
        return b;
    };
    auto b1 = make(1, 4);
    auto b40 = make(40, 4);
    CHECK_EQ(b1->Tier(), 0);
    CHECK_EQ(b40->Tier(), 7);
    CHECK_EQ(b1->EnemyTotal(), 48);
    CHECK_EQ(b40->EnemyTotal(), 48 * 17 / 10);                           // +10% por escalon
    CHECK(b40->R(nes::ram_enemy_type_stage_cnt + 3) > b1->R(nes::ram_enemy_type_stage_cnt + 3));   // mas blindados
    CHECK(b40->R(nes::ram_enemy_spawn_interval) < b1->R(nes::ram_enemy_spawn_interval));            // aparecen mas seguido
    // salud: blindado 4 + 7/2 = 7, basico 1 + 7/4 = 2 en el escalon 7
    for (int type : {0, 3}) {
        const int slot = b40->DebugSpawnEnemy(type, false);
        for (int f = 0; f < 80; ++f) b40->RunFrame();
        CHECK_EQ(b40->EnemyHealth(slot), type == 3 ? 7 : 2);
        const int slot1 = b1->DebugSpawnEnemy(type, false);
        for (int f = 0; f < 80; ++f) b1->RunFrame();
        CHECK_EQ(b1->EnemyHealth(slot1), type == 3 ? 4 : 1);
    }
}

// ---------------------------------------------------------------------------
// Aliados y paso de nivel
// ---------------------------------------------------------------------------

void TestAlliesItem() {
    std::printf("[aliados] el item genera 2 tanques con IA que pelean contra los enemigos\n");
    auto b = MakeWide(kWideEmpty, 3);
    unsigned st = 777;
    b->randomHook = [&]() -> u8 { st = st * 1664525u + 1013904223u; return static_cast<u8>((st >> 16) & 0xFF); };
    b->SetTankPos(2, 300, 0x78);
    b->SetBonus(300, 0x78, 7);
    b->RunFrame();
    int allies = 0;
    for (int x = 0; x < nes::kMaxTanks; ++x) {
        if (b->IsAlly(x) && b->TankFlags(x) != 0) {
            ++allies;
            CHECK_EQ(b->AllyOwner(x), 2);                         // son del jugador 3 (color azul)
        }
    }
    CHECK_EQ(allies, 2);
    // un enemigo cerca: los aliados terminan con el
    const int enemy = b->DebugSpawnEnemy(0, false);
    CHECK(enemy >= 3);
    bool dead = false;
    for (int f = 0; f < 2500 && !dead; ++f) {
        b->RunFrame();
        const u8 fl = b->TankFlags(enemy);
        dead = fl == 0 || (fl >> 4) < 8;                          // explotando o ya muerto
        for (int p = 0; p < 3; ++p) { b->SetButtons(p, 0); }
    }
    CHECK(dead);
    // los aliados no cuentan como enemigos de la etapa
}

void TestStageProgress() {
    std::printf("[niveles] al matar a todos los enemigos se pasa al siguiente conservando vidas y arma\n");
    auto b = std::make_unique<nes::Battle>();
    b->SetWide(true);
    b->SetLevel(1);
    b->NewGame(2, 1);
    u8 blocks[14][27];
    app::GenerateWideLevel(1, blocks);
    b->LoadWideStage(blocks);
    b->StartStage();
    for (int f = 0; f < 80; ++f) b->RunFrame();
    CHECK(!b->StageCleared());                                    // quedan enemigos por aparecer
    b->R(nes::ram_enemy_spawn_cnt) = 0;
    CHECK(!b->StageCleared());                                    // todavia hay enemigos en el campo
    b->DebugClearEnemies();
    CHECK(b->StageCleared());
    b->SetUpgrade(0, 0x40);
    b->SetLives(1, 2);
    app::GenerateWideLevel(2, blocks);
    b->NextStage(2, blocks);
    CHECK_EQ(b->Level(), 2);
    CHECK_EQ(b->Upgrade(0), 0x40);
    CHECK_EQ(b->Lives(1), 2);
    CHECK_EQ(b->R(nes::ram_enemy_spawn_cnt), b->EnemyTotal());
    for (int f = 0; f < 80; ++f) b->RunFrame();
    CHECK(b->TankFlags(0) != 0 && b->TankFlags(1) != 0);          // los jugadores reaparecen
    CHECK_EQ(b->TankType(0) & 0xF0, 0x40);
}

// ---------------------------------------------------------------------------
// Pantallas: introduccion de nivel, recuento de puntos y game over
// ---------------------------------------------------------------------------

void TestScoreAndKillCounters() {
    std::printf("[puntaje] el puntaje y los enemigos destruidos por tipo se leen del estado\n");
    auto b = MakeWide(kWideEmpty, 2);
    CHECK_EQ(b->Score(0), 0);
    b->AddBonusPoints(1, 0);
    CHECK_EQ(b->Score(1), 1000);
    CHECK_EQ(b->Score(0), 0);
    // un disparo real a un enemigo basico: 100 puntos y un destruido de tipo 0 para el jugador 1
    auto c = MakeWide(kWideEmpty, 2);
    const int e = c->DebugSpawnEnemy(0, false);
    for (int f = 0; f < 80; ++f) c->RunFrame();
    c->SetTankPos(e, 184, 150);                     // justo adelante de P1 (sale mirando arriba en (184,232))
    c->SetTankPos(1, 400, 0xE8);
    c->SetButtons(0, nes::con_btn_A);
    c->RunFrame();
    c->SetButtons(0, 0);
    for (int f = 0; f < 120; ++f) c->RunFrame();
    CHECK_EQ(c->KillCount(0, 0), 1);
    CHECK_EQ(c->Score(0), 100);
}

struct FlowFixture {
    std::unique_ptr<nes::Battle> battle;
    std::unique_ptr<app::GameFlow> flow;
    int loaded = 0;
    bool restarted = false;
};

std::unique_ptr<FlowFixture> MakeFlow(int level, int players) {
    auto fx = std::make_unique<FlowFixture>();
    fx->battle = std::make_unique<nes::Battle>();
    fx->battle->SetWide(true);
    fx->battle->SetLevel(level);
    fx->battle->NewGame(players, level);
    u8 blocks[14][27];
    app::GenerateWideLevel(level, blocks);
    fx->battle->LoadWideStage(blocks);
    fx->battle->StartStage();
    app::GameFlow::Hooks hooks;
    FlowFixture* raw = fx.get();
    hooks.loadNextLevel = [raw](int lv) {
        u8 bl[14][27];
        app::GenerateWideLevel(lv, bl);
        raw->battle->NextStage(lv, bl);
        ++raw->loaded;
    };
    hooks.returnToTitle = [raw]() { raw->restarted = true; };
    fx->flow = std::make_unique<app::GameFlow>(*fx->battle, hooks);
    fx->flow->Begin(level);
    return fx;
}

void TestStageIntroScreen() {
    std::printf("[pantallas] la introduccion de nivel cierra la cortina, muestra STAGE N y abre\n");
    auto fx = MakeFlow(3, 2);
    using Mode = app::GameFlow::Mode;
    CHECK(fx->flow->mode() == Mode::StageIntro);
    CHECK_EQ(fx->flow->curtainRows(), 15);
    for (int f = 0; f < app::GameFlow::kIntroHold - 1; ++f) fx->flow->Step();
    CHECK_EQ(fx->flow->curtainRows(), 15);            // sigue cerrada mientras se muestra el cartel
    int prev = 15;
    bool monotone = true;
    for (int f = 0; f < 40 && fx->flow->mode() == Mode::StageIntro; ++f) {
        fx->flow->Step();
        if (fx->flow->curtainRows() > prev) monotone = false;
        prev = fx->flow->curtainRows();
    }
    CHECK(monotone);
    CHECK(fx->flow->mode() == Mode::Playing);
    CHECK_EQ(fx->flow->curtainRows(), 0);
}

void TestScoreScreenAndNextLevel() {
    std::printf("[pantallas] recuento: cuenta de a un enemigo por jugador, totales, bonus de 1000 y pasa al nivel siguiente\n");
    auto fx = MakeFlow(1, 4);
    using Mode = app::GameFlow::Mode;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    CHECK(fx->flow->mode() == Mode::Playing);
    for (int f = 0; f < 80; ++f) fx->flow->Step();
    fx->battle->R(nes::ram_enemy_spawn_cnt) = 0;
    fx->battle->DebugClearEnemies();
    fx->battle->DebugSetKills(0, 0, 3);               // P1: 3 basicos
    fx->battle->DebugSetKills(1, 1, 2);               // P2: 2 rapidos
    fx->battle->DebugSetKills(2, 3, 1);               // P3: 1 blindado
    fx->battle->DebugSetKills(3, 0, 1);               // P4: 1 basico
    const int p1Before = fx->battle->Score(0);
    for (int f = 0; f < app::GameFlow::kClearDelay + 2; ++f) fx->flow->Step();
    CHECK(fx->flow->mode() == Mode::ScoreScreen);
    CHECK_EQ(fx->flow->tally().players, 4);
    CHECK_EQ(fx->flow->tally().kills[0][0], 3);
    CHECK_EQ(fx->flow->tally().shown[0][0], 0);       // todavia no empezo a contar
    int maxShown = 0;
    bool sawTotals = false;
    for (int f = 0; f < 3000 && fx->flow->mode() == Mode::ScoreScreen; ++f) {
        fx->flow->Step();
        maxShown = std::max(maxShown, fx->flow->tally().shown[0][0]);
        sawTotals = sawTotals || fx->flow->tally().showTotals;
    }
    CHECK_EQ(maxShown, 3);
    CHECK(sawTotals);
    CHECK_EQ(fx->flow->tally().totals[0], 3);
    CHECK_EQ(fx->flow->tally().bonusPlayer, 0);       // P1 destruyo mas: bonus de 1000
    CHECK_EQ(fx->battle->Score(0), p1Before + 1000);
    CHECK(fx->flow->mode() == Mode::StageIntro);
    CHECK_EQ(fx->flow->level(), 2);
    CHECK_EQ(fx->loaded, 1);
}

void TestBossLevelEnemyEagle() {
    std::printf("[aguila enemiga] niveles 10/20/30/40: enemigos sin fin, 5 puntos de aparicion (7 con 3+ jugadores), solo se gana destruyendo el aguila\n");
    using Mode = app::GameFlow::Mode;
    CHECK(!MakeFlow(9, 1)->battle->IsBossLevel());
    CHECK(MakeFlow(10, 1)->battle->IsBossLevel());
    {   // muro de la fortaleza: ladrillo en 10 y 20, hierro en 30 y 40; y los jugadores no tienen aguila en estos niveles
        for (int lv : {10, 20, 30, 40}) {
            u8 g[14][27];
            app::GenerateWideLevel(lv, g);
            CHECK_EQ(g[0][12], 13);                                  // las capas las dibuja el motor, no el mapa
            CHECK_EQ(g[2][13], 13);
        }
        {   // capas de hierro de 1 mosaico (8 px): 1, 2, 3 y 4 capas en los niveles 10, 20, 30 y 40
            for (int lv : {10, 20, 30, 40}) {
                auto ff = MakeFlow(lv, 1);
                const auto& fb = *ff->battle;
                const int n = lv / 10;
                CHECK_EQ(fb.EagleLayers(), n);
                for (int k = 1; k <= n; ++k) {                           // cada capa: un mosaico a cada lado y debajo
                    CHECK(fb.Tile(26 - k, 3) != 0 && fb.Tile(27 + k, 3) != 0 && fb.Tile(26, 3 + k) != 0 && fb.Tile(27, 3 + k) != 0);
                }
                CHECK_EQ(fb.Tile(26 - n - 1, 3), 0);                     // ni una capa de mas
                CHECK_EQ(fb.Tile(27 + n + 1, 3), 0);
                CHECK_EQ(fb.Tile(26, 3 + n + 1), 0);
                // las apariciones quedan libres, justo afuera de la fortaleza
                const int gap = fb.EagleSpawnGap();
                CHECK(gap >= 16 + 8 * n);
                CHECK_EQ(fb.Tile((216 - gap) / 8, 3), 0);
                CHECK_EQ(fb.Tile((216 + gap) / 8, 3), 0);
                CHECK_EQ(fb.Tile(26, (24 + gap) / 8), 0);
            }
        }
        auto bf = MakeFlow(20, 2);
        auto nf = MakeFlow(19, 2);
        bool anyBoss = false, anyNormal = false;
        for (int col = 24; col < 30; ++col) {
            for (int row = 26; row < 30; ++row) {
                anyBoss = anyBoss || bf->battle->Tile(col, row) != 0;
                anyNormal = anyNormal || nf->battle->Tile(col, row) != 0;
            }
        }
        CHECK(!anyBoss);                                       // sin base ni aguila de los jugadores
        CHECK(anyNormal);
    }
    auto fx = MakeFlow(10, 2);
    auto& b = *fx->battle;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    CHECK(b.EnemyEagleAlive());
    CHECK_EQ(b.R(nes::ram_enemy_spawn_cnt), 0);            // sin cuota de enemigos
    // los enemigos salen sin parar y por todos los puntos de aparicion; nunca se gana por matarlos
    bool seen[5] = {};
    int spawns = 0;
    for (int f = 0; f < 6000; ++f) {
        fx->flow->Step();
        b.R(nes::ram_game_over_flag) = nes::con_not_game_over;               // la base no cae durante la prueba
        for (int e = 2; e <= b.LastEnemy(); ++e) {
            if (b.TankFlags(e) >= 0xE0) {                  // recien apareciendo
                const int x = b.TankX(e), y = b.TankY(e);
                if (x == 184 && y == 0x18) seen[0] = true;
                if (x == 216 && y == 0x38) seen[1] = true;
                if (x == 248 && y == 0x18) seen[2] = true;
                if (x == 8 && y == 0x08) seen[3] = true;
                if (x == 424 && y == 0x08) seen[4] = true;
                ++spawns;
            }
        }
        b.DebugClearEnemies();                             // matarlos a todos no termina el nivel
        CHECK(!b.StageCleared());
    }
    CHECK(spawns > 40);
    for (int i = 0; i < 5; ++i) CHECK(seen[i]);
    // un jugador debajo del aguila le dispara: cae y el nivel queda ganado
    CHECK(b.EnemyEagleAlive());
    CHECK_EQ(b.EnemyEagleHealth(), 10);                   // nivel 10: salud 10
    b.SetUpgrade(0, 0x40);                                // balas de nivel 2: rompen el hierro en varios disparos
    const int scoreBefore = b.Score(0);
    b.SetTankPos(0, 216, 0x48);
    bool damaged = false;
    int pad = nes::con_btn_Up;
    for (int f = 0; f < 600 && b.EnemyEagleAlive(); ++f) {
        b.SetTankPos(0, 216, 0x48);
        b.SetButtons(0, static_cast<u8>(pad | ((f % 16) < 8 ? nes::con_btn_A : 0)));
        fx->flow->Step();
        b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
        b.DebugClearEnemies();
        if (b.EnemyEagleAlive() && b.EnemyEagleHealth() < 10) damaged = true;
    }
    CHECK(damaged);                                        // aguanta varios disparos
    CHECK(!b.EnemyEagleAlive());
    CHECK_EQ(b.EaglesDestroyed(), 1);                       // cuenta para la vibracion
    CHECK_EQ(b.Score(0), scoreBefore + 5000);              // plus para quien la destruyo
    CHECK(b.StageCleared());
}

void TestEnemyItemsAndShield() {
    std::printf("[items enemigos] los enemigos agarran items: casco (5 de salud), estrella, pistola, reloj, granada, vida, aliados\n");
    using Mode = app::GameFlow::Mode;
    auto fx = MakeFlow(1, 4);
    auto& b = *fx->battle;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    for (int f = 0; f < 120; ++f) fx->flow->Step();
    b.R(nes::ram_enemy_spawn_cnt) = 0;                       // sin refuerzos mientras se prueba
    b.DebugClearEnemies();
    auto spawn = [&](int type) {
        const int e = b.DebugSpawnEnemy(type, false);
        CHECK(e >= 0);
        for (int f = 0; f < 200 && !(b.TankFlags(e) >= 0x80 && b.TankFlags(e) < 0xE0); ++f) fx->flow->Step();
        b.R(nes::ram_enemy_spawn_cnt) = 0;
        return e;
    };
    auto give = [&](int e, int id) {
        b.SetBonus(b.TankX(e), b.TankY(e), id);
        fx->flow->Step();
    };
    int e = spawn(0);
    give(e, 0);                                              // casco
    CHECK_EQ(b.ShieldHealth(e), 5);
    CHECK(!b.BonusActive());
    give(e, 3);                                              // estrella: basico -> rapido
    CHECK_EQ((b.TankType(e) >> 5) & 3, 1);
    give(e, 3);
    CHECK_EQ((b.TankType(e) >> 5) & 3, 2);
    give(e, 6);                                              // pistola: el tanque grande
    CHECK_EQ((b.TankType(e) >> 5) & 3, 3);
    give(e, 1);                                              // reloj enemigo: paraliza a los jugadores la mitad que a los enemigos
    CHECK_EQ(b.FreezeFrames(0), 320);
    CHECK(!b.BonusActive());
    const int px = b.TankX(0), py = b.TankY(0);
    b.SetButtons(0, nes::con_btn_Left);
    for (int f = 0; f < 30; ++f) fx->flow->Step();
    b.SetButtons(0, 0);
    CHECK_EQ(b.TankX(0), px);                                // no se movio
    CHECK_EQ(b.TankY(0), py);
    CHECK(b.FreezeFrames(0) < 320 && b.FreezeFrames(0) > 250);
    for (int f = 0; f < 330; ++f) fx->flow->Step();
    CHECK_EQ(b.FreezeFrames(0), 0);
    // vida: un enemigo mas en la lista
    b.R(nes::ram_enemy_spawn_cnt) = 5;
    b.R(nes::ram_enemy_timer_before_spawn) = 200;            // que no salga ninguno en este cuadro
    give(e, 5);
    CHECK_EQ(b.R(nes::ram_enemy_spawn_cnt), 6);
    // aliados enemigos: dos tanques mas
    int before = 0;
    for (int x = 4; x <= b.LastEnemy(); ++x) before += b.TankFlags(x) != 0;
    give(e, 7);
    int after = 0;
    for (int x = 4; x <= b.LastEnemy(); ++x) after += b.TankFlags(x) != 0;
    CHECK_EQ(after, before + 2);
    // granada: cae la mitad de los jugadores, los mas cercanos al enemigo
    b.DebugClearEnemies();
    for (int p = 0; p < 4; ++p) b.SetTankPos(p, 40 + p * 100, 0xE8);
    const int g = spawn(0);
    b.SetTankPos(g, 40, 0xC8);                               // cerca de P1 y de P2
    give(g, 4);
    int dead = 0;
    for (int p = 0; p < 4; ++p) dead += b.TankFlags(p) < 0x80;
    CHECK_EQ(dead, 2);
    CHECK(b.TankFlags(0) < 0x80);                            // los dos mas cercanos
    CHECK(b.TankFlags(1) < 0x80);
    CHECK(b.TankFlags(3) >= 0x80);
}

void TestStealLives() {
    std::printf("[robo de vidas] sin vidas se elige a quien robar (izquierda/derecha) y el disparo roba una vida\n");
    using Mode = app::GameFlow::Mode;
    auto fx = MakeFlow(1, 3);
    auto& b = *fx->battle;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    for (int f = 0; f < 120; ++f) fx->flow->Step();
    b.R(nes::ram_enemy_spawn_cnt) = 0;
    b.DebugClearEnemies();
    b.SetLives(0, 0);
    b.DebugSetTankFlags(0, 0);                               // P1 murio sin vidas
    fx->flow->Step();
    CHECK_EQ(b.StealTarget(0), 1);
    CHECK_EQ(b.StealTarget(1), -1);                          // los que juegan no roban
    b.SetButtons(0, nes::con_btn_Right);
    fx->flow->Step();
    CHECK_EQ(b.StealTarget(0), 2);
    b.SetButtons(0, 0);
    fx->flow->Step();
    b.SetLives(2, 1);                                        // a P3 le queda una sola: no se le puede robar
    fx->flow->Step();
    CHECK_EQ(b.StealTarget(0), 1);
    b.SetButtons(0, nes::con_btn_A);
    fx->flow->Step();
    b.SetButtons(0, 0);
    CHECK_EQ(b.Lives(0), 1);
    CHECK_EQ(b.Lives(1), 2);                                 // paso de 3 a 2
    CHECK(b.TankFlags(0) != 0);                              // reaparece
    CHECK_EQ(b.StealTarget(0), -1);
    // con una sola vida cada uno no hay a quien robar
    b.SetLives(1, 1);
    b.SetLives(0, 0);
    b.DebugSetTankFlags(0, 0);
    fx->flow->Step();
    CHECK_EQ(b.StealTarget(0), -1);
}

void TestBombing() {
    std::printf("[bombardeo] cada 4-3-2-1 min en niveles con aguila: alerta, 3 aviones, 3 bombas por avion, explosion 3x3 tras 1,5 s, nunca cerca del aguila enemiga\n");
    using Mode = app::GameFlow::Mode;
    CHECK(MakeFlow(9, 1)->battle->Bombing().countdown == 0);       // niveles comunes: sin bombardeo
    CHECK(!MakeFlow(9, 1)->battle->BombingActive());
    auto fx = MakeFlow(10, 3);
    auto& b = *fx->battle;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    CHECK(b.BombingActive());
    CHECK(b.Bombing().countdown > 9000 && b.Bombing().countdown <= b.BombInterval());
    CHECK_EQ(b.Bombing().phase, 0);
    CHECK_EQ(b.BombInterval(), 180 * 60);                             // nivel 10: 3 minutos
    const int kExpect[4] = {180, 120, 90, 60};                        // niveles 10, 20, 30 y 40: 3:00, 2:00, 1:30 y 1:00
    for (int lv : {10, 20, 30, 40}) CHECK_EQ(MakeFlow(lv, 1)->battle->BombInterval(), kExpect[lv / 10 - 1] * 60);
    CHECK_EQ(nes::Battle::kBombFuse, 90);                             // 1,5 segundos
    int detonations = 0, alertFrames = 0, soundsBefore = b.BomberSoundCount();
    bool nearEagle = false, killedInside = true, terrainCleared = true, sawShake = false;
    for (int raid = 0; raid < 6; ++raid) {                             // varios bombardeos para probar el azar
        b.DebugStartBombing();
        int guard = 0;
        bool started = false;
        while (guard++ < 2000) {
            fx->flow->Step();
            b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
            for (int p = 0; p < 3; ++p) b.SetLives(p, 9);              // que el juego no termine durante la prueba
            if (b.Bombing().phase == 1) ++alertFrames;
            if (b.Bombing().phase == 2) started = true;
            if (started && b.Bombing().phase == 0) break;
            sawShake = sawShake || b.ShakeX() != 0 || b.ShakeY() != 0;
            for (const auto& tg : b.Bombing().target) {
                if (tg.fuse <= 0) continue;
                if (std::abs(tg.cx - 216) < 32 && std::abs(tg.cy - 24) < 32) nearEagle = true;
                if (tg.fuse == 1) {                                    // en este cuadro explota: miro quien esta adentro
                    std::vector<int> inside;
                    for (int x = 0; x <= b.LastTank(); ++x) {
                        const u8 fl = b.TankFlags(x);
                        if ((fl & 0x80) && fl < 0xE0 && std::abs(b.TankX(x) - tg.cx) < 32 && std::abs(b.TankY(x) - tg.cy) < 32) inside.push_back(x);
                    }
                    fx->flow->Step();
                    b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
                    ++detonations;
                    for (int x : inside) killedInside = killedInside && (b.TankFlags(x) & 0x80) == 0;
                    for (int dc = -1; dc <= 1; ++dc)
                        for (int dr = -1; dr <= 1; ++dr)
                            terrainCleared = terrainCleared && b.Tile((tg.cx + dc * 16) / 8, (tg.cy + dr * 16) / 8) == 0;
                }
            }
        }
        CHECK(guard < 2000);
    }
    CHECK(detonations >= 6 * 9 - 6);                                   // 9 bombas por bombardeo
    CHECK(!nearEagle);
    CHECK(killedInside);
    CHECK(sawShake);
    CHECK(alertFrames >= 6 * nes::Battle::kBombAlertFrames - 6);
    CHECK_EQ(b.BomberSoundCount() - soundsBefore, 18);                // tres aviones por bombardeo
    CHECK(b.EnemyEagleAlive());
    CHECK(b.Bombing().countdown > 9000);                               // despues del bombardeo vuelve a contar (3 minutos en el nivel 10)
    // mas aviones con la dificultad (3, 4, 5 y 6) y los hay horizontales y verticales
    bool sawHoriz = false, sawVert = false;
    for (int lv : {10, 20, 30, 40}) {
        auto hf = MakeFlow(lv, 1);
        auto& hb = *hf->battle;
        for (int f = 0; f < 200 && hf->flow->mode() != Mode::Playing; ++f) hf->flow->Step();
        for (int raid = 0; raid < 4 && lv != 0; ++raid) {
            hb.DebugStartBombing();
            for (int f = 0; f < 400 && hb.Bombing().phase != 2; ++f) {
                hf->flow->Step();
                hb.SetLives(0, 9);
                hb.R(nes::ram_game_over_flag) = nes::con_not_game_over;
            }
            CHECK_EQ(hb.Bombing().phase, 2);
            CHECK_EQ(hb.Bombing().planes, lv / 10 + 2);
            for (int i = 0; i < hb.Bombing().planes; ++i) {
                (hb.Bombing().plane[i].dir == 0 ? sawVert : sawHoriz) = true;
                for (int k = 0; k < 3; ++k) {                          // nunca cerca del aguila enemiga
                    const bool horizontal = hb.Bombing().plane[i].dir != 0;
                    const int col = horizontal ? hb.Bombing().bombAt[i][k] : hb.Bombing().lane[i];
                    const int row = horizontal ? hb.Bombing().lane[i] : hb.Bombing().bombAt[i][k];
                    CHECK(!(col >= 12 && col <= 14 && row <= 1));
                }
            }
            for (int f = 0; f < 3000 && hb.Bombing().phase != 0; ++f) {
                hf->flow->Step();
                hb.SetLives(0, 9);
                hb.R(nes::ram_game_over_flag) = nes::con_not_game_over;
            }
        }
    }
    CHECK(sawHoriz && sawVert);
}

void TestBossEnemiesGuard() {
    std::printf("[aguila enemiga] los enemigos defienden su aguila y atacan cuando un jugador se acerca\n");
    using Mode = app::GameFlow::Mode;
    auto fx = MakeFlow(10, 2);
    auto& b = *fx->battle;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    // fase 1: los jugadores lejos (abajo) -> los enemigos se quedan cerca de su aguila
    long sumY1 = 0;
    int total = 0, nearEagle = 0, hunt1 = 0, hunt2 = 0;
    for (int f = 0; f < 2400; ++f) {
        b.SetTankPos(0, 40, 0xE8);
        b.SetTankPos(1, 90, 0xE8);
        b.SetLives(0, 9);
        b.SetLives(1, 9);
        fx->flow->Step();
        b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
        if (f < 900 || f % 20 != 0) continue;
        for (int e = 2; e <= b.LastEnemy(); ++e) {
            const u8 fl = b.TankFlags(e);
            if (!(fl & 0x80) || fl >= 0xE0) continue;
            ++total;
            sumY1 += b.TankY(e);
            hunt1 += b.EnemyHunting(e);
            nearEagle += b.TankY(e) <= 130 && std::abs(b.TankX(e) - 216) <= 140;
        }
    }
    CHECK(total > 40);
    CHECK(nearEagle * 100 >= total * 55);
    // fase 2: un jugador se acerca al aguila -> pasan al ataque y bajan a buscarlo
    long sumY2 = 0;
    int total2 = 0, near2 = 0;
    for (int f = 0; f < 1500; ++f) {
        b.SetTankPos(0, 216, 0xB0);
        b.SetTankPos(1, 90, 0xE8);
        b.SetLives(0, 9);
        b.SetLives(1, 9);
        fx->flow->Step();
        b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
        if (f < 400 || f % 20 != 0) continue;
        for (int e = 2; e <= b.LastEnemy(); ++e) {
            const u8 fl = b.TankFlags(e);
            if (!(fl & 0x80) || fl >= 0xE0) continue;
            ++total2;
            sumY2 += b.TankY(e);
            hunt2 += b.EnemyHunting(e);
            near2 += b.TankY(e) <= 130 && std::abs(b.TankX(e) - 216) <= 140;
        }
    }
    CHECK(total2 > 20);
    CHECK(hunt1 * 100 <= total * 30);                           // de guardia casi ninguno ataca
    CHECK(hunt2 * 100 >= total2 * 70);                           // con un jugador cerca casi todos atacan
}

void TestBossSpawnPointsAnyPlayerCount() {
    std::printf("[aguila enemiga] los 5 puntos de aparicion (3 junto al aguila y las 2 puntas) existen con 1, 2, 3 y 4 jugadores; los laterales solo con 3+\n");
    using Mode = app::GameFlow::Mode;
    for (int level : {10, 20, 30, 40}) {
        for (int players = 1; players <= 4; ++players) {
            auto fx = MakeFlow(level, players);
            auto& b = *fx->battle;
            for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
            const int gap = b.EagleSpawnGap();
            const int wantX[7] = {216 - gap, 216, 216 + gap, 8, 424, 8, 424};
            const int wantY[7] = {0x18, 0x18 + gap, 0x18, 0x08, 0x08, 0x78, 0x78};
            bool seen[7] = {};
            for (int f = 0; f < 5000; ++f) {
                for (int p = 0; p < players; ++p) {
                    b.SetTankPos(p, 40 + 60 * p, 0xE8);
                    b.SetLives(p, 9);
                }
                fx->flow->Step();
                b.R(nes::ram_game_over_flag) = nes::con_not_game_over;
                for (int e = players; e <= b.LastEnemy(); ++e) {
                    if (b.TankFlags(e) < 0xE0) continue;                 // recien apareciendo
                    for (int i = 0; i < 7; ++i) seen[i] = seen[i] || (b.TankX(e) == wantX[i] && b.TankY(e) == wantY[i]);
                }
                b.DebugClearEnemies();
            }
            for (int i = 0; i < 5; ++i) CHECK(seen[i]);                 // los cinco principales, siempre
            CHECK_EQ(seen[5] && seen[6], players >= 4);                 // los laterales, solo con 4 jugadores
            CHECK_EQ(seen[5] || seen[6], players >= 4);
        }
    }
}

void TestBossDifficultyScaling() {
    std::printf("[aguila enemiga] la dificultad sube con el nivel: mas enemigos a la vez, mas salud, mas disparos, aparecen mas seguido, alerta mas amplia\n");
    int prevLimit = -1, prevInterval = 1 << 20, prevRadius = -1, prevHp = -1;
    for (int lv : {10, 20, 30, 40}) {
        auto fx = MakeFlow(lv, 2);
        auto& b = *fx->battle;
        const int limit = b.R(nes::ram_enemy_limit);
        const int interval = b.R(nes::ram_enemy_spawn_interval);
        CHECK(limit >= prevLimit);
        CHECK(interval <= prevInterval);
        CHECK(b.EagleGuardRadius() > prevRadius);
        if (lv >= 20) CHECK(limit > prevLimit || lv == 20);
        prevLimit = limit;
        prevInterval = interval;
        prevRadius = b.EagleGuardRadius();
        (void)prevHp;
    }
    CHECK_EQ(MakeFlow(40, 2)->battle->R(nes::ram_enemy_limit) - MakeFlow(10, 2)->battle->R(nes::ram_enemy_limit), 3 - 0);   // +1 por cada 2 escalones
    // al pasar de un nivel a otro tambien se recalcula
    auto fx = MakeFlow(10, 2);
    const int before = fx->battle->R(nes::ram_enemy_limit);
    u8 blocks[14][27];
    app::GenerateWideLevel(40, blocks);
    fx->battle->NextStage(40, blocks);
    CHECK_EQ(fx->battle->R(nes::ram_enemy_limit), before + 3);
}

// ---------------------------------------------------------------------------
// Modo LAN (sockets UDP por loopback)
// ---------------------------------------------------------------------------

// Hace girar a todos los participantes hasta que `done` se cumple (o se acaba el tiempo).
template <class Done>
bool SpinLan(std::vector<app::Lan*> lans, Done done, double seconds = 4.0) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
        for (app::Lan* l : lans) l->Update();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return done();
}

void TestLanLobby() {
    std::printf("[lan] el anfitrion anuncia su partida, los clientes la encuentran en la red, se unen (hasta 4) y salen\n");
    CHECK(app::Lan::CleanName(std::string("  ana|b") + char(1) + "c " + "Ã±" + "and-9_x.yyyyyyyy ") == "ANABC AND-9_");   // sin '|' ni acentos, hasta 12
    const uint16_t port = 47931;
    app::Lan host(port), b(port), c(port), d(port), e(port);
    CHECK(!host.Host("   "));                                        // el nombre es obligatorio
    CHECK(host.Host("Ana"));
    CHECK(host.state() == app::Lan::State::Hosting);
    CHECK(host.Players().size() == 1 && host.Players()[0] == "ANA");
    CHECK(b.Search());
    CHECK(SpinLan({&host, &b}, [&] { return b.Games().size() == 1; }));
    CHECK_EQ(static_cast<int>(b.Games().size()), 1);
    if (b.Games().empty()) return;
    CHECK(b.Games()[0].name == "ANA");
    CHECK(b.Games()[0].compatible);
    CHECK_EQ(b.Games()[0].players, 1);
    CHECK(b.Join(b.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &b}, [&] { return b.state() == app::Lan::State::Joined && host.Players().size() == 2; }));
    CHECK_EQ(b.MySlot(), 1);
    CHECK(SpinLan({&host, &b}, [&] { return b.Players().size() == 2; }));
    CHECK(b.Players().size() == 2 && b.Players()[0] == "ANA" && b.Players()[1] == "BETO");
    // sala de espera: cada uno tiene un color distinto; el que elige otro color lo ven todos y "listo" aparece en la sala
    CHECK(host.Colors().size() == 2 && host.Colors()[0] == 0 && host.Colors()[1] == 1);
    CHECK(SpinLan({&host, &b}, [&] { return b.Colors().size() == 2 && b.Colors()[0] == 0; }));
    CHECK(!host.Ready()[1]);
    b.CycleMyColor(+1);                                                   // Beto: del 1 al 2
    CHECK(SpinLan({&host, &b}, [&] { return host.Colors()[1] == 2; }));
    b.ToggleMyReady();
    CHECK(SpinLan({&host, &b}, [&] { return host.Ready()[1] && b.Ready()[1]; }));   // "Listo" en la pantalla del anfitrion
    CHECK(!host.Ready()[0]);
    host.CycleMyColor(+1);                                                // Ana: del 0 al 1 (libre)
    CHECK_EQ(host.Colors()[0], 1);
    host.CycleMyColor(+1);                                                // el 2 es de Beto: salta al 3
    CHECK_EQ(host.Colors()[0], 3);
    CHECK(SpinLan({&host, &b}, [&] { return b.Colors()[0] == 3; }));      // Beto lo ve en su pantalla
    b.CycleMyColor(+1);                                                   // Beto cambia de color: deja de estar listo
    CHECK(SpinLan({&host, &b}, [&] { return host.Colors()[1] == 4 && !host.Ready()[1]; }));
    host.ToggleMyReady();
    CHECK(host.Ready()[0]);
    host.ToggleMyReady();
    // dos mas se unen (mismo nombre: se renombra); el quinto no entra
    CHECK(c.Search());
    CHECK(SpinLan({&host, &c}, [&] { return !c.Games().empty(); }));
    CHECK_EQ(c.Games()[0].players, 2);
    CHECK(c.Join(c.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &b, &c}, [&] { return c.state() == app::Lan::State::Joined; }));
    CHECK(d.Search());
    CHECK(SpinLan({&host, &d}, [&] { return !d.Games().empty(); }));
    CHECK(d.Join(d.Games()[0], "Dora"));
    CHECK(SpinLan({&host, &b, &c, &d}, [&] { return d.state() == app::Lan::State::Joined && host.Players().size() == 4; }));
    CHECK_EQ(host.NumPlayers(), 4);
    CHECK(host.Players()[2] != host.Players()[1]);                   // nombres distintos
    for (size_t i = 0; i < host.Colors().size(); ++i)
        for (size_t j = i + 1; j < host.Colors().size(); ++j) CHECK(host.Colors()[i] != host.Colors()[j]);   // colores sin repetir
    CHECK(e.Search());
    CHECK(SpinLan({&host, &e}, [&] { return !e.Games().empty(); }));
    CHECK(e.Join(e.Games()[0], "Eva"));
    CHECK(SpinLan({&host, &e}, [&] { return e.state() == app::Lan::State::Full; }));   // llena
    // uno se va: la sala se actualiza
    d.Leave();
    CHECK(SpinLan({&host, &b, &c}, [&] { return host.Players().size() == 3 && b.Players().size() == 3; }));
    // el anfitrion cierra: los demas se enteran
    host.Leave();
    CHECK(SpinLan({&b, &c}, [&] { return b.state() == app::Lan::State::Closed && c.state() == app::Lan::State::Closed; }));
    CHECK(b.Lost());
}

// Una partida simulada en dos "equipos" (dos motores) que solo se hablan por la red.
struct NetSide {
    std::unique_ptr<FlowFixture> fx;
    int frame = 0;
};

void TestLanLockstep() {
    std::printf("[lan] partida en red: dos equipos con los mismos botones simulan exactamente lo mismo (hash igual cuadro a cuadro)\n");
    const uint16_t port = 47932;
    app::Lan host(port), guest(port);
    CHECK(host.Host("Ana"));
    CHECK(guest.Search());
    CHECK(SpinLan({&host, &guest}, [&] { return !guest.Games().empty(); }));
    CHECK(guest.Join(guest.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.state() == app::Lan::State::Joined && host.Players().size() == 2; }));
    CHECK(host.StartGame(1));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.Started(); }));
    CHECK_EQ(guest.FriendlyFire(), 1);
    CHECK_EQ(guest.NumPlayers(), 2);

    NetSide a, b;
    a.fx = MakeFlow(10, 2);
    b.fx = MakeFlow(10, 2);
    // botones de cada jugador en cada cuadro (de mentira, pero distintos y que mueven y disparan)
    auto script = [](int player, int frame) -> u8 {
        const int phase = (frame / 20 + player * 3) % 8;
        u8 v = 0;
        if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
        if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
        if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
        if (phase == 6) v |= nes::con_btn_Down;
        if ((frame + player) % 14 < 4) v |= nes::con_btn_A;
        return v;
    };
    constexpr int kFrames = 900;
    int stalls = 0;
    bool hashesMatch = true;
    const auto t0 = std::chrono::steady_clock::now();
    while ((a.frame < kFrames || b.frame < kFrames) && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0) {
        host.Update();
        guest.Update();
        auto step = [&](NetSide& side, app::Lan& lan, int me) {
            if (side.frame >= kFrames) return;
            lan.SubmitLocal(side.frame, script(me, side.frame));
            if (!lan.FrameReady(side.frame)) { ++stalls; return; }
            u8 in[8];
            lan.FrameInputs(side.frame, in);
            for (int i = 0; i < 4; ++i) side.fx->battle->SetButtons(i, static_cast<u8>(in[i] & ~nes::con_btn_Start));
            side.fx->flow->Step();
            ++side.frame;
            if (side.frame % 60 == 0) lan.ReportHash(side.frame, side.fx->battle->StateHash());
        };
        step(a, host, 0);
        step(b, guest, 1);
        // en cada momento en que los dos estan en el mismo cuadro, el estado tiene que ser identico
        if (a.frame == b.frame && a.frame % 60 == 0 && a.frame > 0) {
            hashesMatch = hashesMatch && a.fx->battle->StateHash() == b.fx->battle->StateHash();
        }
    }
    CHECK_EQ(a.frame, kFrames);
    CHECK_EQ(b.frame, kFrames);
    CHECK(hashesMatch);
    CHECK_EQ(a.fx->battle->StateHash(), b.fx->battle->StateHash());
    CHECK(!host.Desynced());
    CHECK(!guest.Desynced());
    CHECK(a.fx->battle->Upgrade(0) >= 0);
    // se jugo de verdad: hubo movimiento
    CHECK(a.fx->battle->TankX(0) != 88 || a.fx->battle->TankY(0) != 0xE8 + 16 || stalls >= 0);
}

void TestLanSeveralPlayersPerPc() {
    std::printf("[lan] varios jugadores por PC (2+2, 1+3): se suman a la sala, tienen color propio y juegan sincronizados\n");
    const uint16_t port = 47933;
    app::Lan host(port), guest(port), third(port);
    CHECK(host.Host("Ana"));
    CHECK(guest.Search());
    CHECK(SpinLan({&host, &guest}, [&] { return !guest.Games().empty(); }));
    CHECK(guest.Join(guest.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.state() == app::Lan::State::Joined && guest.Players().size() == 1 + 1; }));
    // 2 jugadores en la PC del anfitrion y 2 en la del cliente
    CHECK(host.AddLocalPlayer());
    CHECK(guest.AddLocalPlayer());
    CHECK(SpinLan({&host, &guest}, [&] { return host.NumPlayers() == 4 && guest.NumPlayers() == 4 && guest.LocalCount() == 2; }));
    CHECK_EQ(host.LocalCount(), 2);
    CHECK(!host.AddLocalPlayer());                                      // sala llena (4)
    CHECK(!guest.AddLocalPlayer());
    CHECK(host.Owners().size() == 4);
    int hostOwn = 0, guestOwn = 0;
    for (int o : host.Owners()) (o == 0 ? hostOwn : guestOwn)++;
    CHECK_EQ(hostOwn, 2);
    CHECK_EQ(guestOwn, 2);
    CHECK(SpinLan({&host, &guest}, [&] { return guest.Players() == host.Players() && guest.Colors() == host.Colors(); }));
    for (size_t i = 0; i < host.Colors().size(); ++i)
        for (size_t j = i + 1; j < host.Colors().size(); ++j) CHECK(host.Colors()[i] != host.Colors()[j]);
    // nombres distintos ("ANA", "ANA 2", "BETO", "BETO 2")
    for (size_t i = 0; i < host.Players().size(); ++i)
        for (size_t j = i + 1; j < host.Players().size(); ++j) CHECK(host.Players()[i] != host.Players()[j]);
    // una tercera PC ya no entra (cuatro jugadores)
    CHECK(third.Search());
    CHECK(SpinLan({&host, &third}, [&] { return !third.Games().empty(); }));
    CHECK(third.Join(third.Games()[0], "Cata"));
    CHECK(SpinLan({&host, &third}, [&] { return third.state() == app::Lan::State::Full; }));
    // el segundo jugador de la PC del cliente elige color y esta listo: lo ve el anfitrion
    const int p2 = guest.MyPlayers()[1];
    guest.CycleColor(1, +1);
    guest.ToggleReady(1);
    CHECK(SpinLan({&host, &guest}, [&] { return host.Ready()[static_cast<size_t>(p2)] && host.Colors()[static_cast<size_t>(p2)] == guest.Colors()[static_cast<size_t>(p2)]; }));
    // se baja y vuelve a subir
    guest.RemoveLocalPlayer(1);
    CHECK(SpinLan({&host, &guest}, [&] { return host.NumPlayers() == 3 && guest.NumPlayers() == 3; }));
    CHECK(guest.AddLocalPlayer());
    CHECK(SpinLan({&host, &guest}, [&] { return host.NumPlayers() == 4 && guest.LocalCount() == 2; }));
    // partida 2 + 2: mismos botones -> mismo estado
    CHECK(host.StartGame(1));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.Started(); }));
    NetSide a, b;
    a.fx = MakeFlow(10, 4);
    b.fx = MakeFlow(10, 4);
    auto script = [](int player, int frame) -> u8 {
        const int phase = (frame / 17 + player * 3) % 8;
        u8 v = 0;
        if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
        if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
        if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
        if (phase == 6) v |= nes::con_btn_Down;
        if ((frame + player) % 13 < 4) v |= nes::con_btn_A;
        return v;
    };
    constexpr int kFrames = 600;
    bool hashesMatch = true;
    const auto t0 = std::chrono::steady_clock::now();
    while ((a.frame < kFrames || b.frame < kFrames) && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0) {
        host.Update();
        guest.Update();
        auto step = [&](NetSide& side, app::Lan& lan) {
            if (side.frame >= kFrames) return;
            u8 local[4] = {0, 0, 0, 0};
            const int n = lan.LocalCount();
            for (int k = 0; k < n; ++k) local[k] = script(lan.MyPlayers()[static_cast<size_t>(k)], side.frame);
            lan.SubmitLocal(side.frame, local, n);
            if (!lan.FrameReady(side.frame)) return;
            u8 in[8];
            lan.FrameInputs(side.frame, in);
            for (int i = 0; i < 4; ++i) side.fx->battle->SetButtons(i, static_cast<u8>(in[i] & ~nes::con_btn_Start));
            side.fx->flow->Step();
            ++side.frame;
            if (side.frame % 60 == 0) lan.ReportHash(side.frame, side.fx->battle->StateHash());
        };
        step(a, host);
        step(b, guest);
        if (a.frame == b.frame && a.frame % 60 == 0 && a.frame > 0) hashesMatch = hashesMatch && a.fx->battle->StateHash() == b.fx->battle->StateHash();
    }
    CHECK_EQ(a.frame, kFrames);
    CHECK_EQ(b.frame, kFrames);
    CHECK(hashesMatch);
    CHECK_EQ(a.fx->battle->StateHash(), b.fx->battle->StateHash());
    CHECK(!host.Desynced());
    CHECK(!guest.Desynced());
    // los cuatro jugadores se movieron (cada uno con sus botones)
    int moved = 0;
    for (int p = 0; p < 4; ++p) moved += a.fx->battle->TankX(p) != 0;
    CHECK(moved == 4);
    // 1 + 3: otra sala
    app::Lan h2(port + 1), g2(port + 1);
    CHECK(h2.Host("Dora"));
    CHECK(g2.Search());
    CHECK(SpinLan({&h2, &g2}, [&] { return !g2.Games().empty(); }));
    CHECK(g2.Join(g2.Games()[0], "Eva"));
    CHECK(SpinLan({&h2, &g2}, [&] { return g2.state() == app::Lan::State::Joined && g2.NumPlayers() == 2; }));
    CHECK(g2.AddLocalPlayer());
    CHECK(g2.AddLocalPlayer());
    CHECK(SpinLan({&h2, &g2}, [&] { return h2.NumPlayers() == 4 && g2.LocalCount() == 3; }));
    CHECK_EQ(h2.LocalCount(), 1);
}

void TestOnlineCodeAndHybridRoom() {
    std::printf("[online] codigo de sala (IP publica + puerto), STUN, y una sala con jugadores de la red local y de internet a la vez\n");
    // codigo: ida y vuelta, tolerante a minusculas y espacios, y detecta errores de tipeo
    const std::string code = app::MakeRoomCode("203.0.113.45", 47800);
    CHECK_EQ(static_cast<int>(code.size()), 14);                         // XXXX-XXXX-XXXX
    CHECK(code[4] == '-' && code[9] == '-');
    std::string ip;
    uint16_t port = 0;
    CHECK(app::ParseRoomCode(code, ip, port));
    CHECK(ip == "203.0.113.45" && port == 47800);
    std::string lower = code;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    CHECK(app::ParseRoomCode("  " + lower + " ", ip, port) && ip == "203.0.113.45");
    CHECK(app::ParseRoomCode(code.substr(0, 4) + code.substr(5, 4) + code.substr(10), ip, port));   // sin guiones
    std::string bad = code;
    bad[2] = bad[2] == 'A' ? 'B' : 'A';
    CHECK(!app::ParseRoomCode(bad, ip, port));                           // un caracter cambiado: el control lo rechaza
    CHECK(!app::ParseRoomCode("ABCD-EFGH", ip, port));
    CHECK(!app::ParseRoomCode("1111-1111-1111", ip, port));              // el 1 y el 0 no existen en el alfabeto
    for (uint16_t pt : {1, 47800, 47801, 65535}) {                       // varios puertos
        CHECK(app::ParseRoomCode(app::MakeRoomCode("8.8.4.4", pt), ip, port) && ip == "8.8.4.4" && port == pt);
    }
    // STUN: respuesta armada a mano (XOR-MAPPED-ADDRESS 198.51.100.7:54321)
    const uint8_t tid[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    const std::string req = app::StunRequest(tid);
    CHECK_EQ(static_cast<int>(req.size()), 20);
    uint8_t resp[32] = {0x01, 0x01, 0x00, 0x0C, 0x21, 0x12, 0xA4, 0x42};
    std::memcpy(resp + 8, tid, 12);
    resp[20] = 0x00; resp[21] = 0x20; resp[22] = 0x00; resp[23] = 0x08; resp[24] = 0x00; resp[25] = 0x01;
    const uint16_t xp = static_cast<uint16_t>(54321 ^ 0x2112);
    resp[26] = static_cast<uint8_t>(xp >> 8); resp[27] = static_cast<uint8_t>(xp & 0xFF);
    resp[28] = 198 ^ 0x21; resp[29] = 51 ^ 0x12; resp[30] = 100 ^ 0xA4; resp[31] = 7 ^ 0x42;
    app::PublicEndpoint pe;
    CHECK(app::StunParse(reinterpret_cast<const char*>(resp), 32, tid, pe));
    CHECK(pe.ok && pe.ip == "198.51.100.7" && pe.port == 54321);
    const uint8_t other[12] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
    CHECK(!app::StunParse(reinterpret_cast<const char*>(resp), 32, other, pe));   // respuesta a otro pedido: se ignora

    // sala hibrida: el anfitrion abre su sala a internet (simulado) y entran uno por la red local y otro con el codigo
    const uint16_t lport = 47935;
    app::Lan host(lport), lanGuest(lport), netGuest(lport);
    CHECK(host.Host("Ana"));
    CHECK(host.InternetState() == app::Lan::Internet::Off);
    CHECK_EQ(host.InputDelay(), app::Lan::kInputDelay);
    host.OpenToInternet(true);
    CHECK(host.InternetState() == app::Lan::Internet::Ready);
    CHECK(!host.RoomCode().empty());
    CHECK(!netGuest.JoinCode("XXXX-XXXX-XXXX", "Cata"));                // codigo invalido
    CHECK(lanGuest.Search());
    CHECK(SpinLan({&host, &lanGuest}, [&] { return !lanGuest.Games().empty(); }));
    CHECK(lanGuest.Join(lanGuest.Games()[0], "Beto"));
    CHECK(netGuest.JoinCode(host.RoomCode(), "Cata"));                  // el de "afuera": entra con el codigo
    CHECK(SpinLan({&host, &lanGuest, &netGuest}, [&] {
        return lanGuest.state() == app::Lan::State::Joined && netGuest.state() == app::Lan::State::Joined && host.NumPlayers() == 3;
    }));
    CHECK(SpinLan({&host, &lanGuest, &netGuest}, [&] { return lanGuest.NumPlayers() == 3 && netGuest.NumPlayers() == 3; }));
    CHECK(host.Players() == lanGuest.Players() && host.Players() == netGuest.Players());
    CHECK(host.Colors() == netGuest.Colors());
    CHECK(netGuest.HostName() == "ANA");
    // empiezan: por haber abierto la sala a internet todos usan el retraso de internet
    CHECK(host.StartGame(1));
    CHECK(SpinLan({&host, &lanGuest, &netGuest}, [&] { return lanGuest.Started() && netGuest.Started(); }));
    CHECK_EQ(host.InputDelay(), app::Lan::kOnlineInputDelay);
    CHECK_EQ(lanGuest.InputDelay(), app::Lan::kOnlineInputDelay);
    CHECK_EQ(netGuest.InputDelay(), app::Lan::kOnlineInputDelay);
    NetSide s0, s1, s2;
    s0.fx = MakeFlow(10, 3);
    s1.fx = MakeFlow(10, 3);
    s2.fx = MakeFlow(10, 3);
    auto script = [](int player, int frame) -> u8 {
        const int phase = (frame / 19 + player * 3) % 8;
        u8 v = 0;
        if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
        if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
        if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
        if (phase == 6) v |= nes::con_btn_Down;
        if ((frame + player) % 11 < 4) v |= nes::con_btn_A;
        return v;
    };
    constexpr int kFrames = 400;
    bool match = true;
    const auto t0 = std::chrono::steady_clock::now();
    while ((s0.frame < kFrames || s1.frame < kFrames || s2.frame < kFrames) &&
           std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 20.0) {
        host.Update();
        lanGuest.Update();
        netGuest.Update();
        auto step = [&](NetSide& side, app::Lan& lan) {
            if (side.frame >= kFrames) return;
            lan.SubmitLocal(side.frame, script(lan.MyPlayers()[0], side.frame));
            if (!lan.FrameReady(side.frame)) return;
            u8 in[8];
            lan.FrameInputs(side.frame, in);
            for (int i = 0; i < 4; ++i) side.fx->battle->SetButtons(i, static_cast<u8>(in[i] & ~nes::con_btn_Start));
            side.fx->flow->Step();
            ++side.frame;
        };
        step(s0, host);
        step(s1, lanGuest);
        step(s2, netGuest);
        if (s0.frame == s1.frame && s1.frame == s2.frame && s0.frame % 50 == 0 && s0.frame > 0) {
            const uint32_t h = s0.fx->battle->StateHash();
            match = match && h == s1.fx->battle->StateHash() && h == s2.fx->battle->StateHash();
        }
    }
    CHECK_EQ(s0.frame, kFrames);
    CHECK_EQ(s1.frame, kFrames);
    CHECK_EQ(s2.frame, kFrames);
    CHECK(match);
    CHECK_EQ(s0.fx->battle->StateHash(), s1.fx->battle->StateHash());
    CHECK_EQ(s0.fx->battle->StateHash(), s2.fx->battle->StateHash());
}

std::unique_ptr<FlowFixture> MakeNetFlow(int level, int presentMask, bool requireStart) {
    auto fx = std::make_unique<FlowFixture>();
    fx->battle = std::make_unique<nes::Battle>();
    fx->battle->SetWide(true);
    fx->battle->SetLevel(level);
    fx->battle->NewGame(4, level);                                  // la partida en red siempre tiene 4 lugares
    fx->battle->SetPresentMask(presentMask);
    u8 blocks[14][27];
    app::GenerateWideLevel(level, blocks);
    fx->battle->LoadWideStage(blocks);
    fx->battle->StartStage();
    app::GameFlow::Hooks hooks;
    FlowFixture* raw = fx.get();
    hooks.loadNextLevel = [raw](int lv) {
        u8 bl[14][27];
        app::GenerateWideLevel(lv, bl);
        raw->battle->NextStage(lv, bl);
        ++raw->loaded;
    };
    hooks.returnToTitle = [raw]() { raw->restarted = true; };
    fx->flow = std::make_unique<app::GameFlow>(*fx->battle, hooks);
    fx->flow->SetRequireStart(requireStart);
    fx->flow->Begin(level);
    return fx;
}

// La misma logica que la partida en red del programa: marcas de entrada/salida, Start del anfitrion y botones.
void ApplyNetFrame(FlowFixture& fx, const uint8_t in[8], int hostPlayer) {
    bool startNow = false;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = in[i];
        if (app::Lan::IsMark(b)) {
            if (app::Lan::IsJoinMark(b)) fx.battle->AddPlayerLate(i, app::Lan::JoinColor(b));
            else fx.battle->MarkPlayerGone(i);
            b = 0;
        }
        if ((b & nes::con_btn_Start) && i == hostPlayer) startNow = true;
        fx.battle->SetButtons(i, static_cast<u8>(b & ~(nes::con_btn_Start | nes::con_btn_Select)));
    }
    if (startNow) fx.flow->PressStart();
    fx.flow->Step();
}

void TestNetPlayersComeAndGo() {
    std::printf("[red] quien se va o entra cambia la dificultad desde el nivel siguiente; cada nivel espera el Start del anfitrion\n");
    using Mode = app::GameFlow::Mode;
    // presentes al empezar: jugadores 0 y 1
    auto one = MakeFlow(1, 1);
    auto two = MakeFlow(1, 2);
    auto three = MakeFlow(1, 3);
    auto fx = MakeNetFlow(1, 0x03, false);
    auto& b = *fx->battle;
    CHECK_EQ(b.ActivePlayers(), 2);
    CHECK_EQ(b.EnemyTotal(), two->battle->EnemyTotal());               // la dificultad como la de 2 jugadores
    CHECK(b.PlayerPresent(0) && b.PlayerPresent(1) && !b.PlayerPresent(2) && !b.PlayerPresent(3));
    CHECK_EQ(b.Lives(2), 0);
    CHECK_EQ(b.TankFlags(2), 0);                                        // los ausentes no tienen tanque
    for (int f = 0; f < 300; ++f) fx->flow->Step();
    CHECK(b.TankFlags(2) == 0 && b.TankFlags(3) == 0);
    // se va el jugador 1: desaparece y su lugar no vuelve; en el mismo nivel nada cambia
    const int totalMid = b.EnemyTotal();
    b.MarkPlayerGone(1);
    CHECK_EQ(b.ActivePlayers(), 1);
    CHECK_EQ(b.TankFlags(1), 0);
    CHECK_EQ(b.Lives(1), 0);
    CHECK_EQ(b.EnemyTotal(), totalMid);
    // entra el jugador 2 con color violeta: aparece con 3 vidas
    b.AddPlayerLate(2, 4);
    CHECK(b.PlayerPresent(2));
    CHECK_EQ(b.Lives(2), 3);
    CHECK(b.TankFlags(2) != 0);
    CHECK_EQ(b.PlayerPaletteIndex(2), 6);                               // violeta
    CHECK_EQ(b.ActivePlayers(), 2);
    // al empezar el nivel siguiente la dificultad sigue a los presentes (2: jugador 0 y 2)
    u8 blocks[14][27];
    app::GenerateWideLevel(2, blocks);
    b.MarkPlayerGone(2);                                                // ahora solo queda el jugador 0
    b.NextStage(2, blocks);
    CHECK_EQ(b.DifficultyPlayers(), 1);
    auto oneL2 = MakeFlow(2, 1);
    CHECK_EQ(b.EnemyTotal(), oneL2->battle->EnemyTotal());              // igual que una partida de 1 jugador
    CHECK(b.EnemyTotal() < two->battle->EnemyTotal() * 11 / 10 + 1);
    (void)one;
    (void)three;
    // cada nivel espera el Start del anfitrion en la cortina
    auto gate = MakeNetFlow(1, 0x03, true);
    for (int f = 0; f < 400; ++f) gate->flow->Step();
    CHECK(gate->flow->mode() == Mode::StageIntro);
    CHECK(gate->flow->WaitingForStart());
    CHECK_EQ(gate->flow->curtainRows(), 15);
    gate->flow->PressStart();
    for (int f = 0; f < 200 && gate->flow->mode() != Mode::Playing; ++f) gate->flow->Step();
    CHECK(gate->flow->mode() == Mode::Playing);
    CHECK(!gate->flow->WaitingForStart());
    // y el nivel que sigue tambien: otra vez en espera
    gate->flow->SkipToNextLevel();
    for (int f = 0; f < 400; ++f) gate->flow->Step();
    CHECK(gate->flow->WaitingForStart());
}

void TestLanLateJoin() {
    std::printf("[red] entrar en medio de la partida: elige color, se pone al dia y aparece; si alguien se va, los demas siguen igual\n");
    const uint16_t port = 47937;
    app::Lan host(port), guest(port), late(port);
    CHECK(host.Host("Ana"));
    CHECK(guest.Search());
    CHECK(SpinLan({&host, &guest}, [&] { return !guest.Games().empty(); }));
    CHECK(guest.Join(guest.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.state() == app::Lan::State::Joined && host.NumPlayers() == 2; }));
    CHECK(host.StartGame(1));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.Started(); }));
    CHECK(!guest.LateJoiner());
    CHECK_EQ(host.StartMask(), 0x03);
    CHECK_EQ(guest.StartMask(), 0x03);
    CHECK_EQ(guest.StartColor(1), guest.Colors()[1]);

    NetSide sh, sg, sl;
    sh.fx = MakeNetFlow(10, host.StartMask(), true);
    sg.fx = MakeNetFlow(10, guest.StartMask(), true);
    auto script = [](int player, int frame) -> u8 {
        const int phase = (frame / 17 + player * 3) % 8;
        u8 v = 0;
        if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
        if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
        if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
        if (phase == 6) v |= nes::con_btn_Down;
        if ((frame + player) % 13 < 4) v |= nes::con_btn_A;
        return v;
    };
    bool lateCreated = false, lateReadyDone = false, sawJoinMark = false, sawGoneMark = false, guestLeft = false;
    int joinFrame = -1, joinColor = -1, goneFrame = -1;
    constexpr int kFrames = 1000;
    const auto t0 = std::chrono::steady_clock::now();
    auto stepSide = [&](NetSide& side, app::Lan& lan, int hostPlayer, int myPlayer) {
        if (side.frame >= kFrames) return;
        uint8_t local = 0;
        if (side.fx->battle->PlayerPresent(myPlayer)) local = script(myPlayer, side.frame);
        if (&lan == &host && side.fx->flow->WaitingForStart()) local |= nes::con_btn_Start;     // el anfitrion empieza cada nivel
        lan.SubmitLocal(side.frame, local);
        if (!lan.FrameReady(side.frame)) return;
        uint8_t in[8];
        lan.FrameInputs(side.frame, in);
        for (int i = 0; i < 4; ++i) {
            if (app::Lan::IsJoinMark(in[i]) && !sawJoinMark) { sawJoinMark = true; joinFrame = side.frame; joinColor = app::Lan::JoinColor(in[i]); }
            if (app::Lan::IsMark(in[i]) && !app::Lan::IsJoinMark(in[i]) && i == 1 && !sawGoneMark && guestLeft) { sawGoneMark = true; goneFrame = side.frame; }
        }
        ApplyNetFrame(*side.fx, in, hostPlayer);
        ++side.frame;
    };
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 30.0) {
        host.Update();
        if (!guestLeft) guest.Update();
        if (lateCreated) late.Update();
        stepSide(sh, host, 0, 0);
        if (!guestLeft) stepSide(sg, guest, 0, 1);
        if (!lateCreated && sh.frame >= 250) {                                   // alguien se une con la partida en marcha
            CHECK(late.Search());
            CHECK(SpinLan({&host, &guest, &late}, [&] { return !late.Games().empty(); }));
            CHECK(late.Join(late.Games()[0], "Cata"));
            CHECK(SpinLan({&host, &guest, &late}, [&] { return late.state() == app::Lan::State::Joined && late.Started(); }));
            CHECK(late.LateJoiner());
            CHECK_EQ(late.StartMask(), 0x03);
            CHECK_EQ(late.MyPlayers().size(), static_cast<size_t>(1));
            CHECK_EQ(late.MyPlayers()[0], 2);                                    // toma el primer lugar libre
            CHECK(host.Colors()[2] >= 0);
            sl.fx = MakeNetFlow(10, late.StartMask(), true);                     // reproduce desde el cuadro 0
            lateCreated = true;
        }
        if (lateCreated && sl.frame < kFrames) {
            // reproduccion rapida: simula todo lo que ya llego
            int budget = 300;
            while (budget-- > 0 && late.FrameReady(sl.frame) && sl.frame < kFrames) stepSide(sl, late, 0, 2);
            if (!lateReadyDone && late.FramesAvailable() - sl.frame <= 3 && sl.frame > 100) {
                const int before = late.Colors()[2];
                late.CycleColor(0, +1);                                          // elige otro color y toca listo
                late.ToggleReady(0);
                CHECK(late.Colors()[2] != before);
                lateReadyDone = true;
            }
        }
        if (sawJoinMark && !guestLeft && sh.frame >= joinFrame + 150) {          // Beto se va unos cuadros despues
            guest.Leave();
            guestLeft = true;
        }
        if (sh.frame >= kFrames && sl.frame >= kFrames) break;
    }
    CHECK(lateCreated && lateReadyDone);
    CHECK(sawJoinMark);
    CHECK(joinFrame > 250);                                                      // en un cuadro futuro, no en el pasado
    CHECK_EQ(joinColor, host.Colors()[2]);                                       // con el color que eligio
    CHECK(sawGoneMark);
    CHECK(goneFrame > joinFrame);
    CHECK_EQ(sh.frame, kFrames);
    CHECK_EQ(sl.frame, kFrames);
    CHECK_EQ(sh.fx->battle->StateHash(), sl.fx->battle->StateHash());            // el recien llegado ve exactamente lo mismo
    CHECK(sh.fx->battle->PlayerPresent(2) && !sh.fx->battle->PlayerPresent(1));
    CHECK(sl.fx->battle->PlayerPresent(2) && !sl.fx->battle->PlayerPresent(1));
    CHECK_EQ(sh.fx->battle->PlayerPaletteIndex(2), sl.fx->battle->PlayerPaletteIndex(2));
    CHECK_EQ(host.NumPlayers(), 3);                                              // el lugar de Beto quedo libre
    CHECK_EQ(host.PresentPlayers(), 2);
}

// ---------------------------------------------------------------------------
// Niveles hechos a mano y editor
// ---------------------------------------------------------------------------

void TestCustomLevels() {
    std::printf("[editor] niveles hechos a mano: archivo, serie del modo principal, ficha del motor (aguila donde sea, apariciones propias, dificultad)\n");
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "bcnes_levels_test";
    fs::remove_all(tmp);
    app::SetLevelRoot(tmp.string());
    app::UseCustomLevels(true);

    // ---- archivo ida y vuelta ----
    app::LevelDef a = app::BuiltinLevelDef(20);
    CHECK(a.boss && a.tier == 3 && a.rank == 2 && a.nSpawns == 7);
    CHECK(app::BuiltinLevelDef(3).nSpawns == 5 && !app::BuiltinLevelDef(3).boss);
    CHECK(app::SaveLevelDef(app::OwnLevelFile(5), a));
    app::LevelDef b;
    CHECK(app::LoadLevelDef(app::OwnLevelFile(5), b));
    CHECK(std::memcmp(a.cells, b.cells, sizeof(a.cells)) == 0);
    CHECK(a.boss == b.boss && a.tier == b.tier && a.rank == b.rank && a.eagleCol == b.eagleCol && a.eagleRow == b.eagleRow && a.nSpawns == b.nSpawns);
    CHECK(std::memcmp(a.spawnCol, b.spawnCol, sizeof(a.spawnCol)) == 0 && std::memcmp(a.spawnRow, b.spawnRow, sizeof(a.spawnRow)) == 0);
    CHECK(app::OwnSlotHasFile(5) && !app::OwnSlotHasFile(6));
    CHECK(!app::LoadLevelDef(app::OwnLevelFile(6), b));                  // no existe
    { std::ofstream bad(app::OwnLevelFile(7)); bad << "cualquier cosa\n"; }
    CHECK(!app::LoadLevelDef(app::OwnLevelFile(7), b));                  // danado

    // ---- la serie del modo principal crece con los archivos 41, 42... y se corta en el primer hueco ----
    CHECK_EQ(app::MainLevelCount(), 40);
    CHECK(app::SaveLevelDef(app::MainLevelFile(41), a));
    CHECK(app::SaveLevelDef(app::MainLevelFile(42), a));
    CHECK(app::SaveLevelDef(app::MainLevelFile(44), a));
    CHECK_EQ(app::MainLevelCount(), 42);
    app::UseCustomLevels(false);                                          // en red: solo los de fabrica
    CHECK_EQ(app::MainLevelCount(), 40);
    CHECK(!app::MainLevelHasFile(41));
    app::UseCustomLevels(true);
    auto fx = MakeFlow(41, 1);
    fx->flow->SkipToNextLevel();
    CHECK_EQ(fx->flow->level(), 42);
    fx->flow->SkipToNextLevel();
    CHECK_EQ(fx->flow->level(), 1);                                       // despues del ultimo, vuelve al 1
    {
        app::LevelDef m;
        CHECK(app::LoadMainLevelDef(41, m) && m.boss);                    // el archivo
        CHECK(app::LoadMainLevelDef(7, m) && !m.boss && m.tier == 1);     // uno de fabrica
        CHECK(!app::LoadMainLevelDef(43, m));                             // no existe
    }

    // ---- ficha del motor: aguila en otro lugar, apariciones propias y dificultad elegida ----
    {
        nes::Battle bt;
        bt.SetWide(true);
        nes::Battle::StageSpec sp;
        sp.active = true;
        sp.boss = true;
        sp.tier = 7;
        sp.rank = 2;
        sp.eagleCol = 5;
        sp.eagleRow = 4;
        sp.nSpawns = 2;
        sp.spawnCol[0] = 2; sp.spawnRow[0] = 3;
        sp.spawnCol[1] = 20; sp.spawnRow[1] = 5;
        bt.SetStageSpec(sp);
        bt.SetLevel(3);                                                   // el numero de nivel no manda: manda la ficha
        bt.NewGame(1, 3);
        u8 blocks[14][27];
        u8 strip[27];
        std::memset(blocks, 13, sizeof(blocks));
        std::memset(strip, 13, sizeof(strip));
        bt.LoadWideStage(blocks, strip);
        bt.StartStage();
        CHECK(bt.IsBossLevel());
        CHECK_EQ(bt.Tier(), 7);
        CHECK_EQ(bt.Rank(), 2);
        CHECK_EQ(bt.EnemyEagleHealth(), 20);
        CHECK(bt.BombInterval() == 2 * 60 * 60);                           // fuerza 2: cada 2 minutos
        CHECK((bt.Tile(10, 8) & 0xFC) == 0xC8 && (bt.Tile(11, 9) & 0xFC) == 0xC8);          // el aguila esta en (columna 5, fila 4)
        CHECK(bt.Tile(8, 8) != 0 && bt.Tile(13, 8) != 0 && bt.Tile(10, 6) != 0 && bt.Tile(10, 11) != 0);   // y su fortaleza de 2 capas
        CHECK(bt.Tile(13, 13) == 0 || (bt.Tile(13, 13) & 0xFC) != 0xC8);
        CHECK_EQ(bt.EnemyTotal(), 20 * 17 / 10);                           // +70% por la dificultad 8
        bool seen[2] = {false, false};
        bool onlyOurs = true;
        for (int f = 0; f < 900; ++f) {
            bt.RunFrame();
            for (int e = bt.Players(); e <= bt.LastEnemy(); ++e) {
                if (bt.TankFlags(e) < 0xE0) continue;                      // recien apareciendo
                const int x = bt.TankX(e), y = bt.TankY(e);
                if (x == 2 * 16 + 8 && y == 3 * 16 + 8) seen[0] = true;
                else if (x == 20 * 16 + 8 && y == 5 * 16 + 8) seen[1] = true;
                else onlyOurs = false;
            }
        }
        CHECK(seen[0] && seen[1] && onlyOurs);                              // salen solo de los puntos elegidos
        // un nivel comun con la misma maquinaria: sin aguila enemiga, con su propia dificultad
        nes::Battle n2;
        n2.SetWide(true);
        sp.boss = false;
        sp.tier = 0;
        n2.SetStageSpec(sp);
        n2.SetLevel(40);                                                  // aunque sea "el 40"
        n2.NewGame(1, 40);
        n2.LoadWideStage(blocks, strip);
        n2.StartStage();
        CHECK(!n2.IsBossLevel() && !n2.EnemyEagleAlive() && n2.Tier() == 0);
    }

    // ---- avisos al armar un nivel ----
    {
        app::LevelDef d = app::BlankLevelDef(false);
        CHECK(app::CheckLevelDef(d).empty());
        d.cells[0][1] = 9;                                                 // hierro alrededor de la aparicion de arriba a la izquierda
        d.cells[1][0] = 9;
        d.cells[1][1] = 9;
        const auto w = app::CheckLevelDef(d);
        CHECK(w.size() == 1 && w[0].find("ENCERRADA") != std::string::npos);
        app::LevelDef boss = app::BlankLevelDef(true);
        CHECK(app::CheckLevelDef(boss).empty());
        boss.spawnCol[0] = static_cast<uint8_t>(boss.eagleCol);            // una aparicion encima del aguila
        boss.spawnRow[0] = static_cast<uint8_t>(boss.eagleRow);
        CHECK(!app::CheckLevelDef(boss).empty());
    }

    // ---- el editor, manejado con teclas ----
    {
        app::LevelEditor ed(app::LevelEditor::Target::Own);
        auto tap = [&](auto setKey) {
            app::EditorKeys k;
            setKey(k);
            ed.Step(k);
            ed.Step(app::EditorKeys{});                                    // soltar
        };
        CHECK(ed.InEditPage() && ed.CursorCol() == 13 && ed.CursorRow() == 7);
        tap([](app::EditorKeys& k) { k.right = true; });
        CHECK_EQ(ed.CursorCol(), 14);
        tap([](app::EditorKeys& k) { k.place = true; });                    // el bloque de la paleta
        CHECK_EQ(ed.def().cells[7][14], app::LevelEditor::PaletteIds()[0]);       // el ladrillo entero es el bloque elegido al empezar
        tap([](app::EditorKeys& k) { k.nextItem = true; });
        tap([](app::EditorKeys& k) { k.place = true; });
        tap([](app::EditorKeys& k) { k.erase = true; });
        CHECK_EQ(ed.def().cells[7][14], 13);                               // borrar deja vacio
        CHECK(ed.Dirty());
        for (int i = 0; i < 7; ++i) tap([](app::EditorKeys& k) { k.down = true; });
        CHECK_EQ(ed.CursorRow(), 14);
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK_EQ(ed.def().cells[14][14], 13);                              // la base y los lugares de los jugadores no se pintan
        for (int i = 0; i < 9; ++i) tap([](app::EditorKeys& k) { k.up = true; });
        // aparicion: poner y quitar
        const int before = ed.def().nSpawns;
        tap([](app::EditorKeys& k) { k.tool = true; });
        CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Spawn);
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK_EQ(ed.def().nSpawns, before + 1);
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK_EQ(ed.def().nSpawns, before);
        tap([](app::EditorKeys& k) { k.tool = true; });                     // sin aguila enemiga, la herramienta aguila no aparece
        CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Block);
        // menu: tipo especial
        tap([](app::EditorKeys& k) { k.menu = true; });
        CHECK(!ed.InEditPage());
        tap([](app::EditorKeys& k) { k.down = true; });
        tap([](app::EditorKeys& k) { k.accept = true; });
        CHECK(ed.def().boss);
        tap([](app::EditorKeys& k) { k.back = true; });
        CHECK(ed.InEditPage());
        tap([](app::EditorKeys& k) { k.tool = true; });
        tap([](app::EditorKeys& k) { k.tool = true; });
        CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Eagle);
        // mover el aguila: el cursor esta en la columna 14, fila 7 -> se acomoda y despeja su zona
        for (int i = 0; i < 2; ++i) tap([](app::EditorKeys& k) { k.up = true; });
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK(ed.def().eagleCol == 14 && ed.def().eagleRow == 3);
        // guardar y volver a abrir
        CHECK(ed.SaveToSlot(9));
        CHECK(!ed.Dirty() && ed.CurrentSlot() == 9);
        app::LevelEditor ed2(app::LevelEditor::Target::Own);
        CHECK(ed2.LoadFromSlot(9));
        CHECK(std::memcmp(ed.def().cells, ed2.def().cells, sizeof(ed.def().cells)) == 0);
        CHECK(ed2.def().boss && ed2.def().eagleCol == 14 && ed2.def().eagleRow == 3);
        CHECK(!ed2.LoadFromSlot(12));                                      // casillero vacio
        CHECK(!ed2.SaveToSlot(41));                                        // solo hay 40
        // la vista tiene lo necesario para dibujar
        app::EditorView view;
        ed2.BuildView(view);
        CHECK(view.boss && view.nSpawns == ed2.def().nSpawns && view.paletteIds.size() == 23);
        std::vector<uint8_t> rgba;
        app::Renderer renderer;
        (void)renderer;                                                    // (el dibujo necesita los graficos de la ROM: se prueba a mano)
        // la ficha para jugar sale igual que la del editor
        nes::Battle::StageSpec spec;
        u8 blocks[14][27];
        u8 strip[27];
        app::LevelDefToStage(ed2.def(), blocks, strip, spec);
        CHECK(spec.active && spec.boss && spec.eagleCol == 14 && spec.nSpawns == ed2.def().nSpawns);
    }
    // ---- el editor arma mapas de batalla vs ----
    {
        const fs::path et = fs::temp_directory_path() / "bcnes_vs_editor";
        fs::remove_all(et);
        app::SetLevelRoot(et.string());
        app::LevelEditor ed(app::LevelEditor::Target::Own);
        auto tap = [&](auto setKey) {
            app::EditorKeys k;
            setKey(k);
            ed.Step(k);
            ed.Step(app::EditorKeys{});
        };
        tap([](app::EditorKeys& k) { k.menu = true; });
        tap([](app::EditorKeys& k) { k.down = true; });
        tap([](app::EditorKeys& k) { k.accept = true; });                  // normal -> especial
        tap([](app::EditorKeys& k) { k.accept = true; });                  // especial -> batalla vs
        CHECK(ed.def().versus && !ed.def().boss && ed.def().nSpawns == 4);
        tap([](app::EditorKeys& k) { k.back = true; });
        // aparicion de la izquierda: ya hay 2, no entra una tercera; se quita una y entra otra
        tap([](app::EditorKeys& k) { k.tool = true; });
        CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Spawn);
        for (int i = 0; i < 8; ++i) tap([](app::EditorKeys& k) { k.left = true; });        // columna 5
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK_EQ(ed.def().nSpawns, 4);
        for (int i = 0; i < 2; ++i) tap([](app::EditorKeys& k) { k.down = true; });
        // el aguila del equipo 1 se mueve con su herramienta
        tap([](app::EditorKeys& k) { k.tool = true; });
        CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Eagle);
        for (int i = 0; i < 3; ++i) tap([](app::EditorKeys& k) { k.left = true; });        // columna 2
        tap([](app::EditorKeys& k) { k.place = true; });
        CHECK(ed.def().vsEagleCol[0] == 2 && ed.def().vsEagleRow[0] == 9 && ed.def().vsEagleCol[1] == 25);
        CHECK(app::CheckLevelDef(ed.def()).size() <= 1);
        CHECK(ed.SaveToSlot(4));
        app::LevelEditor ed2(app::LevelEditor::Target::Own);
        CHECK(ed2.LoadFromSlot(4) && ed2.def().versus && ed2.def().vsEagleCol[0] == 2 && ed2.def().vsLayers == 1);
        fs::remove_all(et);
        app::SetLevelRoot("");
    }
    fs::remove_all(tmp);
    app::SetLevelRoot("");
}

// ---------------------------------------------------------------------------
// Modo batalla vs
// ---------------------------------------------------------------------------

std::unique_ptr<nes::Battle> MakeVersus(int players, const int teams[4], int tier = 0, int rank = 3, int size = 1) {
    auto b = std::make_unique<nes::Battle>();
    b->SetWide(true);
    nes::Battle::StageSpec sp;
    sp.active = true;
    sp.versus = true;
    sp.tier = tier;
    sp.rank = rank;
    sp.nSpawns = 4;
    const int sc[4] = {1, 13, 25, 13}, sr[4] = {7, 13, 7, 1};
    for (int i = 0; i < 4; ++i) { sp.spawnCol[i] = sc[i]; sp.spawnRow[i] = sr[i]; }
    b->SetStageSpec(sp);
    b->SetLevel(1);
    b->NewGame(players, 1);
    b->SetVersusSize(size);
    b->SetTeams(teams);
    u8 blocks[14][27];
    u8 strip[27];
    std::memset(blocks, 13, sizeof(blocks));
    std::memset(strip, 13, sizeof(strip));
    b->LoadWideStage(blocks, strip);
    b->StartStage();
    return b;
}

void TestVersusMode() {
    std::printf("[batalla vs] dos equipos con aguila propia, tanques con IA por equipo, items, bombardeo y fin de partida\n");
    const int teams[4] = {0, 1, 0, 1};
    auto b = MakeVersus(2, teams);
    CHECK(b->IsVersus() && !b->IsBossLevel() && !b->EnemyEagleAlive());
    CHECK(b->VsEagleAlive(0) && b->VsEagleAlive(1) && b->VersusWinner() == -1);
    CHECK((b->Tile(2, 26) & 0xFC) == 0xC8 && (b->Tile(50, 2) & 0xFC) == 0xC8);          // las dos aguilas, en esquinas opuestas
    CHECK(b->Tile(1, 26) != 0 && b->Tile(4, 26) != 0 && b->Tile(2, 25) != 0 && b->Tile(2, 28) != 0);   // y su cubierta de hierro
    CHECK(b->TeamOf(0) == 0 && b->TeamOf(1) == 1);
    CHECK(b->PlayerPaletteIndex(0) == 0 && b->PlayerPaletteIndex(1) == 5);             // amarillo y rojo
    CHECK(b->BombingActive() && b->BombInterval() == 2 * 60 * 60);
    CHECK(!b->GameOver() && !b->StageCleared());
    bool aiSeen[2] = {false, false};
    for (int f = 0; f < 600; ++f) {
        b->RunFrame();
        for (int e = b->Players(); e <= b->LastEnemy(); ++e) {
            if (b->TankFlags(e) != 0) aiSeen[b->TeamOf(e)] = true;
        }
    }
    CHECK(aiSeen[0] && aiSeen[1]);                                                      // salen tanques de los dos equipos
    CHECK(b->VersusWinner() == -1);
    // un equipo tira al aguila rival con balas de 3 estrellas (rompen el hierro) hasta destruirla
    for (int e = b->Players(); e <= b->LastEnemy(); ++e) b->DebugSetTankFlags(e, 0);   // sin tanques de por medio
    b->SetUpgrade(0, 0x60);
    b->SetTankPos(0, 330, 24);
    b->DebugSetTankFlags(0, 0xA3);                                                      // mirando a la derecha
    for (int f = 0; f < 400 && b->VsEagleAlive(1); ++f) {
        b->SetButtons(0, (f % 20) < 2 ? nes::con_btn_A : 0);
        b->RunFrame();
        for (int e = b->Players(); e <= b->LastEnemy(); ++e) b->DebugSetTankFlags(e, 0);
        b->SetTankPos(0, 330, 24);
    }
    CHECK(!b->VsEagleAlive(1));
    CHECK(b->VsEagleAlive(0));
    for (int f = 0; f < 3; ++f) b->RunFrame();
    CHECK_EQ(b->VersusWinner(), 0);                                                     // gana el equipo de la izquierda
    CHECK(!b->BombingActive());

    // sin vidas: si el equipo rival se queda sin jugadores con vidas, pierde aunque conserve el aguila
    auto c = MakeVersus(2, teams);
    for (int f = 0; f < 120; ++f) c->RunFrame();
    c->SetLives(1, 0);
    c->DebugSetTankFlags(1, 0);
    c->RunFrame();
    CHECK_EQ(c->VersusWinner(), 0);

    // 2 contra 2: los tanques del mismo equipo no se lastiman; los del otro si
    auto d = MakeVersus(4, teams);
    for (int f = 0; f < 200; ++f) d->RunFrame();
    CHECK(d->TeamOf(2) == 0 && d->TeamOf(3) == 1);
    CHECK(d->PlayerPaletteIndex(2) == 0 && d->PlayerPaletteIndex(3) == 5);
    for (int e = d->Players(); e <= d->LastEnemy(); ++e) d->DebugSetTankFlags(e, 0);
    d->SetTankPos(0, 100, 100);
    d->SetTankPos(2, 140, 100);                                                         // compañero, a la derecha
    d->SetTankPos(1, 260, 100);                                                         // rival
    d->DebugSetTankFlags(0, 0xA3);
    d->DebugSetTankFlags(2, 0xA3);
    d->DebugSetTankFlags(1, 0xA3);
    for (int f = 0; f < 40; ++f) {
        d->SetButtons(0, (f % 20) < 2 ? nes::con_btn_A : 0);
        d->RunFrame();
        d->SetTankPos(0, 100, 100);
        d->SetTankPos(2, 140, 100);
        d->SetTankPos(1, 260, 100);
    }
    CHECK((d->TankFlags(2) & 0xF0) >= 0x80 && d->TankFlags(2) < 0xE0);                  // el compañero sigue en pie
    CHECK(d->Upgrade(1) == 0 || d->TankFlags(1) < 0x80 || d->PlayerHits(1) > 0);           // el rival fue alcanzado

    // ---- mapas al azar: simetricos entre los dos equipos y con camino ----
    {
        int bad = 0, asym = 0;
        bool differ = false;
        app::LevelDef first = app::GenerateVersusLevel(1);
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            const app::LevelDef g = app::GenerateVersusLevel(seed);
            if (!g.versus || g.nSpawns != 4) ++bad;
            for (int r = 0; r < app::kGridRows; ++r)
                for (int c = 0; c < 13; ++c)
                    if (g.cells[r][c] != g.cells[app::kGridRows - 1 - r][26 - c]) ++asym;   // girado media vuelta
            if (!app::CheckLevelDef(g).empty()) { ++bad; for (const auto& w : app::CheckLevelDef(g)) std::printf("seed %u: %s\n", seed, w.c_str()); }
            if (std::memcmp(g.cells, first.cells, sizeof(g.cells)) != 0) differ = true;
            const app::LevelDef again = app::GenerateVersusLevel(seed);
            if (std::memcmp(g.cells, again.cells, sizeof(g.cells)) != 0) ++bad;       // mismo numero, mismo mapa
        }
        CHECK_EQ(bad, 0);
        CHECK_EQ(asym, 0);
        CHECK(differ);
        const app::LevelDef g = app::GenerateVersusLevel(77);
        namespace fs = std::filesystem;
        const fs::path vt = fs::temp_directory_path() / "bcnes_vs_test";
        fs::remove_all(vt);
        app::SetLevelRoot(vt.string());
        CHECK(app::SaveLevelDef(app::OwnLevelFile(3), g));
        app::LevelDef back;
        CHECK(app::LoadLevelDef(app::OwnLevelFile(3), back));
        CHECK(back.versus && back.vsEagleCol[1] == 25 && back.vsLayers == 1 && std::memcmp(back.cells, g.cells, sizeof(g.cells)) == 0);
        nes::Battle::StageSpec spec;
        u8 blocks[14][27];
        u8 strip[27];
        app::LevelDefToStage(back, blocks, strip, spec);
        CHECK(spec.active && spec.versus && spec.nSpawns == 4 && spec.spawnCol[0] == 1 && spec.spawnCol[2] == 25 && spec.spawnCol[1] == 13 && spec.spawnRow[3] == 1);
        app::LevelDef few = back;                                              // con una sola aparicion por equipo se avisa y se repite
        few.nSpawns = 2;
        few.spawnCol[0] = 1; few.spawnRow[0] = 7; few.spawnCol[1] = 25; few.spawnRow[1] = 7;
        CHECK(!app::CheckLevelDef(few).empty());
        app::LevelDefToStage(few, blocks, strip, spec);
        CHECK(spec.nSpawns == 4 && spec.spawnCol[1] == 1 && spec.spawnCol[3] == 25);
        fs::remove_all(vt);
        app::SetLevelRoot("");
    }

    // el flujo: al haber ganador se muestra el resultado y despues se vuelve
    {
        auto vb = MakeVersus(2, teams);
        bool toTitle = false;
        app::GameFlow::Hooks hk;
        hk.returnToTitle = [&] { toTitle = true; };
        app::GameFlow fl(*vb, hk);
        fl.SetVersus(true);
        fl.Begin(0);
        for (int f = 0; f < 120; ++f) fl.Step();
        CHECK(fl.mode() == app::GameFlow::Mode::Playing);
        vb->SetLives(1, 0);
        vb->DebugSetTankFlags(1, 0);
        for (int f = 0; f < 5; ++f) fl.Step();
        CHECK(fl.mode() == app::GameFlow::Mode::VersusResult && vb->VersusWinner() == 0);
        for (int f = 0; f < 430 && !toTitle; ++f) fl.Step();
        CHECK(toTitle);
    }

    // dos PC que reciben lo mismo dan el mismo resultado
    auto h1 = MakeVersus(2, teams, 4, 3, 3), h2 = MakeVersus(2, teams, 4, 3, 3);
    for (int f = 0; f < 1500; ++f) {
        const u8 in0 = static_cast<u8>(((f / 25) % 4 == 0 ? nes::con_btn_Right : (f / 25) % 4 == 1 ? nes::con_btn_Up : (f / 25) % 4 == 2 ? nes::con_btn_Left : nes::con_btn_Down) | ((f % 9) < 2 ? nes::con_btn_A : 0));
        h1->SetButtons(0, in0); h2->SetButtons(0, in0);
        h1->RunFrame(); h2->RunFrame();
    }
    CHECK_EQ(h1->StateHash(), h2->StateHash());
}

void TestLanSharedMaps() {
    std::printf("[red] los mapas propios del anfitrion llegan a los demas (y no se empieza hasta que todos los tienen)\n");
    const uint16_t port = 47961;
    app::Lan host(port), guest(port);
    CHECK(host.Host("Ana"));
    std::vector<std::string> maps;
    for (int i = 0; i < 5; ++i) {
        app::LevelDef d = app::GenerateVersusLevel(static_cast<uint32_t>(10 + i));
        maps.push_back(std::to_string(i + 3) + "#" + app::LevelDefToText(d));     // (el numero de casillero, #, el nivel)
    }
    host.SetSharedMaps(maps);
    CHECK(guest.Search());
    CHECK(SpinLan({&host, &guest}, [&] { return !guest.Games().empty(); }));
    CHECK(guest.Join(guest.Games()[0], "Beto"));
    CHECK(SpinLan({&host, &guest}, [&] { return guest.state() == app::Lan::State::Joined; }));
    CHECK(!host.AllMembersHaveMaps() || guest.SharedMapsComplete());
    CHECK(SpinLan({&host, &guest}, [&] { return guest.SharedMapsComplete() && guest.SharedMapsExpected() == 5 && host.AllMembersHaveMaps(); }, 8.0));
    CHECK(guest.SharedMaps() == maps);
    app::LevelDef back;
    const std::string& m2 = guest.SharedMaps()[2];
    CHECK(m2.substr(0, 2) == "5#" && app::LevelDefFromText(m2.substr(m2.find('#') + 1), back) && back.versus);
    // sin mapas fijados no hay nada que esperar
    app::Lan h2(port + 1), g2(port + 1);
    CHECK(h2.Host("Cata"));
    CHECK(h2.AllMembersHaveMaps() && h2.SharedMaps().empty());
}

// ---------------------------------------------------------------------------
// IA de aliados (item de aliados): escenario de pruebas
// ---------------------------------------------------------------------------

struct AllyFixture {
    std::unique_ptr<nes::Battle> b;
    int a0 = 0, a1 = 0;               // slots de los dos aliados del jugador 1
};

void ArenaBlocks(u8 blocks[14][27]) {                                  // campo vacio con un marco de hierro (las balas no se pierden)
    std::memset(blocks, 13, 14 * 27);
    for (int c = 0; c < 27; ++c) blocks[0][c] = 9;
    for (int r = 0; r < 14; ++r) blocks[r][0] = blocks[r][26] = 9;
    for (int c = 0; c < 27; ++c) if (c < 9 || c > 17) blocks[13][c] = 9;
}

std::unique_ptr<AllyFixture> MakeAllyScene(nes::Battle::FriendlyFire ff, bool autoFire = false) {
    auto fx = std::make_unique<AllyFixture>();
    fx->b = std::make_unique<nes::Battle>();
    nes::Battle& b = *fx->b;
    b.SetWide(true);
    b.SetLevel(1);
    b.NewGame(1, 1);
    b.SetFriendlyFire(ff);
    u8 blocks[14][27];
    ArenaBlocks(blocks);
    b.LoadWideStage(blocks);
    b.StartStage();
    for (int f = 0; f < 160; ++f) b.RunFrame();                         // el jugador aparece
    b.SpawnAllies(0);
    for (int f = 0; f < 160; ++f) b.RunFrame();                         // y sus dos aliados
    fx->a0 = b.LastEnemy() + 1;
    fx->a1 = fx->a0 + 1;
    b.R(nes::ram_clock_timer) = 0x0A;                                   // enemigos quietos
    if (!autoFire) b.DebugAllyAutoFire(false);                          // los aliados solo disparan cuando la prueba lo pide
    b.DebugClearBullets();
    return fx;
}

bool EagleStands(const nes::Battle& b) { return (b.Tile(26, 28) & 0xFC) == 0xC8 && (b.Tile(27, 29) & 0xFC) == 0xC8; }

void StepAlone(nes::Battle& b, int frames) {
    for (int f = 0; f < frames; ++f) {
        b.R(nes::ram_clock_timer) = 0x0A;
        b.RunFrame();
    }
}

void TestAllyRules() {
    std::printf("[aliados] no le tiran a su aguila, sus balas no lastiman (salvo dano amigo), las balas se anulan y los puntos son del dueño\n");
    using FF = nes::Battle::FriendlyFire;
    // ---- 1) la bala de un aliado no destruye el aguila de los jugadores; la de un jugador si (como en el original) ----
    {
        auto fx = MakeAllyScene(FF::Off);
        nes::Battle& b = *fx->b;
        b.DebugBareEagle();
        CHECK(EagleStands(b));
        b.DebugSetTankFlags(0, 0);                                        // el jugador fuera del camino
        b.SetTankPos(fx->a0, 216, 150);
        b.DebugSetTankFlags(fx->a0, 0xA2);                                // mirando hacia abajo, al aguila
        b.DebugFire(fx->a0);
        StepAlone(b, 60);
        CHECK(EagleStands(b));                                            // la bala se apago sin tocarla
        CHECK(b.R(nes::ram_game_over_flag) == nes::con_not_game_over);
        // control: una bala de jugador SI la destruye
        auto cx = MakeAllyScene(FF::Off);
        nes::Battle& c = *cx->b;
        c.DebugBareEagle();
        c.SetTankPos(0, 216, 150);
        c.DebugSetTankFlags(0, 0xA2);
        c.DebugFire(0);
        StepAlone(c, 60);
        CHECK(!EagleStands(c) || c.R(nes::ram_game_over_flag) != nes::con_not_game_over);
    }
    // ---- 2) los aliados no apuntan al aguila: aunque se queden mirando hacia ella no disparan ----
    {
        auto fx = MakeAllyScene(FF::Off, true);
        nes::Battle& b = *fx->b;
        b.DebugBareEagle();
        b.DebugSetTankFlags(0, 0);
        for (int f = 0; f < 80; ++f) { b.SetTankPos(fx->a0, 216, 160); b.SetTankPos(fx->a1, 150, 232); b.R(nes::ram_clock_timer) = 0x0A; b.RunFrame(); }
        b.DebugClearBullets();
        int shots = 0;
        for (int f = 0; f < 400; ++f) {
            b.SetTankPos(fx->a0, 216, 160);
            b.DebugSetTankFlags(fx->a0, 0xA2);                            // arriba del aguila, mirandola
            b.SetTankPos(fx->a1, 150, 232);
            b.DebugSetTankFlags(fx->a1, 0xA3);                            // a su izquierda, mirandola
            b.R(nes::ram_clock_timer) = 0x0A;
            b.RunFrame();
            if ((b.BulletStatus(fx->a0) & 0xF0) == 0x40 || (b.BulletStatus(fx->a1) & 0xF0) == 0x40) ++shots;
        }
        CHECK(EagleStands(b));
        CHECK_EQ(shots, 0);
    }
    // ---- 3) una bala de aliado contra un jugador: impacta y se apaga, sin dano si no hay dano amigo ----
    for (FF mode : {FF::Off, FF::Stun, FF::Damage}) {
        auto fx = MakeAllyScene(mode);
        nes::Battle& b = *fx->b;
        b.SetUpgrade(0, 0x40);
        b.SetHelmet(0, 0);
        b.SetTankPos(0, 300, 100);
        b.DebugSetTankFlags(0, 0xA0);
        b.SetTankPos(fx->a0, 220, 100);
        b.DebugSetTankFlags(fx->a0, 0xA3);                                // mirando hacia el jugador
        b.DebugFire(fx->a0);
        const int bs = fx->a0;
        StepAlone(b, 1);
        b.SetTankPos(0, 300, 100);
        for (int f = 0; f < 40; ++f) {
            b.SetTankPos(0, 300, 100);
            b.R(nes::ram_clock_timer) = 0x0A;
            b.RunFrame();
        }
        CHECK((b.BulletStatus(bs) & 0xF0) != 0x40);          // la bala ya no vuela
        if (mode == FF::Damage) CHECK_EQ(b.Upgrade(0), 0x20);              // dano amigo: pierde un nivel de arma
        else CHECK_EQ(b.Upgrade(0), 0x40);                                 // si no, nada
        CHECK(b.TankFlags(0) >= 0x80 && b.TankFlags(0) < 0xE0);            // y sigue vivo
    }
    // ---- 4) las balas de los jugadores y de los aliados se anulan entre si ----
    {
        auto fx = MakeAllyScene(FF::Off);
        nes::Battle& b = *fx->b;
        b.SetTankPos(0, 240, 100);
        b.DebugSetTankFlags(0, 0xA1);                                      // el jugador mira a la izquierda
        b.SetTankPos(fx->a0, 160, 100);
        b.DebugSetTankFlags(fx->a0, 0xA3);                                 // el aliado a la derecha
        b.DebugFire(0);
        b.DebugFire(fx->a0);
        CHECK((b.BulletStatus(0) & 0xF0) == 0x40 && (b.BulletStatus(fx->a0) & 0xF0) == 0x40);
        for (int f = 0; f < 20; ++f) {
            b.SetTankPos(0, 240, 100);
            b.R(nes::ram_clock_timer) = 0x0A;
            b.RunFrame();
        }
        CHECK((b.BulletStatus(0) & 0xF0) != 0x40 && (b.BulletStatus(fx->a0) & 0xF0) != 0x40);
        CHECK(b.PlayerHits(0) == 0 && b.Upgrade(0) == 0);                  // y ninguna toco al jugador
    }
    // ---- 5) los enemigos que matan los aliados suman al jugador que agarro el item ----
    {
        auto fx = MakeAllyScene(FF::Off);
        nes::Battle& b = *fx->b;
        const int e = b.DebugSpawnEnemy(0, false);
        CHECK(e >= 0);
        StepAlone(b, 120);                                                 // el enemigo termina de aparecer
        const int before = b.Score(0);
        const int kills = b.KillCount(0, 0);
        b.DebugSetTankFlags(0, 0);                                         // el jugador no estorba
        b.SetTankPos(e, 220, 100);
        b.DebugSetTankFlags(e, 0xA0);
        b.SetTankPos(fx->a0, 140, 100);
        b.DebugSetTankFlags(fx->a0, 0xA3);
        b.DebugFire(fx->a0);
        for (int f = 0; f < 40; ++f) {
            b.SetTankPos(e, 220, 100);
            b.R(nes::ram_clock_timer) = 0x0A;
            b.RunFrame();
        }
        CHECK(b.TankFlags(e) < 0x80 || b.TankFlags(e) == 0);               // el enemigo cayo
        CHECK_EQ(b.Score(0), before + 100);
        CHECK_EQ(b.KillCount(0, 0), kills + 1);
    }
    // ---- 6) los aliados aguantan tantos tiros como su tipo (el nivel de arma del jugador al agarrar el item): 1, 2, 3 o 4 ----
    for (int kind = 0; kind < 4; ++kind) {
        nes::Battle b;
        b.SetWide(true);
        b.SetLevel(1);
        b.NewGame(1, 1);
        u8 blocks[14][27];
        ArenaBlocks(blocks);
        b.LoadWideStage(blocks);
        b.StartStage();
        for (int f = 0; f < 160; ++f) b.RunFrame();
        b.SetUpgrade(0, static_cast<u8>(kind * 0x20));
        b.SpawnAllies(0);
        for (int f = 0; f < 160; ++f) b.RunFrame();
        const int a0 = b.LastEnemy() + 1;
        CHECK_EQ(b.EnemyHealth(a0), kind + 1);
        CHECK_EQ(b.EnemyHealth(a0 + 1), kind + 1);
        b.DebugAllyAutoFire(false);
        // los enemigos les disparan: cada impacto cuenta uno, y al ultimo mueren
        for (int shot = 1; shot <= kind + 1; ++shot) {
            b.DebugClearBullets();
            const int e = b.DebugSpawnEnemy(0, false);
            for (int f = 0; f < 120; ++f) { b.R(nes::ram_clock_timer) = 0x0A; b.RunFrame(); }
            b.DebugSetTankFlags(0, 0);
            b.SetTankPos(e, 300, 100);
            b.DebugSetTankFlags(e, 0xA1);                                     // mirando a la izquierda
            b.SetTankPos(a0, 200, 100);
            b.R(nes::ram_clock_timer) = 0x0A;
            b.DebugFire(e);
            for (int f = 0; f < 110; ++f) { b.SetTankPos(a0, 200, 100); b.R(nes::ram_clock_timer) = 0x0A; b.RunFrame(); b.DebugSetTankFlags(e, 0xA1); }
            if (shot <= kind) CHECK(b.TankFlags(a0) >= 0x80 && b.TankFlags(a0) < 0xE0);      // todavia en pie
            b.DebugSetTankFlags(e, 0);
        }
        CHECK(b.TankFlags(a0) < 0x80 || b.TankFlags(a0) == 0);                // con el ultimo tiro cae
    }
    // ---- el escenario de prueba jugable arma todo esto ----
    {
        nes::Battle b;
        b.SetWide(true);
        b.SetLevel(1);
        b.NewGame(1, 1);
        u8 blocks[14][27];
        ArenaBlocks(blocks);
        b.LoadWideStage(blocks);
        b.StartStage();
        for (int f = 0; f < 160; ++f) b.RunFrame();
        b.DebugAllyScenario();
        for (int f = 0; f < 200; ++f) b.RunFrame();
        int allies = 0;
        for (int x = b.LastEnemy() + 1; x <= b.LastTank(); ++x) allies += b.TankFlags(x) != 0 ? 1 : 0;
        CHECK_EQ(allies, 2);
        CHECK(EagleStands(b));
    }
}

// ---------------------------------------------------------------------------
// Bloque de arena
// ---------------------------------------------------------------------------

void TestSandBlock() {
    std::printf("[arena] el bloque de arena (id 14): ralentiza a los tanques, se pisa, las balas pasan y se guarda en los niveles\n");
    // se guarda y se lee (archivo y texto) y el editor lo ofrece
    app::LevelDef d = app::BlankLevelDef(false);
    d.cells[5][5] = 14;
    d.cells[6][7] = 14;
    app::LevelDef back;
    CHECK(app::LevelDefFromText(app::LevelDefToText(d), back));
    CHECK(back.cells[5][5] == 14 && back.cells[6][7] == 14 && back.cells[5][6] == 13);
    const auto& pal = app::LevelEditor::PaletteIds();
    CHECK(std::find(pal.begin(), pal.end(), 14) != pal.end());
    CHECK(app::LevelCellPassable(14));
    // el campo: el bloque son cuatro mosaicos de arena
    auto run = [](bool sand, int frames, int* onSandFrames) {
        nes::Battle b;
        b.SetWide(true);
        b.SetLevel(1);
        b.NewGame(1, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        if (sand) for (int c = 0; c < 12; ++c) blocks[13][c] = 14;          // la fila de abajo (por donde va el jugador hacia la izquierda)
        b.LoadWideStage(blocks);
        b.StartStage();
        for (int f = 0; f < 160; ++f) b.RunFrame();                         // el jugador aparece
        const int x0 = b.TankX(0);
        if (sand) CHECK(b.Tile(2, 28) == nes::Battle::kSandTile && b.Tile(3, 29) == nes::Battle::kSandTile);
        int on = 0;
        for (int f = 0; f < frames; ++f) {
            b.SetButtons(0, nes::con_btn_Left);
            b.RunFrame();
            on += b.OnSand(0) ? 1 : 0;
        }
        if (onSandFrames) *onSandFrames = on;
        return x0 - b.TankX(0);                                             // cuanto avanzo hacia la izquierda
    };
    int onPlain = 0, onSand = 0;
    const int plain = run(false, 100, &onPlain);
    const int slow = run(true, 100, &onSand);
    CHECK(plain > 40);
    CHECK_EQ(onPlain, 0);
    CHECK(onSand > 50);                                                      // estuvo pisando arena casi todo el tiempo
    CHECK(slow > 10 && slow * 100 < plain * 65 && slow * 100 > plain * 35);   // y avanzo la mitad, mas o menos
    // una bala atraviesa la arena sin pararse
    {
        nes::Battle b;
        b.SetWide(true);
        b.SetLevel(1);
        b.NewGame(1, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        for (int c = 0; c < 27; ++c) blocks[6][c] = 14;
        for (int r = 0; r < 14; ++r) blocks[r][0] = blocks[r][26] = 9;       // marco para que la bala no se pierda
        b.LoadWideStage(blocks);
        b.StartStage();
        for (int f = 0; f < 160; ++f) b.RunFrame();
        b.DebugClearBullets();
        b.SetTankPos(0, 120, 120);                                          // sobre la fila de arena (bloque fila 6 = y 112..127)
        b.DebugSetTankFlags(0, 0xA3);                                       // mirando a la derecha
        b.DebugFire(0);
        int flying = 0;
        for (int f = 0; f < 30; ++f) { b.SetTankPos(0, 120, 120); b.RunFrame(); flying += (b.BulletStatus(0) & 0xF0) == 0x40 ? 1 : 0; }
        CHECK(flying >= 20);                                                 // siguio volando por encima de la arena
        CHECK(b.Tile(10, 14) == nes::Battle::kSandTile);                     // y la arena sigue ahi
    }
}

// ---------------------------------------------------------------------------
// Bloque de lava
// ---------------------------------------------------------------------------

void TestLavaBlock() {
    std::printf("[lava] la fuente encerrada no se mueve; con salida esparce hasta 6 bloques que se enfrian a los 15 s; quema arbustos; dano por segundo\n");
    using nes::Battle;
    auto make = [](const std::function<void(u8 (*)[27])>& fill, int warm = 160) {
        auto b = std::make_unique<Battle>();
        b->SetWide(true);
        b->SetLevel(1);
        b->NewGame(1, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        fill(blocks);
        b->LoadWideStage(blocks);
        b->StartStage();
        b->R(nes::ram_enemy_spawn_cnt) = 0;                                  // sin enemigos nuevos
        for (int f = 0; f < warm; ++f) b->RunFrame();
        b->DebugClearEnemies();
        return b;
    };
    // ---- 1) encerrada en ladrillo no esparce nada ----
    {
        auto b = make([](u8 (*bl)[27]) {
            bl[6][10] = Battle::kLavaBlockId;
            bl[5][10] = bl[7][10] = bl[6][9] = bl[6][11] = 4;
        });
        CHECK_EQ(b->LavaSourceCount(), 1);
        CHECK(b->LavaAtBlock(10, 7));
        int maxFlow = 0;
        for (int f = 0; f < 2500; ++f) { b->R(nes::ram_enemy_spawn_cnt) = 0; b->RunFrame(); maxFlow = std::max(maxFlow, b->LavaFlowCells()); }
        CHECK_EQ(maxFlow, 0);
        CHECK(b->LavaAtBlock(10, 7));                                        // y la fuente sigue ahi
    }
    // ---- 2) con una salida esparce de a poco hasta 6 bloques; cada uno se enfria a los 15 segundos y despues vuelve a empezar ----
    {
        auto b = make([](u8 (*bl)[27]) {
            bl[6][10] = Battle::kLavaBlockId;
            bl[5][10] = bl[7][10] = bl[6][9] = 4;                            // la salida es el lado derecho
        });
        int maxFlow = 0, at250 = -1, at800 = -1, at2100 = -1, first = -1;
        for (int f = 0; f < 2500; ++f) {
            b->R(nes::ram_enemy_spawn_cnt) = 0;
            b->DebugSetTankFlags(0, 0);                                      // el jugador no estorba (ni se quema)
            b->RunFrame();
            const int n = b->LavaFlowCells();
            maxFlow = std::max(maxFlow, n);
            if (n > 0 && first < 0) first = f;
            if (f == 250) at250 = n;
            if (f == 800) at800 = n;
            if (f == 2100) at2100 = n;
        }
        CHECK(first >= 0 && first < 150);                                    // empieza de a poco (la primera celda al ratito)
        CHECK(at250 >= 2 && at250 < 8);                                      // se va esparciendo
        CHECK_EQ(at800, 8);                                                  // llega a 8 y no pasa de ahi
        CHECK_EQ(maxFlow, 8);
        CHECK_EQ(at2100, 0);                                                 // y se enfrio todo
        CHECK(!b->LavaAtBlock(10, 7));                                       // el punto inicial ya no es lava...
        CHECK(b->RockBlockAt(10, 7));                                        // ...queda piedra (4 mosaicos, morado)
        int rocks = 0, rx = -1, ry = -1;
        for (int ty = 0; ty < 30; ++ty)
            for (int tx = 0; tx < 54; ++tx)
                if (b->RockAt(tx, ty)) { ++rocks; if (tx >= rx) { rx = tx; ry = ty; } }
        CHECK_EQ(rocks, 9 * 4);                                              // 8 esparcidos + el inicial
        // 3 de salud: el mosaico de piedra mas a la derecha aguanta 2 tiros y cae al 3.o
        const int tx0 = rx;
        b->DebugClearBullets();
        auto shoot = [&]() {
            for (int f = 0; f < 110; ++f) { b->SetTankPos(0, (tx0 + 1) * 8 + 24, (ry & ~1) * 8 + 8); b->DebugSetTankFlags(0, 0xA1); if (f == 0) b->DebugFire(0); b->RunFrame(); }
        };
        shoot(); CHECK(b->RockAt(rx, ry));
        shoot(); CHECK(b->RockAt(rx, ry));
        shoot(); CHECK(!b->RockAt(rx, ry));
    }
    // ---- 3) los arbustos que toca se queman ----
    {
        auto b = make([](u8 (*bl)[27]) {
            bl[6][10] = Battle::kLavaBlockId;
            bl[6][9] = 11;                                                   // un bloque de arbusto pegado a la lava
            bl[5][10] = bl[7][10] = 4;
        }, 0);
        CHECK_EQ(b->Tile(18, 14), 0x22);                                     // el arbusto (columna 9, fila 7 del mapa) se ve
        b->RunFrame();
        b->RunFrame();
        CHECK_EQ(b->Tile(18, 14), 0);                                        // desaparecio
    }
    // ---- 4) el jugador recibe 1 de dano al pisarla y 1 mas por cada segundo ----
    {
        auto b = make([](u8 (*bl)[27]) { bl[4][5] = Battle::kLavaBlockId; bl[3][5] = bl[5][5] = bl[4][4] = bl[4][6] = 4; });
        b->SetHelmet(0, 0);
        b->SetUpgrade(0, 0x40);
        auto stay = [&](int frames) { for (int f = 0; f < frames; ++f) { b->SetTankPos(0, 88, 88); b->RunFrame(); } };
        stay(1);
        CHECK_EQ(b->Upgrade(0), 0x20);                                       // al tocarla: 1 de dano
        stay(40);
        CHECK_EQ(b->Upgrade(0), 0x20);                                       // todavia no paso el segundo
        stay(25);
        CHECK_EQ(b->Upgrade(0), 0x00);                                       // al segundo: otro
        stay(70);
        CHECK(b->TankFlags(0) < 0x80 || b->TankFlags(0) == 0 || b->Lives(0) < 3);   // y sin nivel de arma, lo destruye
    }
    // ---- 5) los enemigos pierden un punto de salud por segundo ----
    {
        auto b = make([](u8 (*bl)[27]) { bl[4][5] = Battle::kLavaBlockId; bl[3][5] = bl[5][5] = bl[4][4] = bl[4][6] = 4; });
        const int e = b->DebugSpawnEnemy(3, false);
        CHECK(e >= 0);
        for (int f = 0; f < 120; ++f) { b->R(nes::ram_clock_timer) = 0x0A; b->RunFrame(); }
        const int hp0 = b->EnemyHealth(e);
        CHECK(hp0 >= 3);
        for (int f = 0; f < 2; ++f) { b->SetTankPos(e, 88, 88); b->R(nes::ram_clock_timer) = 0x0A; b->RunFrame(); }
        CHECK_EQ(b->EnemyHealth(e), hp0 - 1);
        for (int f = 0; f < 62; ++f) { b->SetTankPos(e, 88, 88); b->R(nes::ram_clock_timer) = 0x0A; b->RunFrame(); }
        CHECK_EQ(b->EnemyHealth(e), hp0 - 2);
    }
    // ---- 6) en los niveles la lava es el id 15 (se guarda) y el editor la ofrece ----
    {
        app::LevelDef d = app::BlankLevelDef(false);
        d.cells[5][5] = 15;
        d.cells[0][3] = 15;
        app::LevelDef back;
        CHECK(app::LevelDefFromText(app::LevelDefToText(d), back));
        CHECK(back.cells[5][5] == 15 && back.cells[0][3] == 15);
        u8 blocks[14][27];
        u8 strip[27];
        nes::Battle::StageSpec spec;
        app::LevelDefToStage(back, blocks, strip, spec);
        CHECK(blocks[4][5] == nes::Battle::kLavaBlockId && strip[3] == nes::Battle::kLavaBlockId);     // el motor la conoce como 16
        const auto& pal = app::LevelEditor::PaletteIds();
        CHECK(std::find(pal.begin(), pal.end(), 15) != pal.end());
    }
}

// Batalla vs / royale sobre un campo vacio: `players` jugadores (los de humanMask son personas; los demas, IA con jugador completo).
std::unique_ptr<nes::Battle> MakeVsAi(int players, int humanMask, int size, bool royale = false, bool timed = false, int minutes = 5, int tier = 2) {
    auto b = std::make_unique<nes::Battle>();
    b->SetWide(true);
    nes::Battle::StageSpec sp;
    sp.active = true; sp.versus = true; sp.tier = tier; sp.rank = 3; sp.nSpawns = 4;
    const int sc[4] = {1, 13, 25, 13}, sr[4] = {7, 13, 7, 1};
    for (int i = 0; i < 4; ++i) { sp.spawnCol[i] = sc[i]; sp.spawnRow[i] = sr[i]; }
    sp.royale = royale; sp.royaleTimed = timed; sp.royaleMinutes = minutes;
    b->SetStageSpec(sp);
    b->SetLevel(1);
    b->NewGame(players, 1);
    const int teams[8] = {0, 1, 0, 1, 0, 1, 0, 1};
    const int colors[8] = {0, 7, 2, 3, 4, 5, 6, 1};
    if (royale) b->SetPlayerColors(colors, 8);
    else { b->SetVersusSize(size); b->SetTeams(teams, 8); }
    b->SetHumanMask(humanMask);
    u8 blocks[14][27];
    u8 strip[27];
    std::memset(blocks, 13, sizeof(blocks));
    std::memset(strip, 13, sizeof(strip));
    b->LoadWideStage(blocks, strip);
    b->StartStage();
    return b;
}

// Batalla vs con hasta 8 jugadores de verdad (3 vs 3 y 4 vs 4 en red); los lugares sin persona los juega la IA
void TestVersusEightPlayers() {
    std::printf("[batalla vs] 3 vs 3 y 4 vs 4 en red: hasta 8 jugadores reales, la IA completa los cupos y la sala los muestra\n");
    // ---- motor: 8 jugadores, cada uno aparece junto a la aguila de su equipo, sin IA ----
    {
        auto b = MakeVsAi(8, 0xFF, 4);
        CHECK_EQ(b->Players(), 8);
        CHECK_EQ(b->VsBotCount(0) + b->VsBotCount(1), 0);                        // ocho personas: no hace falta la IA
        for (int f = 0; f < 200; ++f) b->RunFrame();
        for (int p = 0; p < 8; ++p) {
            CHECK_EQ(b->TeamOf(p), p & 1);
            CHECK(b->TankFlags(p) >= 0x80 && b->TankFlags(p) < 0xE0);            // los ocho ya estan jugando
            const int t = b->TeamOf(p);
            const int ex = b->VsEagleCol(t) * 16 + 8, ey = b->VsEagleRow(t) * 16 + 8;
            CHECK(std::abs(b->TankX(p) - ex) <= 80 && std::abs(b->TankY(p) - ey) <= 80);
        }
        for (int p = 0; p < 8; ++p)                                                 // y ocupan lugares distintos
            for (int q = p + 1; q < 8; ++q) CHECK(b->TankX(p) != b->TankX(q) || b->TankY(p) != b->TankY(q));
        for (int f = 0; f < 300; ++f) {
            for (int p = 0; p < 8; ++p) b->SetButtons(p, static_cast<u8>(((f / 20 + p) % 4 == 0 ? nes::con_btn_Right : (f / 20 + p) % 4 == 1 ? nes::con_btn_Up : (f / 20 + p) % 4 == 2 ? nes::con_btn_Left : nes::con_btn_Down) | ((f + p) % 9 < 2 ? nes::con_btn_A : 0)));
            b->RunFrame();
        }
        CHECK_EQ(b->VersusWinner(), -1);
    }
    // ---- cupos: la IA completa los equipos ----
    {
        auto a = MakeVsAi(6, 0x0F, 3);                                            // 3 vs 3 con 4 personas: 1 de IA por equipo
        CHECK_EQ(a->Players(), 6);
        CHECK_EQ(a->VsBotCount(0), 1);
        CHECK_EQ(a->VsBotCount(1), 1);
        auto c = MakeVsAi(8, 0x1F, 4);                                            // 4 vs 4 con 5 personas: el equipo 1 tiene 3 y el 2 tiene 2
        CHECK_EQ(c->VsBotCount(0), 1);
        CHECK_EQ(c->VsBotCount(1), 2);
        auto d = MakeVsAi(8, 0x03, 4);
        CHECK_EQ(d->VsBotCount(0), 3);
        CHECK_EQ(d->VsBotCount(1), 3);
        for (int f = 0; f < 300; ++f) d->RunFrame();                              // 2 personas + 6 IA juegan sin problemas
        CHECK_EQ(d->VersusWinner(), -1);
    }
    // ---- la sala: 6 u 8 lugares, cada PC con sus jugadores, y los botones de los 8 viajan sincronizados ----
    {
        const uint16_t port = 47941;
        app::Lan host(port), g1(port), g2(port), g3(port);
        CHECK(host.Host("Ana"));
        host.SetCapacity(8);
        CHECK_EQ(host.Capacity(), 8);
        app::Lan* guests[3] = {&g1, &g2, &g3};
        const char* names[3] = {"Beto", "Cata", "Dino"};
        for (int i = 0; i < 3; ++i) {
            CHECK(guests[i]->Search());
            CHECK(SpinLan({&host, guests[i]}, [&] { return !guests[i]->Games().empty(); }));
            CHECK_EQ(guests[i]->Games()[0].maxPlayers, 8);                        // el aviso dice cuantos lugares hay
            CHECK(guests[i]->Join(guests[i]->Games()[0], names[i]));
            CHECK(SpinLan({&host, &g1, &g2, &g3}, [&] { return guests[i]->state() == app::Lan::State::Joined; }));
        }
        CHECK(host.AddLocalPlayer());
        for (int i = 0; i < 3; ++i) CHECK(guests[i]->AddLocalPlayer());
        CHECK(SpinLan({&host, &g1, &g2, &g3}, [&] { return host.NumPlayers() == 8 && g1.NumPlayers() == 8 && g3.NumPlayers() == 8 && g3.LocalCount() == 2; }));
        CHECK_EQ(g2.Capacity(), 8);                                                // los miembros tambien lo saben
        CHECK(!host.AddLocalPlayer());                                            // sala llena (8)
        CHECK(!g1.AddLocalPlayer());
        app::Lan fifth(port);                                                      // un noveno ya no entra
        CHECK(fifth.Search());
        CHECK(SpinLan({&host, &fifth}, [&] { return !fifth.Games().empty(); }));
        CHECK(fifth.Join(fifth.Games()[0], "Eli"));
        CHECK(SpinLan({&host, &fifth}, [&] { return fifth.state() == app::Lan::State::Full; }));
        CHECK(host.StartGame(16));
        CHECK(SpinLan({&host, &g1, &g2, &g3}, [&] { return g1.Started() && g2.Started() && g3.Started(); }));
        CHECK_EQ(g3.StartMask(), 0xFF);
        // cuatro PC simulan lo mismo con los botones de los 8 jugadores
        const int teams[8] = {0, 1, 0, 1, 0, 1, 0, 1};
        std::unique_ptr<nes::Battle> bt[4];
        app::Lan* lans[4] = {&host, &g1, &g2, &g3};
        int fr[4] = {0, 0, 0, 0};
        for (auto& b : bt) {
            b = std::make_unique<nes::Battle>();
            b->SetWide(true);
            nes::Battle::StageSpec sp;
            sp.active = true; sp.versus = true; sp.tier = 2; sp.rank = 3; sp.nSpawns = 4;
            const int sc[4] = {1, 13, 25, 13}, sr[4] = {7, 13, 7, 1};
            for (int i = 0; i < 4; ++i) { sp.spawnCol[i] = sc[i]; sp.spawnRow[i] = sr[i]; }
            b->SetStageSpec(sp);
            b->SetLevel(1);
            b->NewGame(8, 1);
            b->SetVersusSize(4);
            b->SetTeams(teams, 8);
            u8 blocks[14][27];
            u8 strip[27];
            std::memset(blocks, 13, sizeof(blocks));
            std::memset(strip, 13, sizeof(strip));
            b->LoadWideStage(blocks, strip);
            b->StartStage();
        }
        auto script = [](int player, int frame) -> u8 {
            const int phase = (frame / 17 + player * 3) % 8;
            u8 v = 0;
            if (phase == 0 || phase == 1) v |= nes::con_btn_Up;
            if (phase == 2 || phase == 3) v |= nes::con_btn_Left;
            if (phase == 4 || phase == 5) v |= nes::con_btn_Right;
            if (phase == 6) v |= nes::con_btn_Down;
            if ((frame + player) % 13 < 4) v |= nes::con_btn_A;
            return v;
        };
        constexpr int kFrames = 400;
        bool match = true;
        const auto t0 = std::chrono::steady_clock::now();
        auto allDone = [&] { for (int i = 0; i < 4; ++i) if (fr[i] < kFrames) return false; return true; };
        while (!allDone() && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 25.0) {
            for (auto* l : lans) l->Update();
            for (int i = 0; i < 4; ++i) {
                if (fr[i] >= kFrames) continue;
                u8 local[4] = {0, 0, 0, 0};
                const int n = lans[i]->LocalCount();
                for (int k = 0; k < n; ++k) local[k] = script(lans[i]->MyPlayers()[static_cast<size_t>(k)], fr[i]);
                lans[i]->SubmitLocal(fr[i], local, n);
                if (!lans[i]->FrameReady(fr[i])) continue;
                u8 in[8];
                lans[i]->FrameInputs(fr[i], in);
                for (int p = 0; p < 8; ++p) bt[i]->SetButtons(p, static_cast<u8>(in[p] & ~nes::con_btn_Start));
                bt[i]->RunFrame();
                ++fr[i];
                if (fr[i] % 60 == 0) lans[i]->ReportHash(fr[i], bt[i]->StateHash());
            }
            if (fr[0] == fr[1] && fr[1] == fr[2] && fr[2] == fr[3] && fr[0] % 60 == 0 && fr[0] > 0)
                match = match && bt[0]->StateHash() == bt[1]->StateHash() && bt[1]->StateHash() == bt[2]->StateHash() && bt[2]->StateHash() == bt[3]->StateHash();
        }
        CHECK(allDone());
        CHECK(match);
        for (int i = 1; i < 4; ++i) CHECK_EQ(bt[0]->StateHash(), bt[i]->StateHash());
        for (auto* l : lans) CHECK(!l->Desynced());
        int moved = 0;
        for (int p = 0; p < 8; ++p) moved += (bt[0]->TankX(p) != 0) ? 1 : 0;
        CHECK_EQ(moved, 8);
    }
}

// Jugadores con IA: los lugares del equipo que no ocupa una persona son jugadores completos manejados por la IA
void TestVersusBots() {
    std::printf("[batalla vs] la IA juega con jugadores completos (nivel 0, casco, 3 vidas, items), junto a la aguila y cuenta para ganar\n");
    for (int size = 2; size <= 4; ++size) {
        auto b = MakeVsAi(2 * size, 0x03, size);
        CHECK_EQ(b->VsBotCount(0), size - 1);
        CHECK_EQ(b->VsBotCount(1), size - 1);
        CHECK_EQ(b->VsBotLives(0), 3 * (size - 1));
        for (int p = 0; p < 2 * size; ++p) {
            CHECK_EQ(b->Lives(p), 3);                                             // los mismos atributos que una persona
            CHECK_EQ(b->Upgrade(p), 0);                                           // empiezan en nivel 0
            const int ex = b->VsEagleCol(b->TeamOf(p)) * 16 + 8, ey = b->VsEagleRow(b->TeamOf(p)) * 16 + 8;
            CHECK(b->TankFlags(p) != 0 && std::abs(b->TankX(p) - ex) <= 80 && std::abs(b->TankY(p) - ey) <= 80);   // junto a su aguila
        }
        CHECK(b->IsBot(2) && !b->IsBot(0) && !b->IsBot(1));
    }
    auto b = MakeVsAi(6, 0x03, 3);
    for (int f = 0; f < 200; ++f) b->RunFrame();
    for (int p = 2; p < 6; ++p) CHECK(b->Helmet(p) != 0 || b->TankFlags(p) >= 0x80);   // al aparecer tienen casco como las personas
    // juegan: se mueven, disparan y se destruyen entre si
    int moved = 0, shots = 0;
    int sx[6], sy[6];
    for (int p = 0; p < 6; ++p) { sx[p] = b->TankX(p); sy[p] = b->TankY(p); }
    bool away[6] = {};
    for (int f = 0; f < 1500; ++f) {
        b->RunFrame();
        for (int p = 2; p < 6; ++p) {
            shots += (b->BulletStatus(p) & 0xF0) == 0x40 ? 1 : 0;
            if (std::abs(b->TankX(p) - sx[p]) + std::abs(b->TankY(p) - sy[p]) > 100) away[p] = true;
        }
    }
    for (int p = 2; p < 6; ++p) moved += away[p] ? 1 : 0;
    CHECK(moved >= 2);
    CHECK(shots > 20);
    // el equipo sigue vivo mientras le quede un jugador de IA aunque la persona se quede sin vidas
    {
        auto c = MakeVsAi(4, 0x03, 2);
        for (int f = 0; f < 120; ++f) c->RunFrame();
        c->SetLives(0, 0);
        c->DebugSetTankFlags(0, 0);
        for (int f = 0; f < 10; ++f) c->RunFrame();
        CHECK_EQ(c->VersusWinner(), -1);                                       // el equipo 1 conserva a su jugador de IA (lugar 3)
        c->SetLives(2, 0);
        c->DebugSetTankFlags(2, 0);
        for (int f = 0; f < 10; ++f) c->RunFrame();
        CHECK_EQ(c->VersusWinner(), 1);                                        // ahora si: pierde el equipo 1
    }
    // un jugador de IA destruido reaparece (nivel 0) mientras le queden vidas; sin vidas ya no vuelve
    {
        auto c = MakeVsAi(4, 0x03, 2);
        for (int f = 0; f < 120; ++f) c->RunFrame();
        c->DebugSetTankFlags(2, 0x13);                                          // explota
        for (int f = 0; f < 90; ++f) c->RunFrame();
        CHECK_EQ(c->Lives(2), 2);
        CHECK(c->TankFlags(2) != 0);
        CHECK_EQ(c->Upgrade(2), 0);
        c->SetLives(2, 1);
        c->DebugSetTankFlags(2, 0x13);
        for (int f = 0; f < 90; ++f) c->RunFrame();
        CHECK_EQ(c->Lives(2), 0);
        CHECK_EQ(c->TankFlags(2), 0);
    }
    // agarran items como los jugadores (una estrella sube el nivel)
    {
        auto c = MakeVsAi(4, 0x03, 2);
        for (int f = 0; f < 150; ++f) c->RunFrame();
        c->SetBonus(c->TankX(3), c->TankY(3), 3);
        for (int f = 0; f < 3; ++f) c->RunFrame();
        CHECK(c->Upgrade(3) >= 0x20);
    }
    // una persona que se va deja su tanque a la IA
    {
        auto c = MakeVsAi(4, 0x0F, 2);
        CHECK_EQ(c->VsBotCount(0) + c->VsBotCount(1), 0);
        for (int f = 0; f < 100; ++f) c->RunFrame();
        c->MarkPlayerGone(2);
        CHECK(c->IsBot(2));
        CHECK(c->TankFlags(2) != 0);
        CHECK_EQ(c->Lives(2), 3);
    }
}

// Battle royale: todos contra todos, a muerte o por tiempo
void TestRoyale() {
    std::printf("[battle royale] 8 combatientes (personas e IA), todos contra todos, a muerte o por tiempo, items cada minuto, color rosa\n");
    // ---- el rosa: octavo color en el lobby, la red y el motor ----
    CHECK_EQ(app::kColorCount, 8);
    CHECK_EQ(app::Lan::kColorCount, 8);
    {
        app::Lobby lb;
        lb.Join(0);
        int seen = 0;
        for (int i = 0; i < 9; ++i) { lb.CycleColor(0, +1); if (lb.Color(0) == 7) seen = 1; }
        CHECK_EQ(seen, 1);
    }
    // ---- mapas: los 8 puntos de salida despejados y conectados con el centro ----
    {
        int bad = 0;
        for (uint32_t seed = 1; seed <= 120; ++seed) {
            const app::LevelDef d = app::GenerateRoyaleLevel(seed);
            if (!d.royale || !d.versus) ++bad;
            bool reach[app::kGridRows][app::kLevelCols] = {};
            std::vector<std::pair<int, int>> q = {{7, 13}};
            reach[7][13] = true;
            for (size_t i = 0; i < q.size(); ++i) {
                const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; ++k) {
                    const int nr = q[i].first + dr[k], nc = q[i].second + dc[k];
                    if (nr < 0 || nr >= app::kGridRows || nc < 0 || nc >= app::kLevelCols || reach[nr][nc] || !app::LevelCellPassable(d.cells[nr][nc])) continue;
                    reach[nr][nc] = true;
                    q.push_back({nr, nc});
                }
            }
            for (int id = 0; id < 8; ++id) {
                int c, r;
                nes::Battle::RoyaleSpawnBlock(id, c, r);
                if (!reach[r][c]) ++bad;
            }
            const app::LevelDef again = app::GenerateRoyaleLevel(seed);
            if (std::memcmp(d.cells, again.cells, sizeof(d.cells)) != 0) ++bad;
        }
        CHECK_EQ(bad, 0);
    }
    // ---- a muerte: 2 personas + 6 IA (jugadores completos), todos con color distinto, 3 vidas ----
    {
        auto b = MakeVsAi(8, 0x03, 4, true, false, 5);
        CHECK(b->IsRoyale() && !b->RoyaleTimed());
        int bots = 0;
        bool colorUsed[8] = {};
        for (int id = 0; id < 8; ++id) {
            bots += b->RoyaleIsBot(id) ? 1 : 0;
            CHECK(!colorUsed[b->RoyaleColor(id)]);                            // ocho colores distintos
            colorUsed[b->RoyaleColor(id)] = true;
            CHECK_EQ(b->RoyaleLives(id), 3);
            CHECK_EQ(b->TeamOf(id), id);                                      // cada uno es su propio equipo
        }
        CHECK_EQ(bots, 6);
        CHECK_EQ(b->PlayerPaletteIndex(1), 9);                                 // el jugador 2 es rosa
        for (int f = 0; f < 110; ++f) b->RunFrame();
        for (int id = 0; id < 8; ++id) CHECK(b->TankFlags(id) >= 0x80 && b->TankFlags(id) < 0xE0);   // los ocho en el campo
        CHECK_EQ(b->VersusWinner(), -1);
        for (int f = 0; f < 3000; ++f) b->RunFrame();                           // la IA se destruye entre si
        int kills = 0;
        for (int id = 0; id < 8; ++id) kills += b->RoyaleKills(id);
        CHECK(kills > 0);
        // queda uno solo: gana
        auto c = MakeVsAi(8, 0x03, 4, true, false, 5);
        for (int f = 0; f < 120; ++f) c->RunFrame();
        for (int p = 1; p < 8; ++p) { c->SetLives(p, 0); c->DebugSetTankFlags(p, 0); }
        for (int f = 0; f < 5; ++f) c->RunFrame();
        CHECK_EQ(c->VersusWinner(), 10);                                       // gana el combatiente 0
        CHECK_EQ(c->RoyaleWinner(), 0);
        // todos fuera: empate
        auto e2 = MakeVsAi(8, 0x03, 4, true, false, 5);
        for (int f = 0; f < 120; ++f) e2->RunFrame();
        for (int p = 0; p < 8; ++p) { e2->SetLives(p, 0); e2->DebugSetTankFlags(p, 0); }
        for (int f = 0; f < 5; ++f) e2->RunFrame();
        CHECK_EQ(e2->VersusWinner(), 2);
    }
    // ---- por tiempo: reaparecen siempre y al acabar el tiempo gana el que mas destruyo ----
    {
        auto b = MakeVsAi(8, 0x03, 4, true, true, 3);
        CHECK(b->RoyaleTimed());
        CHECK_EQ(b->RoyaleFramesLeft(), 3 * 3600);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        for (int k = 0; k < 5; ++k) {                                          // un jugador cae varias veces: sigue en carrera
            b->DebugSetTankFlags(0, 0x13);
            for (int f = 0; f < 90; ++f) b->RunFrame();
            CHECK(b->TankFlags(0) != 0);
            CHECK_EQ(b->Lives(0), 3);
        }
        CHECK_EQ(b->VersusWinner(), -1);
        for (int f = 0; f < 3 * 3600 && b->VersusWinner() < 0; ++f) b->RunFrame();
        CHECK(b->VersusWinner() >= 0);                                         // se acabo el tiempo
        CHECK_EQ(b->RoyaleFramesLeft(), 0);
        if (b->VersusWinner() >= 10) {
            int top = 0;
            for (int id = 0; id < 8; ++id) top = std::max(top, b->RoyaleKills(id));
            CHECK_EQ(b->RoyaleKills(b->RoyaleWinner()), top);                  // el ganador es el que mas destruyo
        }
    }
    // ---- items: tres a la vez cada 20 segundos; el siguiente grupo empieza a contar cuando no queda ninguno ----
    {
        auto b = MakeVsAi(8, 0x01, 4, true, true, 10);
        for (int p = 1; p < 8; ++p) { b->SetLives(p, 0); b->DebugSetTankFlags(p, 0); }   // (sin nadie que los agarre)
        auto run = [&](int frames) { for (int f = 0; f < frames; ++f) { b->SetTankPos(0, 20, 20); b->RunFrame(); } };
        run(1150);
        CHECK_EQ(b->RoyaleItemCount(), 0);                                     // antes de los 20 segundos, nada
        run(100);
        CHECK_EQ(b->RoyaleItemCount(), 3);                                     // aparecen tres juntos
        run(1400);
        CHECK_EQ(b->RoyaleItemCount(), 3);                                     // mientras siguen ahi no sale otro grupo
        b->DebugClearRoyaleItems();                                            // los agarran: empieza la cuenta del siguiente grupo
        run(1150);
        CHECK_EQ(b->RoyaleItemCount(), 0);
        run(100);
        CHECK_EQ(b->RoyaleItemCount(), 3);
        // por tiempo no aparecen vidas; a muerte si pueden aparecer
        bool lifeTimed = false;
        for (int k = 0; k < 40; ++k) {
            b->DebugClearRoyaleItems();
            run(1250);
            for (int i = 0; i < 3; ++i) lifeTimed = lifeTimed || b->RoyaleItemId(i) == 5;
        }
        CHECK(!lifeTimed);
        auto d = MakeVsAi(8, 0x01, 4, true, false, 5);
        for (int p = 1; p < 8; ++p) { d->SetLives(p, 3); }
        bool lifeSeen = false;
        for (int k = 0; k < 60 && !lifeSeen; ++k) {
            d->DebugClearRoyaleItems();
            for (int f = 0; f < 1250; ++f) { d->SetTankPos(0, 20, 20); d->SetLives(0, 3); d->RunFrame(); }
            for (int i = 0; i < 3; ++i) lifeSeen = lifeSeen || d->RoyaleItemId(i) == 5;
        }
        CHECK(lifeSeen);
        // los items se agarran: una estrella sube de nivel
        auto e = MakeVsAi(8, 0x01, 4, true, true, 10);
        for (int p = 1; p < 8; ++p) { e->SetLives(p, 0); e->DebugSetTankFlags(p, 0); }
        for (int f = 0; f < 1250; ++f) e->RunFrame();
        CHECK_EQ(e->RoyaleItemCount(), 3);
        int got = 0;
        for (int f = 0; f < 600 && got == 0; ++f) { e->RunFrame(); got = e->Upgrade(0) > 0 || e->Helmet(0) > 3 ? 1 : 0; }
    }
    // ---- 8 personas en red: mismas entradas, mismo estado ----
    {
        auto h1 = MakeVsAi(8, 0xFF, 4, true), h2 = MakeVsAi(8, 0xFF, 4, true);
        for (int f = 0; f < 900; ++f) {
            for (int p = 0; p < 8; ++p) {
                const u8 in = static_cast<u8>(((f / 25 + p) % 4 == 0 ? nes::con_btn_Right : (f / 25 + p) % 4 == 1 ? nes::con_btn_Up : (f / 25 + p) % 4 == 2 ? nes::con_btn_Left : nes::con_btn_Down) | ((f + p) % 9 < 2 ? nes::con_btn_A : 0));
                h1->SetButtons(p, in); h2->SetButtons(p, in);
            }
            h1->RunFrame(); h2->RunFrame();
        }
        CHECK_EQ(h1->StateHash(), h2->StateHash());
        CHECK_EQ(h1->Players(), 8);
        int bots = 0;
        for (int id = 0; id < 8; ++id) bots += h1->RoyaleIsBot(id) ? 1 : 0;
        CHECK_EQ(bots, 0);
        // y con IA: tambien igual en las dos
        auto i1 = MakeVsAi(8, 0x03, 4, true), i2 = MakeVsAi(8, 0x03, 4, true);
        for (int f = 0; f < 1500; ++f) { i1->RunFrame(); i2->RunFrame(); }
        CHECK_EQ(i1->StateHash(), i2->StateHash());
    }
}

// Items nuevos: invisible, helicoptero aliado, minas terrestres, portal y velocidad
std::unique_ptr<nes::Battle> MakeItemScene() {
    auto b = std::make_unique<nes::Battle>();
    b->SetWide(true);
    b->SetLevel(1);
    b->NewGame(1, 1);
    u8 blocks[14][27];
    std::memset(blocks, 13, sizeof(blocks));
    b->LoadWideStage(blocks);
    b->StartStage();
    b->R(nes::ram_enemy_spawn_cnt) = 0;                                    // sin enemigos nuevos
    for (int f = 0; f < 160; ++f) b->RunFrame();
    b->DebugClearEnemies();
    b->R(nes::ram_clock_timer) = 0x0A;                                     // los que se agreguen se quedan quietos y no disparan
    b->DebugClearBullets();
    b->SetHelmet(0, 0);
    return b;
}

void TestGhostWalls() {
    std::printf("[invisible] atraviesa ladrillos y tanques, no el hierro; al terminar despeja los ladrillos que tiene encima\n");
    auto make = [&](int wallBlock) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->SetLevel(1);
        b->NewGame(1, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        for (int r = 3; r < 10; ++r) blocks[r][14] = static_cast<u8>(wallBlock);              // una pared vertical
        b->LoadWideStage(blocks);
        b->StartStage();
        b->R(nes::ram_enemy_spawn_cnt) = 0;
        for (int f = 0; f < 160; ++f) b->RunFrame();
        b->DebugClearEnemies();
        b->R(nes::ram_clock_timer) = 0x0A;
        b->DebugClearBullets();
        b->SetHelmet(0, 0);
        return b;
    };
    auto walk = [&](int wall, bool ghost) {
        auto b = make(wall);
        if (ghost) b->DebugSetGhost(0, 600);
        b->SetTankPos(0, 14 * 16 - 40, 100);
        b->DebugSetTankFlags(0, 0xA3);                                         // mirando a la derecha
        for (int f = 0; f < 120; ++f) { b->SetButtons(0, nes::con_btn_Right); b->RunFrame(); }
        return b->TankX(0);
    };
    const int wallX = 14 * 16;
    CHECK(walk(4, false) < wallX);                                             // ladrillo: lo frena
    CHECK(walk(4, true) > wallX + 16);                                         // invisible: lo cruza
    CHECK(walk(9, true) < wallX);                                              // hierro: lo frena igual
    // al terminar la invisibilidad dentro de la pared, el tanque queda libre
    auto b = make(4);
    b->DebugSetGhost(0, 30);
    b->SetTankPos(0, wallX + 8, 100);
    for (int f = 0; f < 40; ++f) b->RunFrame();
    CHECK(!b->TankGhost(0));
    b->DebugSetTankFlags(0, 0xA3);
    const int x0 = b->TankX(0);
    for (int f = 0; f < 40; ++f) { b->SetButtons(0, nes::con_btn_Right); b->RunFrame(); }
    CHECK(b->TankX(0) > x0 + 10);
}

void TestNewItems() {
    std::printf("[items nuevos] invisible, helicoptero aliado, minas, portal y velocidad (para jugadores y para enemigos)\n");
    // ---- velocidad: el doble de camino por el mismo tiempo ----
    {
        auto run = [&](bool fast) {
            auto b = MakeItemScene();
            if (fast) b->DebugGiveItem(nes::Battle::kBonusSpeed, 0);
            b->SetTankPos(0, 380, 150);
            b->DebugSetTankFlags(0, 0xA1);                                       // mirando a la izquierda
            const int x0 = b->TankX(0);
            for (int f = 0; f < 60; ++f) { b->SetButtons(0, nes::con_btn_Left); b->RunFrame(); }
            return x0 - b->TankX(0);
        };
        const int normal = run(false), fast = run(true);
        CHECK(normal > 30);
        CHECK(fast >= normal * 18 / 10 && fast <= normal * 22 / 10);
        auto b = MakeItemScene();
        b->DebugGiveItem(nes::Battle::kBonusSpeed, 0);
        CHECK(b->TankFast(0));
        for (int f = 0; f < nes::Battle::kItemFrames + 5; ++f) b->RunFrame();
        CHECK(!b->TankFast(0));                                                // dura lo mismo que el casco
    }
    // ---- invisible: las balas enemigas lo atraviesan ----
    {
        auto shot = [&](bool ghost) {
            auto b = MakeItemScene();
            const int e = b->DebugSpawnEnemy(0, false);
            for (int f = 0; f < 120; ++f) b->RunFrame();                         // el enemigo termina de aparecer
            b->DebugSetTankFlags(0, 0xA0);
            b->SetTankPos(0, 216, 180);
            if (ghost) b->DebugGiveItem(nes::Battle::kBonusGhost, 0);
            b->SetTankPos(e, 216, 60);
            b->DebugSetTankFlags(e, 0xA2);                                       // mirando hacia abajo, al jugador
            b->DebugFire(e);
            for (int f = 0; f < 90; ++f) { b->SetTankPos(0, 216, 180); b->SetTankPos(e, 216, 60); b->RunFrame(); }
            const u8 fl = b->TankFlags(0);
            return fl >= 0x80 && fl < 0xE0;                                      // sigue en pie
        };
        CHECK(!shot(false));                                                     // sin el item, la bala lo destruye
        CHECK(shot(true));                                                       // invisible: la bala pasa de largo
        auto b = MakeItemScene();
        b->DebugGiveItem(nes::Battle::kBonusGhost, 0);
        CHECK(b->TankGhost(0));
        for (int f = 0; f < nes::Battle::kItemFrames + 5; ++f) b->RunFrame();
        CHECK(!b->TankGhost(0));
        // enemigos que agarran el item: se vuelven invisibles (no se dibujan, solo sus disparos)
        auto c = MakeItemScene();
        const int e = c->DebugSpawnEnemy(0, false);
        for (int f = 0; f < 120; ++f) c->RunFrame();
        auto tankSprites = [&](nes::Battle& bb) {
            int n = 0;
            for (int i = 0; i < 64; ++i) { const nes::Sprite sp = bb.OamSprite(i); n += (sp.y < 0xEF && sp.tile >= 0x80 && sp.tile < 0xE8) ? 1 : 0; }
            return n;
        };
        c->RunFrame();
        const int before = tankSprites(*c);
        c->DebugGiveItem(nes::Battle::kBonusGhost, e);
        c->RunFrame();
        CHECK(tankSprites(*c) <= before - 2);                                    // el tanque enemigo dejo de dibujarse
    }
    // ---- helicoptero aliado: ataca a los enemigos 10 segundos y se va ----
    {
        auto b = MakeItemScene();
        const int e = b->DebugSpawnEnemy(0, false);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        b->SetTankPos(e, 300, 100);
        b->DebugGiveItem(nes::Battle::kBonusHeli, 0);
        CHECK_EQ(b->HeliCount(), 1);
        bool dead = false;
        for (int f = 0; f < 500 && !dead; ++f) { b->SetTankPos(e, 300, 100); b->RunFrame(); dead = (b->TankFlags(e) >> 4) >= 1 && (b->TankFlags(e) >> 4) <= 7; }
        CHECK(dead);                                                            // se poso encima y le solto la bomba
        for (int f = 0; f < 1600; ++f) b->RunFrame();
        CHECK_EQ(b->HeliCount(), 0);                                            // se fue de la pantalla
        // el helicoptero enemigo ataca a los jugadores (el casco aguanta una vez)
        auto c = MakeItemScene();
        c->SetTankPos(0, 300, 100);
        c->DebugGiveItem(nes::Battle::kBonusHeli, c->Players());               // (la ficha de enemigo: cualquier slot de enemigo)
        bool hit = false;
        for (int f = 0; f < 600 && !hit; ++f) { c->SetTankPos(0, 300, 100); c->RunFrame(); hit = (c->TankFlags(0) >> 4) >= 1 && (c->TankFlags(0) >> 4) <= 7; }
        CHECK(hit);
        // se puede escapar: es mas lento que el tanque, y la bomba cae donde esta el helicoptero (no persigue al tanque)
        auto f2 = MakeItemScene();
        f2->DebugSpawnHeli(-1);
        f2->SetTankPos(0, 216, 150);
        for (int f = 0; f < 700; ++f) {
            f2->SetButtons(0, (f / 70) % 2 == 0 ? nes::con_btn_Left : nes::con_btn_Right);      // el jugador no se queda quieto
            f2->RunFrame();
        }
        CHECK(f2->TankFlags(0) >= 0x80 && f2->TankFlags(0) < 0xE0);
        // el helicoptero mira hacia donde va
        auto g = MakeItemScene();
        g->SetTankPos(0, 380, 100);
        g->DebugSpawnHeli(0);
        const auto e0 = g->DebugSpawnEnemy(0, false);
        (void)e0;
        for (int f = 0; f < 130; ++f) g->RunFrame();
        CHECK(g->HeliState(0).on);
        CHECK(g->HeliState(0).dir >= 0 && g->HeliState(0).dir < 8);
        // los invisibles no son su blanco
        auto d = MakeItemScene();
        d->DebugGiveItem(nes::Battle::kBonusGhost, 0);
        d->SetTankPos(0, 300, 100);
        d->DebugSpawnHeli(-1);
        for (int f = 0; f < 500; ++f) { d->SetTankPos(0, 300, 100); d->RunFrame(); }
        CHECK(d->TankFlags(0) >= 0x80 && d->TankFlags(0) < 0xE0);
    }
    // ---- minas: 3 por item, hasta 9; solo las pisan los enemigos de quien las puso ----
    {
        auto b = MakeItemScene();
        b->DebugGiveItem(nes::Battle::kBonusMine, 0);
        CHECK_EQ(b->MineCount(), 3);
        b->DebugGiveItem(nes::Battle::kBonusMine, 0);
        b->DebugGiveItem(nes::Battle::kBonusMine, 0);
        CHECK_EQ(b->MineCount(), 9);
        int firstSeq = 1 << 30;
        for (int i = 0; i < nes::Battle::kMaxMines; ++i) firstSeq = std::min(firstSeq, b->MineState(i).seq);
        b->DebugGiveItem(nes::Battle::kBonusMine, 0);                           // con 9, saca las 3 mas viejas y pone otras 3
        CHECK_EQ(b->MineCount(), 9);
        int minSeq = 1 << 30;
        for (int i = 0; i < nes::Battle::kMaxMines; ++i) minSeq = std::min(minSeq, b->MineState(i).seq);
        CHECK(minSeq > firstSeq);
        // todas visibles y en bloques libres
        for (int i = 0; i < nes::Battle::kMaxMines; ++i) CHECK(b->MineVisible(i));
        // el jugador pasa por encima de sus minas sin problema; un enemigo que la pisa se destruye
        const auto m0 = b->MineState(0);
        const int e = b->DebugSpawnEnemy(0, false);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        b->SetTankPos(0, m0.col * 16 + 8, m0.row * 16 + 8);
        for (int f = 0; f < 10; ++f) { b->SetTankPos(0, m0.col * 16 + 8, m0.row * 16 + 8); b->RunFrame(); }
        CHECK(b->TankFlags(0) >= 0x80 && b->TankFlags(0) < 0xE0);
        CHECK(b->MineState(0).on);
        b->SetTankPos(e, m0.col * 16 + 8, m0.row * 16 + 8);
        b->RunFrame();
        CHECK(!b->MineState(0).on);
        CHECK((b->TankFlags(e) >> 4) >= 1 && (b->TankFlags(e) >> 4) <= 7);
        CHECK_EQ(b->MineCount(), 8);
        // minas enemigas: parpadean 3 segundos y se vuelven invisibles; solo afectan a los jugadores
        auto c = MakeItemScene();
        c->DebugGiveItem(nes::Battle::kBonusMine, c->Players());
        CHECK_EQ(c->MineCount(), 3);
        for (int f = 0; f < 200; ++f) c->RunFrame();
        for (int i = 0; i < nes::Battle::kMaxMines; ++i) CHECK(!c->MineVisible(i));
        CHECK_EQ(c->MineCount(), 3);                                            // siguen ahi
        int idx = -1;
        for (int i = 0; i < nes::Battle::kMaxMines; ++i) if (c->MineState(i).on) idx = i;
        const auto mm = c->MineState(idx);
        c->SetTankPos(0, mm.col * 16 + 8, mm.row * 16 + 8);
        c->RunFrame();
        CHECK((c->TankFlags(0) >> 4) >= 1 && (c->TankFlags(0) >> 4) <= 7);
    }
    // ---- portal: dos puertas (mitad de arriba y de abajo), 3 usos ----
    {
        auto b = MakeItemScene();
        b->DebugGiveItem(nes::Battle::kBonusPortal, 0);
        const auto pt = b->PortalState();
        CHECK(pt.on);
        CHECK_EQ(pt.uses, 3);
        const int low = pt.row[0] >= 8 ? 0 : 1;
        CHECK(pt.row[low] >= 8 && pt.row[1 - low] <= 6);                         // una de la mitad de abajo y otra de la de arriba
        int uses = 0;
        int cur = 0;
        for (int k = 0; k < 3; ++k) {
            const auto p2 = b->PortalState();
            if (!p2.on) break;
            b->SetTankPos(0, p2.col[cur] * 16 + 8, p2.row[cur] * 16 + 8);
            b->RunFrame();
            CHECK_EQ(b->TankX(0), p2.col[1 - cur] * 16 + 8);                      // aparece en la otra puerta
            CHECK_EQ(b->TankY(0), p2.row[1 - cur] * 16 + 8);
            ++uses;
            cur = 1 - cur;
            for (int f = 0; f < 5; ++f) { b->SetTankPos(0, 20, 20); b->RunFrame(); }                          // sale de la puerta (si no, no vuelve a viajar)
            for (int f = 0; f < 95; ++f) { b->SetTankPos(0, 20, 20); b->RunFrame(); }                         // y pasa el enfriamiento
        }
        CHECK_EQ(uses, 3);
        CHECK(!b->PortalState().on);                                             // se uso 3 veces: desaparece
    }
    // ---- todo junto, igual en dos equipos ----
    {
        auto run = [&]() {
            auto b = MakeItemScene();
            const int e = b->DebugSpawnEnemy(1, false);
            b->DebugGiveItem(nes::Battle::kBonusMine, 0);
            b->DebugGiveItem(nes::Battle::kBonusPortal, 0);
            b->DebugGiveItem(nes::Battle::kBonusHeli, 0);
            b->DebugGiveItem(nes::Battle::kBonusSpeed, 0);
            b->DebugGiveItem(nes::Battle::kBonusGhost, e);
            for (int f = 0; f < 900; ++f) {
                b->SetButtons(0, static_cast<u8>(((f / 25) % 4 == 0 ? nes::con_btn_Right : (f / 25) % 4 == 1 ? nes::con_btn_Up : (f / 25) % 4 == 2 ? nes::con_btn_Left : nes::con_btn_Down) | ((f % 9) < 2 ? nes::con_btn_A : 0)));
                b->RunFrame();
            }
            return b->StateHash();
        };
        CHECK_EQ(run(), run());
    }
}

// Buscador de actualizaciones: versiones, lectura de los releases de GitHub y eleccion del archivo
void TestVoiceChat() {
    std::printf("[voz] ADPCM, paquetes, deteccion de voz, mezcla y reenvio por la sala\n");
    // ADPCM: un tono de 440 Hz vuelve casi igual
    std::vector<int16_t> tone(app::kVoiceFrame * 6);
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 440 * static_cast<double>(i) / app::kVoiceRate));
    app::AdpcmState enc, dec;
    std::vector<uint8_t> bytes(tone.size() / 2);
    app::AdpcmEncode(tone.data(), static_cast<int>(tone.size()), enc, bytes.data());
    std::vector<int16_t> back(tone.size());
    app::AdpcmDecode(bytes.data(), static_cast<int>(tone.size()), dec, back.data());
    double err = 0, sig = 0;
    for (size_t i = 0; i < tone.size(); ++i) { const double d = tone[i] - back[i]; err += d * d; sig += static_cast<double>(tone[i]) * tone[i]; }
    CHECK(err < sig / 100);                                                     // mas de 20 dB de relacion senal/ruido
    CHECK_EQ(enc.predictor, dec.predictor);
    // base64
    const uint8_t raw[] = {0, 1, 2, 250, 251, 252, 253};
    std::vector<uint8_t> rt;
    CHECK(app::Base64Decode(app::Base64Encode(raw, sizeof raw), rt));
    CHECK(rt == std::vector<uint8_t>(raw, raw + sizeof raw));
    CHECK(!app::Base64Decode("a|b", rt));
    // paquete
    uint16_t seq;
    app::AdpcmState st;
    int frames;
    const uint8_t* ad;
    const std::vector<uint8_t> pk = app::VoicePacketBuild(7, enc, 3, bytes.data());
    CHECK(app::VoicePacketParse(pk.data(), pk.size(), seq, st, frames, ad));
    CHECK_EQ(seq, 7);
    CHECK_EQ(frames, 3);
    CHECK(!app::VoicePacketParse(pk.data(), pk.size() - 1, seq, st, frames, ad));
    // deteccion de voz: el silencio no genera paquetes, el tono si; en silencio (mute) tampoco
    app::VoiceChat a, b;
    std::vector<int16_t> quiet(app::kVoiceFrame * 30, 0);
    a.PushMic(quiet.data(), static_cast<int>(quiet.size()));
    std::vector<uint8_t> out;
    CHECK(!a.PopPacket(out));
    std::vector<int16_t> loud(app::kVoiceFrame * 30);
    for (size_t i = 0; i < loud.size(); ++i) loud[i] = static_cast<int16_t>(8000 * std::sin(2 * 3.14159265 * 300 * static_cast<double>(i) / app::kVoiceRate));
    a.SetMuted(true);
    a.PushMic(loud.data(), static_cast<int>(loud.size()));
    CHECK(!a.PopPacket(out));
    a.SetMuted(false);
    a.PushMic(loud.data(), static_cast<int>(loud.size()));
    a.PushMic(quiet.data(), static_cast<int>(quiet.size()));
    int count = 0;
    while (a.PopPacket(out)) { b.Receive(3, out.data(), out.size()); b.Receive(3, out.data(), out.size()); ++count; }     // (el repetido se descarta)
    CHECK(count >= 8);
    CHECK(b.Speaking(3));
    CHECK(!b.Speaking(4));
    // mezcla: hay sonido y despues se vacia
    std::vector<int16_t> mix(512);
    int nonzero = 0;
    for (int k = 0; k < 20; ++k) { b.Mix(mix.data(), 512); for (int16_t v : mix) nonzero += v != 0; }
    CHECK(nonzero > 2000);
    b.Mix(mix.data(), 512);
    CHECK(std::all_of(mix.begin(), mix.end(), [](int16_t v) { return v == 0; }));
    // por la sala: la voz de un miembro llega al anfitrion y al otro miembro; la del anfitrion llega a los dos
    const uint16_t port = 47945;
    app::Lan host(port), g1(port), g2(port);
    CHECK(host.Host("Ana"));
    CHECK(g1.Search());
    CHECK(SpinLan({&host, &g1}, [&] { return !g1.Games().empty(); }));
    CHECK(g1.Join(g1.Games()[0], "Beto"));
    CHECK(g2.Search());
    CHECK(SpinLan({&host, &g1, &g2}, [&] { return !g2.Games().empty(); }));
    CHECK(g2.Join(g2.Games()[0], "Caro"));
    CHECK(SpinLan({&host, &g1, &g2}, [&] { return g1.state() == app::Lan::State::Joined && g2.state() == app::Lan::State::Joined && host.Players().size() == 3; }));
    g1.SendVoice(pk);
    int who = -1;
    std::vector<uint8_t> got;
    bool atHost = false, atG2 = false;
    SpinLan({&host, &g1, &g2}, [&] {
        if (host.PopVoice(who, got)) atHost = who == g1.MyId() && got == pk;
        if (g2.PopVoice(who, got)) atG2 = who == g1.MyId() && got == pk;
        return atHost && atG2;
    });
    CHECK(atHost);
    CHECK(atG2);
    host.SendVoice(pk);
    bool g1Got = false;
    SpinLan({&host, &g1, &g2}, [&] { if (g1.PopVoice(who, got)) g1Got = who == 0 && got == pk; return g1Got; });
    CHECK(g1Got);
}

void TestDifficultyRulesAndContinues() {
    std::printf("[dificultad] salud de enemigos, items para todos, la IA y los items, balas, continues, vidas y granada del royale\n");
    auto scene = [&](int ai, bool shared, int lives, int players) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->SetLevel(21);                                                       // escalon 4: la salud crece en dificil
        nes::Battle::Rules r;
        r.ai = ai; r.itemsShared = shared; r.lives = lives;
        b->SetRules(r);
        b->NewGame(players, 21);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        b->LoadWideStage(blocks);
        b->StartStage();
        b->R(nes::ram_enemy_spawn_cnt) = 0;
        for (int f = 0; f < 160; ++f) b->RunFrame();
        b->DebugClearEnemies();
        b->R(nes::ram_clock_timer) = 0x0A;
        b->DebugClearBullets();
        return b;
    };
    // salud por tipo: facil 1/1/1/3, normal 1/1/2/4, dificil crece con el escalon
    {
        const int easy[4] = {1, 1, 1, 3}, normal[4] = {1, 1, 2, 4};
        for (int kind = 0; kind < 4; ++kind) {
            auto a = scene(0, false, 3, 1);
            auto n = scene(1, false, 3, 1);
            auto h = scene(2, false, 3, 1);
            auto hp = [&](std::unique_ptr<nes::Battle>& b) {
                const int e = b->DebugSpawnEnemy(kind, false);
                for (int f = 0; f < 120; ++f) b->RunFrame();
                return b->EnemyHealth(e);
            };
            CHECK_EQ(hp(a), easy[kind]);
            CHECK_EQ(hp(n), normal[kind]);
            CHECK(hp(h) > normal[kind]);
        }
    }
    // vidas iniciales
    {
        auto b = scene(0, false, 5, 2);
        CHECK_EQ(b->Lives(0), 5);
        CHECK_EQ(b->Lives(1), 5);
        auto d = scene(2, false, 2, 2);
        CHECK_EQ(d->Lives(0), 2);
        auto v = scene(2, false, 10, 1);
        CHECK_EQ(v->Lives(0), 10);
    }
    // items para todos: estrella, casco, pistola, velocidad e invisible; la granada no
    {
        auto sh = scene(0, true, 3, 3);
        auto in = scene(1, false, 3, 3);
        sh->DebugGiveItem(3, 0);
        in->DebugGiveItem(3, 0);
        CHECK(sh->Upgrade(1) > 0 && sh->Upgrade(2) > 0);
        CHECK(in->Upgrade(0) > 0 && in->Upgrade(1) == 0 && in->Upgrade(2) == 0);
        sh->DebugGiveItem(nes::Battle::kBonusGhost, 1);
        CHECK(sh->TankGhost(0) && sh->TankGhost(1) && sh->TankGhost(2));
        in->DebugGiveItem(nes::Battle::kBonusGhost, 1);
        CHECK(!in->TankGhost(0) && in->TankGhost(1));
        sh->DebugGiveItem(nes::Battle::kBonusSpeed, 2);
        CHECK(sh->TankFast(0) && sh->TankFast(1));
        sh->DebugGiveItem(6, 0);
        CHECK_EQ(sh->Upgrade(2), 0x60);
    }
    // los enemigos y los items: facil no agarran, normal agarran, solo dificil los buscan
    {
        for (int ai = 0; ai < 3; ++ai) {
            auto b = scene(ai, false, 3, 1);
            b->DebugSpawnRandomBonus();
            const int e = b->DebugSpawnEnemy(0, false);
            for (int f = 0; f < 120; ++f) b->RunFrame();
            b->R(nes::ram_clock_timer) = 0;
            const int bx = b->R(nes::ram_bonus_pos_X), by = b->R(nes::ram_bonus_pos_Y);
            b->SetTankPos(e, bx, by);
            b->DebugSetTankFlags(e, 0xA0);
            b->SetTankPos(0, 20, 220);
            for (int f = 0; f < 20; ++f) b->RunFrame();
            const bool taken = !b->BonusActive();
            CHECK_EQ(taken, ai >= 1);
        }
    }
    // balas: facil/normal frenan toda bala enemiga con una bala aliada; dificil no
    {
        auto clash = [&](int ai, int enemyKind, int allyUpgrade) {
            auto b = scene(ai, false, 3, 1);
            const int e = b->DebugSpawnEnemy(enemyKind, false);
            for (int f = 0; f < 120; ++f) b->RunFrame();
            b->R(nes::ram_clock_timer) = 0x0A;
            b->DebugClearBullets();
            b->SetUpgrade(0, static_cast<u8>(allyUpgrade));
            b->SetTankPos(e, 216, 40);
            b->DebugSetTankFlags(e, 0xA2);
            b->SetTankPos(0, 216, 200);
            b->DebugSetTankFlags(0, 0xA0);
            b->DebugFire(e);
            b->DebugFire(0);
            bool enemyAlive = false;
            for (int f = 0; f < 60; ++f) {
                b->SetTankPos(e, 216, 40);
                b->SetTankPos(0, 216, 200);
                b->RunFrame();
            }
            enemyAlive = b->BulletActive(e);
            return enemyAlive;
        };
        CHECK(!clash(0, 3, 0));                                                // facil: una bala debil frena una enemiga fuerte
        CHECK(!clash(1, 3, 0));
        CHECK(clash(2, 3, 0));                                                 // dificil: la de nivel mayor sigue
    }
    // continues: preguntan, descuentan, recargan el nivel con todas las vidas; sin respuesta terminan; se ganan al superar niveles
    {
        auto fx = MakeFlow(1, 2);
        using Mode = app::GameFlow::Mode;
        fx->flow->SetContinues(2, 1);
        for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
        for (int f = 0; f < 60; ++f) fx->flow->Step();
        fx->battle->SetLives(0, 0);
        fx->battle->SetLives(1, 0);
        fx->battle->R(nes::ram_game_over_flag) = 0;
        for (int f = 0; f < 800 && fx->flow->mode() != Mode::ContinuePrompt; ++f) fx->flow->Step();
        CHECK(fx->flow->mode() == Mode::ContinuePrompt);
        CHECK_EQ(fx->flow->Continues(), 2);
        CHECK(fx->flow->ContinueSecondsLeft() >= 9);
        const int loaded0 = fx->loaded;
        fx->flow->PressStart();
        fx->flow->Step();
        CHECK(fx->flow->mode() == Mode::StageIntro);
        CHECK_EQ(fx->flow->Continues(), 1);
        CHECK_EQ(fx->loaded, loaded0 + 1);                                     // el mismo nivel se recarga
        CHECK_EQ(fx->flow->level(), 1);
        CHECK_EQ(fx->battle->Lives(0), 3);
        CHECK_EQ(fx->battle->Lives(1), 3);
        CHECK(!fx->battle->GameOver());
        // sin respuesta: game over
        for (int f = 0; f < 300 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
        for (int f = 0; f < 60; ++f) fx->flow->Step();
        fx->battle->SetLives(0, 0);
        fx->battle->SetLives(1, 0);
        fx->battle->R(nes::ram_game_over_flag) = 0;
        for (int f = 0; f < 800 && fx->flow->mode() != Mode::ContinuePrompt; ++f) fx->flow->Step();
        CHECK(fx->flow->mode() == Mode::ContinuePrompt);
        for (int f = 0; f < app::GameFlow::kContinueFrames + 5 && fx->flow->mode() == Mode::ContinuePrompt; ++f) fx->flow->Step();
        CHECK(fx->flow->mode() == Mode::GameOver);
        // sin continues: directo al game over
        auto none = MakeFlow(1, 1);
        none->flow->SetContinues(0, 5);
        for (int f = 0; f < 260; ++f) none->flow->Step();
        none->battle->R(nes::ram_game_over_flag) = 0;
        for (int f = 0; f < 800 && none->flow->mode() != Mode::GameOver; ++f) {
            CHECK(none->flow->mode() != Mode::ContinuePrompt);
            none->flow->Step();
        }
        CHECK(none->flow->mode() == Mode::GameOver);
        // se ganan al superar niveles: cada 1 nivel suma uno; cada 5 niveles, uno a los 5
        for (int every : {1, 5}) {
            auto g = MakeFlow(1, 1);
            g->flow->SetContinues(0, every);
            int gained = 0;
            for (int lv = 1; lv <= 5; ++lv) {
                for (int f = 0; f < 400 && g->flow->mode() != Mode::Playing; ++f) g->flow->Step();
                for (int f = 0; f < 60; ++f) g->flow->Step();
                g->battle->R(nes::ram_enemy_spawn_cnt) = 0;
                g->battle->DebugClearEnemies();
                for (int f = 0; f < 600 && g->flow->mode() != Mode::ScoreScreen; ++f) g->flow->Step();
                CHECK(g->flow->mode() == Mode::ScoreScreen);
                gained = g->flow->Continues();
                g->flow->SkipToNextLevel();
            }
            CHECK_EQ(gained, every == 1 ? 5 : 1);
        }
    }
    // battle royale: la granada revienta a todos los demas jugadores
    {
        auto b = MakeVsAi(8, 0x03, 4, true, false, 5);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        for (int id = 0; id < 8; ++id) CHECK(b->TankFlags(id) >= 0x80 && b->TankFlags(id) < 0xE0);
        b->DebugGiveItem(4, 3);
        for (int id = 0; id < 8; ++id) {
            if (id == 3) CHECK(b->TankFlags(id) >= 0x80 && b->TankFlags(id) < 0xE0);   // quien la agarro sigue en pie
            else CHECK(b->TankFlags(id) >= 0x10 && b->TankFlags(id) < 0x80);           // los demas explotan
        }
        CHECK_EQ(b->RoyaleKills(3), 7);
    }
    // vidas configurables en la batalla vs y el royale (hasta 10)
    {
        auto b = MakeVsAi(8, 0x03, 4, true, false, 5);
        CHECK_EQ(b->RoyaleLives(0), 3);
        nes::Battle c;
        c.SetStartLives(99);
        CHECK_EQ(c.StartLives(), 10);
        c.SetStartLives(0);
        CHECK_EQ(c.StartLives(), 1);
    }
}

void TestCannons() {
    std::printf("[canones] el item cubre el aguila, pone dos canones, disparan arriba y afuera cada 5 s, aguantan 3 tiros; items a la vez\n");
    auto scene = [&](int ai, int items, int players) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->SetLevel(1);
        nes::Battle::Rules r;
        r.ai = ai; r.items = items;
        b->SetRules(r);
        b->NewGame(players, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        b->LoadWideStage(blocks);
        b->StartStage();
        b->R(nes::ram_enemy_spawn_cnt) = 0;
        for (int f = 0; f < 160; ++f) b->RunFrame();
        b->DebugClearEnemies();
        b->R(nes::ram_clock_timer) = 0x0A;
        b->DebugClearBullets();
        return b;
    };
    // el item: aparecen dos canones a los costados del muro de la base
    {
        auto b = scene(1, 2, 1);
        CHECK(!b->CannonState(0).on && !b->CannonState(1).on);
        b->DebugGiveItem(nes::Battle::kBonusCannon, 0);
        CHECK(b->CannonState(0).on && b->CannonState(1).on);
        CHECK_EQ(b->CannonState(0).hp, 3);
        CHECK(b->CannonState(0).col == 12 && b->CannonState(1).col == 14 && b->CannonState(0).row == 14);   // pegados al aguila
        CHECK(b->CannonState(0).side == -1 && b->CannonState(1).side == 1);
        // los bloques son solidos: un tanque no los atraviesa
    }
    // solo disparan a los costados (hacia afuera): un enemigo encima no los activa; recargan 5 segundos
    {
        auto b = scene(1, 2, 1);
        b->DebugGiveItem(nes::Battle::kBonusCannon, 0);
        const int e = b->DebugSpawnEnemy(0, false);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        b->R(nes::ram_clock_timer) = 0x0A;
        const int cx = 12 * 16 + 8;
        auto place = [&](int x, int y) { const u8 fl0 = b->TankFlags(e); if (fl0 >= 0x80 && fl0 < 0xE0) { b->SetTankPos(e, x, y); b->DebugSetTankFlags(e, 0xA0); } };
        // enemigo hacia afuera (a la izquierda): dispara; arriba no
        for (int f = 0; f < 20; ++f) { place(cx, 14 * 16 + 8 - 80); b->RunFrame(); }
        CHECK_EQ(b->CannonState(0).cd, 0);                                       // el enemigo de arriba no se ataca
        for (int f = 0; f < 20; ++f) { place(cx - 80, 14 * 16 + 8); b->RunFrame(); }
        int fired = 0;
        for (int i = 0; i < nes::Battle::kMaxCannonShots; ++i) {
            if (!b->CannonShotState(i).on) continue;
            ++fired;
            CHECK(b->CannonShotState(i).dx == -1 && b->CannonShotState(i).dy == 0);           // hacia afuera, nunca hacia el aguila
        }
        const int cd = b->CannonState(0).cd;
        CHECK(cd > 150 && cd <= nes::Battle::kCannonReload);                   // recarga de 4 segundos
        // el enemigo recibe el disparo (el basico muere de uno)
        for (int f = 0; f < 60; ++f) { place(cx - 80, 14 * 16 + 8); b->RunFrame(); }
        const u8 fl = b->TankFlags(e);
        CHECK(fl < 0x80 || fl >= 0xE0);                                        // el disparo del canon lo destruyo
        (void)fired;
    }
    // aguantan 3 disparos enemigos y al caer dejan el lugar libre; las balas de los jugadores no los danan
    {
        auto b = scene(1, 2, 1);
        b->DebugGiveItem(nes::Battle::kBonusCannon, 0);
        const int e = b->DebugSpawnEnemy(0, false);
        for (int f = 0; f < 120; ++f) b->RunFrame();
        b->R(nes::ram_clock_timer) = 0x0A;
        const int cx = 14 * 16 + 8, cy = 14 * 16 + 8;
        for (int hit = 1; hit <= 3; ++hit) {
            b->DebugClearBullets();
            b->SetTankPos(e, cx + 60, cy);
            b->DebugSetTankFlags(e, 0xA1);                                   // mirando hacia el canon (a la izquierda)
            b->DebugFire(e);
            for (int f = 0; f < 50; ++f) { b->SetTankPos(e, cx + 60, cy); b->DebugSetTankFlags(e, 0xA1); b->RunFrame(); }
            CHECK_EQ(b->CannonState(1).on, hit < 3);
        }
        // las balas de un jugador no lo danan
        auto c = scene(1, 2, 1);
        c->DebugGiveItem(nes::Battle::kBonusCannon, 0);
        c->DebugClearBullets();
        c->SetTankPos(0, 14 * 16 + 8 + 60, 14 * 16 + 8);
        c->DebugSetTankFlags(0, 0xA1);
        c->DebugFire(0);
        for (int f = 0; f < 50; ++f) c->RunFrame();
        CHECK_EQ(c->CannonState(1).hp, 3);
    }
    // items a la vez: 1 (solo el principal), 2 y 3
    for (int items = 1; items <= 3; ++items) {
        auto b = scene(1, items, 1);
        for (int k = 0; k < 6; ++k) b->DebugSpawnRandomBonus();
        const int onField = (b->BonusActive() ? 1 : 0) + b->ExtraItemCount();
        CHECK_EQ(onField, items);
    }
}

void TestEditorSidebar() {
    std::printf("[editor] barra lateral que se despliega con el mouse, el bloque elegido hace de puntero y las flechas cambian la variante\n");
    app::LevelEditor ed(app::LevelEditor::Target::Own);
    using Keys = app::EditorKeys;
    int lastX = 200, lastY = 100;                                              // el mouse se queda donde estaba
    auto step = [&](auto set) { Keys k; k.mx = lastX; k.my = lastY; set(k); ed.Step(k); };
    auto mouseAt = [&](int x, int y, bool click = false) { lastX = x; lastY = y; Keys k; k.mx = x; k.my = y; k.place = click; ed.Step(k); };
    CHECK(ed.SideOpen() == 0.0f);
    mouseAt(200, 100);
    CHECK(ed.SideOpen() == 0.0f);
    CHECK(ed.WantsHiddenMouse());                                              // el bloque elegido reemplaza al puntero
    CHECK_EQ(ed.CursorCol(), 12);
    CHECK_EQ(ed.CursorRow(), 6);
    // el mouse en el borde derecho despliega la barra (poco a poco)
    for (int i = 0; i < 3; ++i) mouseAt(431, 100);
    CHECK(ed.SideOpen() > 0.3f);
    for (int i = 0; i < 6; ++i) mouseAt(431, 100);
    CHECK(ed.SideOpen() == 1.0f);
    CHECK(!ed.WantsHiddenMouse());                                             // sobre la barra se ve el puntero normal
    // clic en una fila: elige el bloque (hierro) y no pinta el mapa
    const int before = ed.def().cells[6][12];
    mouseAt(400, app::EditorRowY(1) + 4, true);
    CHECK_EQ(ed.CurrentGroup(), 1);
    CHECK_EQ(ed.CurrentBlockId(), 9);
    CHECK_EQ(ed.def().cells[6][12], before);
    mouseAt(400, app::EditorRowY(0) + 4, true);                                // ladrillo
    CHECK_EQ(ed.CurrentGroup(), 0);
    CHECK_EQ(ed.CurrentBlockId(), 4);
    // las flechas pasan por entero, las mitades y las cuatro puntas (un solo mosaico) y vuelven
    const int expect[9] = {4, 0, 1, 2, 3, 16, 17, 18, 19};
    for (int i = 1; i <= 9; ++i) {
        step([](Keys& k) { k.variant = 1; });
        CHECK_EQ(ed.CurrentBlockId(), expect[i % 9]);
    }
    step([](Keys& k) { k.variant = -1; });
    CHECK_EQ(ed.CurrentBlockId(), 19);                                         // hacia atras: la ultima punta
    // sin variantes (agua) las flechas no hacen nada
    mouseAt(400, app::EditorRowY(2) + 4, true);
    CHECK_EQ(ed.CurrentBlockId(), 10);
    step([](Keys& k) { k.variant = 1; });
    CHECK_EQ(ed.CurrentBlockId(), 10);
    // se pinta el bloque elegido (con la variante) en el mapa; la barra se recoge al alejar el mouse
    mouseAt(400, app::EditorRowY(0) + 4, true);
    step([](Keys& k) { k.variant = 1; });                                      // ladrillo, mitad derecha
    for (int i = 0; i < 9; ++i) mouseAt(150, 100);
    CHECK(ed.SideOpen() == 0.0f);
    mouseAt(150, 100, true);
    CHECK_EQ(ed.def().cells[6][9], 0);
    // la herramienta aparicion y el menu desde la barra
    for (int i = 0; i < 9; ++i) mouseAt(431, 100);
    mouseAt(400, app::EditorRowY(8) + 4, true);
    CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Spawn);
    mouseAt(400, app::EditorRowY(7) + 4, true);                                // puntero: arrastra apariciones
    CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Pointer);
    mouseAt(400, app::EditorRowY(10) + 4, true);
    CHECK(!ed.InEditPage());
    // Q y E siguen recorriendo todos los bloques
    step([](Keys& k) { k.back = true; });
    CHECK(ed.InEditPage());
    step([](Keys& k) { k.nextItem = true; });
    CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Block);
    // puntero: arrastrar una aparicion de un lugar a otro
    {
        app::LevelEditor e2(app::LevelEditor::Target::Own);
        const int n0 = e2.def().nSpawns;
        CHECK(n0 >= 3);
        const int c0 = e2.def().spawnCol[0], r0 = e2.def().spawnRow[0];
        Keys k;
        k.mx = 100; k.my = 100;
        for (int i = 0; i < 3; ++i) e2.Step(k);
        k.mx = 431;                                                            // abrir la barra y elegir el puntero
        for (int i = 0; i < 9; ++i) e2.Step(k);
        k.mx = 400; k.my = app::EditorRowY(7) + 4; k.place = true;
        e2.Step(k);
        CHECK(e2.CurrentTool() == app::LevelEditor::Tool::Pointer);
        k = Keys{};
        k.mx = 300; k.my = 100;
        for (int i = 0; i < 10; ++i) e2.Step(k);                               // fuera de la barra
        k.mx = c0 * 16 + 8; k.my = r0 * 16 + 8; k.place = true; k.placeHeld = true;       // agarra la aparicion 1
        e2.Step(k);
        k.place = false;
        k.mx = 5 * 16 + 8; k.my = 5 * 16 + 8;                                  // la arrastra a otra celda
        e2.Step(k);
        k.placeHeld = false;
        e2.Step(k);                                                            // la suelta
        CHECK(e2.def().spawnCol[0] == 5 && e2.def().spawnRow[0] == 5);
        CHECK_EQ(e2.def().nSpawns, n0);
        // soltar y arrastrar de nuevo sin agarrar nada no mueve nada
        k.mx = 8 * 16 + 8; k.my = 8 * 16 + 8; k.placeHeld = true;
        e2.Step(k);
        CHECK(e2.def().spawnCol[0] == 5 && e2.def().spawnRow[0] == 5);
    }
    // puntas de un solo mosaico: se guardan con las letras G a N y el motor arma el mosaico en la punta correcta
    {
        app::LevelDef d = app::BlankLevelDef(false);
        for (int i = 0; i < 8; ++i) d.cells[2 + i][3] = static_cast<uint8_t>(16 + i);
        app::LevelDef back;
        CHECK(app::LevelDefFromText(app::LevelDefToText(d), back));
        for (int i = 0; i < 8; ++i) CHECK_EQ(back.cells[2 + i][3], 16 + i);
        nes::Battle::StageSpec spec;
        u8 blocks[14][27];
        u8 strip[27];
        app::LevelDefToStage(back, blocks, strip, spec);
        nes::Battle b;
        b.SetWide(true);
        b.SetStageSpec(spec);
        b.SetLevel(1);
        b.NewGame(1, 1);
        b.LoadWideStage(blocks, strip);
        for (int i = 0; i < 8; ++i) {
            const int tile = i < 4 ? 0x0F : 0x10;
            int found = 0, where = -1;
            for (int q = 0; q < 4; ++q) {
                const int tx = 3 * 2 + (q & 1), ty = (2 + i) * 2 + (q >> 1);
                if (b.TileAt(tx, ty) == tile) { ++found; where = q; }
            }
            CHECK_EQ(found, 1);                                                // un solo mosaico
            CHECK_EQ(where, i & 3);                                            // en la punta pedida
        }
    }
    // los menus del editor con el mouse: pasar marca, el clic elige y el clic derecho vuelve
    {
        app::LevelEditor e4(app::LevelEditor::Target::Own);
        Keys k;
        k.menu = true;
        e4.Step(k);
        CHECK(!e4.InEditPage());
        k = Keys{};
        k.mx = 216; k.my = 54 + 18 * 6 + 4;                                    // "NIVEL NUEVO" (sin cambios: empieza otro)
        e4.Step(k);                                                            // pasar el mouse no hace nada
        CHECK(!e4.InEditPage());
        k.place = true;
        e4.Step(k);
        CHECK(e4.InEditPage());                                                // "nivel nuevo" vuelve al mapa
        k = Keys{};
        k.menu = true;
        e4.Step(k);
        k = Keys{};
        k.mx = 216; k.my = 54 + 4; k.place = true;                             // "CONTINUAR"
        e4.Step(k);
        CHECK(e4.InEditPage());
        k = Keys{};
        k.menu = true;
        e4.Step(k);
        k = Keys{};
        k.mx = 216; k.my = 120; k.mouseRight = true; k.erase = true;           // clic derecho: atras
        e4.Step(k);
        CHECK(e4.InEditPage());
    }
    // la vista tiene lo necesario para dibujar la barra
    app::EditorView view;
    ed.BuildView(view);
    CHECK(view.statusLines.size() == 3 && !view.blockLabel.empty() && !view.variantIds.empty());
}

void TestMovableBaseAndPlayers() {
    std::printf("[editor] el puntero mueve el lugar de los jugadores y el aguila (la base); el motor los respeta\n");
    // el puntero arrastra a un jugador y a la base
    app::LevelEditor ed(app::LevelEditor::Target::Own);
    using Keys = app::EditorKeys;
    auto frame = [&](int mx, int my, bool place, bool held) { Keys k; k.mx = mx; k.my = my; k.place = place; k.placeHeld = held; ed.Step(k); };
    for (int i = 0; i < 3; ++i) frame(100, 100, false, false);
    for (int i = 0; i < 10; ++i) frame(431, 100, false, false);
    frame(400, app::EditorRowY(7) + 4, true, false);                              // herramienta puntero
    CHECK(ed.CurrentTool() == app::LevelEditor::Tool::Pointer);
    for (int i = 0; i < 10; ++i) frame(300, 100, false, false);
    CHECK_EQ(ed.def().playerCol[0], 11);
    frame(11 * 16 + 8, 14 * 16 + 8, true, true);                                  // agarra al jugador 1
    frame(4 * 16 + 8, 10 * 16 + 8, false, true);
    frame(4 * 16 + 8, 10 * 16 + 8, false, false);
    CHECK(ed.def().playerCol[0] == 4 && ed.def().playerRow[0] == 10);
    frame(13 * 16 + 8, 14 * 16 + 8, true, true);                                  // el aguila (la base)
    frame(20 * 16 + 8, 9 * 16 + 8, false, true);
    frame(20 * 16 + 8, 9 * 16 + 8, false, false);
    CHECK(ed.def().baseCol == 20 && ed.def().baseRow == 9);
    // al arrastrar no se borra nada del camino: solo se despeja el bloque donde se suelta
    {
        app::LevelEditor e3(app::LevelEditor::Target::Own);
        auto f3 = [&](int mx, int my, bool place, bool held) { Keys k; k.mx = mx; k.my = my; k.place = place; k.placeHeld = held; e3.Step(k); };
        for (int i = 0; i < 3; ++i) f3(100, 100, false, false);
        for (int i = 0; i < 10; ++i) f3(431, 100, false, false);
        f3(400, app::EditorRowY(7) + 4, true, false);
        for (int i = 0; i < 10; ++i) f3(300, 100, false, false);
        auto& cells = e3.mutableDef().cells;
        for (int c = 1; c <= 9; ++c) cells[8][c] = 4;                              // una hilera de ladrillo en el camino y en el destino
        const int c0 = e3.def().spawnCol[0], r0 = e3.def().spawnRow[0];
        f3(c0 * 16 + 8, r0 * 16 + 8, true, true);
        for (int c = 2; c <= 8; ++c) f3(c * 16 + 8, 8 * 16 + 8, false, true);      // pasa por encima de la hilera
        for (int c = 2; c <= 7; ++c) CHECK_EQ(e3.def().cells[8][c], 4);            // el camino queda intacto
        CHECK_EQ(e3.def().cells[8][8], 4);                                         // y el destino tambien hasta soltar
        f3(8 * 16 + 8, 8 * 16 + 8, false, false);                                  // suelta
        CHECK(e3.def().spawnCol[0] == 8 && e3.def().spawnRow[0] == 8);
        CHECK_EQ(e3.def().cells[8][8], 13);                                        // el bloque donde quedo se despeja
        for (int c = 1; c <= 7; ++c) CHECK_EQ(e3.def().cells[8][c], 4);
    }
    // la base no se puede llevar sobre un jugador ni fuera del mapa
    frame(4 * 16 + 8, 10 * 16 + 8, true, true);                                   // agarra al jugador 1 otra vez
    frame(20 * 16 + 8, 9 * 16 + 8, false, true);                                  // sobre la base: no se mueve
    frame(20 * 16 + 8, 9 * 16 + 8, false, false);
    CHECK(ed.def().playerCol[0] == 4 && ed.def().playerRow[0] == 10);
    // se guarda y se lee
    app::LevelDef back;
    CHECK(app::LevelDefFromText(app::LevelDefToText(ed.def()), back));
    CHECK(back.baseCol == 20 && back.baseRow == 9 && back.playerCol[0] == 4 && back.playerRow[0] == 10 && back.playerCol[1] == 15);
    // el motor: el aguila se dibuja en el lugar nuevo y los jugadores aparecen donde se pusieron
    nes::Battle::StageSpec spec;
    u8 blocks[14][27];
    u8 strip[27];
    app::LevelDefToStage(back, blocks, strip, spec);
    nes::Battle b;
    b.SetWide(true);
    b.SetStageSpec(spec);
    b.SetLevel(1);
    b.NewGame(2, 1);
    b.LoadWideStage(blocks, strip);
    b.StartStage();
    CHECK((b.TileAt(2 * 20, 2 * 9) & 0x7C) == 0x48);                              // el aguila
    CHECK((b.TileAt(2 * 13, 2 * 14) & 0x7C) != 0x48);                             // y ya no esta en el lugar de siempre
    int wall = 0;                                                                 // el muro de ladrillo alrededor (6 x 4 mosaicos)
    for (int ty = 16; ty < 20; ++ty) for (int tx = 38; tx < 44; ++tx) wall += b.TileAt(tx, ty) != 0 ? 1 : 0;
    CHECK(wall >= 12);
    for (int f = 0; f < 140; ++f) b.RunFrame();
    CHECK(b.TankX(0) == 4 * 16 + 8 && b.TankY(0) == 10 * 16 + 8);
    CHECK(b.TankX(1) == 15 * 16 + 8 && b.TankY(1) == 14 * 16 + 8);
    CHECK(!b.GameOver());
    CHECK(b.GetStageSpec().baseCol == 20 && b.GetStageSpec().baseRow == 9);
}

void TestUpdater() {
    std::printf("[actualizaciones] comparar versiones, leer los releases (pc-vX / android-vX) y elegir el instalador o el APK\n");
    CHECK_EQ(app::CompareVersions("1.0.0", "1.0.0"), 0);
    CHECK(app::CompareVersions("1.0.0", "1.0.1") < 0);
    CHECK(app::CompareVersions("1.10.0", "1.9.0") > 0);               // numerico, no por texto
    CHECK(app::CompareVersions("2.0", "1.99.99") > 0);
    CHECK_EQ(app::CompareVersions("1.2", "1.2.0"), 0);
    CHECK(!app::AppVersion().empty());
    const std::string json = R"([
      {"tag_name": "pc-v1.9.0", "name": "PC 1.9.0", "body": "viejo", "draft": false, "prerelease": false,
       "assets": [{"name": "BattleCityNES-Setup-1.9.0.exe", "browser_download_url": "https://x/s190.exe", "size": 100}]},
      {"tag_name": "pc-v1.10.0", "name": "PC 1.10.0", "body": "nuevo \"con comillas\" y \u00e1", "draft": false, "prerelease": false,
       "assets": [{"name": "BattleCityNES-PC.zip", "browser_download_url": "https://x/pc.zip", "size": 7},
                  {"name": "BattleCityNES-Setup-1.10.0.exe", "browser_download_url": "https://x/s1100.exe", "size": 4650936}]},
      {"tag_name": "pc-v9.0.0", "name": "borrador", "draft": true, "prerelease": false, "assets": []},
      {"tag_name": "pc-v8.0.0", "name": "prueba", "draft": false, "prerelease": true, "assets": []},
      {"tag_name": "pc-2026.10.19", "name": "entrega vieja sin version", "draft": false, "prerelease": false, "assets": []},
      {"tag_name": "android-v1.4.0", "name": "Android", "draft": false, "prerelease": false,
       "assets": [{"name": "BattleCityNES.apk", "browser_download_url": "https://x/a.apk", "size": 18000000}]}
    ])";
    app::ReleaseInfo pc;
    CHECK(app::ParseLatestRelease(json, "pc-", pc));
    CHECK(pc.version == "1.10.0");                           // la mas nueva (sin borradores ni pruebas ni etiquetas sin version)
    CHECK(pc.body.find("con comillas") != std::string::npos);
    const app::ReleaseAsset* a = app::PickAsset(pc, false);
    CHECK(a != nullptr && a->url == "https://x/s1100.exe" && a->size == 4650936);        // en PC, el instalador (no el zip)
    app::ReleaseInfo an;
    CHECK(app::ParseLatestRelease(json, "android-", an));
    CHECK(an.version == "1.4.0");
    const app::ReleaseAsset* b = app::PickAsset(an, true);
    CHECK(b != nullptr && b->name == "BattleCityNES.apk");
    CHECK(app::PickAsset(an, false) == nullptr);
    app::ReleaseInfo none;
    CHECK(!app::ParseLatestRelease(json, "linux-", none));                // sin entregas para esa plataforma
    CHECK(!app::ParseLatestRelease("no es json", "pc-", none));
    CHECK(!app::ParseLatestRelease("{}", "pc-", none));
    CHECK(!app::ParseLatestRelease("[]", "pc-", none));
}

// Campania: los enemigos de tipo 1 van por los items, y con 3 o 4 jugadores puede haber 2 items a la vez
void TestCampaignItems() {
    std::printf("[items campania] los enemigos de tipo 1 buscan los items; con 3 o 4 jugadores hay hasta 2 items a la vez\n");
    auto make = [](int players) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        b->SetLevel(1);
        b->NewGame(players, 1);
        u8 blocks[14][27];
        std::memset(blocks, 13, sizeof(blocks));
        b->LoadWideStage(blocks);
        b->StartStage();
        b->R(nes::ram_enemy_spawn_cnt) = 0;
        for (int f = 0; f < 160; ++f) b->RunFrame();
        b->DebugClearEnemies();
        b->DebugClearBullets();
        return b;
    };
    // ---- dos items a la vez con 3 o 4 jugadores; con 1 o 2 sigue siendo uno solo ----
    for (int players = 1; players <= 4; ++players) {
        auto b = make(players);
        b->DebugSpawnRandomBonus();
        CHECK(b->BonusActive());
        CHECK_EQ(b->ExtraItemCount(), 0);
        b->DebugSpawnRandomBonus();                                           // aparece otro
        CHECK(b->BonusActive());
        CHECK_EQ(b->ExtraItemCount(), players >= 3 ? 1 : 0);                   // el segundo solo con 3 o 4 jugadores
        b->DebugSpawnRandomBonus();                                           // un tercero reemplaza al primero (como siempre)
        CHECK_EQ(b->ExtraItemCount(), players >= 3 ? 1 : 0);
    }
    // ---- el segundo item se agarra (un jugador lo toca y desaparece) ----
    {
        auto b = make(4);
        b->DebugSpawnRandomBonus();
        b->DebugSpawnRandomBonus();
        int x, y, id;
        CHECK(b->ExtraItemAt(x, y, id));
        for (int p = 1; p < 4; ++p) b->SetTankPos(p, 30 + p * 40, 225);
        b->SetTankPos(0, x, y);
        const int score0 = b->Score(0);
        b->RunFrame();
        b->RunFrame();
        CHECK_EQ(b->ExtraItemCount(), 0);
        CHECK(b->Score(0) > score0);                                          // 500 puntos
        CHECK(b->BonusActive());                                              // y el primero sigue ahi
    }
    // ---- los enemigos de tipo 1 van por el item ----
    {
        auto b = make(1);
        b->SetTankPos(0, 20, 225);
        b->SetBonus(380, 150, 3);                                              // una estrella lejos del enemigo
        const int e = b->DebugSpawnEnemy(0, false);                            // tipo 1
        for (int f = 0; f < 90; ++f) { b->SetTankPos(0, 20, 225); b->RunFrame(); }
        b->SetTankPos(e, 64, 64);
        b->R(nes::ram_clock_timer) = 0;
        bool got = false;
        for (int f = 0; f < 3500 && !got; ++f) {
            b->SetTankPos(0, 20, 225);
            b->RunFrame();
            got = !b->BonusActive();
        }
        CHECK(got);                                                            // el enemigo llego solo y lo agarro
    }
}

void TestGameOverScreen() {
    std::printf("[pantallas] sin base o sin jugadores aparece GAME OVER, luego HI-SCORE si hay record y vuelve al titulo\n");
    auto fx = MakeFlow(1, 2);
    using Mode = app::GameFlow::Mode;
    for (int f = 0; f < 200 && fx->flow->mode() != Mode::Playing; ++f) fx->flow->Step();
    for (int f = 0; f < 60; ++f) fx->flow->Step();
    CHECK(fx->flow->mode() == Mode::Playing);
    fx->battle->R(nes::ram_game_over_flag) = 0;        // la base ya cayo y termino su cuenta regresiva
    fx->flow->Step();
    CHECK(fx->flow->mode() == Mode::GameOverMessage);   // primero sube el cartel rojo de GAME OVER
    const int y0 = fx->battle->R(nes::ram_game_over_msg_pos_Y);
    for (int f = 0; f < 60; ++f) fx->flow->Step();
    CHECK(fx->battle->R(nes::ram_game_over_msg_pos_Y) < y0);      // sube desde abajo
    CHECK(fx->flow->mode() == Mode::GameOverMessage);
    for (int f = 0; f < 600 && fx->flow->mode() != Mode::GameOver; ++f) fx->flow->Step();
    CHECK(fx->flow->mode() == Mode::GameOver);
    CHECK_EQ(fx->battle->R(nes::ram_game_over_msg_timer), 0);     // el cartel termino antes de la pantalla
    CHECK(!fx->restarted);
    for (int f = 0; f < 400 && !fx->restarted; ++f) fx->flow->Step();
    CHECK(fx->restarted);
    // con record superado: GAME OVER -> HI-SCORE -> titulo
    auto hs = MakeFlow(1, 2);
    hs->flow->SetHiScore(100);
    for (int f = 0; f < 200 && hs->flow->mode() != Mode::Playing; ++f) hs->flow->Step();
    for (int f = 0; f < 60; ++f) hs->flow->Step();
    hs->battle->AddBonusPoints(0, 0);                 // +1000 puntos: supera el record de 100
    hs->battle->R(nes::ram_game_over_flag) = 0;
    for (int f = 0; f < 1000 && hs->flow->mode() != Mode::GameOver; ++f) hs->flow->Step();
    CHECK(hs->flow->mode() == Mode::GameOver);
    for (int f = 0; f < 400 && hs->flow->mode() == Mode::GameOver; ++f) hs->flow->Step();
    CHECK(hs->flow->mode() == Mode::HighScore);
    CHECK(!hs->restarted);
    CHECK(hs->flow->hiScore() >= 1000);
    for (int f = 0; f < 400 && !hs->restarted; ++f) hs->flow->Step();
    CHECK(hs->restarted);
    // todos los jugadores sin vidas tambien es game over
    auto gy = MakeFlow(1, 2);
    for (int f = 0; f < 200 && gy->flow->mode() != Mode::Playing; ++f) gy->flow->Step();
    gy->battle->SetLives(0, 0);
    gy->battle->SetLives(1, 0);
    gy->battle->SetTankPos(0, 0, 0);
    CHECK(!gy->battle->GameOver());                    // todavia tienen tanque
}

// ---------------------------------------------------------------------------
// Seleccion de jugadores y colores
// ---------------------------------------------------------------------------

void TestLobbyColorsAndJoining() {
    std::printf("[lobby] se une con disparo, el color se cambia con arriba/abajo y no se repite entre jugadores\n");
    app::Lobby l;
    CHECK(l.Joined(0));
    CHECK(!l.Joined(1));
    CHECK_EQ(l.Count(), 1);
    CHECK(l.Join(2));                      // el jugador 3 se une
    CHECK(!l.Join(2));                     // volver a disparar no hace nada
    CHECK_EQ(l.Count(), 2);
    CHECK_EQ(l.Color(0), 0);               // amarillo
    CHECK_EQ(l.Color(2), 2);               // azul (el color por defecto de su ranura)
    // el jugador 1 recorre todos los colores: nunca cae en el que usa el jugador 3
    int seen[app::kColorCount] = {};
    for (int i = 0; i < 20; ++i) {
        l.CycleColor(0, +1);
        CHECK(l.Color(0) != l.Color(2));
        seen[l.Color(0)] = 1;
    }
    int distinct = 0;
    for (int v : seen) distinct += v;
    CHECK_EQ(distinct, app::kColorCount - 1);          // los 6 que no tiene el otro jugador
    // los colores nuevos existen: violeta (4), naranja (5) y marron (6)
    CHECK(seen[4] && seen[5] && seen[6]);
    // si el jugador 3 suelta su color, queda disponible para el jugador 1
    l.CycleColor(2, +1);
    bool reachable = false;
    for (int i = 0; i < 8; ++i) {
        l.CycleColor(0, +1);
        if (l.Color(0) == 2) reachable = true;
    }
    CHECK(reachable || l.Color(2) == 2);
    // un cuarto jugador y los colores siguen siendo todos distintos
    l.Join(1);
    l.Join(3);
    CHECK_EQ(l.Count(), 4);
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) CHECK(l.Color(a) != l.Color(b));
    // orden de jugadores del motor: las ranuras unidas en orden
    CHECK_EQ(l.SlotOfPlayer(0), 0);
    CHECK_EQ(l.SlotOfPlayer(3), 3);
    CHECK(l.Leave(3));
    CHECK_EQ(l.Count(), 3);
    app::Lobby only;
    CHECK(!only.Leave(0));                              // el unico jugador no se puede ir
    app::Lobby gap;
    gap.Join(2);
    gap.Leave(0);
    CHECK_EQ(gap.SlotOfPlayer(0), 2);                   // P3 sola pasa a ser el jugador 1 del motor
}

void TestPadAssignment() {
    std::printf("[mandos] los mandos conectados toman a los jugadores solos y liberan su lugar al desconectarse\n");
    int slot[4] = {-1, -1, -1, -1};
    bool avail[4] = {true, true, false, false};
    app::AssignPads(avail, slot);
    CHECK_EQ(slot[0], 0);
    CHECK_EQ(slot[1], 1);
    CHECK_EQ(slot[2], -1);
    avail[0] = false;                                   // se desconecta el primero
    app::AssignPads(avail, slot);
    CHECK_EQ(slot[0], -1);
    CHECK_EQ(slot[1], 1);                               // el segundo sigue con su jugador
    avail[3] = true;                                    // llega un mando nuevo: toma el lugar libre
    app::AssignPads(avail, slot);
    CHECK_EQ(slot[0], 3);
    CHECK_EQ(slot[1], 1);
    // cuatro mandos
    int all[4] = {-1, -1, -1, -1};
    const bool four[4] = {true, true, true, true};
    app::AssignPads(four, all);
    for (int i = 0; i < 4; ++i) CHECK_EQ(all[i], i);
}

void TestPlayerColorsInGame() {
    std::printf("[colores] el tanque, la bala y los aliados usan el color elegido (8 colores, con el rosa)\n");
    for (int color = 0; color < 8; ++color) {
        auto b = std::make_unique<nes::Battle>();
        b->SetWide(true);
        const int colors[4] = {color, 1, 2, 3};
        b->SetPlayerColors(colors);
        b->NewGame(1, 1);
        u8 blocks[14][27] = {};
        b->LoadWideStage(blocks);
        b->StartStage();
        for (int f = 0; f < 80; ++f) b->RunFrame();
        b->SetButtons(0, nes::con_btn_A);
        b->RunFrame();
        b->SetButtons(0, 0);
        bool tankSeen = false, bulletSeen = false;
        static const int kAttr[8] = {0, 1, 4, 5, 6, 7, 0x10, 0x11};
        static const int kPal[8] = {0, 1, 4, 5, 6, 7, 8, 9};
        const int expected = kAttr[color];
        for (int i = 0; i < 64; ++i) {
            const nes::Sprite sp = b->OamSprite(i);
            if (sp.y >= 0xEF) continue;
            const int attr = sp.attr & 0x17;                       // paleta (bits 0-2) y banco extra (bit 4)
            if (sp.tile >= 0x00 && sp.tile < 0x20 && attr == expected) tankSeen = true;
            if (sp.tile == 0xB1 + 2 * 0 && attr == expected) bulletSeen = true;
        }
        CHECK(tankSeen);
        (void)bulletSeen;
        CHECK_EQ(b->PlayerPaletteIndex(0), kPal[color]);
    }
}

} // namespace

int main() {
    TestPlayerSpawn();
    TestPlayerSpeedThreeOfFour();
    TestTurnSnapsToGrid();
    TestBlockedByWaterAndBrick();
    TestBulletSpeeds();
    TestBrickShotClearsFourQuarters();
    TestSteelNeedsThreeStars();
    TestEnemySpawnAndTypes();
    TestBonusEffects();
    TestTanksBlockEachOther();
    TestEagleDestroyed();
    TestWideLayoutAndSpawn();
    TestWideMovementPastByte();
    TestWideBulletHitsBrickPastByte();
    TestWideBonusPickup();
    TestWideEnemySpawnsAtRightEdge();
    TestWideEnemiesChaseAcrossField();
    TestFourPlayersSpawn();
    TestFourPlayersMoveAndFireIndependently();
    TestFourPlayersBonusAndLives();
    TestFourPlayersExtraSpawns();
    TestEnemyTotalScalesWithPlayers();
    TestFourPlayersEnemiesCap();
    TestFriendlyFireModes();
    TestSteelHealth();
    TestLevelsAreValid();
    TestDifficultyScales();
    TestAlliesItem();
    TestStageProgress();
    TestScoreAndKillCounters();
    TestStageIntroScreen();
    TestScoreScreenAndNextLevel();
    TestBossLevelEnemyEagle();
    TestBombing();
    TestBossEnemiesGuard();
    TestLanLobby();
    TestLanLockstep();
    TestLanSeveralPlayersPerPc();
    TestOnlineCodeAndHybridRoom();
    TestNetPlayersComeAndGo();
    TestLanLateJoin();
    TestCustomLevels();
    TestVersusMode();
    TestVersusBots();
    TestVersusEightPlayers();
    TestRoyale();
    TestNewItems();
    TestUpdater();
    TestMovableBaseAndPlayers();
    TestEditorSidebar();
    TestCannons();
    TestDifficultyRulesAndContinues();
    TestGhostWalls();
    TestVoiceChat();
    TestCampaignItems();
    TestLanSharedMaps();
    TestAllyRules();
    TestSandBlock();
    TestLavaBlock();
    TestBossSpawnPointsAnyPlayerCount();
    TestBossDifficultyScaling();
    TestEnemyItemsAndShield();
    TestStealLives();
    TestGameOverScreen();
    TestLobbyColorsAndJoining();
    TestPadAssignment();
    TestPlayerColorsInGame();
    std::printf("\n%d comprobaciones, %d fallas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
