#include "levels.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <vector>

#include "nes_data.h"

namespace app {

namespace {

enum Id : uint8_t {
    kBrickRight = 0, kBrickBottom, kBrickLeft, kBrickTop, kBrick,
    kSteelRight, kSteelBottom, kSteelLeft, kSteelTop, kSteel,
    kWater, kForest, kIce, kEmpty, kSand,
    kLava = 16,                      // (el motor la conoce como 16; en los niveles del editor es el 15)
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


// Tematica por tandas de 5 niveles: 1-5 ladrillos, 6-10 arbustos, 11-15 agua, 16-20 arena, 21-25 hielo, 26-30 hierro, 31-35 lava, 36-40 mezcla.
// Cada tanda convierte parte de los ladrillos al bloque que predomina, suma parches de ese bloque en las zonas vacias y deja un poco
// de los bloques de las tandas anteriores. Los costados son simetricos; el centro (la construccion original) no.
struct ThemeRule { int convert[16]; int patch[16]; };      // por id de bloque: % de ladrillos que pasan a ese bloque / % de celdas vacias donde empieza un parche

ThemeRule ThemeFor(int theme) {
    ThemeRule t{};
    auto cv = [&](int id, int pct) { t.convert[id] = pct; };
    auto pa = [&](int id, int pct) { t.patch[id] = pct; };
    switch (theme) {
        case 1: cv(kForest, 30); pa(kForest, 14); break;
        case 2: cv(kWater, 30); pa(kWater, 10); cv(kForest, 6); pa(kForest, 3); break;
        case 3: cv(kSand, 35); pa(kSand, 18); cv(kForest, 6); pa(kForest, 3); cv(kWater, 5); pa(kWater, 2); break;
        case 4: cv(kIce, 35); pa(kIce, 18); cv(kSand, 8); pa(kSand, 4); cv(kWater, 4); pa(kWater, 2); cv(kForest, 5); pa(kForest, 3); break;
        case 5: cv(kSteel, 45); pa(kSteel, 8); cv(kIce, 7); pa(kIce, 3); cv(kSand, 6); pa(kSand, 3); cv(kWater, 4); pa(kWater, 2); cv(kForest, 4); pa(kForest, 2); break;
        case 6: cv(kSteel, 22); pa(kSteel, 4); cv(kIce, 4); pa(kIce, 2); cv(kSand, 4); pa(kSand, 2); cv(kWater, 3); pa(kWater, 2); cv(kForest, 3); pa(kForest, 2); break;
        case 7: cv(kForest, 8); pa(kForest, 5); cv(kWater, 8); pa(kWater, 4); cv(kSand, 8); pa(kSand, 5); cv(kIce, 8); pa(kIce, 5); cv(kSteel, 10); pa(kSteel, 3); break;
        default: break;
    }
    return t;
}

void ApplyTheme(uint8_t out[kLevelRows][kLevelCols], int l) {
    const int theme = (l - 1) / 5;
    if (theme == 0) return;
    const ThemeRule rule = ThemeFor(theme);
    Rng rng(static_cast<uint32_t>(l) * 15485863u + 101u);
    static const int kIds[] = {kForest, kWater, kSand, kIce, kSteel};
    // 1) parte de los ladrillos pasa al bloque del tema
    for (int row = 1; row < kLevelRows; ++row) {
        for (int col = 0; col <= 19; ++col) {
            if (out[row][col] > kBrick) continue;                       // solo ladrillos (enteros o mitades)
            int roll = static_cast<int>(rng.Next() % 100u);
            for (int id : kIds) {
                if (roll < rule.convert[id]) { out[row][col] = static_cast<uint8_t>(id); break; }
                roll -= rule.convert[id];
            }
            if (col <= 6) out[row][kLevelCols - 1 - col] = out[row][col];
        }
    }
    // 2) parches en las celdas vacias: de 1 a 3 bloques seguidos
    for (int row = 1; row <= 12; ++row) {
        for (int col = 0; col <= 19; ++col) {
            if (out[row][col] != kEmpty) continue;
            int roll = static_cast<int>(rng.Next() % 100u);
            int pick = -1;
            for (int id : kIds) {
                if (roll < rule.patch[id]) { pick = id; break; }
                roll -= rule.patch[id];
            }
            if (pick < 0) continue;
            const int len = 1 + static_cast<int>(rng.Next() % 3u);
            for (int k = 0; k < len && col + k <= 19; ++k) {
                if (out[row][col + k] != kEmpty) break;
                out[row][col + k] = static_cast<uint8_t>(pick);
                if (col + k <= 6) out[row][kLevelCols - 1 - (col + k)] = static_cast<uint8_t>(pick);
            }
        }
    }
}

// Lava contenida: 1 o 2 celdas de lava rodeadas de ladrillo y hierro (mientras no se rompa el borde no se mueve). Va al final, sobre celdas
// que no sean agua ni hierro, lejos de las apariciones, la base y el aguila enemiga, y despues de garantizar los caminos (rodear con bloques no los cierra).
void PlaceLavaPools(uint8_t out[kLevelRows][kLevelCols], int l, int attempts) {
    Rng rng(static_cast<uint32_t>(l) * 49979687u + 7u);
    const bool boss = l % 10 == 0;
    auto zoneOk = [&](int r0, int r1, int c0, int c1) {
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                if (r < 2 || r > 10 || c < 3 || c > kLevelCols - 4) return false;                 // lejos de los bordes, las apariciones y la base
                if (boss && r <= 5 && c >= 7 && c <= 19) return false;                            // la fortaleza del aguila enemiga
                if (out[r][c] == kLava) return false;                                                    // lo demas se reemplaza
            }
        }
        return true;
    };
    auto pool = [&](int r, int c, int w) {                      // lava de w celdas con su borde
        for (int rr = r - 1; rr <= r + 1; ++rr)
            for (int cc = c - 1; cc <= c + w; ++cc) {
                const bool inner = rr == r && cc >= c && cc < c + w;
                if (inner) { out[rr][cc] = kLava; continue; }
                const bool corner = (rr != r) && (cc == c - 1 || cc == c + w);
                out[rr][cc] = (corner && rng.Chance(60)) ? kSteel : kBrick;
            }
    };
    int placed = 0;
    for (int i = 0; i < attempts * 40 && placed < attempts; ++i) {
        const int w = rng.Chance(40) ? 2 : 1;
        const int r = 3 + static_cast<int>(rng.Next() % 7u);
        const int c = 4 + static_cast<int>(rng.Next() % 18u);
        if (!zoneOk(r - 1, r + 1, c - 1, c + w)) continue;
        // los costados se reflejan: cuenta como dos pozos
        const int mc = kLevelCols - c - w;
        const bool mirror = c <= 6 && mc != c && zoneOk(r - 1, r + 1, mc - 1, mc + w);
        if (c <= 6 && !(c + w + 1 < mc - 1)) continue;
        pool(r, c, w);
        if (mirror) pool(r, mc, w);
        ++placed;
    }
}

} // namespace

int DifficultyTier(int level) {
    const int l = std::max(1, std::min(kLevelCount, level));
    return (l - 1) / 5;
}

bool LevelCellPassable(uint8_t id) {
    return !(id == kWater || (id >= kSteelRight && id <= kSteel) || (id >= 20 && id <= 23));          // (20..23: hierro de un solo mosaico, solo en los niveles del editor)
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

    ApplyTheme(out, l);

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
    const int theme = (l - 1) / 5;
    if (l == 5) PlaceLavaPools(out, l, 2);                // la lava se presenta en el nivel 5: dos pozos contenidos
    else if (theme == 6) PlaceLavaPools(out, l, 7);
    else if (theme == 7) PlaceLavaPools(out, l, 3);
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


// ---------------------------------------------------------------------------
// Niveles hechos a mano
// ---------------------------------------------------------------------------

namespace {

std::string g_levelRoot;
bool g_useCustom = true;

void SetDefaultSpawns(LevelDef& d) {
    d.nSpawns = 0;
    auto add = [&](int c, int r) {
        if (d.nSpawns >= kMaxSpawns) return;
        d.spawnCol[d.nSpawns] = static_cast<uint8_t>(c);
        d.spawnRow[d.nSpawns] = static_cast<uint8_t>(r);
        ++d.nSpawns;
    };
    if (d.versus) {
        add(1, 7);                                                 // equipo 1: en el medio a la izquierda y en el medio abajo
        add(13, 13);
        add(25, 7);                                                // equipo 2: en el medio a la derecha y en el medio arriba
        add(13, 1);
        return;
    }
    if (d.boss) {
        const int gc = 1 + (d.rank + 1) / 2;                       // afuera de las capas de la fortaleza
        add(d.eagleCol - gc, d.eagleRow);
        add(d.eagleCol, d.eagleRow + gc);
        add(d.eagleCol + gc, d.eagleRow);
        add(0, 0);
        add(26, 0);
    } else {
        add(0, 0);
        add(13, 0);
        add(26, 0);
    }
    add(0, 7);
    add(26, 7);
    // los que quedaron fuera del mapa o repetidos se descartan
    int n = 0;
    for (int i = 0; i < d.nSpawns; ++i) {
        if (d.spawnCol[i] >= kLevelCols || d.spawnRow[i] >= kGridRows) continue;
        bool dup = false;
        for (int j = 0; j < n; ++j) dup = dup || (d.spawnCol[j] == d.spawnCol[i] && d.spawnRow[j] == d.spawnRow[i]);
        if (dup) continue;
        d.spawnCol[n] = d.spawnCol[i];
        d.spawnRow[n] = d.spawnRow[i];
        ++n;
    }
    d.nSpawns = n;
}

}  // namespace

LevelDef::LevelDef() {
    std::memset(cells, kEmpty, sizeof(cells));
}

int EagleReachBlocks(int rank) {
    const int r = std::max(1, std::min(4, rank));
    return (r + 1) / 2;
}

void ClearEagleZone(LevelDef& d) {
    const int reach = EagleReachBlocks(d.rank);
    for (int r = d.eagleRow - reach; r <= d.eagleRow + reach; ++r)
        for (int c = d.eagleCol - reach; c <= d.eagleCol + reach; ++c)
            if (r >= 0 && r < kGridRows && c >= 0 && c < kLevelCols) d.cells[r][c] = kEmpty;
}

LevelDef BlankLevelDef(bool boss) {
    LevelDef d;
    d.boss = boss;
    if (boss) ClearEagleZone(d);
    SetDefaultSpawns(d);
    return d;
}

LevelDef BuiltinLevelDef(int level) {
    const int l = std::max(1, std::min(kLevelCount, level));
    LevelDef d;
    GenerateTopStrip(l, d.cells[0]);
    uint8_t body[kLevelRows][kLevelCols];
    GenerateWideLevel(l, body);
    for (int r = 0; r < kLevelRows; ++r)
        for (int c = 0; c < kLevelCols; ++c) d.cells[r + 1][c] = body[r][c] == kLava ? 15 : body[r][c];
    d.boss = l % 10 == 0;
    d.tier = DifficultyTier(l);
    d.rank = std::max(1, std::min(4, l / 10));
    SetDefaultSpawns(d);
    return d;
}

// Los ids de bloque de los niveles (editor) y los del motor: la lava es el 15 en los niveles y el 16 en el motor (el 15 es "borrar" en la ROM);
// las puntas de ladrillo y hierro (16..23 en los niveles) son 17..24 en el motor.
uint8_t EngineBlockId(uint8_t id) {
    if (id == 15) return 16;
    if (id >= 16 && id <= 23) return static_cast<uint8_t>(id + 1);
    return id;
}

void LevelDefToStage(const LevelDef& d, uint8_t blocks[kLevelRows][kLevelCols], uint8_t strip[kLevelCols], nes::Battle::StageSpec& spec) {
    // (en los niveles la lava es el id 15; el motor la conoce como 16 porque el 15 es "borrar" en la ROM)
    for (int c = 0; c < kLevelCols; ++c) strip[c] = EngineBlockId(d.cells[0][c]);
    for (int r = 0; r < kLevelRows; ++r)
        for (int c = 0; c < kLevelCols; ++c) blocks[r][c] = EngineBlockId(d.cells[r + 1][c]);
    spec = nes::Battle::StageSpec{};
    spec.active = true;
    if (d.versus) {
        spec.versus = true;
        spec.royale = d.royale;
        spec.boss = false;
        spec.tier = std::max(0, std::min(7, d.tier));
        spec.rank = std::max(1, std::min(4, d.rank));
        spec.vsLayers = std::max(1, std::min(2, d.vsLayers));
        for (int t = 0; t < 2; ++t) {
            spec.vsEagleCol[t] = std::max(1, std::min(kLevelCols - 2, d.vsEagleCol[t]));
            spec.vsEagleRow[t] = std::max(1, std::min(kGridRows - 2, d.vsEagleRow[t]));
        }
        // 2 puntos por equipo (el equipo 1 a la izquierda del centro, el 2 a la derecha); si falta alguno se repite o se usa el de siempre
        int a[2][2], cnt[2] = {0, 0};
        for (int i = 0; i < d.nSpawns; ++i) {
            const int t = VsSpawnTeam(spec.vsEagleCol, spec.vsEagleRow, d.spawnCol[i], d.spawnRow[i]);
            if (t < 0 || cnt[t] >= 2) continue;
            a[t][cnt[t]++] = i;
        }
        for (int t = 0; t < 2; ++t) {
            for (int k = 0; k < 2; ++k) {
                if (cnt[t] == 0) {
                    spec.spawnCol[t * 2 + k] = k == 0 ? (t == 0 ? 1 : 25) : 13;
                    spec.spawnRow[t * 2 + k] = k == 0 ? 7 : (t == 0 ? 13 : 1);
                } else {
                    const int i = a[t][k < cnt[t] ? k : 0];
                    spec.spawnCol[t * 2 + k] = std::min<int>(d.spawnCol[i], kLevelCols - 1);
                    spec.spawnRow[t * 2 + k] = std::min<int>(d.spawnRow[i], kGridRows - 1);
                }
            }
        }
        spec.nSpawns = 4;
        return;
    }
    spec.boss = d.boss;
    spec.tier = std::max(0, std::min(7, d.tier));
    spec.rank = std::max(1, std::min(4, d.rank));
    spec.eagleCol = std::max(0, std::min(kLevelCols - 1, d.eagleCol));
    spec.eagleRow = std::max(0, std::min(kGridRows - 1, d.eagleRow));
    for (int i = 0; i < 4; ++i) { spec.playerCol[i] = d.playerCol[i]; spec.playerRow[i] = d.playerRow[i]; }
    spec.baseCol = d.baseCol;
    spec.baseRow = d.baseRow;
    spec.nSpawns = std::max(0, std::min(kMaxSpawns, d.nSpawns));
    for (int i = 0; i < spec.nSpawns; ++i) {
        spec.spawnCol[i] = std::min<int>(d.spawnCol[i], kLevelCols - 1);
        spec.spawnRow[i] = std::min<int>(d.spawnRow[i], kGridRows - 1);
    }
}

namespace {
void WriteLevel(std::ostream& f, const LevelDef& d) {
    f << "BCNES-NIVEL 1\n";
    f << "tipo=" << (d.versus ? "batalla" : (d.boss ? "especial" : "normal")) << "\n";
    if (d.versus) {
        f << "aguilas=" << d.vsEagleCol[0] << "," << d.vsEagleRow[0] << ";" << d.vsEagleCol[1] << "," << d.vsEagleRow[1] << "\n";
        f << "capas=" << d.vsLayers << "\n";
    }
    f << "dificultad=" << d.tier << "\n";
    f << "aguila=" << d.rank << "\n";
    f << "posicion_aguila=" << d.eagleCol << "," << d.eagleRow << "\n";
    if (!d.versus) {
        f << "base=" << d.baseCol << "," << d.baseRow << "\n";
        f << "jugadores=";
        for (int i = 0; i < 4; ++i) f << (i ? ";" : "") << static_cast<int>(d.playerCol[i]) << "," << static_cast<int>(d.playerRow[i]);
        f << "\n";
    }
    f << "apariciones=";
    for (int i = 0; i < d.nSpawns; ++i) f << (i ? ";" : "") << static_cast<int>(d.spawnCol[i]) << "," << static_cast<int>(d.spawnRow[i]);
    f << "\nmapa\n";
    static const char kHex[] = "0123456789ABCDEFGHIJKLMN";                 // G..N: ladrillo y hierro de un solo mosaico (puntas)
    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kLevelCols; ++c) f << kHex[d.cells[r][c] % 24];
        f << "\n";
    }
}

bool ReadLevel(std::istream& f, LevelDef& out) {
    std::string line;
    if (!std::getline(f, line) || line.rfind("BCNES-NIVEL", 0) != 0) return false;
    LevelDef d;
    d.nSpawns = 0;
    int rows = 0;
    bool inMap = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (inMap) {
            if (rows >= kGridRows) break;
            if (static_cast<int>(line.size()) < kLevelCols) return false;
            for (int c = 0; c < kLevelCols; ++c) {
                const char ch = line[static_cast<size_t>(c)];
                const int v = ch >= '0' && ch <= '9' ? ch - '0' : (ch >= 'A' && ch <= 'N' ? ch - 'A' + 10 : (ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1));
                if (v < 0) return false;
                d.cells[rows][c] = static_cast<uint8_t>(v);
            }
            ++rows;
            continue;
        }
        if (line == "mapa") { inMap = true; continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "tipo") {
            d.boss = val == "especial";
            d.versus = val == "batalla";
        } else if (key == "aguilas") {
            int c0 = 1, r0 = 7, c1 = 25, r1 = 7;
            if (std::sscanf(val.c_str(), "%d,%d;%d,%d", &c0, &r0, &c1, &r1) == 4) {
                d.vsEagleCol[0] = std::max(1, std::min(kLevelCols - 2, c0));
                d.vsEagleRow[0] = std::max(1, std::min(kGridRows - 2, r0));
                d.vsEagleCol[1] = std::max(1, std::min(kLevelCols - 2, c1));
                d.vsEagleRow[1] = std::max(1, std::min(kGridRows - 2, r1));
            }
        } else if (key == "capas") {
            d.vsLayers = std::max(1, std::min(2, std::atoi(val.c_str())));
        } else if (key == "dificultad") {
            d.tier = std::max(0, std::min(7, std::atoi(val.c_str())));
        } else if (key == "aguila") {
            d.rank = std::max(1, std::min(4, std::atoi(val.c_str())));
        } else if (key == "posicion_aguila") {
            int c = 13, r = 1;
            if (std::sscanf(val.c_str(), "%d,%d", &c, &r) == 2) {
                d.eagleCol = std::max(0, std::min(kLevelCols - 1, c));
                d.eagleRow = std::max(0, std::min(kGridRows - 1, r));
            }
        } else if (key == "base") {
            int c = 13, r = 14;
            if (std::sscanf(val.c_str(), "%d,%d", &c, &r) == 2) {
                d.baseCol = std::max(1, std::min(kLevelCols - 2, c));
                d.baseRow = std::max(2, std::min(kGridRows - 1, r));
            }
        } else if (key == "jugadores") {
            std::stringstream ss(val);
            std::string item;
            for (int i = 0; i < 4 && std::getline(ss, item, ';'); ++i) {
                int c = 0, r = 0;
                if (std::sscanf(item.c_str(), "%d,%d", &c, &r) == 2 && c >= 0 && c < kLevelCols && r >= 1 && r < kGridRows) {
                    d.playerCol[i] = static_cast<uint8_t>(c);
                    d.playerRow[i] = static_cast<uint8_t>(r);
                }
            }
        } else if (key == "apariciones") {
            std::stringstream ss(val);
            std::string item;
            while (std::getline(ss, item, ';') && d.nSpawns < kMaxSpawns) {
                int c = 0, r = 0;
                if (std::sscanf(item.c_str(), "%d,%d", &c, &r) == 2 && c >= 0 && c < kLevelCols && r >= 0 && r < kGridRows) {
                    d.spawnCol[d.nSpawns] = static_cast<uint8_t>(c);
                    d.spawnRow[d.nSpawns] = static_cast<uint8_t>(r);
                    ++d.nSpawns;
                }
            }
        }
    }
    if (rows != kGridRows) return false;
    out = d;
    return true;
}
}  // namespace

bool SaveLevelDef(const std::string& path, const LevelDef& d) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    WriteLevel(f, d);
    return static_cast<bool>(f);
}

bool LoadLevelDef(const std::string& path, LevelDef& out) {
    std::ifstream f(path, std::ios::binary);
    return f && ReadLevel(f, out);
}

std::string LevelDefToText(const LevelDef& d) {
    std::ostringstream os;
    WriteLevel(os, d);
    return os.str();
}

bool LevelDefFromText(const std::string& text, LevelDef& out) {
    std::istringstream is(text);
    return ReadLevel(is, out);
}

std::vector<std::string> CheckLevelDef(const LevelDef& d) {
    std::vector<std::string> warn;
    if (d.versus) {
        int cnt[2] = {0, 0};
        for (int i = 0; i < d.nSpawns; ++i) {
            const int t = VsSpawnTeam(d.vsEagleCol, d.vsEagleRow, d.spawnCol[i], d.spawnRow[i]);
            if (t >= 0) ++cnt[t];
        }
        if (cnt[0] < 2 || cnt[1] < 2) warn.push_back("HACEN FALTA 2 APARICIONES POR EQUIPO (" + std::to_string(cnt[0]) + " Y " + std::to_string(cnt[1]) + ")");
    } else if (d.nSpawns == 0) {
        warn.push_back("SIN PUNTOS DE APARICIÓN: SE USAN LOS DE SIEMPRE");
    }
    // alcanzables desde la base (sin cruzar agua ni hierro)
    bool reach[kGridRows][kLevelCols] = {};
    std::queue<std::pair<int, int>> q;
    int startR = d.versus ? kGridRows - 1 : d.baseRow, startC = d.versus ? 13 : d.baseCol;
    if (d.versus) nes::Battle::VsSideBlock(d.vsEagleCol[0], d.vsEagleRow[0], 0, startC, startR);
    q.push({startR, startC});
    reach[startR][startC] = true;
    while (!q.empty()) {
        const auto [r, c] = q.front();
        q.pop();
        const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            const int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nr >= kGridRows || nc < 0 || nc >= kLevelCols || reach[nr][nc]) continue;
            if (!LevelCellPassable(d.cells[nr][nc])) continue;
            reach[nr][nc] = true;
            q.push({nr, nc});
        }
    }
    if (d.versus) {
        int c1, r1;
        nes::Battle::VsSideBlock(d.vsEagleCol[1], d.vsEagleRow[1], 0, c1, r1);
        if (!reach[r1][c1]) warn.push_back("NO HAY CAMINO ENTRE LAS DOS ÁGUILAS");
    }
    if (!d.versus) {
        for (int i = 0; i < d.nSpawns; ++i)
            if (std::abs(d.spawnCol[i] - d.baseCol) <= 1 && d.spawnRow[i] >= d.baseRow - 1 && d.spawnRow[i] <= d.baseRow) warn.push_back("APARICIÓN " + std::to_string(i + 1) + " DENTRO DE LA BASE");
        for (int p = 0; p < 4; ++p) {
            const int c = d.playerCol[p], r = d.playerRow[p];
            if (!reach[r][c]) warn.push_back("JUGADOR " + std::to_string(p + 1) + " ENCERRADO");
            for (int i = 0; i < d.nSpawns; ++i)
                if (d.spawnCol[i] == c && d.spawnRow[i] == r) warn.push_back("JUGADOR " + std::to_string(p + 1) + " SOBRE UNA APARICIÓN");
        }
    }
    const int zone = EagleReachBlocks(d.rank);
    for (int i = 0; i < d.nSpawns; ++i) {
        const int c = d.spawnCol[i], r = d.spawnRow[i];
        if (d.versus) {
            bool inside = false;
            for (int t = 0; t < 2; ++t) inside = inside || (std::abs(c - d.vsEagleCol[t]) <= 1 && std::abs(r - d.vsEagleRow[t]) <= 1);
            if (inside) warn.push_back("APARICIÓN " + std::to_string(i + 1) + " DENTRO DE UNA FORTALEZA");
            else if (!reach[r][c]) warn.push_back("APARICIÓN " + std::to_string(i + 1) + " ENCERRADA");
        } else if (d.boss && std::abs(c - d.eagleCol) <= zone && std::abs(r - d.eagleRow) <= zone) {
            warn.push_back("APARICIÓN " + std::to_string(i + 1) + " DENTRO DE LA FORTALEZA");
        } else if (!reach[r][c]) {
            warn.push_back("APARICIÓN " + std::to_string(i + 1) + " ENCERRADA");
        }
    }
    return warn;
}

void SetLevelRoot(const std::string& dir) {
    g_levelRoot = dir;
    if (!g_levelRoot.empty() && g_levelRoot.back() != '/' && g_levelRoot.back() != '\\') g_levelRoot += '/';
}

const std::string& LevelRoot() { return g_levelRoot; }

std::string MainLevelFile(int level) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "principal/nivel_%02d.bcl", level);
    return g_levelRoot + buf;
}

std::string OwnLevelFile(int slot) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "propios/slot_%02d.bcl", slot);
    return g_levelRoot + buf;
}

void UseCustomLevels(bool on) { g_useCustom = on; }
bool CustomLevelsEnabled() { return g_useCustom && !g_levelRoot.empty(); }

bool MainLevelHasFile(int level) {
    if (!CustomLevelsEnabled() || level < 1 || level > kMaxMainLevel) return false;
    std::error_code ec;
    return std::filesystem::exists(MainLevelFile(level), ec);
}

int MainLevelCount() {
    int n = kLevelCount;
    while (n < kMaxMainLevel && MainLevelHasFile(n + 1)) ++n;
    return n;
}

bool LoadMainLevelDef(int level, LevelDef& def) {
    if (MainLevelHasFile(level) && LoadLevelDef(MainLevelFile(level), def)) return true;
    if (level < 1 || level > kLevelCount) return false;
    def = BuiltinLevelDef(level);
    return true;
}

bool OwnSlotHasFile(int slot) {
    if (g_levelRoot.empty() || slot < 1 || slot > kOwnSlots) return false;
    std::error_code ec;
    return std::filesystem::exists(OwnLevelFile(slot), ec);
}

bool LoadOwnLevelDef(int slot, LevelDef& def) {
    return OwnSlotHasFile(slot) && LoadLevelDef(OwnLevelFile(slot), def);
}

bool DeleteLevelFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}


// ---------------------------------------------------------------------------
// Batalla vs: mapas
// ---------------------------------------------------------------------------

int VsSpawnTeam(const int ec[2], const int er[2], int col, int row) {
    const int d0 = std::abs(col - ec[0]) + std::abs(row - er[0]), d1 = std::abs(col - ec[1]) + std::abs(row - er[1]);
    return d0 < d1 ? 0 : (d1 < d0 ? 1 : -1);
}

void ClearVersusZones(LevelDef& d) {
    auto clear = [&](int r, int c) {
        if (r >= 0 && r < kGridRows && c >= 0 && c < kLevelCols) d.cells[r][c] = kEmpty;
    };
    for (int t = 0; t < 2; ++t) {
        const int ec = d.vsEagleCol[t], er = d.vsEagleRow[t];
        for (int r = er - 1; r <= er + 1; ++r)
            for (int c = ec - 1; c <= ec + 1; ++c) clear(r, c);
        for (int k = 0; k < 4; ++k) {                          // donde empiezan los miembros del equipo
            int c, r;
            nes::Battle::VsSideBlock(ec, er, k, c, r);
            clear(r, c);
        }
    }
    for (int i = 0; i < d.nSpawns; ++i) clear(d.spawnRow[i], d.spawnCol[i]);
}

LevelDef BlankVersusDef() {
    LevelDef d;
    d.versus = true;
    d.tier = 2;
    d.rank = 3;
    SetDefaultSpawns(d);
    ClearVersusZones(d);
    return d;
}

LevelDef GenerateVersusLevel(uint32_t seed) {
    LevelDef d = BlankVersusDef();
    Rng rng(seed * 2246822519u + 3266489917u);
    // 1) una construccion original (de las 35) como mitad izquierda; sin la base que traen
    const int stage = 1 + static_cast<int>(rng.Next() % 35u);
    const uint8_t* data = &nes::tbl_F07A_stage_data[(stage - 1) * 91];
    uint8_t orig[13][13];
    for (int row = 0; row < 13; ++row) {
        for (int col = 0; col < 13; ++col) {
            const int nibble = row * 14 + col;
            const uint8_t byte = data[nibble >> 1];
            orig[row][col] = (nibble & 1) ? (byte & 0x0F) : (byte >> 4);
        }
    }
    for (int row = 11; row < 13; ++row)
        for (int col = 5; col <= 7; ++col) orig[row][col] = kEmpty;
    const bool flipV = rng.Chance(50);
    for (int r = 0; r < 13; ++r) {
        for (int c = 0; c <= 13; ++c) {
            const int sr = flipV ? 12 - r : r;
            d.cells[1 + r][c] = orig[sr][c < 13 ? c : 6];
        }
    }
    for (int c = 0; c <= 13; ++c) d.cells[14][c] = rng.Chance(22) ? kBrick : kEmpty;
    // franja de arriba: almenas de ladrillo
    for (int c = 2; c <= 12; ++c) d.cells[0][c] = ((c * 5 + static_cast<int>(seed % 7u)) % 7) < 3 ? kBrick : kEmpty;
    // 2) un rasgo por mapa para que no se parezcan todos
    switch (rng.Next() % 6u) {
        case 1: {                                                       // un rio en el medio con dos puentes
            const int b1 = 2 + static_cast<int>(rng.Next() % 5u), b2 = 8 + static_cast<int>(rng.Next() % 5u);
            for (int r = 1; r <= 14; ++r) {
                if (r == b1 || r == b2) continue;
                d.cells[r][12] = d.cells[r][13] = kWater;
            }
            break;
        }
        case 2: {                                                       // bloques de hierro repartidos
            for (int i = 0; i < 6; ++i) {
                const int r = 2 + static_cast<int>(rng.Next() % 11u), c = 5 + static_cast<int>(rng.Next() % 8u);
                d.cells[r][c] = kSteel;
            }
            break;
        }
        case 3: {                                                       // manchas de arbustos
            for (int i = 0; i < 5; ++i) {
                const int r = 2 + static_cast<int>(rng.Next() % 11u), c = 4 + static_cast<int>(rng.Next() % 9u);
                d.cells[r][c] = d.cells[r][c + 1] = kForest;
                if (r + 1 < kGridRows) d.cells[r + 1][c] = d.cells[r + 1][c + 1] = kForest;
            }
            break;
        }
        case 4: {                                                       // hielo en el centro
            for (int r = 4; r <= 10; ++r)
                for (int c = 10; c <= 13; ++c)
                    if (d.cells[r][c] == kEmpty) d.cells[r][c] = kIce;
            break;
        }
        case 5: {                                                       // un desierto de arena en el centro
            for (int r = 3; r <= 11; ++r)
                for (int c = 8; c <= 13; ++c)
                    if (d.cells[r][c] == kEmpty) d.cells[r][c] = kSand;
            break;
        }
        default: break;
    }
    // 3) un poco de hierro mas segun la dificultad
    for (int r = 1; r < kGridRows; ++r)
        for (int c = 0; c <= 13; ++c)
            if (d.cells[r][c] == kBrick && rng.Chance(d.tier * 2)) d.cells[r][c] = kSteel;
    ClearVersusZones(d);
    // 4) camino: desde el frente de la aguila izquierda hasta el centro y hasta las dos apariciones de ese lado
    {
        int ec, er;
        nes::Battle::VsSideBlock(d.vsEagleCol[0], d.vsEagleRow[0], 0, ec, er);
        const int targets[3][2] = {{7, 13}, {d.spawnRow[0], d.spawnCol[0]}, {d.spawnRow[1], d.spawnCol[1]}};
        for (const auto& tg : targets) {
            for (int guard = 0; guard < 6; ++guard) {
                bool reach[kGridRows][kLevelCols] = {};
                std::queue<std::pair<int, int>> q;
                q.push({er, ec});
                reach[er][ec] = true;
                while (!q.empty()) {
                    const auto [r, c] = q.front();
                    q.pop();
                    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; ++k) {
                        const int nr = r + dr[k], nc = c + dc[k];
                        if (nr < 0 || nr >= kGridRows || nc < 0 || nc > 13 || reach[nr][nc] || !LevelCellPassable(d.cells[nr][nc])) continue;
                        reach[nr][nc] = true;
                        q.push({nr, nc});
                    }
                }
                if (reach[tg[0]][tg[1]]) break;
                // se abre el camino de menor costo desde el objetivo hasta lo ya alcanzado
                struct Node { int cost, r, c; bool operator>(const Node& o) const { return cost > o.cost; } };
                std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
                int dist[kGridRows][kLevelCols], pr[kGridRows][kLevelCols], pc[kGridRows][kLevelCols];
                for (auto& row : dist) for (int& v : row) v = 1 << 20;
                dist[tg[0]][tg[1]] = 0;
                pq.push({0, tg[0], tg[1]});
                int endR = -1, endC = -1;
                while (!pq.empty()) {
                    const Node n = pq.top();
                    pq.pop();
                    if (n.cost > dist[n.r][n.c]) continue;
                    if (reach[n.r][n.c]) { endR = n.r; endC = n.c; break; }
                    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                    for (int k = 0; k < 4; ++k) {
                        const int nr = n.r + dr[k], nc = n.c + dc[k];
                        if (nr < 0 || nr >= kGridRows || nc < 0 || nc > 13) continue;
                        const int nd = n.cost + (LevelCellPassable(d.cells[nr][nc]) ? 0 : 1);
                        if (nd < dist[nr][nc]) { dist[nr][nc] = nd; pr[nr][nc] = n.r; pc[nr][nc] = n.c; pq.push({nd, nr, nc}); }
                    }
                }
                if (endR < 0) break;
                for (int r = endR, c = endC; !(r == tg[0] && c == tg[1]);) {
                    if (!LevelCellPassable(d.cells[r][c])) d.cells[r][c] = kEmpty;
                    const int nr = pr[r][c], nc = pc[r][c];
                    r = nr;
                    c = nc;
                }
                if (!LevelCellPassable(d.cells[tg[0]][tg[1]])) d.cells[tg[0]][tg[1]] = kEmpty;
            }
        }
    }
    // 5) la otra mitad es el mismo mapa girado media vuelta (las aguilas estan en esquinas opuestas): justo para los dos equipos
    for (int r = 0; r < kGridRows; ++r)
        for (int c = 0; c < 13; ++c) d.cells[kGridRows - 1 - r][kLevelCols - 1 - c] = d.cells[r][c];
    for (int r = 0; r < 7; ++r) {                                                    // la columna del centro, con la misma simetria (si uno de los dos es transitable, los dos)
        uint8_t& a = d.cells[r][13];
        uint8_t& b = d.cells[kGridRows - 1 - r][13];
        if (LevelCellPassable(a)) b = a;
        else if (LevelCellPassable(b)) a = b;
        else b = a;
    }
    d.cells[7][13] = kEmpty;
    return d;
}

LevelDef GenerateRoyaleLevel(uint32_t seed) {
    LevelDef d = GenerateVersusLevel(seed * 2654435761u + 12345u);
    d.royale = true;
    int sp[8][2];
    for (int id = 0; id < 8; ++id) nes::Battle::RoyaleSpawnBlock(id, sp[id][0], sp[id][1]);     // (col, fila)
    auto clear = [&](int r, int c) {
        if (r >= 0 && r < kGridRows && c >= 0 && c < kLevelCols) d.cells[r][c] = kEmpty;
    };
    for (int id = 0; id < 8; ++id) {                                      // cada salida y su alrededor, libres
        const int c = sp[id][0], r = sp[id][1];
        clear(r, c); clear(r - 1, c); clear(r + 1, c); clear(r, c - 1); clear(r, c + 1);
    }
    // todos los puntos de salida tienen que poder llegar al centro: si no, se abre un camino en L hacia el centro
    for (int id = 0; id < 8; ++id) {
        bool reach[kGridRows][kLevelCols] = {};
        std::queue<std::pair<int, int>> q;
        q.push({7, 13});
        reach[7][13] = true;
        while (!q.empty()) {
            const auto [r, c] = q.front();
            q.pop();
            const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int k = 0; k < 4; ++k) {
                const int nr = r + dr[k], nc = c + dc[k];
                if (nr < 0 || nr >= kGridRows || nc < 0 || nc >= kLevelCols || reach[nr][nc] || !LevelCellPassable(d.cells[nr][nc])) continue;
                reach[nr][nc] = true;
                q.push({nr, nc});
            }
        }
        int r = sp[id][1], c = sp[id][0];
        if (reach[r][c]) continue;
        while (c != 13) { c += c < 13 ? 1 : -1; if (!LevelCellPassable(d.cells[r][c])) d.cells[r][c] = kEmpty; if (reach[r][c]) break; }
        while (r != 7 && !reach[r][c]) { r += r < 7 ? 1 : -1; if (!LevelCellPassable(d.cells[r][c])) d.cells[r][c] = kEmpty; }
    }
    d.cells[7][13] = kEmpty;
    return d;
}

} // namespace app
