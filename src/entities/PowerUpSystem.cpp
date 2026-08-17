#include "PowerUpSystem.h"

#include <random>
#include <utility>
#include <vector>

namespace bc {

namespace {
constexpr double kSpawnInterval = 12.0; // segundos entre apariciones
constexpr double kLifetime = 10.0;      // segundos que permanece antes de desaparecer sin recoger
constexpr double kBlinkInterval = 0.15; // segundos entre cada toggle de visibilidad

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}
} // namespace

namespace {
bool CellOccupiedByTank(int x, int y, const std::vector<TankOccupiedBounds>& occupiedByTanks) {
    const float cellLeft = static_cast<float>(x);
    const float cellTop = static_cast<float>(y);
    for (const TankOccupiedBounds& t : occupiedByTanks) {
        if (t.left < cellLeft + 1.0f && t.right > cellLeft && t.top < cellTop + 1.0f && t.bottom > cellTop) {
            return true;
        }
    }
    return false;
}
} // namespace

void PowerUpSystem::SpawnAt(PowerUpType type, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks) {
    std::vector<std::pair<int, int>> openCells;
    for (int y = 0; y < map.Height(); ++y) {
        for (int x = 0; x < map.Width(); ++x) {
            const TileType cellType = map.At(x, y).type;
            if (!TileBlocksMovement(cellType) && cellType != TileType::Base && !CellOccupiedByTank(x, y, occupiedByTanks)) {
                openCells.emplace_back(x, y);
            }
        }
    }
    if (openCells.empty()) {
        return;
    }

    std::uniform_int_distribution<size_t> dist(0, openCells.size() - 1);
    const auto [cx, cy] = openCells[dist(Rng())];
    powerUp_.x = static_cast<float>(cx);
    powerUp_.y = static_cast<float>(cy);
    powerUp_.type = type;

    powerUp_.alive = true;
    lifeTimer_ = kLifetime;
    blinkTimer_ = 0.0;
    blinkVisible_ = true;
}

void PowerUpSystem::ForceSpawn(PowerUpType type, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks) {
    SpawnAt(type, map, occupiedByTanks);
    spawnTimer_ = kSpawnInterval;
}

void PowerUpSystem::Update(double dt, const TileMap& map, const std::vector<TankOccupiedBounds>& occupiedByTanks) {
    if (powerUp_.alive) {
        lifeTimer_ -= dt;

        blinkTimer_ += dt;
        if (blinkTimer_ >= kBlinkInterval) {
            blinkTimer_ -= kBlinkInterval;
            blinkVisible_ = !blinkVisible_;
        }

        if (lifeTimer_ <= 0.0) {
            powerUp_.alive = false;
            spawnTimer_ = kSpawnInterval;
        }
        return;
    }

    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0.0) {
        // Probabilidades pedidas (suman 100%): Vida 3%, Pistola 8%, Granada
        // 10%, Pala 12%, Estrella 17%, Reloj 20%, Escudo 30%.
        constexpr PowerUpType kTypes[7] = {
            PowerUpType::Life, PowerUpType::Gun, PowerUpType::Grenade, PowerUpType::Shovel,
            PowerUpType::Star, PowerUpType::Clock, PowerUpType::Helmet,
        };
        std::discrete_distribution<int> typeDist({3.0, 8.0, 10.0, 12.0, 17.0, 20.0, 30.0});
        SpawnAt(kTypes[typeDist(Rng())], map, occupiedByTanks);
    }
}

bool PowerUpSystem::TryPickup(float tankX, float tankY, PowerUpType& outType) {
    if (!powerUp_.alive) {
        return false;
    }

    const bool overlapX = tankX < powerUp_.x + 1.0f && tankX + 1.0f > powerUp_.x;
    const bool overlapY = tankY < powerUp_.y + 1.0f && tankY + 1.0f > powerUp_.y;
    if (overlapX && overlapY) {
        outType = powerUp_.type;
        powerUp_.alive = false;
        spawnTimer_ = kSpawnInterval;
        return true;
    }
    return false;
}

} // namespace bc
