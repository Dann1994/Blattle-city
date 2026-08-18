#pragma once

#include <vector>

#include "BaseDistanceField.h"
#include "Bullet.h"
#include "Enemy.h"
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;
class SpecialExplosionSystem;

// Tanque "Basico": la mitad de veloz que un tanque de jugador nivel 1.
constexpr float kEnemySpeedMultiplier = 0.5f;

// Tanques enemigo "Basico" (seccion 5): en vez de heuristicas de
// probabilidad, siguen el camino real mas corto hacia el aguila (ver
// BaseDistanceField, recalculado cada tanto y compartido por todos). Reusa
// Tank para todo el movimiento/colision/disparo (ver Enemy.h); esta clase
// solo agrega la logica de decision.
class EnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: destino del campo de distancias (ver BaseDistanceField).
    // playerTanks: tanques de jugador activos, solo para colision (son un
    // obstaculo mas a esquivar, igual que un bloque: no hay modo de
    // defensa). otherEnemyTanks: tanques de otros tipos de enemigo (por
    // ejemplo el "Rapido", ver FastEnemySystem), solo para colision, nunca
    // como blanco. specialExplosions recibe la animacion chica de muerte
    // (igual que Game::DestroyTank para los jugadores) cuando un enemigo es
    // destruido.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    // 1 (mas pasiva) a 5 (mas agresiva); 3 es el comportamiento "de base"
    // (valores originales). Escala que tan seguido dispara y que tan
    // directo es el camino hacia el aguila (ver las tablas *ByLevel en
    // EnemySystem.cpp); la velocidad de movimiento no cambia con esto.
    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;

    // Compartido por todos los enemigos (el objetivo es el mismo para
    // todos): se recalcula cada tanto, no cada frame (ver
    // kFieldRecomputeInterval en EnemySystem.cpp).
    BaseDistanceField baseField_;
    double fieldRecomputeTimer_ = 0.0;
};

} // namespace bc
