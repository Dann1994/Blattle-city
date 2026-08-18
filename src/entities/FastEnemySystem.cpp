#include "FastEnemySystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

// Tanque "Rapido": EXACTAMENTE la misma IA que el "Basico" (ver
// EnemySystem.cpp — misma reconstruccion paso a paso, mismos pasos 1-3),
// solo que mas veloz moviendose y disparando (ver
// kFastEnemySpeedMultiplier/kFastBulletSpeedMultiplier). Ver el comentario
// de cabecera de EnemySystem.cpp para el detalle completo de cada paso; acá
// solo se repite lo esencial para no tener que ir y volver entre archivos.
//
// Paso 1: movimiento en linea recta vertical u horizontal, en tramos de 1 a
// 6 celdas elegidos al azar; al completarse un tramo, elige uno nuevo y una
// direccion nueva, y recien entonces vuelve a decidir.
//
// Paso 2: en niveles 1-3, direccion ponderada segun la posicion (celda) del
// enemigo respecto del aguila (ver DirectionWeights/kWeightsByLevel y
// WeightedRandomDirection). Desde nivel 4, pathfinding real
// (BaseDistanceField) en vez de la ponderacion.
//
// Paso 3: reaccion al choque. Si la direccion actual esta bloqueada, cada
// frame que siga asi hace 2 sorteos independientes de 50%: uno para
// cancelar el movimiento y pasar ya al siguiente (una direccion distinta a
// la bloqueada, prefiriendo una que ya este libre entre varios intentos),
// y otro para disparar hacia donde esta mirando (encarando YA la direccion
// bloqueada antes de tirar, para no apuntar para otro lado).

namespace bc {

namespace {
constexpr int kMinRunCells = 1;
constexpr int kMaxRunCells = 6;

// Red de seguridad minima (NO es esquive "inteligente"): si el tramo
// elegido choca contra algo y nunca se completa durante ~15s seguidos, se
// repone a la fuerza en una direccion libre (si encuentra alguna).
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
// nivel >= kMinPathfindingLevel).
constexpr double kFieldRecomputeInterval = 0.5;

// Diferencia del "Rapido" respecto al "Basico": +0.5 tambien a la velocidad
// de la bala (Tank::BulletSpeed() devuelve el valor base sin multiplicador
// propio; esto lo escala 1.5x = "+0.5" en la misma logica que el
// multiplicador de movimiento, ver kFastEnemySpeedMultiplier en el .h).
constexpr float kFastBulletSpeedMultiplier = 1.5f;

// Nivel de arma que llevan sus balas: mismo dano contra hierro que un
// jugador con arma nivel 2 (ver kSteelDamageByLevel en BulletSystem.cpp).
constexpr int kWeaponLevel = 2;

constexpr int kMinAggressivenessLevel = 1;
constexpr int kMaxAggressivenessLevel = 5;

Direction RandomDirection(std::mt19937& rng) {
    constexpr Direction kAllDirs[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
    std::uniform_int_distribution<int> dist(0, 3); // 25% cada una
    return kAllDirs[dist(rng)];
}

// Los numeros que arman la tabla de pesos de WeightedRandomDirection (ver
// comentario equivalente en EnemySystem.cpp para el detalle completo).
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

// Nivel de agresividad 1-5: niveles 1-3 confirmados; nivel 4 no usa esta
// tabla (ver kMinPathfindingLevel, usa pathfinding real en su lugar);
// nivel 5 es placeholder (copia de nivel 3) hasta que se defina.
constexpr DirectionWeights kWeightsByLevel[5] = {
    {30.0f, 20.0f, 50.0f, 75.0f, 25.0f, 50.0f, 75.0f, 15.0f}, // nivel 1
    {25.0f, 10.0f, 75.0f, 98.0f, 25.0f, 75.0f, 98.0f, 10.0f}, // nivel 2
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 3
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 4 (no se usa: pathfinding)
    {10.0f, 5.0f, 80.0f, 99.0f, 25.0f, 80.0f, 99.0f, 5.0f},   // nivel 5 (placeholder, pendiente)
};

// Direccion al azar, ponderada segun la posicion (celda) del enemigo
// respecto del aguila y el nivel de agresividad actual: ver la tabla de
// pesos en el comentario de arriba del archivo. Sortea un numero entre 0 y
// la suma total de los 4 pesos, y devuelve la direccion en cuyo "tramo" cayo.
Direction WeightedRandomDirection(std::mt19937& rng, int enemyCellX, int enemyCellY, int baseCellX, int baseCellY, const DirectionWeights& w) {
    const bool sameColumn = (enemyCellX == baseCellX);
    const bool sameRow = (enemyCellY == baseCellY);

    const float wUp = sameColumn ? w.upAlignedPenalty : w.upBase;
    const float wDown = sameColumn ? w.downAligned : w.downFavored;

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

// Nivel de agresividad >= kMinPathfindingLevel: ver comentario equivalente
// en EnemySystem.cpp.
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

void FastEnemySystem::SetAggressivenessLevel(int level) {
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

void FastEnemySystem::SpawnAt(float x, float y) {
    Enemy enemy;
    enemy.tank.SetPosition(x, y);
    enemy.tank.SetFacing(Direction::Down); // entra mirando hacia adentro del mapa
    enemy.tank.SetSpeedMultiplier(kFastEnemySpeedMultiplier);
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

void FastEnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY) {
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
        if (bullets.KillPlayerBulletsHittingBox(eLeft, eRight, eTop, eBottom, impacts, hitLevels)) {
            specialExplosions.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
            enemy.alive = false;
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

        // Red de seguridad: nunca trabada para siempre.
        bool hardUnstuckTriggered = false;
        if (enemy.stuckFrames > kHardUnstuckFrames) {
            Direction bestDir = enemy.moveDir;
            for (int tries = 0; tries < 4; ++tries) {
                const Direction candidate = RandomDirection(enemy.rng);
                if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                    bestDir = candidate;
                    break;
                }
            }
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

        // --- Reaccion al choque (ver comentario de cabecera, paso 3) ---
        if (!hardUnstuckTriggered && WouldBeBlocked(enemy.tank, enemy.moveDir, map, others)) {
            if (RollChance(enemy.rng, 0.5)) {
                Direction newDir = enemy.moveDir;
                if (usePathfinding) {
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
                    while (newDir == enemy.moveDir) {
                        newDir = WeightedRandomDirection(enemy.rng, cellX, cellY, baseCellX, baseCellY, weights);
                    }
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
                // Encara la nueva direccion YA (ver comentario equivalente
                // en EnemySystem.cpp): giro inmediato, no depende de que
                // tank.Update() llegue a procesar el input mas abajo.
                enemy.tank.SetFacing(newDir);
                enemy.decisionCellX = cellX;
                enemy.decisionCellY = cellY;
                enemy.deviationCellsRemaining = RandomRunLength(enemy.rng);
            }
            if (RollChance(enemy.rng, 0.5)) {
                const bool bulletAlreadyAlive = bullets.HasAliveBullet(enemy.ownerId);
                if (!bulletAlreadyAlive && enemy.tank.CanShoot()) {
                    // Encara YA la direccion bloqueada antes de disparar
                    // (ver comentario equivalente en EnemySystem.cpp).
                    enemy.tank.SetFacing(enemy.moveDir);
                    float muzzleX = 0.0f, muzzleY = 0.0f;
                    enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                    bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.moveDir, enemy.tank.BulletSpeed() * kFastBulletSpeedMultiplier, kWeaponLevel, 1);
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
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed() * kFastBulletSpeedMultiplier, kWeaponLevel, 1);
            }
        }
    }
}

} // namespace bc
