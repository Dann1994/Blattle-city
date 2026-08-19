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

// Tanque "Basico": la mitad de veloz que un tanque de jugador nivel 1.
constexpr float kEnemySpeedMultiplier = 0.5f;

// Tanque enemigo "Basico": IA reconstruida paso a paso (ver el comentario
// de cabecera de EnemySystem.cpp) — movimiento en linea recta por tramos,
// direccion ponderada segun la posicion respecto del aguila (peso por
// nivel de agresividad, ver kWeightsByLevel), reaccion al choque, y a
// partir de nivel 4 pathfinding real (BaseDistanceField) en vez de la
// ponderacion. Reusa Tank para todo el movimiento/colision/disparo (ver
// Enemy.h); esta clase solo agrega la logica de decision.
class EnemySystem {
public:
    void SpawnAt(float x, float y);

    // baseX/baseY: posicion de la base (objetivo posible, ver
    // EnemyTargetType::Base). playerTanks: tanques de jugador activos,
    // objetivo posible (EnemyTargetType::Player) o simple obstaculo a
    // esquivar segun a quien apunte cada enemigo este frame (ver
    // SelectTarget en EnemySystem.cpp). otherEnemyTanks: tanques de otros
    // tipos de enemigo (por ejemplo el "Rapido", ver FastEnemySystem), solo
    // para colision, nunca como blanco. specialExplosions recibe la
    // animacion chica de muerte (igual que Game::DestroyTank para los
    // jugadores) cuando un enemigo es destruido. outScoreEvents se limpia y
    // se llena aca: un ScoreEvent por cada enemigo que una bala de jugador
    // mato este frame, para que Game le acredite el puntaje al que disparo.
    void Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY, std::vector<ScoreEvent>& outScoreEvents);

    std::vector<Enemy>& Enemies() { return enemies_; }
    const std::vector<Enemy>& Enemies() const { return enemies_; }

    // 1 (mas pasiva) a 5 (mas agresiva). Cambia la tabla de pesos de
    // direccion (kWeightsByLevel en EnemySystem.cpp) y, desde nivel 4,
    // activa el pathfinding real hacia el aguila en vez de la ponderacion.
    // Se lee de nuevo cada Update(), asi que afecta de inmediato a los
    // enemigos ya en pantalla, no solo a los que aparezcan despues.
    void SetAggressivenessLevel(int level);
    int AggressivenessLevel() const { return aggressivenessLevel_; }

private:
    std::vector<Enemy> enemies_;
    int nextOwnerId_ = kEnemyOwnerIdBase;
    int aggressivenessLevel_ = 1;

    // Semilla base para el RNG propio de cada enemigo (ver Enemy::rng):
    // cada uno se siembra con globalSeed_ + su ownerId. Fija por instancia
    // de EnemySystem para que, dada la misma secuencia de spawns, el
    // comportamiento sea reproducible.
    unsigned int globalSeed_ = std::random_device{}();

    // Solo se usa/recalcula a partir de nivel de agresividad 4 (ver
    // kFieldRecomputeInterval en EnemySystem.cpp): campo de distancias
    // compartido por todos los enemigos de este tipo hacia el aguila.
    BaseDistanceField baseField_;
    double fieldRecomputeTimer_ = 0.0;
};

} // namespace bc
