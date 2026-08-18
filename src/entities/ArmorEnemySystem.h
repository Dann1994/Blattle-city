#pragma once

#include <random>
#include <vector>

#include "BaseDistanceField.h"
#include "Bullet.h"
#include "Enemy.h"
#include "FastEnemySystem.h" // kFastEnemyOwnerIdBase
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;
class SpecialExplosionSystem;

// Tanque "Blindado" (enemigo 3): misma IA que el "Basico"/"Rapido" (ver
// EnemySystem.cpp — movimiento en linea recta por tramos, direccion
// ponderada segun la posicion respecto del aguila, y reaccion al choque),
// pero mas lento (igual de lento que un jugador con arma nivel 4, ver
// kArmorEnemySpeedMultiplier en ArmorEnemySystem.cpp) y con 2 diferencias
// de combate: puede tener 2 balas propias en vuelo a la vez (en vez de 1,
// ver kMaxAliveBullets) y sus balas llevan un weaponLevel alto (ver
// kArmorWeaponLevel) que les alcanza para ademas ir rompiendo hierro, no
// solo ladrillo (el acero dejo de ser intransitable "para siempre" para
// este tipo: ver WouldBeBlocked, la reaccion al choque lo tirotea igual que
// a un ladrillo).
constexpr float kArmorEnemySpeedMultiplier = 0.5f; // = misma velocidad que un jugador en nivel de arma 4

constexpr int kArmorEnemyOwnerIdBase = kFastEnemyOwnerIdBase + 100000;

class ArmorEnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: posicion del aguila, hacia donde se orienta el
    // movimiento ponderado. otherEnemyTanks: tanques de otros tipos de
    // enemigo, solo para colision, nunca como blanco.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kArmorEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;

    // Semilla base para el RNG propio de cada enemigo (ver Enemy::rng): cada
    // uno se siembra con globalSeed_ + su ownerId, igual que EnemySystem.
    unsigned int globalSeed_ = std::random_device{}();

    // Solo se usa/recalcula a partir de nivel de agresividad 4 (ver
    // kFieldRecomputeInterval en ArmorEnemySystem.cpp): campo de distancias
    // hacia el aguila.
    BaseDistanceField baseField_;
    double fieldRecomputeTimer_ = 0.0;
};

} // namespace bc
