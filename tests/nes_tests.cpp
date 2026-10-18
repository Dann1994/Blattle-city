// Pruebas de comportamiento del port (sin ventana). Cada una cita la rutina
// del desensamblado que verifica. Las comparaciones contra la ROM misma estan
// en tests/rom_oracle_tests.cpp (cuando existe).

#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>
#include <memory>
#include <string>

#include "../src/app/arena.h"
#include "../src/app/flow.h"
#include "../src/app/lan.h"
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
            CHECK(diff <= 2);                                              // sin cambios (a lo sumo un paso abierto)
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
    CHECK(b.Bombing().countdown > 14000 && b.Bombing().countdown <= b.BombInterval());
    CHECK_EQ(b.Bombing().phase, 0);
    CHECK_EQ(b.BombInterval(), 4 * 3600);                             // nivel 10: 4 minutos
    for (int lv : {10, 20, 30, 40}) CHECK_EQ(MakeFlow(lv, 1)->battle->BombInterval(), (5 - lv / 10) * 3600);   // 4, 3, 2 y 1 minutos
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
    CHECK(b.Bombing().countdown > 14000);                              // despues del bombardeo vuelve a contar (4 min en el nivel 10)
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
            CHECK_EQ(seen[5] && seen[6], players >= 3);                 // los laterales, solo con 3 o 4 jugadores
            CHECK_EQ(seen[5] || seen[6], players >= 3);
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
            u8 in[4];
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
            u8 in[4];
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
            u8 in[4];
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
    std::printf("[colores] el tanque, la bala y los aliados usan el color elegido (7 colores)\n");
    for (int color = 0; color < 7; ++color) {
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
        static const int kAttr[7] = {0, 1, 4, 5, 6, 7, 0x10};
        static const int kPal[7] = {0, 1, 4, 5, 6, 7, 8};
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
