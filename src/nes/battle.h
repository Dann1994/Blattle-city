#pragma once

// Port a C++ de la logica de partida de Battle City (Famicom, 1985), hecho a
// partir del desensamblado completo de cyneprepou4uk. Cada rutina conserva el
// nombre y la direccion de la ROM (por ejemplo Sub_DBF1_tank_movement es
// "sub_DBF1_tank_movement" en 00:DBF1), asi se puede comparar linea por linea
// con el original. La RAM es un arreglo de 2 KB con las mismas direcciones
// que la NES, y los nombres de variables salen de nes_data.h (generado).
//
// Lo que NO se porta (a proposito, para esta primera etapa): el sonido (solo
// se marcan los mismos flags ram_sfx_*), el dibujo del HUD por nametable (el
// renderer lo dibuja a partir del estado) y las pantallas fuera de la partida.

#include <cstdint>
#include <cstring>
#include <functional>

#include "nes_data.h"

namespace nes {

using u8 = uint8_t;

constexpr int kMaxTanks = 20;    // modo clasico: 2 jugadores + 6 enemigos; ancho: hasta 4 jugadores + 8 enemigos
constexpr int kMaxBullets = 24;  // un slot por tanque (indice del tanque) + una segunda bala por jugador

// Una entrada de la OAM tal como la deja el juego (sprites de 8x16).
struct Sprite {
    u8 y, tile, attr;
    uint16_t x;      // en el campo panoramico la X puede pasar de 255
};

class Battle {
public:
    Battle();

    // Campo panoramico (16:9): 25x13 bloques, posiciones X de 16 bits y mapa de
    // 54 columnas. Hay que elegirlo antes de NewGame. En modo clasico (por
    // defecto) el comportamiento es byte a byte el de la ROM.
    void SetWide(bool wide);
    bool IsWide() const { return wide_; }
    // Prueba: la zona de arriba (16 px, donde iba la barra del HUD) pasa a ser parte del campo y el HUD se dibuja encima, con
    // transparencia. Poner en false para volver a la barra gris de antes (el renderer usa la misma constante).
    static constexpr bool kOpenTopStrip = true;
    static constexpr int kWideBlocksX = 27;
    static constexpr int kWideBlocksY = 14;
    static constexpr int kWideMapCols = 54;

    // Arranca una partida de prueba: 1 o 2 jugadores, etapa "stage" para las
    // tablas de enemigos (el mapa se carga aparte con LoadStageData).
    void NewGame(int players, int stage);

    // Despues de dibujar el campo: pone los jugadores, el contador de
    // enemigos, los temporizadores y la paleta (sub_C331).
    void StartStage();

    // Dibuja el mapa (13x13 bloques) a partir de 91 bytes con el formato de
    // los niveles originales (nibbles; ver Sub_F000_draw_stage) y la base.
    void LoadStageData(const u8* data91);
    void LoadOriginalStage(int stage1to35);
    // Campo panoramico: un id de bloque (0..15, mismos que el nibble original)
    // por celda, 13 filas de 25 bloques.
    // strip (opcional): una fila de 27 bloques para la franja de arriba del campo (si kOpenTopStrip).
    void LoadWideStage(const u8 blocks[kWideBlocksY][kWideBlocksX], const u8* strip = nullptr);

    // Entrada del host: botones con los bits de con_btn_* (A=01, B=02,
    // Select=04, Start=08, Arriba=10, Abajo=20, Izq=40, Der=80).
    void SetButtons(int player, u8 buttons) { pad_[player & 3] = buttons; }

    // Un cuadro completo: NMI + cuerpo del bucle de la partida (C1F9).
    void RunFrame();

    // ---- acceso al estado -------------------------------------------------
    u8& R(uint16_t a) { return ram_[a & 0x7FF]; }
    u8 R(uint16_t a) const { return ram_[a & 0x7FF]; }
    int MapCols() const { return wide_ ? kWideMapCols : 32; }
    enum class FriendlyFire { Off, Stun, Damage };   // atraviesa / paraliza (original) / quita un nivel de arma
    void SetFriendlyFire(FriendlyFire m) { friendlyFire_ = m; }
    FriendlyFire GetFriendlyFire() const { return friendlyFire_; }
    int Players() const { return nPlayers_; }
    // Colores de los jugadores (0 amarillo, 1 verde, 2 azul, 3 rojo, 4 violeta, 5 naranja, 6 marron). Solo campo ancho.
    void SetPlayerColors(const int colors[4]) {
        for (int i = 0; i < 4; ++i) playerColor_[i] = colors[i] < 0 ? 0 : (colors[i] > 6 ? 6 : colors[i]);
    }
    void SetPlayerColor(int p, int color) { playerColor_[p] = color < 0 ? 0 : (color > 6 ? 6 : color); }
    // ---- partida en red: jugadores que entran y salen ----
    // La partida en red siempre se crea con 4 lugares de jugador; los que no estan quedan "ausentes" (sin tanque, sin vidas).
    // La dificultad (enemigos totales y en pantalla, ritmo de aparicion, puntos de aparicion) se calcula con los jugadores
    // presentes AL EMPEZAR CADA NIVEL: si alguien entra o se va, el cambio se nota a partir del nivel siguiente.
    void SetPresentMask(int mask);                       // antes de StartStage: quienes estan al empezar (bit p = jugador p)
    void MarkPlayerGone(int p);                          // se fue: su tanque desaparece y no vuelve
    void AddPlayerLate(int p, int color);                // entro en medio de la partida: aparece con 3 vidas
    bool PlayerPresent(int p) const { return p >= 0 && p < 4 && present_[p]; }
    int ActivePlayers() const;                           // jugadores presentes (al menos 1)
    int DifficultyPlayers() const { return diffPlayers_; }
    int PlayerPaletteIndex(int p) const { return wide_ ? kColorPalette[playerColor_[p]] : (p < 2 ? p : p + 2); }
    // Puntaje (en puntos) y enemigos destruidos por tipo (0 basico .. 3 blindado) de un jugador.
    int Score(int p) const;
    int KillCount(int p, int kind) const;
    // Bonus de la pantalla de recuento: hundreds = BCD de centenas (0x10 = 100; 0 = 1000 puntos).
    void AddBonusPoints(int p, u8 hundreds);
    // Todos los jugadores sin vidas ni tanque, o la base destruida y su cuenta regresiva terminada (solo despues de StartStage).
    bool GameOver() const;
    // Un cuadro del motor de sonido sin avanzar el juego (pantallas de recuento, game over...).
    void RunSoundFrame();
    // GAME OVER como en la ROM (sub_C728): los jugadores quedan quietos y el cartel rojo sube desde abajo hasta el
    // centro (112 cuadros), se queda un rato y recien despues viene la pantalla de GAME OVER.
    void StartGameOverMessage();
    bool GameOverMessageActive() const { return R(ram_game_over_msg_timer) != 0; }
    void PlayScoreTick();
    void DebugSetKills(int p, int kind, int n);       // solo pruebas
    int EnemyTotal() const { return enemyTotal_; }
    // Nivel del campo ancho (1..40; 0 = mapa de pruebas). Fija el escalon de dificultad
    // (nivel - 1) / 5: mas enemigos, mas fuertes, mas agresivos y con mas salud. Antes de NewGame.
    void SetLevel(int level) {
        level_ = level;
        tier_ = spec_.active ? spec_.tier : (level > 0 ? (level - 1) / 5 : 0);
    }
    // Nivel hecho a mano (editor de niveles): en vez de salir del numero de nivel, el tipo (comun o con aguila enemiga), la
    // dificultad, la fuerza del aguila, su lugar y los puntos de aparicion de enemigos los dicta esta ficha. Hay que fijarla antes
    // de NewGame / NextStage; con active = false todo vuelve a depender del numero de nivel.
    static constexpr int kMaxSpawns = 12;
    struct StageSpec {
        bool active = false;
        bool boss = false;
        int tier = 0;                 // dificultad 0..7
        int rank = 1;                 // aguila enemiga 1..4: capas de hierro, salud, premio y tiempo entre bombardeos
        int eagleCol = 13, eagleRow = 1;      // aguila enemiga: bloque (columna 0..26, fila 0..14; la fila 0 es la franja de arriba)
        int nSpawns = 0;              // puntos de aparicion de enemigos (0 = los de siempre)
        int spawnCol[kMaxSpawns] = {}, spawnRow[kMaxSpawns] = {};
        // MODO BATALLA VS: dos equipos con su aguila a cada lado. Las apariciones son los 4 puntos de los tanques con IA de cada equipo
        // (0 y 1: equipo izquierdo, 2 y 3: equipo derecho); tier = fuerza de los tanques con IA y de los bombardeos; rank = tiempo entre bombardeos.
        bool versus = false;
        int vsEagleCol[2] = {1, 25}, vsEagleRow[2] = {7, 7};
        int vsLayers = 1;             // capas de hierro de cada aguila
    };
    void SetStageSpec(const StageSpec& s) { spec_ = s; }
    const StageSpec& GetStageSpec() const { return spec_; }
    int Level() const { return level_; }
    // Niveles 10, 20, 30 y 40: hay un aguila enemiga arriba; los enemigos no se acaban y solo se gana destruyendola.
    bool IsBossLevel() const { return wide_ && level_ > 0 && (spec_.active ? spec_.boss : level_ % 10 == 0); }
    // Fuerza del aguila enemiga 1..4 (niveles 10/20/30/40; en un nivel hecho a mano, la que se eligio).
    // ---- MODO BATALLA VS (dos equipos; el equipo 0 es el amarillo de la izquierda y el 1 el rojo de la derecha) ----
    bool IsVersus() const { return wide_ && level_ > 0 && spec_.active && spec_.versus; }
    static constexpr int kTeamColor[2] = {0, 3};                 // amarillo y rojo (los colores de las dos aguilas)
    void SetTeams(const int team[4]) {                           // equipo (0/1) de cada jugador; tambien les pone el color del equipo
        for (int i = 0; i < 4; ++i) {
            playerTeam_[i] = team[i] ? 1 : 0;
            playerColor_[i] = kTeamColor[playerTeam_[i]];
        }
    }
    int TeamOf(int tank) const { return tank >= 0 && tank < nPlayers_ ? playerTeam_[tank] : vsTeam_[tank < 0 || tank >= kMaxTanks ? 0 : tank]; }
    int PlayerTeam(int p) const { return playerTeam_[p & 3]; }
    int TeamPaletteIndex(int t) const { return kColorPalette[kTeamColor[t & 1]]; }   // paleta de sprites del color del equipo
    int VersusWinner() const { return vsWinner_; }               // -1 sigue la partida, 0 / 1 gano ese equipo, 2 empate
    bool VsEagleAlive(int team) const { return vsEagleAlive_[team & 1]; }
    int VsEagleCol(int team) const { return spec_.vsEagleCol[team & 1]; }
    int VsEagleRow(int team) const { return spec_.vsEagleRow[team & 1]; }
    int VsTeamClock(int team) const { return teamClock_[team & 1]; }    // cuadros-por-64 que le quedan congelado a ese equipo
    int Rank() const { return spec_.active ? spec_.rank : (level_ / 10 < 1 ? 1 : (level_ / 10 > 4 ? 4 : level_ / 10)); }
    int EagleCol() const { return spec_.active ? spec_.eagleCol : 13; }     // bloque del aguila enemiga
    int EagleRow() const { return spec_.active ? spec_.eagleRow : 1; }
    // Un jugador a menos de esto del aguila enemiga (en pixeles) la pone en alerta: 160 y +16 por escalon de dificultad.
    int EagleGuardRadius() const { return 160 + 16 * tier_; }
    bool EnemyHunting(int x) const { return hunting_[x]; }   // el enemigo x esta en modo ataque (niveles del aguila)
    int EagleLayers() const { return Rank(); }   // 10: 1 capa, 20: 2, 30: 3, 40: 4   // capas de hierro de la fortaleza (cada una de 1 mosaico = 8 px)
    int EagleSpawnGap() const { return 16 * (1 + (EagleLayers() + 1) / 2); }   // distancia de las apariciones a la fortaleza (px)
    bool EnemyEagleAlive() const { return enemyEagleAlive_; }
    // Robo de vidas: a quien quedo sin vidas, la vida de que jugador va a robar (-1: no puede robar ahora).
    // Bombardeo (niveles del aguila enemiga): cada 2 minutos pasan tres aviones y bombardean el campo.
    static constexpr int kMaxPlanes = 6;
    struct BombingState {
        int phase = 0;                    // 0 espera, 1 alerta, 2 bombardeo
        int countdown = 0;                // cuadros hasta la proxima alerta
        int alert = 0;                    // cuadros de alerta que quedan
        int raidT = 0;                    // cuadros desde que empezo el bombardeo
        int planes = 3;                   // aviones de este bombardeo (3 a 6 segun la dificultad)
        int lane[kMaxPlanes] = {};        // vertical: columna (bloque) que recorre; horizontal: fila (bloque)
        int bombAt[kMaxPlanes][3] = {};   // donde cae cada bomba sobre su carril: fila (vertical) o columna (horizontal)
        bool dropped[kMaxPlanes][3] = {};
        struct Plane {
            bool active = false;
            int x = 0, y = 0;
            int dir = 0;                  // 0 baja (vertical), 2 va a la derecha, 3 va a la izquierda (horizontales)
        } plane[kMaxPlanes];
        struct Target { int cx = 0, cy = 0, fuse = 0; } target[kMaxPlanes * 3];    // fuse > 0: parpadea en rojo hasta explotar
        struct Blast { int cx = 0, cy = 0, age = -1; } blast[kMaxPlanes * 3];      // age >= 0: explosion en curso
    };
    // Tiempo entre bombardeos: nivel 10: 4 min, 20: 3 min, 30: 2 min, 40: 1 min (60 cuadros por segundo).
    int BombInterval() const { return (5 - Rank()) * 60 * 60; }
    static constexpr int kBombAlertFrames = 150;
    static constexpr int kBombFuse = 90;                // un segundo y medio entre la marca roja y la explosion
    static constexpr int kBombBlastFrames = 40;         // duracion de la animacion de la explosion
    const BombingState& Bombing() const { return bomb_; }
    bool BombingActive() const { return (IsBossLevel() && enemyEagleAlive_) || (IsVersus() && vsWinner_ < 0); }
    int BomberSoundCount() const { return bomberSounds_; }   // sube cada vez que entra un avion (el programa hace sonar el efecto)
    int ShakeX() const;                                // temblor del escenario (pixeles)
    int ShakeY() const;
    void DebugStartBombing() { if (BombingActive() && bomb_.phase == 0) bomb_.countdown = 1; }
    int StealTarget(int p) const { return wide_ && p >= 0 && p < 4 ? stealTarget_[p] : -1; }
    int FreezeFrames(int p) const { return p >= 0 && p < 4 ? playerFreeze_[p] : 0; }   // cuadros que le quedan paralizado por el reloj enemigo
    int ShieldHealth(int x) const { return shieldHp_[x]; }     // salud del casco (5 al agarrarlo; 0 = sin casco o proteccion de aparicion)
    // Eventos para la vibracion del mando (solo suben): disparos recibidos por un jugador (aunque el casco los frene),
    // items que agarro y explosiones del bombardeo.
    int PlayerHits(int p) const { return p >= 0 && p < 4 ? playerHits_[p] : 0; }
    int ItemPickups(int p) const { return p >= 0 && p < 4 ? itemPickups_[p] : 0; }
    int BlastCount() const { return blastCount_; }
    // Resumen del estado de la simulacion: dos equipos que reciben los mismos botones tienen que dar el mismo valor (juego en red).
    uint32_t StateHash() const;
    int EaglesDestroyed() const { return eaglesDestroyed_; }   // aguilas destruidas (la propia o la enemiga)
    int EnemyEagleHealth() const { return enemyEagleHp_; }
    int Tier() const { return tier_; }
    int EnemyHealth(int tank) const { return wd_.hp[tank]; }   // enemigos por etapa (20 en la ROM; mas con 3 o 4 jugadores)
    int LastTank() const { return lastTank_; }
    int LastEnemy() const { return lastEnemy_; }     // ultimo slot de enemigo; despues vienen los slots de aliados
    bool IsAlly(int x) const { return wide_ && x >= allyBegin_ && x <= lastTank_; }
    int AllyOwner(int x) const { return allyOwner_[x]; }
    void SpawnAllies(int owner);                      // item de aliados: 2 tanques con IA al lado del jugador
    // Todos los enemigos de la etapa muertos (solo campo ancho con nivel > 0).
    bool StageCleared() const;
    // Pasa a la etapa siguiente conservando vidas, puntajes y nivel de arma.
    void NextStage(int level, const u8 blocks[kWideBlocksY][kWideBlocksX], const u8* strip = nullptr);       // ultimo indice de tanque (jugadores primero, despues enemigos)
    u8 TankFlags(int i) const;
    u8 TankType(int i) const;
    int TankX(int i) const;
    int TankY(int i) const;
    void SetTankPos(int i, int x, int y);
    u8 Lives(int p) const;
    void SetLives(int p, u8 v);
    u8 Upgrade(int p) const;
    u8 Helmet(int p) const;
    void SetHelmet(int p, u8 v);
    void SetUpgrade(int p, u8 v);                    // 0, $20, $40, $60
    void SetBonus(int x, int y, int id);              // pone un item en el campo (timer a 0)
    void SetEnemyLimitMax();                          // solo pruebas: todos los enemigos posibles a la vez
    u8 Tile(int col, int row) const { return M(row * Stride() + col); }
    // Byte de atributos (paleta de cuatro cuadrantes 2x2) de la celda 4x4 mosaicos que contiene a (tx, ty).
    u8 AttrCell(int tx, int ty) const {
        return wide_ ? wideAttr_[(ty >> 2) * 16 + (tx >> 2)] : R(ram_nmt_attr_buffer + (ty >> 2) * 8 + (tx >> 2));
    }
    Sprite OamSprite(int i) const {
        const uint16_t a = ram_oam + i * 4;
        return Sprite{R(a), R(a + 1), R(a + 2), static_cast<uint16_t>(wide_ ? oamX_[i] : R(a + 3))};
    }
    u8 BgPaletteSet() const { return bgPalette_; }
    u8* RamData() { return ram_; }
    const u8* RamData() const { return ram_; }

    // Solo para pruebas: reemplaza el generador de sub_D44D por un flujo
    // externo (la ROM usa bytes sueltos de la pagina cero como entropia, que
    // el port no reproduce byte a byte). Con el mismo flujo en la ROM y en el
    // port, las comparaciones cuadro a cuadro no dependen del azar.
    std::function<u8()> randomHook;
    // Solo para depurar: se llama en cada sub_DA2B con (spr_X, spr_Y, mosaico bajo el sprite, $0004 antes de combinar).
    std::function<void(int, int, u8, u8)> spriteTraceHook;
    // Escrituras a la APU ($4000-$4017) que hace el motor de sonido portado.
    std::function<void(uint16_t, u8)> apuWrite;
    // Se llama cuando el motor de sonido arranca un efecto (indice 0-27 de ram_sounds): permite reemplazarlo por una grabacion.
    std::function<void(int)> sfxStartHook;

    // ---- herramientas de prueba (no existen en el original) ---------------
    // type: 0 basico ($80), 1 rapido ($A0), 2 power ($C0), 3 blindado ($E0).
    // Devuelve el slot usado o -1 si no hay lugar.
    int DebugSpawnEnemy(int type, bool flashing);
    void DebugSpawnBonus(int bonusId);   // 0 casco, 1 reloj, 2 pala, 3 estrella, 4 granada, 5 tanque, 6 pistola (extension)
    void DebugKillAllEnemies();
    int BulletStatus(int i) const { return wide_ ? wd_.bstatus[i] : R(ram_bullet_status + i); }     // estado de la bala del slot i (0x4x: en vuelo)
    void DebugFire(int tank);                          // hace disparar a ese tanque (si puede)
    void DebugClearBullets() { Sub_E409_clear_bullet_status(); }
    void DebugAllyAutoFire(bool on) { allyAutoFire_ = on; }      // pruebas: los aliados dejan de disparar solos
    void DebugBareEagle();                             // el aguila de los jugadores sin muros alrededor
    // Escenario de pruebas de los aliados: aguila sin muros, dos aliados del jugador 1 y enemigos quietos (con el reloj) para que se vea
    // que los aliados no le disparan al aguila, que sus balas no lastiman a los jugadores y que los enemigos que matan suman al dueño.
    void DebugAllyScenario();
    void DebugRespawnPlayer(int player);
    void DebugClearEnemies();
    void DebugSetTankFlags(int i, u8 flags);           // solo pruebas
    // acceso a los bytes altos de las X de 16 bits (para pruebas)
    u8 TankXHi(int tank) const { return tankXHi_[tank]; }
    void SetXHi(bool tank, int index, u8 hi) { (tank ? tankXHi_ : bulletXHi_)[index] = hi; }
    void SetBonusXHi(u8 hi) { bonusXHi_ = hi; }
    bool BonusActive() const { return (R(ram_bonus_pos_X) | bonusXHi_) != 0; }
    void DebugPlaySound(int which);      // 0 arranque de etapa, 1 record, 2 game over (las melodias del juego)

private:
    // ---- campo panoramico: mapa, atributos y posiciones de 16 bits ----
    int Stride() const { return wide_ ? 64 : 32; }
    u8& M(int idx) {
        if (wide_) {
            return wideMap_[idx < 0 ? 0 : (idx >= kWideMapSize ? kWideMapSize - 1 : idx)];
        }
        return ram_[(0x0400 + idx) & 0x7FF];
    }
    u8 M(int idx) const {
        if (wide_) {
            return wideMap_[idx < 0 ? 0 : (idx >= kWideMapSize ? kWideMapSize - 1 : idx)];
        }
        return ram_[(0x0400 + idx) & 0x7FF];
    }
    u8& AttrCellRef(int tx, int ty) {
        return wide_ ? wideAttr_[(ty >> 2) * 16 + (tx >> 2)] : ram_[(ram_nmt_attr_buffer + ((ty << 1) & 0xF8) + (tx >> 2)) & 0x7FF];
    }
    int WX(int v) const { return v & (wide_ ? 0xFFFF : 0xFF); }      // envuelve una X como la ROM (8 bits) o como el campo ancho (16)
    int AbsDiffX(int a, int b) const;
    // balas: duenio (jugador) de un slot, o -1 si es de un enemigo; segunda bala de cada jugador
    int BulletOwner(int b) const;
    bool IsAllyBullet(int b) const { return wide_ && b >= allyBegin_ && b <= lastTank_; }
    int NearestEnemy(int x) const;
    void Sub_AllyFire();
    int BulletShooter(int b) const;                  // indice del tanque que la disparo
    int SecondBullet(int player) const { return wide_ ? 20 + player : 8 + player; }
    // Atributo de paleta del tanque de un jugador. En el campo ancho se elige entre 7 colores
    // (0 amarillo, 1 verde, 2 azul, 3 rojo, 4 violeta, 5 naranja, 6 marron): los bancos 4-8 salen de
    // los bits 2 y 4 del atributo (libres en la NES).
    static constexpr u8 kColorAttr[7] = {0, 1, 4, 5, 6, 7, 0x10};
    static constexpr u8 kColorPalette[7] = {0, 1, 4, 5, 6, 7, 8};   // indice de paleta de sprites que usa el renderer
    u8 PlayerPalette(int x) const { return wide_ ? kColorAttr[playerColor_[x]] : static_cast<u8>(x < 2 ? x : x + 2); }
    int ShiftX() const { return wide_ ? 96 : 0; }
    int ShiftY() const { return wide_ ? 16 : 0; }                    // el campo ancho tiene una fila de bloques mas (sin borde inferior)                    // desplazamiento de la base al centro del campo ancho

    // referencia a una X de 16 bits: el byte bajo vive en la RAM de la ROM
    struct XRef {
        u8& lo;
        u8& hi;
        bool wide;
        operator int() const { return lo | (wide ? hi << 8 : 0); }
        XRef& operator=(int v) {
            lo = static_cast<u8>(v & 0xFF);
            if (wide) hi = static_cast<u8>((v >> 8) & 0xFF);
            return *this;
        }
        XRef& operator=(const XRef& o) { return *this = static_cast<int>(o); }
    };

    // utilidades de la ROM
    u8 Sub_D44D_generate_random_number();
    uint16_t Sub_D5FB_calculate_pointer(int col, int row) const;
    uint16_t PointerPx(int px, int py) const;           // sub_D706
    u8 Sub_D725_tile_mask(int px, int py) const;
    void Sub_D784_write_tile(uint16_t addr, u8 value);
    void Sub_D6B3_fill_buffer_with_tiles(int col, int row, const u8* tiles);
    void Sub_D635_calculate_nametable_attribute(int tileX, int tileY, u8 attr);
    void Sub_D80B_write_block_tiles_and_attribute(u8 tileId, int px, int py);
    void Sub_D9E1_calculate_decimal_number(u8 a);
    void Sub_D9BE_add_score(int x);
    void Sub_D138_gain_extra_life_for_20000_pts();
    u8 Sub_E451_convert_Dpad_buttons(u8 a) const;
    void Sub_E420_change_tank_status(int x, u8 status);

    // bases
    void Sub_CAF5_draw_default_base();
    void Sub_CB5D_draw_default_eagle();
    void Sub_CB9E_draw_protected_base();
    void Sub_CC08_draw_destroyed_eagle();
    void DrawEnemyEagle(bool destroyed);
    void DrawEagleFortress();
    void DestroyEnemyEagle(int owner);
    void SetBaseQuadrantPalette(int quadX, int quadY, u8 pal);
    void Sub_D7CC_create_default_stage_field();

    // motor de sonido (sound_driver.cpp)
    void ApuWrite(uint16_t addr, u8 value);
    u8 SoundDataByte(uint16_t addr) const;
    void Sub_EA51_clear_sound_engine_data();
    void Sub_EA7E_sound_driver();
    void Sub_ECAF_prepare_sound_engine_data_pointer();
    u8 Sub_ECBE_read_byte_from_sound_data(uint16_t entry);
    void SoundReadNextBytes(uint16_t entry);

    // bucle principal
    void Nmi();
    void Sub_C2E6_main_battle_script();
    void Sub_C31D_water_palette_swap_handler();
    void Sub_C972_game_over_text_handler();
    void Sub_C947_display_game_over_text_with_sprites();
    void Sub_DB0B_player_movement_sfx_handler();
    bool IsPlayerMoving(int x);
    void Sub_C331_prepare_tanks_and_spawn_players();
    void Sub_E42B_prepare_enemy_tanks_for_stage();

    // tanques
    void Sub_E181_ice_detection();
    void Sub_E1FA_clear_tank_marks();
    void Sub_DB75_ice_movement();
    void Sub_DBF1_tank_movement();
    void Sub_DC3D_tank_status_handler(int x);
    void Status_80(int x);
    void Status_90(int x);
    void Status_A0(int x);
    void Status_FollowTarget(int x, u8 status);
    void Status_Explosion(int x);
    void Status_E0(int x);
    void Status_F0(int x);
    void Loc_DC97_try_move(int x);
    void Loc_DD11_blocked(int x);
    void Sub_DE72_pick_goal(int x);
    u8 Sub_DDA2_direction_to_target(int x);
    void Sub_E363_tank_spawn_handler(int x);
    void Sub_E3B8_finish_respawn(int x);
    void Sub_E409_clear_bullet_status();
    void Sub_E413_clear_some_tank_addresses();
    void Sub_DB48_enemy_spawn_handler();

    // balas
    void Sub_E02E_bullets_status_handler();
    void Sub_E08C_fire_bullet(int x);
    void Sub_E122_players_fire();
    void Sub_E162_enemies_fire();
    void Sub_E604_bullets_movement();
    int Sub_E693_bullet_probe(int px, int py, int bulletIndex);
    void Sub_E70C_check_bullets_collision_with_tanks();
    void Sub_E910_check_bullets_collision_with_bullets();

    // items y base
    bool Sub_E972_try_to_pick_up_bonus();
    void Sub_E8BE_spawn_bonus();
    void BonusHandler(int id, int player);
    void EnemyBonus(int id, int x);                  // un enemigo agarra un item (campo ancho)
    void SetEnemyKind(int x, int kind);
    int RandomEnemyKind();
    void EnemyGrenade(int x);
    void SpawnExtraEnemies(int x);
    void DrawBareBase();
    bool ShieldHit(int x, int dmg);
    void Sub_StealLives();
    void SetEnemyLimitForStage();                  // enemigos a la vez en pantalla segun jugadores y dificultad
    bool AimsAtEnemyEagle(int x);
    bool AimsAtEagleAt(int x, int col, int row, int layers);
    // modo batalla vs
    void VersusSpawnHandler();
    void VersusPickGoal(int x);
    void VersusCollisions();
    void VersusCheckEnd();
    int VsEagleAt(int px, int py) const;                         // equipo del aguila en ese pixel (-1 si no hay)
    void DestroyVsEagle(int team);
    void DrawEagleAt(int ec, int er, bool destroyed, u8 attr);
    void DrawFortressAt(int ec, int er, int layers);
    bool NearVsEagle(int bombCol, int bombRow) const;
    void SpawnPointPx(int index, int& x, int& y) const;       // donde sale el enemigo del punto de aparicion `index`
    int SpawnPointCount() const;
    bool BossAggro(int x, int& nearest);           // niveles del aguila: hay un jugador cerca del aguila o del enemigo x
    void Sub_Bombing();
    void StartRaid();
    void DetonateBomb(int cx, int cy);
    void Sub_E2A9_HQ_handler();
    void Sub_E27C_players_invincibility_sprites();

    // sprites (OAM)
    void Sub_DA2B_display_sprite(int sprX, int sprY);
    void Sub_DA64_display_bullet_sprite(u8 dir, int sprX, int sprY);
    void Sub_DA73_display_2_sprites_dir(u8 dir, int sprX, int sprY);
    void Sub_DA7B_display_2_sprites(int sprX, int sprY);
    void Sub_DA93_hide_unused_sprites();
    void Sub_DEA6_tanks_handler();
    void Sub_DEB8_tank_handler(int x);
    void Sub_DEE2_draw_bullet_explosion(u8 a, int sprX, int sprY);
    void Sub_DEF0(u8 a, int sprX, int sprY);
    void Sub_E0D8_bullets_status_handler();
    void Sub_E23B_display_bonus_on_screen();
    void Sub_E328_hq_sprite(u8 tile, int sprX, int sprY);

    // Estado por tanque / bala / jugador del campo ancho (la RAM de la ROM solo
    // alcanza para 2 jugadores y 8 tanques). En modo clasico no se usa.
    struct WideData {
        u8 tflags[kMaxTanks], ttype[kMaxTanks], tposX[kMaxTanks], tposY[kMaxTanks], twheels[kMaxTanks];
        u8 tstLo[kMaxTanks], tstHi[kMaxTanks];
        u8 bstatus[kMaxBullets], bposX[kMaxBullets], bposY[kMaxBullets], bprop[kMaxBullets];
        u8 upgrade[4], plrflags[kMaxTanks], helmet[4], stun[4], lives[4], btnHold[4], btnPress[4], extra[4];
        u8 hp[kMaxTanks];                         // salud de los enemigos (campo ancho)
        u8 score[4 * 8];
        u8 kill[4 * 4];
    };
    WideData wd_ = {};
    FriendlyFire friendlyFire_ = FriendlyFire::Stun;
    int playerColor_[4] = {0, 1, 2, 3};
    bool inputLocked_ = false;
    bool enemyEagleAlive_ = false;
    int bossSpawned_ = 0;
    int enemyEagleHp_ = 0;
    int playerHits_[4] = {}, itemPickups_[4] = {}, blastCount_ = 0, eaglesDestroyed_ = 0;
    bool present_[4] = {true, true, true, true};
    int diffPlayers_ = 1;                                // jugadores con los que se calcula la dificultad del nivel en curso
    BombingState bomb_;
    int bomberSounds_ = 0;
    int shake_ = 0;
    u8 shieldHp_[kMaxTanks] = {};
    uint16_t playerFreeze_[4] = {};
    bool extraEnemy_[kMaxTanks] = {};
    int stealTarget_[4] = {-1, -1, -1, -1};
    int nPlayers_ = 2;
    int enemyTotal_ = 20;
    int level_ = 0;
    StageSpec spec_;
    bool allyAutoFire_ = true;
    int playerTeam_[4] = {0, 1, 0, 1};
    int vsTeam_[kMaxTanks] = {};                          // equipo de cada tanque con IA (los jugadores usan playerTeam_)
    bool vsEagleAlive_[2] = {false, false};
    int vsWinner_ = -1;
    int vsAiTimer_[2] = {0, 0}, vsSpawned_[2] = {0, 0}, teamClock_[2] = {0, 0};
    int tier_ = 0;
    int lastTank_ = 7;
    int lastEnemy_ = 7;
    int allyBegin_ = 8;
    int allyOwner_[kMaxTanks] = {};
    int allyKind_[kMaxTanks] = {};            // tipo del aliado = nivel de arma de quien agarro el item (0 basico .. 3 blindado)
    static constexpr int kWideAllySlots = 4;
    int nBullets_ = 10;
    int chaseTarget_[kMaxTanks] = {};
    bool hunting_[kMaxTanks] = {};                        // niveles del aguila: true = en modo ataque, false = de guardia
    int guardX_[kMaxTanks] = {}, guardY_[kMaxTanks] = {};   // niveles del aguila: punto de guardia de cada enemigo
    // Combate del campo ancho: nivel de arma de cada bala (0-3; los enemigos: tanque 1-4),
    // si ya atraveso un enemigo, y salud del hierro por mosaico (0 = intacto = 20).
    u8 bulletLevel_[kMaxBullets] = {};
    u8 bulletPierced_[kMaxBullets] = {};
    u8 steelHp_[64 * 34] = {};
    static constexpr int kSteelHealth = 20;
    static constexpr int kWideEnemySlots = 12;

    static constexpr int kWideMapSize = 64 * 34;
    bool wide_ = false;
    u8 wideMap_[kWideMapSize] = {};
    u8 wideAttr_[16 * 8] = {};
    u8 tankXHi_[kMaxTanks] = {};
    u8 bulletXHi_[kMaxBullets] = {};
    u8 bonusXHi_ = 0, msgXHi_ = 0, destXHi_ = 0;
    uint16_t oamX_[64] = {};
    int markPtr_[kMaxTanks] = {};

    u8 ram_[0x800];
    u8 pad_[4] = {0, 0, 0, 0};
    u8 bgPalette_ = 0;
    bool skipRestOfScript_ = false;
    int debugForcedType_[kMaxTanks];
};

} // namespace nes
