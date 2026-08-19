#pragma once

#include <random>
#include <vector>

#include "ArmorEnemySystem.h" // kArmorEnemyOwnerIdBase
#include "BaseDistanceField.h"
#include "Bullet.h"
#include "Enemy.h"
#include "ScoreEvent.h"
#include "TileMap.h"

namespace bc {

class BulletSystem;
class BulletImpactSystem;
class SpecialExplosionSystem;

// Tanque "Power" (enemigo 4): misma IA que el resto (ver EnemySystem.cpp —
// movimiento en linea recta por tramos, direccion ponderada segun la
// posicion respecto del aguila, y reaccion al choque). Sprite propio (fila
// 8 de Tanques.png, paleta gris). Diferencias de combate/velocidad (ver
// PowerEnemySystem.cpp): tan lento como un jugador con arma nivel 4, puede
// tener 2 balas propias en vuelo a la vez (con la misma cadencia
// escalonada que el "Blindado", ver kSecondShotMinDelay/MaxDelay en
// ArmorEnemySystem.cpp), y sus balas llevan la potencia (velocidad + dano)
// de un arma nivel 4 tambien.
//
// A diferencia del resto (que muere de 1 sola bala de jugador sin importar
// el nivel), este SI tiene resistencia real (ver Enemy::hp,
// kPowerEnemyMaxHp y kPowerEnemyDamageByLevel): cada impacto de bala de
// jugador le resta el dano que corresponda al nivel de arma de quien
// disparo, calibrado para que haga falta exactamente 4 disparos nivel 1, 5
// nivel 2, 2 nivel 3 o 1 nivel 4 para matarlo. Un disparo ESPECIAL siempre
// lo destruye de una, y a diferencia de como el especial atraviesa al
// resto de los enemigos, contra este SI explota al impactar (ver
// Game::Update, que lo agrega a la lista de blancos validos del especial
// con weaponLevel=4 para forzar esa rama).
constexpr float kPowerEnemySpeedMultiplier = 0.5f; // = misma velocidad que un jugador en nivel de arma 4

// HP total y dano por impacto segun el nivel de arma de quien dispara (indice
// 0 = nivel 1 .. indice 3 = nivel 4). Con HP=20: 20/5=4, 20/4=5, 20/10=2,
// 20/20=1 disparos exactos por nivel.
constexpr int kPowerEnemyMaxHp = 20;
constexpr int kPowerEnemyDamageByLevel[4] = {5, 4, 10, 20};

constexpr int kPowerEnemyOwnerIdBase = kArmorEnemyOwnerIdBase + 100000;

class PowerEnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: posicion del aguila, hacia donde se orienta el
    // movimiento ponderado. otherEnemyTanks: tanques de otros tipos de
    // enemigo, solo para colision, nunca como blanco.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY, std::vector<ScoreEvent>& outScoreEvents);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kPowerEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;

    // Semilla base para el RNG propio de cada enemigo (ver Enemy::rng): cada
    // uno se siembra con globalSeed_ + su ownerId, igual que EnemySystem.
    unsigned int globalSeed_ = std::random_device{}();

    // Solo se usa/recalcula a partir de nivel de agresividad 4 (ver
    // kFieldRecomputeInterval en PowerEnemySystem.cpp): campo de distancias
    // hacia el aguila.
    BaseDistanceField baseField_;
    double fieldRecomputeTimer_ = 0.0;
};

} // namespace bc
