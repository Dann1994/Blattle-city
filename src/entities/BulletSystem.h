#pragma once

#include <vector>

#include "Bullet.h"
#include "TileMap.h"

namespace bc {

class BulletSystem {
public:
    // Respeta el limite de balas propias en pantalla (1 salvo doble disparo nivel 3+, ver seccion 4.3).
    bool TryShoot(int ownerId, float muzzleX, float muzzleY, Direction direction, float speed, bool canDestroySteel, int maxPerOwner = 1);

    void Update(double dt, TileMap& map);

    const std::vector<Bullet>& Bullets() const { return bullets_; }

private:
    static void DestroyBrickHalf(TileMap& map, int cellX, int cellY, Direction hitFrom);

    std::vector<Bullet> bullets_;
};

} // namespace bc
