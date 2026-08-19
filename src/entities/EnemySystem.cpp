#include "EnemySystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

// Tanque enemigo "Basico": IA en reconstruccion paso a paso (a pedido del
// usuario: "vamos por parte, te voy dando configuracion por configuracion").
//
// Paso 1: patron de movimiento en linea recta vertical u horizontal, en
// tramos de 1 a 6 celdas elegidos al azar; al completarse un tramo, elige
// uno nuevo y una direccion nueva, y recien entonces vuelve a decidir.
//
// Paso 2: en niveles de agresividad 1-3, la direccion no es 25%/25%/25%/25%
// parejo. Cada direccion tiene un peso que depende de la posicion (por
// celda) del enemigo respecto del aguila (ver WeightedRandomDirection):
//   Arriba: upBase, o upAlignedPenalty si la columna (X) del enemigo
//     coincide con la del aguila (ver Abajo).
//   Abajo: downFavored, o downAligned si la columna (X) del enemigo
//     coincide con la del aguila.
//   Derecha: rlFavored si el aguila esta a la derecha (X mayor), si no
//     rlBase; pero si la fila (Y) coincide con la del aguila, el lado
//     hacia el que realmente esta (segun esa misma comparacion de X) sube
//     a rlAligned y el lado contrario baja a rlAlignedPenalty.
//   Izquierda: espejo de Derecha (rlFavored si el aguila esta a la
//     izquierda, si no rlBase; con fila alineada, rlAligned/
//     rlAlignedPenalty segun de que lado este el aguila).
// Los pesos NO suman 100: se sortea proporcional al peso total (un peso
// mas alto es mas probable, no un porcentaje independiente). Los numeros
// dependen del nivel de agresividad (ver DirectionWeights/kWeightsByLevel
// mas abajo): niveles 1-3 confirmados, nivel 5 pendiente.
//
// Desde nivel de agresividad 4, se deja de lado la ponderacion: busca el
// camino real mas corto hacia el aguila (BaseDistanceField, igual que la
// version pre-reconstruccion) y prioriza destruirla. El resto (tramos de
// 1-6 celdas, reaccion al choque) sigue igual, solo cambia como se elige
// la direccion.
//
// Paso 3: reaccion al choque. Si la direccion actual esta bloqueada
// (no puede avanzar), cada frame que siga asi hace 2 sorteos independientes
// de 50%: uno para cancelar el movimiento y pasar ya al siguiente (una
// direccion distinta a la bloqueada, elegida igual que el paso 2 segun el
// nivel), y otro para disparar hacia donde esta mirando. Evita que se
// quede trabada insistiendo en avanzar contra algo.

namespace bc {

namespace {
constexpr int kMinRunCells = 1;
constexpr int kMaxRunCells = 6;

// Red de seguridad minima (NO es esquive "inteligente", eso es un paso
// futuro): si el tramo elegido choca contra algo y nunca se completa
// durante ~15s seguidos, se repone a la fuerza en una direccion libre (si
// encuentra alguna) para que nunca quede trabada para siempre mientras se
// prueba este paso.
constexpr int kHardUnstuckFrames = 900; // ~15s a 60fps

constexpr float kLookaheadStep = 0.2f;

// Disparo: intervalo fijo, sin puntoria ni prioridades — placeholder
// temporal hasta que el disparo sea otro de los pasos de la reconstruccion.
// Reducido a la mitad (era 3.0) para que disparen mas seguido.
constexpr double kPlaceholderShootInterval = 1.5;

// A partir de este nivel de agresividad, la direccion se elige con
// pathfinding real (BaseDistanceField) en vez de la tabla de pesos.
constexpr int kMinPathfindingLevel = 4;

// Cada cuanto se recalcula el campo de distancias hacia el aguila (solo
// nivel >= kMinPathfindingLevel): la grilla es chica y el terreno solo
// cambia cuando algo rompe un ladrillo/hierro, asi que no hace falta cada
// frame.
constexpr double kFieldRecomputeInterval = 0.5;

// Nivel de arma que llevan sus balas: mismo dano contra hierro que un
// jugador con arma nivel 1 (ver kSteelDamageByLevel en BulletSystem.cpp).
// Ya alcanza para ir rompiendolo de a poco cuando la reaccion al choque le
// dispara (WouldBeBlocked no distingue hierro de cualquier otro bloqueo).
constexpr int kWeaponLevel = 1;

Direction RandomDirection(std::mt19937& rng) {
    constexpr Direction kAllDirs[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
    std::uniform_int_distribution<int> dist(0, 3); // 25% cada una
    return kAllDirs[dist(rng)];
}

// Los numeros que arman la tabla de pesos de WeightedRandomDirection
// (seccion "Niveles de agresividad" mas abajo). Arriba y Abajo/Derecha/
// Izquierda tienen cada uno su propio numero "base" (a diferencia de una
// version anterior, no se asume que sean iguales entre si: por ejemplo
// nivel 1 usa 30 para Arriba pero 25 para el lado no favorable de
// Derecha/Izquierda).
//   upBase / upAlignedPenalty: Arriba sin/con columna alineada con el aguila.
//   downFavored / downAligned: Abajo sin/con columna alineada.
//   rlBase: el lado "no favorable" de Derecha/Izquierda (sin alinear en fila).
//   rlFavored: el lado favorable de Derecha/Izquierda (sin alinear en fila).
//   rlAligned / rlAlignedPenalty: el lado favorable/contrario de Derecha o
//     Izquierda cuando la fila SI esta alineada con el aguila.
struct DirectionWeights {
    float upBase;
    float upAlignedPenalty;
    float downFavored;
    float downAligned;
    float rlBase;
    float rlFavored;
    float rlAligned;
    float rlAlignedPenalty;
};

// Nivel de agresividad 1-5 (ver EnemySystem::SetAggressivenessLevel):
// niveles 1-3 confirmados por el usuario; nivel 4 no usa esta tabla (ver
// kMinPathfindingLevel, usa pathfinding real en su lugar); nivel 5 es
// placeholder (copia de nivel 3) hasta que se defina.
constexpr DirectionWeights kWeightsByLevel[5] = {
    {30.0f, 20.0f, 50.0f, 75.0f, 25.0f, 50.0f, 75.0f, 15.0f}, // nivel 1
    {25.0f, 10.0f, 75.0f, 98.0f, 25.0f, 75.0f, 98.0f, 10.0f}, // nivel 2
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 3
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 4 (no se usa: pathfinding, ver kMinPathfindingLevel)
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 5 (placeholder, pendiente)
};

// Direccion al azar, ponderada segun la posicion (celda) del enemigo
// respecto del aguila y el nivel de agresividad actual (ver
// DirectionWeights/kWeightsByLevel arriba). Sortea un numero entre 0 y la
// suma total de los 4 pesos, y devuelve la direccion en cuyo "tramo" cayo.
Direction WeightedRandomDirection(std::mt19937& rng, int enemyCellX, int enemyCellY, int baseCellX, int baseCellY, const DirectionWeights& w) {
    const bool sameColumn = (enemyCellX == baseCellX);
    const bool sameRow = (enemyCellY == baseCellY);

    const float wUp = sameColumn ? w.upAlignedPenalty : w.upBase;
    const float wDown = sameColumn ? w.downAligned : w.downFavored;

    // Derecha/Izquierda: si esta alineada en la misma fila que el aguila,
    // el lado hacia el que realmente esta el aguila (segun la comparacion
    // de X) sube a w.rlAligned y el lado contrario baja a
    // w.rlAlignedPenalty (en vez de que ambas reglas se pisen y las 2
    // terminen altas). Sin alineacion de fila, cada lado usa
    // w.rlFavored/w.rlBase segun de que lado esta el aguila.
    float wRight = (baseCellX > enemyCellX) ? w.rlFavored : w.rlBase;
    float wLeft = (baseCellX < enemyCellX) ? w.rlFavored : w.rlBase;
    if (sameRow) {
        if (baseCellX > enemyCellX) {
            wRight = w.rlAligned;
            wLeft = w.rlAlignedPenalty;
        } else if (baseCellX < enemyCellX) {
            wLeft = w.rlAligned;
            wRight = w.rlAlignedPenalty;
        }
    }

    const float total = wUp + wDown + wRight + wLeft;
    std::uniform_real_distribution<float> dist(0.0f, total);
    float roll = dist(rng);

    if (roll < wUp) {
        return Direction::Up;
    }
    roll -= wUp;
    if (roll < wDown) {
        return Direction::Down;
    }
    roll -= wDown;
    if (roll < wRight) {
        return Direction::Right;
    }
    return Direction::Left;
}

int RandomRunLength(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(kMinRunCells, kMaxRunCells);
    return dist(rng);
}

bool RollChance(std::mt19937& rng, double probability) {
    std::bernoulli_distribution dist(probability);
    return dist(rng);
}

bool IsTankAhead(const Tank& tank, Direction dir, const std::vector<Tank*>& others) {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    float dx = 0.0f, dy = 0.0f;
    DirectionVector(dir, dx, dy);
    const float newLeft = left + dx * kLookaheadStep;
    const float newRight = right + dx * kLookaheadStep;
    const float newTop = top + dy * kLookaheadStep;
    const float newBottom = bottom + dy * kLookaheadStep;
    for (const Tank* other : others) {
        if (other == nullptr) {
            continue;
        }
        float oLeft = 0.0f, oRight = 0.0f, oTop = 0.0f, oBottom = 0.0f;
        other->GetBounds(oLeft, oRight, oTop, oBottom);
        if (newLeft < oRight && newRight > oLeft && newTop < oBottom && newBottom > oTop) {
            return true;
        }
    }
    return false;
}

// Usado solo por la red de seguridad de arriba, para elegir una direccion
// que al menos ahora mismo este libre.
bool WouldBeBlocked(const Tank& tank, Direction dir, const TileMap& map, const std::vector<Tank*>& others) {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    float dx = 0.0f, dy = 0.0f;
    DirectionVector(dir, dx, dy);
    const float newLeft = left + dx * kLookaheadStep;
    const float newRight = right + dx * kLookaheadStep;
    const float newTop = top + dy * kLookaheadStep;
    const float newBottom = bottom + dy * kLookaheadStep;
    if (newLeft < 0.0f || newTop < 0.0f || newRight > static_cast<float>(map.Width()) || newBottom > static_cast<float>(map.Height())) {
        return true;
    }
    if (map.IsBoxBlocked(newLeft, newRight, newTop, newBottom)) {
        return true;
    }
    return IsTankAhead(tank, dir, others);
}

// Nivel de agresividad >= kMinPathfindingLevel: en vez de ponderar, elige
// la mejor direccion segun el campo de distancias real (BaseDistanceField,
// ya tiene en cuenta el terreno — ladrillo transitable pero caro, acero/
// agua intransitable). Solo descarta una candidata si un tanque la tapa
// ahora mismo (eso el campo no lo sabe); si las 4 estan tapadas por
// tanques, devuelve igual la mejor (la reaccion al choque se encarga).
Direction FieldBestDirection(const BaseDistanceField& field, int cellX, int cellY, const Tank& tank, const std::vector<Tank*>& others) {
    const std::array<Direction, 4> ranked = field.RankedDirections(cellX, cellY);
    for (Direction d : ranked) {
        if (!IsTankAhead(tank, d, others)) {
            return d;
        }
    }
    return ranked[0];
}
} // namespace

void EnemySystem::SetAggressivenessLevel(int level) {
    constexpr int kMinAggressivenessLevel = 1;
    constexpr int kMaxAggressivenessLevel = 5;
    if (level < kMinAggressivenessLevel) {
        level = kMinAggressivenessLevel;
    } else if (level > kMaxAggressivenessLevel) {
        level = kMaxAggressivenessLevel;
    }
    aggressivenessLevel_ = level;
    // Cambia la tabla de pesos de direccion (ver kWeightsByLevel): mas
    // agresivo, mas directo hacia el aguila. Se lee de nuevo cada Update(),
    // asi que afecta de inmediato a todos los enemigos de este tipo ya en
    // pantalla, no solo a los que aparezcan despues.
}

void EnemySystem::SpawnAt(float x, float y) {
    Enemy enemy;
    enemy.tank.SetPosition(x, y);
    enemy.tank.SetFacing(Direction::Down); // entra mirando hacia adentro del mapa
    enemy.tank.SetSpeedMultiplier(kEnemySpeedMultiplier);
    enemy.ownerId = nextOwnerId_++;
    enemy.alive = true;

    enemy.rng.seed(globalSeed_ + static_cast<unsigned int>(enemy.ownerId));
    // deviationCellsRemaining arranca en 0 (default de Enemy): el primer
    // Update() la trata como "tramo recien terminado" y elige la primera
    // direccion ya ponderada segun la posicion real del aguila.
    enemy.shootTimer = kPlaceholderShootInterval;
    enemy.spawn.Start(x, y); // destello de aparicion, igual que los jugadores
    enemies_.push_back(enemy);
}

void EnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY, std::vector<ScoreEvent>& outScoreEvents) {
    outScoreEvents.clear();
    const int baseCellX = static_cast<int>(std::round(baseX));
    const int baseCellY = static_cast<int>(std::round(baseY));
    const DirectionWeights& weights = kWeightsByLevel[aggressivenessLevel_ - 1];
    const bool usePathfinding = aggressivenessLevel_ >= kMinPathfindingLevel;
    if (usePathfinding) {
        fieldRecomputeTimer_ -= dt;
        if (fieldRecomputeTimer_ <= 0.0) {
            baseField_.Recompute(map, baseCellX, baseCellY);
            fieldRecomputeTimer_ = kFieldRecomputeInterval;
        }
    }

    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) {
            continue;
        }

        enemy.tank.TickShootCooldown(dt);
        enemy.tank.TickFreeze(dt);
        enemy.tank.TickShield(dt);

        // Muere de un solo impacto de bala de JUGADOR, sin importar el
        // nivel (misma animacion chica de explosion que Game::DestroyTank
        // usa para los jugadores). Las balas de otros enemigos (de
        // cualquier tipo) la atraviesan sin hacerle nada: no se matan
        // entre ellos.
        float eLeft = 0.0f, eRight = 0.0f, eTop = 0.0f, eBottom = 0.0f;
        enemy.tank.GetBounds(eLeft, eRight, eTop, eBottom);
        std::vector<int> hitLevels;
        std::vector<int> hitOwnerIds;
        if (bullets.KillPlayerBulletsHittingBox(eLeft, eRight, eTop, eBottom, impacts, hitLevels, hitOwnerIds)) {
            specialExplosions.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
            enemy.alive = false;
            outScoreEvents.push_back(ScoreEvent{hitOwnerIds[0], enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, kScoreBasicKill});
            continue;
        }

        if (enemy.spawn.IsActive()) {
            enemy.spawn.Update(dt);
            continue;
        }

        if (enemy.tank.IsFrozen()) {
            continue;
        }

        std::vector<Tank*> others = playerTanks;
        others.insert(others.end(), otherEnemyTanks.begin(), otherEnemyTanks.end());
        for (size_t j = 0; j < enemies_.size(); ++j) {
            if (j == i || !enemies_[j].alive) {
                continue;
            }
            others.push_back(&enemies_[j].tank);
        }

        // Recien aparecido y el destello termino justo encima de otro
        // tanque (ver Enemy::ignoreSpawnOverlap): lo atraviesa esta vez
        // (se saca de 'others' solo mientras dure el solape), para no
        // quedar trabado contra el para siempre.
        if (enemy.ignoreSpawnOverlap) {
            float sLeft = 0.0f, sRight = 0.0f, sTop = 0.0f, sBottom = 0.0f;
            enemy.tank.GetBounds(sLeft, sRight, sTop, sBottom);
            bool stillOverlapping = false;
            others.erase(std::remove_if(others.begin(), others.end(), [&](Tank* other) {
                if (other == nullptr) {
                    return false;
                }
                float oLeft = 0.0f, oRight = 0.0f, oTop = 0.0f, oBottom = 0.0f;
                other->GetBounds(oLeft, oRight, oTop, oBottom);
                const bool overlaps = sLeft < oRight && sRight > oLeft && sTop < oBottom && sBottom > oTop;
                stillOverlapping = stillOverlapping || overlaps;
                return overlaps;
            }), others.end());
            if (!stillOverlapping) {
                enemy.ignoreSpawnOverlap = false;
            }
        }

        // --- Movimiento: linea recta, tramos de 1 a 6 celdas al azar ---
        // Cada vez que entra a una celda nueva, descuenta el tramo actual;
        // cuando se completa, elige una direccion nueva (25% cada una) y un
        // tramo nuevo, y recien ahi vuelve a decidir.
        const int cellX = static_cast<int>(std::floor(enemy.tank.X() + 0.5f));
        const int cellY = static_cast<int>(std::floor(enemy.tank.Y() + 0.5f));
        if (cellX != enemy.decisionCellX || cellY != enemy.decisionCellY) {
            enemy.decisionCellX = cellX;
            enemy.decisionCellY = cellY;
            if (enemy.deviationCellsRemaining > 0) {
                --enemy.deviationCellsRemaining;
            }
        }
        if (enemy.deviationCellsRemaining <= 0) {
            enemy.moveDir = usePathfinding
                ? FieldBestDirection(baseField_, cellX, cellY, enemy.tank, others)
                : WeightedRandomDirection(enemy.rng, cellX, cellY, baseCellX, baseCellY, weights);
            enemy.deviationCellsRemaining = RandomRunLength(enemy.rng);
        }
        enemy.debugMode = 'M';

        // Red de seguridad (ver comentario arriba): nunca trabada para
        // siempre mientras se prueba este paso.
        bool hardUnstuckTriggered = false;
        if (enemy.stuckFrames > kHardUnstuckFrames) {
            Direction bestDir = enemy.moveDir;
            bool foundFreeDirection = false;
            for (int tries = 0; tries < 4; ++tries) {
                const Direction candidate = RandomDirection(enemy.rng);
                if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                    bestDir = candidate;
                    foundFreeDirection = true;
                    break;
                }
            }
            // Si las 4 direcciones probadas seguian bloqueadas, NO
            // teletransporta (antes lo hacia igual, con bestDir==moveDir,
            // la MISMA direccion bloqueada que lo tenia trabado: eso podia
            // meterlo adentro de una pared sin volver a chequear). Deja
            // stuckFrames como esta para reintentar el proximo frame.
            if (foundFreeDirection) {
                float ddx = 0.0f, ddy = 0.0f;
                DirectionVector(bestDir, ddx, ddy);
                enemy.tank.SetPosition(std::round(enemy.tank.X()) + ddx, std::round(enemy.tank.Y()) + ddy);
                enemy.tank.SetFacing(bestDir);
                enemy.moveDir = bestDir;
                enemy.decisionCellX = static_cast<int>(std::round(enemy.tank.X()));
                enemy.decisionCellY = static_cast<int>(std::round(enemy.tank.Y()));
                enemy.deviationCellsRemaining = RandomRunLength(enemy.rng);
                enemy.stuckFrames = 0;
                enemy.debugMode = 'U';
                hardUnstuckTriggered = true;
            }
        }

        // --- Paso 3: reaccion al choque. Si la direccion actual esta
        // bloqueada ahora mismo (no puede avanzar), 2 sorteos
        // independientes, cada uno 50%: uno para cancelar el movimiento
        // actual y pasar ya al siguiente (una direccion distinta a la que
        // estaba bloqueada), y otro para disparar hacia donde esta mirando
        // (por ejemplo, para intentar volar el ladrillo que la frena). Esto
        // evita que se quede trabada insistiendo en avanzar contra algo. Se
        // evalua cada frame que siga bloqueada, no solo la primera vez.
        if (!hardUnstuckTriggered && WouldBeBlocked(enemy.tank, enemy.moveDir, map, others)) {
            if (RollChance(enemy.rng, 0.5)) {
                // Prefiere una direccion que ya ahora mismo este libre (si
                // hay alguna disponible entre varios intentos ponderados):
                // si no, redirige igual a la mejor candidata ponderada
                // (puede seguir bloqueada, pero el proximo frame se vuelve
                // a intentar). Antes elegia a ciegas sin mirar si la nueva
                // tambien chocaba, lo que la podia dejar rebotando entre 2
                // direcciones igual de tapadas.
                Direction newDir = enemy.moveDir;
                if (usePathfinding) {
                    // Igual que en la decision normal, pero descartando
                    // ademas la direccion bloqueada actual.
                    const std::array<Direction, 4> ranked = baseField_.RankedDirections(cellX, cellY);
                    bool found = false;
                    for (Direction candidate : ranked) {
                        if (candidate == enemy.moveDir) {
                            continue;
                        }
                        if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                            newDir = candidate;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        for (Direction candidate : ranked) {
                            if (candidate != enemy.moveDir) {
                                newDir = candidate;
                                break;
                            }
                        }
                    }
                } else {
                    Direction firstCandidate = enemy.moveDir;
                    while (firstCandidate == enemy.moveDir) {
                        firstCandidate = WeightedRandomDirection(enemy.rng, cellX, cellY, baseCellX, baseCellY, weights);
                    }
                    newDir = firstCandidate; // si ninguna sale libre, se usa esta igual (se reintenta el proximo frame)
                    for (int tries = 0; tries < 6; ++tries) {
                        Direction candidate = enemy.moveDir;
                        while (candidate == enemy.moveDir) {
                            candidate = WeightedRandomDirection(enemy.rng, cellX, cellY, baseCellX, baseCellY, weights);
                        }
                        if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                            newDir = candidate;
                            break;
                        }
                    }
                }
                enemy.moveDir = newDir;
                // Encara la nueva direccion YA, sin esperar a tank.Update()
                // mas abajo en este mismo frame (que tambien lo haria, pero
                // solo si el input de movimiento efectivamente se procesa):
                // asi el giro es inmediato y visible pase lo que pase con
                // el intento de movimiento en si.
                enemy.tank.SetFacing(newDir);
                enemy.decisionCellX = cellX;
                enemy.decisionCellY = cellY;
                enemy.deviationCellsRemaining = RandomRunLength(enemy.rng);
            }
            if (RollChance(enemy.rng, 0.5)) {
                const bool bulletAlreadyAlive = bullets.HasAliveBullet(enemy.ownerId);
                if (!bulletAlreadyAlive && enemy.tank.CanShoot()) {
                    // Importante: encara YA la direccion bloqueada antes de
                    // disparar. tank.Facing() todavia refleja el movimiento
                    // del frame anterior (recien se actualiza mas abajo, en
                    // tank.Update()), asi que sin esto el tiro podia salir
                    // apuntando para otro lado en vez de al ladrillo que en
                    // realidad la esta frenando.
                    enemy.tank.SetFacing(enemy.moveDir);
                    float muzzleX = 0.0f, muzzleY = 0.0f;
                    enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                    bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.moveDir, enemy.tank.BulletSpeed(), kWeaponLevel, 1);
                }
            }
        }

        PlayerInput input;
        switch (enemy.moveDir) {
            case Direction::Up:    input.moveUp = true;    break;
            case Direction::Down:  input.moveDown = true;  break;
            case Direction::Left:  input.moveLeft = true;  break;
            case Direction::Right: input.moveRight = true; break;
        }

        const float prevX = enemy.tank.X();
        const float prevY = enemy.tank.Y();
        enemy.tank.Update(dt, input, map, others);

        // Fiel a la fila/columna: si avanza en horizontal, se mantiene
        // exactamente en su fila (Y entero); si avanza en vertical, se
        // mantiene exactamente en su columna (X entero).
        if (enemy.moveDir == Direction::Left || enemy.moveDir == Direction::Right) {
            enemy.tank.SetPosition(enemy.tank.X(), std::round(enemy.tank.Y()));
        } else {
            enemy.tank.SetPosition(std::round(enemy.tank.X()), enemy.tank.Y());
        }

        const float movedDist = std::fabs(enemy.tank.X() - prevX) + std::fabs(enemy.tank.Y() - prevY);
        const bool moved = movedDist > 0.0005f;
        enemy.stuckFrames = moved ? 0 : (enemy.stuckFrames + 1);

        // --- Disparo: placeholder fijo, sin puntoria (paso pendiente) ---
        const bool bulletAliveNow = bullets.HasAliveBullet(enemy.ownerId);
        if (enemy.bulletWasAlive && !bulletAliveNow) {
            enemy.shootTimer = kPlaceholderShootInterval;
        }
        enemy.bulletWasAlive = bulletAliveNow;

        if (!bulletAliveNow && enemy.tank.CanShoot()) {
            enemy.shootTimer -= dt;
            if (enemy.shootTimer <= 0.0) {
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed(), kWeaponLevel, 1);
            }
        }
    }
}

} // namespace bc
