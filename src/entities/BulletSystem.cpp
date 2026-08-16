#include "BulletSystem.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
constexpr float kBulletHitRadius = 0.2f; // celdas, para la anulacion bala-contra-bala (seccion 4.5)
}

bool BulletSystem::TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, float speed, bool canDestroySteel, int maxPerOwner) {
    int aliveCount = 0;
    for (const Bullet& b : bullets_) {
        if (b.alive && b.ownerId == ownerId) {
            ++aliveCount;
        }
    }
    if (aliveCount >= maxPerOwner) {
        return false;
    }

    Bullet bullet;
    bullet.x = muzzleX;
    bullet.y = muzzleY;
    bullet.direction = direction;
    bullet.ownerId = ownerId;
    bullet.alive = true;
    bullet.speed = speed;
    bullet.canDestroySteel = canDestroySteel;
    bullets_.push_back(bullet);
    return true;
}

void BulletSystem::DestroyBrickHalf(TileMap& map, int cellX, int cellY, Direction hitFrom) {
    Cell& cell = map.At(cellX, cellY);

    uint8_t clearMask = 0;
    switch (hitFrom) {
        case Direction::Right: clearMask = kSubCellLeftHalf; break;   // la bala viene desde la izquierda
        case Direction::Left:  clearMask = kSubCellRightHalf; break;  // la bala viene desde la derecha
        case Direction::Down:  clearMask = kSubCellTopHalf; break;    // la bala viene desde arriba
        case Direction::Up:    clearMask = kSubCellBottomHalf; break; // la bala viene desde abajo
    }

    cell.subMask &= static_cast<uint8_t>(~clearMask);
    if (cell.subMask == 0) {
        cell.type = TileType::Empty;
    }
}

void BulletSystem::Update(double dt, TileMap& map) {
    for (Bullet& bullet : bullets_) {
        if (!bullet.alive) {
            continue;
        }

        float dx = 0.0f, dy = 0.0f;
        DirectionVector(bullet.direction, dx, dy);

        const float distance = static_cast<float>(bullet.speed * dt);
        const float newX = bullet.x + dx * distance;
        const float newY = bullet.y + dy * distance;
        const int cellX = static_cast<int>(std::floor(newX));
        const int cellY = static_cast<int>(std::floor(newY));

        if (!map.InBounds(cellX, cellY)) {
            bullet.alive = false;
            continue;
        }

        const TileType hitType = map.At(cellX, cellY).type;
        if (TileBlocksShots(hitType)) {
            if (hitType == TileType::Brick) {
                DestroyBrickHalf(map, cellX, cellY, bullet.direction);
            } else if (hitType == TileType::Steel && bullet.canDestroySteel) {
                map.At(cellX, cellY).type = TileType::Empty;
                map.At(cellX, cellY).subMask = 0;
            }
            bullet.alive = false;
            continue;
        }

        bullet.x = newX;
        bullet.y = newY;
    }

    // Dos balas que chocan de frente se anulan mutuamente (seccion 4.5).
    for (size_t i = 0; i < bullets_.size(); ++i) {
        if (!bullets_[i].alive) {
            continue;
        }
        for (size_t j = i + 1; j < bullets_.size(); ++j) {
            if (!bullets_[j].alive) {
                continue;
            }
            const float dx = bullets_[i].x - bullets_[j].x;
            const float dy = bullets_[i].y - bullets_[j].y;
            if (std::fabs(dx) <= kBulletHitRadius && std::fabs(dy) <= kBulletHitRadius) {
                bullets_[i].alive = false;
                bullets_[j].alive = false;
            }
        }
    }

    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return !b.alive; }), bullets_.end());
}

} // namespace bc
