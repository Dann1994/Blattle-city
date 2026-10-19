#pragma once

// Los 40 niveles del campo panoramico (27x14 bloques), generados de forma
// determinista: el mismo numero de nivel da siempre el mismo mapa.
//
//  - 8 temas que rotan (ladrillos, murallas, rio, fortaleza, bosque, hielo,
//    zigzag, caos), 5 apariciones de cada uno a lo largo de los 40 niveles.
//  - El escalon de dificultad es (nivel - 1) / 5 (0..7): con cada escalon hay
//    mas hierro y mas obstaculos.
//  - Todos son simetricos izquierda/derecha, dejan libres la fila de arriba, las
//    zonas de aparicion de enemigos y la de la base, y garantizan que desde la
//    base se pueda llegar (sin cruzar agua ni hierro) a todos los puntos de aparicion.

#include <cstdint>
#include <string>
#include <vector>

#include "../nes/battle.h"

namespace app {

constexpr int kLevelCount = 40;
constexpr int kLevelRows = 14;
constexpr int kLevelCols = 27;

// Escalon de dificultad (0..7) de un nivel (1..40).
int DifficultyTier(int level);

// Rellena out con los ids de bloque (los mismos nibbles que el original: 0-3 mitades de
// ladrillo, 4 ladrillo, 5-8 mitades de hierro, 9 hierro, A agua, B arbustos, C hielo, D vacio).
// level 1..40.
void GenerateWideLevel(int level, uint8_t out[kLevelRows][kLevelCols]);

// La franja de arriba del campo (una fila de 16 px que antes ocupaba la barra del HUD): almenas de ladrillo (y algo de hierro con
// la dificultad), simetricas y con las apariciones de enemigos libres. level 1..40.
void GenerateTopStrip(int level, uint8_t out[kLevelCols]);

// Celdas por las que se puede pasar sin romper nada duro: no son agua ni hierro.
bool LevelCellPassable(uint8_t id);


// ---------------------------------------------------------------------------
// Niveles hechos a mano (editor de niveles)
// ---------------------------------------------------------------------------

constexpr int kGridRows = 15;                 // fila 0 = la franja de arriba (16 px) y filas 1..14 = el campo
constexpr int kMaxSpawns = 12;                // puntos de aparicion de enemigos que puede tener un nivel
constexpr int kOwnSlots = 40;                 // casilleros de la seccion "niveles propios"
constexpr int kMaxMainLevel = 99;             // el modo principal puede crecer hasta el nivel 99

struct LevelDef {
    uint8_t cells[kGridRows][kLevelCols];     // ids de bloque 0..15 (ver GenerateWideLevel; 14 = arena, 15 = lava)
    bool boss = false;                        // nivel especial: aguila enemiga, enemigos sin fin y bombardeos
    int tier = 0;                             // dificultad 0..7 (mas enemigos, mas fuertes y mas agresivos)
    int rank = 1;                             // fuerza del aguila enemiga 1..4 (capas de hierro, salud, premio, bombardeos)
    int eagleCol = 13, eagleRow = 1;          // lugar del aguila enemiga (bloque; la fila 0 es la franja de arriba)
    int nSpawns = 0;                          // puntos de aparicion de enemigos
    uint8_t spawnCol[kMaxSpawns] = {}, spawnRow[kMaxSpawns] = {};
    // MODO BATALLA VS: dos equipos, una aguila en cada esquina opuesta. Las apariciones son los puntos de los tanques con IA (2 por equipo:
    // cada punto es del equipo de la aguila que tiene mas cerca); tier = fuerza de la IA y de los bombardeos, rank = tiempo
    // entre bombardeos (1: cada 4 minutos ... 4: cada minuto).
    bool versus = false;
    bool royale = false;                      // battle royale (sobre un mapa de batalla vs): todos contra todos
    int vsEagleCol[2] = {1, 25}, vsEagleRow[2] = {13, 1};      // aguilas en esquinas opuestas (abajo a la izquierda y arriba a la derecha)
    int vsLayers = 1;                         // capas de hierro de cada aguila (1 o 2)
    LevelDef();
};

LevelDef BlankVersusDef();                               // mapa vacio de batalla vs con aguilas, apariciones y bombardeo por defecto
// Mapa de batalla vs al azar (mismo numero = mismo mapa): parte de una de las construcciones originales, es simetrico entre los dos
// equipos y deja un camino entre las aguilas.
LevelDef GenerateVersusLevel(uint32_t seed);
// Mapa de battle royale: uno de batalla vs con los 8 puntos de salida despejados y conectados.
LevelDef GenerateRoyaleLevel(uint32_t seed);
int VsSpawnTeam(const int eagleCol[2], const int eagleRow[2], int col, int row);   // equipo (0/1) de un punto de aparicion: el de la aguila mas cercana (-1 si empata)
void ClearVersusZones(LevelDef& def);                    // despeja las aguilas, los lugares de los jugadores y las apariciones

LevelDef BlankLevelDef(bool boss = false);              // vacio, con aguila/apariciones por defecto
LevelDef BuiltinLevelDef(int level);                     // uno de los 40 niveles de fabrica, convertido a LevelDef
// Arma los datos para el motor: bloques del campo, franja de arriba y la ficha del nivel.
void LevelDefToStage(const LevelDef& def, uint8_t blocks[kLevelRows][kLevelCols], uint8_t strip[kLevelCols], nes::Battle::StageSpec& spec);
bool SaveLevelDef(const std::string& path, const LevelDef& def);
// El mismo formato del archivo como texto (para mandar un nivel por la red).
std::string LevelDefToText(const LevelDef& def);
bool LevelDefFromText(const std::string& text, LevelDef& def);
bool LoadLevelDef(const std::string& path, LevelDef& def);       // false si no existe o esta danado
// Cuidados al armar un nivel: apariciones encerradas, dentro de la fortaleza, etc. (vacio = todo bien).
std::vector<std::string> CheckLevelDef(const LevelDef& def);
// Libera el lugar del aguila enemiga y de su fortaleza (de rank capas) en las celdas.
void ClearEagleZone(LevelDef& def);
// Cuantas capas de bloque ocupa la fortaleza alrededor del aguila.
int EagleReachBlocks(int rank);

// Carpeta donde viven los niveles hechos a mano (junto al ejecutable): "principal/nivel_NN.bcl" y "propios/slot_NN.bcl".
void SetLevelRoot(const std::string& dir);
const std::string& LevelRoot();
std::string MainLevelFile(int level);
std::string OwnLevelFile(int slot);
// Si es false (partidas en red) se ignoran los archivos y valen solo los 40 niveles de fabrica: todas las PC tienen que jugar lo mismo.
void UseCustomLevels(bool on);
bool CustomLevelsEnabled();
bool MainLevelHasFile(int level);
int MainLevelCount();                                    // 40, o mas si hay niveles 41, 42... guardados
// El nivel del modo principal: su archivo si existe, si no el de fabrica (solo hasta el 40).
bool LoadMainLevelDef(int level, LevelDef& def);
bool OwnSlotHasFile(int slot);
bool LoadOwnLevelDef(int slot, LevelDef& def);
bool DeleteLevelFile(const std::string& path);

} // namespace app
