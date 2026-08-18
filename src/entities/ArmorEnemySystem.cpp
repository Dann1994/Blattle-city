#include "ArmorEnemySystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

// Tanque "Blindado" (enemigo 3): misma IA que "Basico"/"Rapido" (ver el
// comentario de cabecera de EnemySystem.cpp para el detalle completo de
// cada paso). Las unicas diferencias son de combate/velocidad:
//   - Mas lento: misma velocidad que un jugador con arma nivel 4 (ver
//     kArmorEnemySpeedMultiplier en el .h).
//   - Puede tener hasta 2 balas propias en vuelo a la vez (kMaxAliveBullets),
//     no 1: "capacidad de tirar dos disparos seguidos". El segundo tiro de
//     cada tanda de rutina no sale pegado al primero: espera un intervalo
//     al azar entre kSecondShotMinDelay y kSecondShotMaxDelay (0.5s-2s, ver
//     Enemy::secondShotDelay).
//   - Sus balas llevan weaponLevel alto (kArmorWeaponLevel) para poder
//     romper hierro, no solo ladrillo (ver kSteelDamageByLevel en
//     BulletSystem.cpp: nivel 3 = 15 de dano por impacto, un bloque de
//     hierro tiene 75, asi que se abre paso en unos 5 tiros sostenidos).
//     El acero ya no es "intransitable para siempre" para este tipo (ver
//     WouldBeBlocked mas abajo): la reaccion al choque lo tirotea igual que
//     a un ladrillo.
// Desde nivel de agresividad 4, la direccion se elige con pathfinding real
// (BaseDistanceField) en vez de la tabla de pesos por nivel (ver
// kMinPathfindingLevel).

namespace bc {

namespace {
constexpr int kMinRunCells = 1;
constexpr int kMaxRunCells = 6;

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

// Nivel de arma que llevan sus balas (no el de movimiento del tanque, que
// no tiene: ver kArmorEnemySpeedMultiplier en el .h). Nivel 3 = suficiente
// para ir rompiendo hierro en varios impactos sostenidos, sin llegar al
// "revienta todo de un tiro" del nivel 4.
constexpr int kArmorWeaponLevel = 3;

// Puede tener hasta 2 balas propias en pantalla a la vez (el resto de los
// enemigos, 1).
constexpr int kMaxAliveBullets = 2;

// El segundo tiro de cada tanda no sale junto con el primero: espera entre
// esto (ver Enemy::secondShotDelay).
constexpr double kSecondShotMinDelay = 0.5;
constexpr double kSecondShotMaxDelay = 2.0;

constexpr int kMinAggressivenessLevel = 1;
constexpr int kMaxAggressivenessLevel = 5;

double RandomInRange(std::mt19937& rng, double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(rng);
}

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
// respecto del aguila y el nivel de agresividad actual. Ver el comentario
// de cabecera de EnemySystem.cpp para la tabla de pesos completa (identica
// aca).
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

// Bloqueada ahora mismo: pared del escenario, borde del mapa u otro tanque.
// A diferencia de EnemySystem/FastEnemySystem, el hierro NO se trata
// distinto de un ladrillo aca: este tipo tiene bala suficiente para
// romperlo (ver kArmorWeaponLevel), asi que map.IsBoxBlocked ya alcanza sin
// necesitar una excepcion para acero (que de todos modos hoy no existe en
// este archivo: fisicamente sigue siendo solido hasta que las balas lo
// terminen de romper, TileBlocksMovement no cambia por esto).
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

void ArmorEnemySystem::SetAggressivenessLevel(int level) {
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

void ArmorEnemySystem::SpawnAt(float x, float y) {
    Enemy enemy;
    enemy.tank.SetPosition(x, y);
    enemy.tank.SetFacing(Direction::Down); // entra mirando hacia adentro del mapa
    enemy.tank.SetSpeedMultiplier(kArmorEnemySpeedMultiplier);
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

void ArmorEnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY) {
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

        // --- Reaccion al choque: 2 sorteos independientes de 50% cada frame
        // que siga bloqueada (redirigir / disparar hacia lo que la frena,
        // incluido el hierro: ver comentario de cabecera). ---
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
                if (bullets.AliveBulletCount(enemy.ownerId) < kMaxAliveBullets && enemy.tank.CanShoot()) {
                    // Encara YA la direccion bloqueada antes de disparar
                    // (ver comentario equivalente en EnemySystem.cpp).
                    enemy.tank.SetFacing(enemy.moveDir);
                    float muzzleX = 0.0f, muzzleY = 0.0f;
                    enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                    bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.moveDir, enemy.tank.BulletSpeed(), kArmorWeaponLevel, kMaxAliveBullets);
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

        // --- Disparo: tanda de 2 tiros de rutina, sin puntoria (paso
        // pendiente). Dispara el primero cuando vence shootTimer; el
        // segundo no sale junto con el primero, sino recien despues de un
        // intervalo al azar entre kSecondShotMinDelay y kSecondShotMaxDelay
        // (secondShotDelay, -1 mientras no hay un segundo tiro pendiente).
        // Recien cuando sale el segundo se vuelve a armar shootTimer para
        // la proxima tanda. ---
        const int aliveCount = bullets.AliveBulletCount(enemy.ownerId);

        if (enemy.secondShotDelay >= 0.0) {
            enemy.secondShotDelay -= dt;
            if (enemy.secondShotDelay <= 0.0 && aliveCount < kMaxAliveBullets && enemy.tank.CanShoot()) {
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed(), kArmorWeaponLevel, kMaxAliveBullets);
                enemy.secondShotDelay = -1.0;
                enemy.shootTimer = kPlaceholderShootInterval; // arranca la cuenta para la proxima tanda
            }
        } else if (aliveCount < kMaxAliveBullets && enemy.tank.CanShoot()) {
            enemy.shootTimer -= dt;
            if (enemy.shootTimer <= 0.0) {
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed(), kArmorWeaponLevel, kMaxAliveBullets);
                enemy.secondShotDelay = RandomInRange(enemy.rng, kSecondShotMinDelay, kSecondShotMaxDelay);
            }
        }
    }
}

} // namespace bc
