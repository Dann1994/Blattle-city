#pragma once

// Pantalla de pruebas: un nivel de 13x13 bloques en el formato original
// (un nibble por bloque, ver tbl_DACB_block_data) con al menos uno de cada
// tipo de bloque, pasillos de 1 bloque para probar colisiones y las zonas de
// aparicion libres. El aguila y su muro los dibuja sub_CAF5 como en el juego.
//
//  Nibble: 0 ladrillo mitad der | 1 mitad inf | 2 mitad izq | 3 mitad sup | 4 ladrillo entero
//          5 acero mitad der    | 6 mitad inf | 7 mitad izq | 8 mitad sup | 9 acero entero
//          A agua | B arbustos | C hielo | D vacio

#include <cstdint>

namespace app {

inline constexpr const char* kArenaRows[13] = {
    "DDDDDDDDDDDDD",   // 0  fila de aparicion de enemigos (arriba)
    "D40123D95678D",   // 1  ladrillo entero + 4 mitades | acero entero + 4 mitades
    "DDDDDDDDDDDDD",   // 2
    "DAAADDBBBDCCC",   // 3  agua | arbustos | hielo
    "DDDDDDDDDDDDD",   // 4
    "4444D444D4444",   // 5  muro de ladrillo con dos huecos de 1 bloque
    "9999DDDDD9999",   // 6  muro de acero con un hueco ancho
    "DBBDDCCDDBBDD",   // 7  arbustos / hielo para ver tanques "debajo"
    "AADDAAAAADDAA",   // 8  agua con pasos
    "DD4DDD9DDD4DD",   // 9  bloques sueltos
    "D33D11D22D00D",   // 10 mitades de ladrillo
    "DDDDDDDDDDDDD",   // 11 (el muro del aguila lo dibuja sub_CAF5)
    "DDDDDDDDDDDDD",   // 12 jugadores y aguila abajo
};

// Convierte las filas a los 91 bytes de un nivel original (13 filas de 7 bytes,
// 13 nibbles de datos + 1 de relleno).
inline void ArenaToStageData(uint8_t out[91]) {
    for (int row = 0; row < 13; ++row) {
        for (int i = 0; i < 14; ++i) {
            char c = (i < 13) ? kArenaRows[row][i] : 'D';
            int v = (c >= 'A') ? (c - 'A' + 10) : (c - '0');
            const int index = row * 7 + (i >> 1);
            if ((i & 1) == 0) {
                out[index] = static_cast<uint8_t>(v << 4);
            } else {
                out[index] = static_cast<uint8_t>(out[index] | v);
            }
        }
    }
}

// Campo panoramico (25x13 bloques): cada fila es la mitad izquierda de kArenaRows
// espejada (la columna 12 es la central, donde esta el aguila).
inline void ArenaToWideBlocks(uint8_t out[14][27]) {
    for (int row = 0; row < 14; ++row) {
        // 14 filas: la 5 es una fila vacia extra; el resto son las 13 del mapa clasico
        const int srcRow = row < 5 ? row : (row == 5 ? 4 : row - 1);
        for (int col = 0; col < 27; ++col) {
            const int src = col <= 12 ? col : 26 - col;           // la columna 13 (centro) queda vacia
            const char c = col == 13 ? 'D' : kArenaRows[srcRow][src];
            out[row][col] = static_cast<uint8_t>((c >= 'A') ? (c - 'A' + 10) : (c - '0'));
        }
    }
}

} // namespace app
