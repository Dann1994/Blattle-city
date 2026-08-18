#include "BaseDistanceField.h"

#include <algorithm>
#include <limits>
#include <queue>
#include <utility>

namespace bc {

namespace {
constexpr float kOpenEntryCost = 1.0f;  // Empty, Ice, Trees: cuesta lo mismo que cualquier celda normal
constexpr float kBrickEntryCost = 6.0f; // Se puede atravesar a tiros, pero sale caro: solo conviene si ahorra mas de eso rodeando
constexpr float kInfinity = std::numeric_limits<float>::infinity();

// Cuanto "cuesta" entrar a una celda de este tipo viniendo de cualquier
// direccion. Acero y agua son intransitables (ni a tiros: el tanque
// "Basico" no le hace mella al acero en tiempo razonable, y nadie cruza el
// agua). Base no deberia consultarse nunca como destino intermedio (es el
// origen del campo, no un nodo mas de la grilla), pero por las dudas se
// trata igual que "abierto" en vez de romper el calculo.
float EntryCost(TileType type) {
    switch (type) {
        case TileType::Steel:
        case TileType::Water:
            return kInfinity;
        case TileType::Brick:
            return kBrickEntryCost;
        default:
            return kOpenEntryCost;
    }
}

constexpr int kDx[4] = {0, 0, -1, 1};
constexpr int kDy[4] = {-1, 1, 0, 0};
constexpr Direction kDirs[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
} // namespace

void BaseDistanceField::Recompute(const TileMap& map, int baseX, int baseY) {
    width_ = map.Width();
    height_ = map.Height();
    dist_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), kInfinity);

    if (!map.InBounds(baseX, baseY)) {
        return;
    }

    // Dijkstra clasico, con el aguila como nodo fuente virtual (no forma
    // parte de la grilla transitable: sus vecinos arrancan directo con su
    // propio costo de entrada, en vez de sumarle un costo al aguila misma).
    using QueueItem = std::pair<float, int>; // (distancia acumulada, indice)
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    for (int i = 0; i < 4; ++i) {
        const int nx = baseX + kDx[i];
        const int ny = baseY + kDy[i];
        if (!map.InBounds(nx, ny)) {
            continue;
        }
        const float cost = EntryCost(map.At(nx, ny).type);
        if (cost >= kInfinity) {
            continue;
        }
        const int idx = Index(nx, ny);
        if (cost < dist_[idx]) {
            dist_[idx] = cost;
            queue.push({cost, idx});
        }
    }

    while (!queue.empty()) {
        const auto [d, idx] = queue.top();
        queue.pop();
        if (d > dist_[idx]) {
            continue; // entrada vieja de la cola, ya mejorada despues
        }
        const int x = idx % width_;
        const int y = idx / width_;
        for (int i = 0; i < 4; ++i) {
            const int nx = x + kDx[i];
            const int ny = y + kDy[i];
            if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                continue;
            }
            const int nidx = Index(nx, ny);
            const float cost = EntryCost(map.At(nx, ny).type);
            if (cost >= kInfinity) {
                continue;
            }
            const float nd = d + cost;
            if (nd < dist_[nidx]) {
                dist_[nidx] = nd;
                queue.push({nd, nidx});
            }
        }
    }
}

float BaseDistanceField::DistanceAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return kInfinity;
    }
    return dist_[Index(x, y)];
}

std::array<Direction, 4> BaseDistanceField::RankedDirections(int x, int y) const {
    struct Candidate {
        Direction dir;
        float dist;
    };
    std::array<Candidate, 4> candidates{{
        {kDirs[0], DistanceAt(x + kDx[0], y + kDy[0])},
        {kDirs[1], DistanceAt(x + kDx[1], y + kDy[1])},
        {kDirs[2], DistanceAt(x + kDx[2], y + kDy[2])},
        {kDirs[3], DistanceAt(x + kDx[3], y + kDy[3])},
    }};
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.dist < b.dist;
    });
    return {candidates[0].dir, candidates[1].dir, candidates[2].dir, candidates[3].dir};
}

} // namespace bc
