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

} // namespace app
