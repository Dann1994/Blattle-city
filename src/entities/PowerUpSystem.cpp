#include "PowerUpSystem.h"

#include <random>
#include <utility>
#include <vector>

namespace bc {

namespace {
constexpr double kSpawnInterval = 12.0; // segundos entre apariciones
constexpr double kLifetime = 10.0;      // segundos que permanece antes de desaparecer sin recoger

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}
} // namespace

void PowerUpSystem::SpawnRandom(const TileMap& map) {
    std::vector<std::pair<int, int>> openCells;
    for (int y = 0; y < map.Height(); ++y) {
        for (int x = 0; x < map.Width(); ++x) {
            const TileType type = map.At(x, y).type;
            if (!TileBlocksMovement(type) && type != TileType::Base) {
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
    powerUp_.alive = true;
    lifeTimer_ = kLifetime;
}

void PowerUpSystem::Update(double dt, const TileMap& map) {
    if (powerUp_.alive) {
        lifeTimer_ -= dt;
        if (lifeTimer_ <= 0.0) {
            powerUp_.alive = false;
            spawnTimer_ = kSpawnInterval;
        }
        return;
    }

    spawnTimer_ -= dt;
    if (spawnTimer_ <= 0.0) {
        SpawnRandom(map);
    }
}

bool PowerUpSystem::TryPickup(float tankX, float tankY) {
    if (!powerUp_.alive) {
        return false;
    }

    const bool overlapX = tankX < powerUp_.x + 1.0f && tankX + 1.0f > powerUp_.x;
    const bool overlapY = tankY < powerUp_.y + 1.0f && tankY + 1.0f > powerUp_.y;
    if (overlapX && overlapY) {
        powerUp_.alive = false;
        spawnTimer_ = kSpawnInterval;
        return true;
    }
    return false;
}

} // namespace bc
