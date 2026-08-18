#pragma once

#include <vector>

#include "Bullet.h"
#include "Enemy.h"
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;
class SpecialExplosionSystem;

// Tanque "Rapido": mas veloz moviendose y disparando que el "Basico" (ver
// kFastEnemySpeedMultiplier/kFastBulletSpeedMultiplier en
// FastEnemySystem.cpp), sprite propio (fila 6 de Tanques.png, ver
// EnemySprites). A diferencia del "Basico" (que persigue al aguila
// pathfinding de por medio), este PATRULLA el escenario al azar y no le
// interesa el aguila para nada: si detecta a un jugador dentro de su radio
// (ver kDetectionRadiusByLevel, nivel 1 = 1 sola casilla), entra en "modo
// matar" y lo persigue/dispara hasta perderlo de vista.
constexpr float kFastEnemySpeedMultiplier = 1.0f; // 0.5 (Basico) + 0.5

// Arranca los ownerId bien lejos de los del "Basico" (ver kEnemyOwnerIdBase
// en Bullet.h) para que nunca se pisen: cada tanque enemigo, de cualquier
// tipo, necesita un ownerId unico para que las balas propias/ajenas y el
// "solo 1 bala a la vez" se resuelvan bien.
constexpr int kFastEnemyOwnerIdBase = kEnemyOwnerIdBase + 100000;

class FastEnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY se reciben por consistencia con EnemySystem pero no se
    // usan: este tipo no busca el aguila, solo patrulla/persigue jugadores.
    // otherEnemyTanks: tanques de otros tipos de enemigo (el "Basico", ver
    // EnemySystem), solo para colision, nunca como blanco.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kFastEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;
};

} // namespace bc
