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
    // Destruye unidades minimas alrededor del punto de impacto (hitX,hitY):
    // si el golpe cae en una fila/columna central de la grilla 4x4, destruye
    // toda esa fila/columna (4 unidades); si cae en el borde ("esquina"),
    // destruye solo la mitad cercana (2 unidades). Devuelve true si frena la
    // bala (habia material ahi), false si ya estaba todo destruido y la bala
    // puede seguir de largo.
    static bool HandleBrickHit(TileMap& map, int cellX, int cellY, float hitX, float hitY, Direction hitFrom);

    std::vector<Bullet> bullets_;
};

} // namespace bc
