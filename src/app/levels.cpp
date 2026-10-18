#include "levels.h"

#include <algorithm>
#include <cstring>
#include <queue>
#include <vector>

#include "nes_data.h"

namespace app {

namespace {

enum Id : uint8_t {
    kBrickRight = 0, kBrickBottom, kBrickLeft, kBrickTop, kBrick,
    kSteelRight, kSteelBottom, kSteelLeft, kSteelTop, kSteel,
    kWater, kForest, kIce, kEmpty,
};

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) {
        if (s == 0) s = 1;
        for (int i = 0; i < 4; ++i) Next();
    }
    uint32_t Next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    bool Chance(int percent) { return static_cast<int>(Next() % 100u) < percent; }
};

// La construccion original de la ROM ocupa las columnas 7..19 y las filas 1..13 del campo ancho.
bool InOriginalArea(int r, int c) { return r >= 1 && c >= 7 && c <= 19; }

// Que todos los puntos de aparicion sean alcanzables desde la base sin cruzar agua ni hierro. Se vacian celdas duras
// del camino de menor costo; las de la construccion original casi nunca se tocan.
void EnsureReachable(uint8_t out[kLevelRows][kLevelCols], int bossLayers) {
    const bool boss = bossLayers > 0;                  // (las capas de hierro del aguila las dibuja el motor, de 1 mosaico)
    // puntos de aparicion: en los niveles del aguila, a sus lados, enfrente y las dos puntas de arriba
    const int normal[5][2] = {{0, 0}, {0, 13}, {0, 26}, {6, 0}, {6, 26}};
    const int gc = 1 + (bossLayers + 1) / 2;            // distancia de las apariciones a la fortaleza, en bloques (2 o 3)
    const int bossPts[7][2] = {{0, 13 - gc}, {gc, 13}, {0, 13 + gc}, {0, 0}, {0, 26}, {6, 0}, {6, 26}};
    const int (*spawns)[2] = boss ? bossPts : normal;
    const int nSpawns = boss ? 7 : 5;
    for (int guard = 0; guard < 20; ++guard) {
        bool reach[kLevelRows][kLevelCols] = {};
        std::queue<std::pair<int, int>> q;
        q.push({13, 13});
        reach[13][13] = true;
        while (!q.empty()) {
            const auto [r, c] = q.front();
            q.pop();
            const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nr = r + dr[k], nc = c + dc[k];
                if (nr < 0 || nr >= kLevelRows || nc < 0 || nc >= kLevelCols || reach[nr][nc]) continue;
                if (!LevelCellPassable(out[nr][nc])) continue;
                reach[nr][nc] = true;
                q.push({nr, nc});
            }
        }
        int target = -1;
        for (int i = 0; i < nSpawns; ++i) {
            if (!reach[spawns[i][0]][spawns[i][1]]) { target = i; break; }
        }
        if (target < 0) break;
        struct Node { int cost, r, c; bool operator>(const Node& o) const { return cost > o.cost; } };
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
        int dist[kLevelRows][kLevelCols];
        int prevR[kLevelRows][kLevelCols], prevC[kLevelRows][kLevelCols];
        for (auto& row : dist) for (int& d : row) d = 1 << 20;
        dist[spawns[target][0]][spawns[target][1]] = 0;
        pq.push({0, spawns[target][0], spawns[target][1]});
        int endR = -1, endC = -1;
        while (!pq.empty()) {
            const Node n = pq.top();
            pq.pop();
            if (n.cost > dist[n.r][n.c]) continue;
            if (reach[n.r][n.c]) { endR = n.r; endC = n.c; break; }
            const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nr = n.r + dr[k], nc = n.c + dc[k];
                if (nr < 0 || nr >= kLevelRows || nc < 0 || nc >= kLevelCols) continue;
                const int nd = n.cost + (LevelCellPassable(out[nr][nc]) ? 0 : (InOriginalArea(nr, nc) ? 1000 : 1));
                if (nd < dist[nr][nc]) {
                    dist[nr][nc] = nd;
                    prevR[nr][nc] = n.r;
                    prevC[nr][nc] = n.c;
                    pq.push({nd, nr, nc});
                }
            }
        }
        if (endR < 0) break;
        for (int r = endR, c = endC; !(r == spawns[target][0] && c == spawns[target][1]);) {
            if (!LevelCellPassable(out[r][c])) out[r][c] = kEmpty;
            const int pr = prevR[r][c], pc = prevC[r][c];
            r = pr;
            c = pc;
        }
        if (!LevelCellPassable(out[spawns[target][0]][spawns[target][1]])) out[spawns[target][0]][spawns[target][1]] = kEmpty;
    }
}

} // namespace

int DifficultyTier(int level) {
    const int l = std::max(1, std::min(kLevelCount, level));
    return (l - 1) / 5;
}

bool LevelCellPassable(uint8_t id) {
    return !(id == kWater || (id >= kSteelRight && id <= kSteel));
}

void GenerateWideLevel(int level, uint8_t out[kLevelRows][kLevelCols]) {
    const int l = std::max(1, std::min(kLevelCount, level));
    const int tier = DifficultyTier(l);
    std::memset(out, kEmpty, kLevelRows * kLevelCols);

    // 1) Centro: la construccion original de la ROM (etapas 1-35; del 36 al 40 se repiten 1-5, como hace el juego al terminar).
    const int stage = (l - 1) % 35 + 1;
    const uint8_t* data = &nes::tbl_F07A_stage_data[(stage - 1) * 91];
    uint8_t orig[13][13];
    for (int row = 0; row < 13; ++row) {
        for (int col = 0; col < 13; ++col) {
            const int nibble = row * 14 + col;                   // 13 nibbles de datos + 1 de relleno por fila
            const uint8_t byte = data[nibble >> 1];
            orig[row][col] = (nibble & 1) ? (byte & 0x0F) : (byte >> 4);
            out[row + 1][7 + col] = orig[row][col];
        }
    }

    // 2) Relleno coherente con el mapa: los costados continúan el dibujo de la propia construccion, reflejando sus
    //    columnas hacia afuera (mismos bloques y mismo estilo). Del 36 al 40 se refleja el borde opuesto.
    const bool flip = l > 35;
    Rng rng(static_cast<uint32_t>(l) * 7919u + 17u);
    for (int row = 0; row < 13; ++row) {
        for (int k = 0; k < 7; ++k) {
            out[row + 1][6 - k] = orig[row][flip ? 12 - k : k];
            out[row + 1][20 + k] = orig[row][flip ? k : 12 - k];
        }
    }
    // con la dificultad parte del ladrillo del relleno pasa a hierro
    for (int row = 1; row < kLevelRows; ++row) {
        for (int col = 0; col < kLevelCols; ++col) {
            if (!InOriginalArea(row, col) && out[row][col] == kBrick && rng.Chance(tier * 3)) out[row][col] = kSteel;
        }
    }

    // 2b) Mas relleno en los costados (las zonas vacias): bloques sueltos o de a dos, de ladrillo, algo de hierro con la dificultad
    //     y arbustos; siempre separados entre si, simetricos y sin tocar la construccion original del centro.
    {
        Rng fill(static_cast<uint32_t>(l) * 104729u + 31u);
        const int chance = 50 + tier * 2;
        for (int row = 1; row <= 12; ++row) {
            for (int col = 0; col <= 6; ++col) {
                bool free = out[row][col] == kEmpty;              // la celda y sus 4 vecinas (arriba, abajo, izquierda y derecha), vacias
                static const int kD[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (const auto& d : kD) {
                    const int rr = row + d[0], cc = col + d[1];
                    if (rr < 0 || rr >= kLevelRows || cc < 0 || cc >= kLevelCols) continue;
                    if (out[rr][cc] != kEmpty) free = false;
                }
                if (!free || !fill.Chance(chance)) continue;
                const int roll = static_cast<int>(fill.Next() % 100u);
                const uint8_t id = roll < 58 ? kBrick : (roll < 58 + tier * 4 ? kSteel : kForest);
                out[row][col] = id;
                if (col < 5 && fill.Chance(40) && out[row][col + 1] == kEmpty) out[row][col + 1] = id;    // de a dos
                out[row][kLevelCols - 1 - col] = out[row][col];
                if (out[row][col + 1] == id && col < 5) out[row][kLevelCols - 2 - col] = id;
            }
        }
    }

    // 3) Zonas que quedan libres: fila de arriba, aparicion lateral a mitad del mapa y los lugares de los jugadores
    for (int c = 0; c < kLevelCols; ++c) out[0][c] = kEmpty;
    for (int r = 5; r <= 7; ++r)
        for (int c : {0, 1, 25, 26}) out[r][c] = kEmpty;
    for (int c : {10, 11, 12, 13, 14, 15, 16}) out[13][c] = kEmpty;   // jugadores y base
    for (int c = 12; c <= 14; ++c) out[12][c] = kEmpty;

    const int layers = l % 10 == 0 ? l / 10 : 0;          // capas de hierro del aguila: 10 -> 1, 20 -> 2, 30 -> 3, 40 -> 4
    if (layers > 0) {
        // Aguila enemiga arriba al centro: el motor dibuja el aguila (fila 0, columna 13) y sus capas de hierro de 1 mosaico;
        // aca solo se despeja la zona (los costados y el frente, donde aparecen los enemigos, quedan libres).
        const int gc = 1 + (layers + 1) / 2;
        for (int r = 0; r <= gc + 1; ++r)
            for (int c = 13 - gc - 2; c <= 13 + gc + 2; ++c) out[r][c] = kEmpty;
    }
    EnsureReachable(out, layers);
}

void GenerateTopStrip(int level, uint8_t out[kLevelCols]) {
    const int l = std::max(1, std::min(kLevelCount, level));
    const int tier = DifficultyTier(l);
    const bool boss = l % 10 == 0;
    for (int c = 0; c < kLevelCols; ++c) out[c] = kEmpty;
    const int lastCol = boss ? 7 : 10;                        // en los niveles del aguila el centro queda libre para su fortaleza
    for (int c = 2; c <= lastCol; ++c) {
        const int v = (c * 5 + l * 3) % 7;
        uint8_t id = kEmpty;
        if (v < 4) id = kBrick;                                // mas denso que antes
        else if (v == 4 && tier >= 1) id = kSteel;
        else if (v == 5 && tier >= 4) id = kSteel;
        out[c] = out[kLevelCols - 1 - c] = id;                // simetrico; las columnas 0-1 y 25-26 (apariciones) y el centro, libres
    }
}

} // namespace app
