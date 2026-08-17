#pragma once

#include <vector>

#include "Bullet.h"
#include "Enemy.h"
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;

// Radio de deteccion de jugadores (celdas, centro a centro): dentro de esto
// la prioridad pasa a AttackPlayer; fuera, vuelve a SeekBase.
constexpr float kEnemyDetectionRadius = 2.0f;

// Tanque "Basico": la mitad de veloz que un tanque de jugador nivel 1.
constexpr float kEnemySpeedMultiplier = 0.5f;

// Tanques enemigo "Basico" (seccion 5): maquina de estados simple por
// tanque, sin pathfinding. Reusa Tank para todo el movimiento/colision/
// disparo (ver Enemy.h); esta clase solo agrega la logica de decision.
class EnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: objetivo por defecto (SeekBase). playerTanks: tanques de
    // jugador activos, para colision, deteccion y como blanco (AttackPlayer).
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, const std::vector<Tank*>& playerTanks, float baseX, float baseY);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kEnemyOwnerIdBase;
};

} // namespace bc
