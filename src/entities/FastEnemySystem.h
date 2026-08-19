#pragma once

#include <random>
#include <vector>

#include "BaseDistanceField.h"
#include "Bullet.h"
#include "Enemy.h"
#include "ScoreEvent.h"
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;
class SpecialExplosionSystem;

// Tanque "Rapido": mas veloz moviendose y disparando que el "Basico" (ver
// kFastEnemySpeedMultiplier/kFastBulletSpeedMultiplier en
// FastEnemySystem.cpp), sprite propio (fila 6 de Tanques.png, ver
// EnemySprites). Misma IA que el "Basico" (ver EnemySystem.cpp: movimiento
// en linea recta por tramos, direccion ponderada segun la posicion respecto
// del aguila, y reaccion al choque), solo que mas veloz.
constexpr float kFastEnemySpeedMultiplier = 1.0f; // 0.5 (Basico) + 0.5

// Arranca los ownerId bien lejos de los del "Basico" (ver kEnemyOwnerIdBase
// en Bullet.h) para que nunca se pisen: cada tanque enemigo, de cualquier
// tipo, necesita un ownerId unico para que las balas propias/ajenas y el
// "solo 1 bala a la vez" se resuelvan bien.
constexpr int kFastEnemyOwnerIdBase = kEnemyOwnerIdBase + 100000;

class FastEnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: posicion del aguila, hacia donde se orienta el
    // movimiento ponderado (ver WeightedRandomDirection en
    // FastEnemySystem.cpp, igual que EnemySystem). otherEnemyTanks: tanques
    // de otros tipos de enemigo (el "Basico", ver EnemySystem), solo para
    // colision, nunca como blanco.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY, std::vector<ScoreEvent>& outScoreEvents);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kFastEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;

    // Semilla base para el RNG propio de cada enemigo (ver Enemy::rng): cada
    // uno se siembra con globalSeed_ + su ownerId, igual que EnemySystem.
    unsigned int globalSeed_ = std::random_device{}();

    // Solo se usa/recalcula a partir de nivel de agresividad 4 (ver
    // kFieldRecomputeInterval en FastEnemySystem.cpp): campo de distancias
    // hacia el aguila.
    BaseDistanceField baseField_;
    double fieldRecomputeTimer_ = 0.0;
};

} // namespace bc
