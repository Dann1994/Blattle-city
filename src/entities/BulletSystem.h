#pragma once

#include <vector>

#include "Bullet.h"
#include "TileMap.h"

namespace bc {

class BulletSystem {
public:
    // Respeta el limite de balas propias en pantalla (nivel 1 = 1, ver seccion 4.3).
    bool TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, int maxPerOwner = 1);

    void Update(double dt, TileMap& map);

    const std::vector<Bullet>& Bullets() const { return bullets_; }

private:
    static void DestroyBrickHalf(TileMap& map, int cellX, int cellY, Direction hitFrom);

    std::vector<Bullet> bullets_;
    float speed_ = 8.0f; // celdas por segundo
};

} // namespace bc
