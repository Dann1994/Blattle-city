#include "EnemySystem.h"

#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

namespace bc {

namespace {
// Cada cuanto se recalcula el campo de distancias hacia el aguila (ver
// BaseDistanceField): no hace falta cada frame, la grilla es chica y el
// terreno solo cambia cuando algo rompe un ladrillo o la Pala fortifica la
// base, asi que un valor mas alto igual se nota "al toque" en la practica.
constexpr double kFieldRecomputeInterval = 0.5;

// Cuantos frames seguidos sin moverse antes de considerarla realmente
// atascada (un tanque en el medio, no un ladrillo por romper: eso tiene su
// propia paciencia mas larga, ver kBrickBreachPatienceFrames): rota entre
// las 4 direcciones hasta encontrar una libre.
constexpr int kDodgeStuckFrames = 20;  // ~0.33s a 60fps
constexpr int kDodgeRotateFrames = 15; // ~0.25s a 60fps: cuanto prueba cada candidata antes de pasar a la siguiente
constexpr int kDodgeCandidateCount = 4;

// El camino mas corto hacia el aguila (ver BaseDistanceField) a veces pasa
// por un ladrillo: en vez de rodearlo (ya se decidio que romperlo es lo
// mejor), se planta encarandolo y lo tirotea hasta abrirlo. Este es el
// limite de paciencia antes de rendirse igual, por si algo raro pasa (ver
// kBrickBreachPatienceFramesByLevel: mas agresivo, mas paciencia).
constexpr int kBrickBreachPatienceFramesByLevel[5] = {180, 240, 300, 400, 540}; // ~3s..~9s a 60fps

// Si lleva ~15s seguidos sin poder avanzar, ninguna heuristica lo resolvio
// solo (deadlock genuino, por ejemplo con otro tanque en un punto muerto):
// se reposiciona directo en la mejor celda del campo, sin importar que la
// bloquee algo ahora mismo, para garantizar que nunca quede trabada para
// siempre.
constexpr int kHardUnstuckFrames = 900; // ~15s a 60fps

// Que tan lejos por delante del tanque se chequea que hay en el mapa (ver
// TileAhead) antes de comprometerse a una direccion.
constexpr float kLookaheadStep = 0.2f;

// Buscando el aguila y a 2 casillas o mas: en cada disparo "de rutina"
// (ninguno de los otros casos ya justificados, ver Update), esta chance de
// girar a apuntarle antes de tirar, en vez de disparar para donde este
// mirando (mas agresivo, mas chance, ver kBaseSnapShotChanceByLevel).
constexpr float kBaseSnapShotMinRadius = 2.0f;
constexpr double kBaseSnapShotChanceByLevel[5] = {0.15, 0.25, 0.40, 0.55, 1.00};

// Cuanto tarda el proximo disparo despues de que el anterior impacto o
// desaparecio (ver Enemy::shootTimer/bulletWasAlive): normal, mas seguido
// si esta atascada o plantada rompiendo un ladrillo, y mas seguido todavia
// cerca del aguila. Escalan con el nivel de agresividad (ver
// EnemySystem::SetAggressivenessLevel): indice 0 = nivel 1 (mas pasivo) ..
// indice 4 = nivel 5 (mas agresivo); indice 2 = nivel 3 son los valores
// originales.
constexpr double kShootIntervalMinByLevel[5] = {5.0, 3.0, 3.0, 1.0, 1.2};
constexpr double kShootIntervalMaxByLevel[5] = {5.0, 3.0, 5.0, 1.0, 2.5};
constexpr double kShootIntervalStuckMinByLevel[5] = {0.8, 0.6, 0.4, 0.3, 0.2};
constexpr double kShootIntervalStuckMaxByLevel[5] = {1.5, 1.2, 1.0, 0.7, 0.5};
// Cerca del aguila (ver kNearBaseRadius), como fallback para cuando todavia
// no la tiene encarada (ver baseAimed en Update: si la tiene encarada, ni
// siquiera espera este intervalo — salvo en los niveles bajos, ver
// kBaseAimedMinLevel, donde ni encarada se salta el temporizador, para no
// liquidar el aguila enseguida).
constexpr double kShootIntervalNearBaseMinByLevel[5] = {5.0, 3.0, 0.8, 0.5, 0.1};
constexpr double kShootIntervalNearBaseMaxByLevel[5] = {5.0, 3.0, 1.5, 1.0, 0.2};

// A partir de que nivel de agresividad dispara de una apenas tiene al
// aguila encarada y cerca (ver baseAimed), sin esperar el temporizador de
// arriba. En los niveles bajos ni encarada dispara antes de tiempo, para
// que "cada 3 segundos" sea de verdad cada 3 segundos y no se la liquide
// enseguida.
constexpr int kBaseAimedMinLevel = 3;

// Nivel 5 (el mas agresivo): si un jugador la esta tocando, en vez de
// esquivarlo/rodearlo como a cualquier obstaculo (niveles 1-4), se alinea
// con su fila o columna para encararlo y dispararle a matar. Radio de
// "tocando": centro a centro, un tanque mide 1 celda de lado, asi que 1.0
// es justo cuando los bordes se tocan.
constexpr float kAdjacentSnapRadius = 1.0f;
constexpr int kFightOnTouchMinLevel = 5;

// Tolerancia de alineacion para disparar "ya encarada" contra el aguila (ver
// baseAimed): no hace falta estar perfilada, alcanza con estar en una
// posicion desde la que la bala pueda llegar a pegarle.
constexpr float kBaseAlignTolerance = 0.9f;

// Igual, pero contra un jugador (nivel 5, ver kFightOnTouchMinLevel): un
// blanco chico y movil, asi que pide bastante mas precision.
constexpr float kPlayerAlignTolerance = 0.4f;

// Distancia (celdas, centro a centro) a la que el aguila empieza a merecer
// las bonificaciones de disparo de arriba (tolerancia laxa, cadencia mas
// rapida): el movimiento en si ya es optimo siempre gracias al campo de
// distancias, esto solo afecta que tan ansiosa esta por tirar.
constexpr float kNearBaseRadius = 5.0f;

// Cada vez que entra a una celda nueva del camino hacia el aguila (y no
// esta ya en medio de un desvio), esta chance de arrancar uno: una
// direccion al azar entre las 4 (incluso alejandose del aguila, "para
// arriba") durante un tramo de kPathDeviationMinCells a
// kPathDeviationMaxCells celdas, antes de volver al camino optimo. Asi no
// es tan robotica siguiendo siempre el camino perfecto — mas agresivo,
// menos chance de desviarse (mas directo al aguila).
constexpr double kPathDeviationChanceByLevel[5] = {0.45, 0.30, 0.15, 0.03, 0.0};
constexpr int kPathDeviationMinCells = 2;
constexpr int kPathDeviationMaxCells = 6;

// Nivel de agresividad valido: 1 (mas pasivo) a 5 (mas agresivo).
constexpr int kMinAggressivenessLevel = 1;
constexpr int kMaxAggressivenessLevel = 5;

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

double RandomInRange(double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(Rng());
}

bool RollChance(double probability) {
    std::bernoulli_distribution dist(probability);
    return dist(Rng());
}

int RandomDeviationLength() {
    std::uniform_int_distribution<int> dist(kPathDeviationMinCells, kPathDeviationMaxCells);
    return dist(Rng());
}

Direction RandomDirection() {
    constexpr Direction kAllDirs[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
    std::uniform_int_distribution<int> dist(0, 3);
    return kAllDirs[dist(Rng())];
}

// Que hay pegado al frente del tanque en esa direccion (a kLookaheadStep
// celdas): fuera de mapa cuenta como Acero (intransitable total), para que
// el resto de las funciones no tengan que chequear bordes por separado.
TileType TileAhead(const Tank& tank, Direction dir, const TileMap& map) {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    float checkX = (left + right) * 0.5f;
    float checkY = (top + bottom) * 0.5f;
    switch (dir) {
        case Direction::Up:    checkY = top - kLookaheadStep; break;
        case Direction::Down:  checkY = bottom + kLookaheadStep; break;
        case Direction::Left:  checkX = left - kLookaheadStep; break;
        case Direction::Right: checkX = right + kLookaheadStep; break;
    }
    const int cx = static_cast<int>(std::floor(checkX));
    const int cy = static_cast<int>(std::floor(checkY));
    if (!map.InBounds(cx, cy)) {
        return TileType::Steel;
    }
    return map.At(cx, cy).type;
}

// Ladrillo (destructible) justo al frente: a diferencia de un bloqueo de
// acero/agua, conviene plantarse y tirotearlo en vez de rodearlo (ver
// Update). El campo de distancias ya decide CUANDO conviene (kBrickEntryCost
// en BaseDistanceField.cpp), esto solo detecta el caso para reaccionar bien.
bool IsBrickAhead(const Tank& tank, Direction dir, const TileMap& map) {
    return TileAhead(tank, dir, map) == TileType::Brick;
}

// Acero, agua o fuera de mapa: intransitable de verdad, ni a tiros. El
// ladrillo NO cuenta aca a proposito (se puede volar, ver IsBrickAhead).
bool IsStaticImpassable(const Tank& tank, Direction dir, const TileMap& map) {
    const TileType t = TileAhead(tank, dir, map);
    return t == TileType::Steel || t == TileType::Water;
}

// Hay otro tanque pegado al frente en esa direccion (jugador o enemigo): un
// obstaculo mas a esquivar/rodear, igual que un bloque — no hay prioridad
// de combate, el campo de distancias no sabe nada de tanques (solo terreno
// estatico), asi que esto evita que dos tanques intenten ocupar la misma
// celda.
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

// Igual que IsStaticImpassable + IsTankAhead combinados: se usa solo para
// el esquive reactivo por atascamiento (ver Update), donde SI conviene
// tratar al ladrillo como bloqueo (ya se esta ahi porque romperlo no
// funciono / no correspondia, hay que probar otra cosa, no insistir).
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

// El tanque esta mirando hacia el objetivo (mismo pasillo, en la direccion
// correcta): suficiente para animo de disparo simple, sin trazar linea de
// vision real contra obstaculos (la IA no necesita puntaria perfecta).
// alignTolerance es cuanto puede estar corrida la otra coordenada y seguir
// contando como "encarada" (ver kBaseAlignTolerance).
bool IsFacingTarget(const Tank& tank, float targetX, float targetY, float alignTolerance) {
    const float cx = tank.X() + 0.5f;
    const float cy = tank.Y() + 0.5f;
    switch (tank.Facing()) {
        case Direction::Up:    return std::fabs(cx - targetX) < alignTolerance && targetY < cy;
        case Direction::Down:  return std::fabs(cx - targetX) < alignTolerance && targetY > cy;
        case Direction::Left:  return std::fabs(cy - targetY) < alignTolerance && targetX < cx;
        case Direction::Right: return std::fabs(cy - targetY) < alignTolerance && targetX > cx;
    }
    return false;
}
} // namespace

void EnemySystem::SetAggressivenessLevel(int level) {
    if (level < kMinAggressivenessLevel) {
        level = kMinAggressivenessLevel;
    } else if (level > kMaxAggressivenessLevel) {
        level = kMaxAggressivenessLevel;
    }
    aggressivenessLevel_ = level;
}

void EnemySystem::SpawnAt(float x, float y) {
    Enemy enemy;
    enemy.tank.SetPosition(x, y);
    enemy.tank.SetFacing(Direction::Down); // entra mirando hacia adentro del mapa
    enemy.tank.SetSpeedMultiplier(kEnemySpeedMultiplier);
    enemy.ownerId = nextOwnerId_++;
    enemy.alive = true;
    enemy.shootTimer = RandomInRange(1.0, 2.5); // el primer disparo no espera el ciclo completo
    enemy.spawn.Start(x, y); // destello de aparicion, igual que los jugadores
    enemies_.push_back(enemy);
}

void EnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float baseX, float baseY) {
    const int baseCellX = static_cast<int>(baseX);
    const int baseCellY = static_cast<int>(baseY);
    const int lvl = aggressivenessLevel_ - 1; // indice 0..4 para las tablas *ByLevel

    fieldRecomputeTimer_ -= dt;
    if (fieldRecomputeTimer_ <= 0.0) {
        baseField_.Recompute(map, baseCellX, baseCellY);
        fieldRecomputeTimer_ = kFieldRecomputeInterval;
    }

    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) {
            continue;
        }

        enemy.tank.TickShootCooldown(dt);
        enemy.tank.TickFreeze(dt);

        // Muere de un solo impacto de bala de jugador, sin importar el nivel:
        // misma animacion chica de explosion que Game::DestroyTank usa para
        // los jugadores.
        float eLeft = 0.0f, eRight = 0.0f, eTop = 0.0f, eBottom = 0.0f;
        enemy.tank.GetBounds(eLeft, eRight, eTop, eBottom);
        std::vector<int> hitLevels;
        if (bullets.KillBulletsHittingBox(enemy.ownerId, eLeft, eRight, eTop, eBottom, impacts, hitLevels)) {
            specialExplosions.Spawn(enemy.tank.X() + 0.5f, enemy.tank.Y() + 0.5f, /*nativeScale=*/true);
            enemy.alive = false;
            continue;
        }

        if (enemy.spawn.IsActive()) {
            // Mientras dura el destello de aparicion (igual que los
            // jugadores), no se mueve, no razona ni dispara.
            enemy.spawn.Update(dt);
            continue;
        }

        if (enemy.tank.IsFrozen()) {
            // Paralizada (onda del disparo especial, igual que los
            // jugadores): no se mueve, no razona ni dispara mientras dure.
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

        // Nivel 5 (maxima agresividad): si un jugador la esta tocando,
        // prioriza matarlo en vez de esquivarlo. En el resto de los niveles
        // esto ni se calcula: un jugador es un obstaculo mas a rodear, igual
        // que un bloque (ver el bucle de movimiento de abajo).
        Tank* touchingPlayer = nullptr;
        if (aggressivenessLevel_ >= kFightOnTouchMinLevel) {
            const float ex = enemy.tank.X() + 0.5f;
            const float ey = enemy.tank.Y() + 0.5f;
            float bestDist = kAdjacentSnapRadius;
            for (Tank* p : playerTanks) {
                if (p == nullptr) {
                    continue;
                }
                const float px = p->X() + 0.5f;
                const float py = p->Y() + 0.5f;
                const float d = std::sqrt((px - ex) * (px - ex) + (py - ey) * (py - ey));
                if (d <= kAdjacentSnapRadius && d < bestDist) {
                    bestDist = d;
                    touchingPlayer = p;
                }
            }
        }

        // Ultimo recurso: si lleva ~15s seguidos sin poder avanzar, ninguna
        // de las heuristicas de abajo lo resolvio solo (deadlock genuino,
        // por ejemplo con otro tanque en un punto muerto). Se reposiciona
        // directo en la mejor celda del campo hacia el aguila, sin importar
        // que la bloquee algo ahora mismo, para garantizar que nunca quede
        // trabada para siempre.
        bool hardUnstuckTriggered = false;
        if (enemy.stuckFrames > kHardUnstuckFrames) {
            const int hereX = static_cast<int>(std::round(enemy.tank.X()));
            const int hereY = static_cast<int>(std::round(enemy.tank.Y()));
            const Direction bestDir = baseField_.RankedDirections(hereX, hereY)[0];
            float ddx = 0.0f, ddy = 0.0f;
            DirectionVector(bestDir, ddx, ddy);
            enemy.tank.SetPosition(std::round(enemy.tank.X()) + ddx, std::round(enemy.tank.Y()) + ddy);
            enemy.tank.SetFacing(bestDir);
            enemy.moveDir = bestDir;
            enemy.stuckFrames = 0;
            enemy.debugMode = 'U';
            hardUnstuckTriggered = true;
        }

        // --- Movimiento: SIEMPRE hacia el aguila. Un jugador (o cualquier
        // otro tanque) en el camino es un obstaculo mas a esquivar/rodear,
        // igual que un bloque — no hay prioridad de combate ni modo de
        // defensa: el campo de distancias ya calculo el camino real mas
        // corto, aca solo se elige el mejor vecino que no este bloqueado
        // ahora mismo por algo dinamico (un tanque) que el campo no sabe.
        bool breachingNow = false;
        bool alignedStandStill = false;
        if (hardUnstuckTriggered) {
            // Ya se repuso a la fuerza este frame (ver arriba): no
            // reconsiderar de nuevo con logica que ya demostro no resolverlo.
        } else if (touchingPlayer != nullptr) {
            // Nivel 5: se alinea con la fila o columna del jugador que la
            // toca (la que le falte menos) para encararlo y dispararle a
            // matar. Si ya esta alineada, se queda quieta apuntando en vez
            // de intentar avanzar hacia el (chocaria de lleno contra el; ver
            // el comentario de mas abajo sobre el asistente de deslizamiento).
            const float pdx = touchingPlayer->X() - enemy.tank.X();
            const float pdy = touchingPlayer->Y() - enemy.tank.Y();
            constexpr float kAlignEpsilon = 0.05f;
            enemy.debugMode = 'T';
            if (std::fabs(pdy) <= kAlignEpsilon) {
                enemy.moveDir = pdx >= 0.0f ? Direction::Right : Direction::Left;
                alignedStandStill = true;
            } else if (std::fabs(pdx) <= kAlignEpsilon) {
                enemy.moveDir = pdy >= 0.0f ? Direction::Down : Direction::Up;
                alignedStandStill = true;
            } else {
                enemy.moveDir = std::fabs(pdx) <= std::fabs(pdy)
                    ? (pdx >= 0.0f ? Direction::Right : Direction::Left)
                    : (pdy >= 0.0f ? Direction::Down : Direction::Up);
            }
        } else {
            const int cellX = static_cast<int>(std::floor(enemy.tank.X() + 0.5f));
            const int cellY = static_cast<int>(std::floor(enemy.tank.Y() + 0.5f));

            // Cada celda nueva (y si no esta ya en medio de un desvio),
            // sortea si arranca uno: una direccion al azar entre las 4 (ver
            // kPathDeviationChance) por un tramo de 1 a 5 celdas, para que
            // no sea tan robotica siguiendo siempre el camino optimo.
            if (cellX != enemy.decisionCellX || cellY != enemy.decisionCellY) {
                enemy.decisionCellX = cellX;
                enemy.decisionCellY = cellY;
                if (enemy.deviationCellsRemaining > 0) {
                    --enemy.deviationCellsRemaining;
                }
                if (enemy.deviationCellsRemaining <= 0 && RollChance(kPathDeviationChanceByLevel[lvl])) {
                    enemy.deviationDir = RandomDirection();
                    enemy.deviationCellsRemaining = RandomDeviationLength();
                }
            }

            Direction fieldDir = enemy.moveDir;
            bool fieldFound = false;
            bool deviating = false;
            if (enemy.deviationCellsRemaining > 0 &&
                !IsStaticImpassable(enemy.tank, enemy.deviationDir, map) &&
                !IsTankAhead(enemy.tank, enemy.deviationDir, others)) {
                fieldDir = enemy.deviationDir;
                fieldFound = true;
                deviating = true;
            } else {
                if (enemy.deviationCellsRemaining > 0) {
                    // La direccion del desvio se bloqueo: se cancela, vuelve
                    // al camino optimo esta celda en vez de insistir.
                    enemy.deviationCellsRemaining = 0;
                }
                for (Direction d : baseField_.RankedDirections(cellX, cellY)) {
                    if (IsStaticImpassable(enemy.tank, d, map)) {
                        continue; // acero/agua/fuera de mapa: nunca elegible
                    }
                    if (IsTankAhead(enemy.tank, d, others)) {
                        continue; // otro tanque encima: probar la siguiente candidata
                    }
                    fieldDir = d;
                    fieldFound = true;
                    break;
                }
            }

            // El camino elegido (optimo o desviado) pasa por un ladrillo:
            // se planta a romperlo en vez de rodearlo, mientras no se pase
            // de paciencia (por si algo raro pasa y nunca se abre paso).
            const bool wantsBreach = fieldFound && IsBrickAhead(enemy.tank, fieldDir, map);
            breachingNow = wantsBreach && enemy.stuckFrames <= kBrickBreachPatienceFramesByLevel[lvl];
            const bool stuck = !breachingNow && enemy.stuckFrames > kDodgeStuckFrames;

            if (breachingNow) {
                enemy.moveDir = fieldDir;
                enemy.debugMode = 'B';
            } else if (stuck) {
                // Atascada de verdad (un tanque tapando las 3-4 mejores
                // opciones, o se agoto la paciencia de romper el ladrillo):
                // rota entre las 4 direcciones hasta encontrar una libre,
                // sin importar que diga el campo por ahora. Apenas logre
                // moverse de nuevo, el campo retoma el control solo.
                enemy.debugMode = 'D';
                constexpr std::array<Direction, kDodgeCandidateCount> kAllDirs{Direction::Up, Direction::Down, Direction::Left, Direction::Right};
                const int dodgeStage = (enemy.stuckFrames - kDodgeStuckFrames) / kDodgeRotateFrames;
                Direction chosen = kAllDirs[dodgeStage % kDodgeCandidateCount];
                bool foundClear = false;
                for (int tries = 0; tries < kDodgeCandidateCount; ++tries) {
                    const Direction candidate = kAllDirs[(dodgeStage + tries) % kDodgeCandidateCount];
                    if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                        chosen = candidate;
                        foundClear = true;
                        break;
                    }
                }
                if (!foundClear && fieldFound && IsBrickAhead(enemy.tank, fieldDir, map)) {
                    // Ni esquivando encuentra una salida (las 4 bloqueadas,
                    // y el esquive de por si evita el ladrillo): si el
                    // camino que recomienda el campo es justo un ladrillo,
                    // mejor plantarse a romperlo que girar sin sentido.
                    chosen = fieldDir;
                    enemy.debugMode = 'B';
                }
                enemy.moveDir = chosen;
            } else if (fieldFound) {
                enemy.debugMode = deviating ? 'V' : 'F';
                enemy.moveDir = fieldDir;
            } else {
                // Las 4 direcciones del campo estan bloqueadas ahora mismo
                // (acero/agua/pared del escenario, u otro tanque) y todavia
                // no paso suficiente tiempo para el esquive por atascamiento
                // de arriba: en vez de esperar, prueba ya una direccion
                // distinta al azar que si este libre, para no quedarse
                // empujando contra nada un rato antes de reaccionar.
                enemy.debugMode = 'N';
                Direction candidate = enemy.moveDir;
                for (int tries = 0; tries < 4; ++tries) {
                    candidate = RandomDirection();
                    if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                        enemy.moveDir = candidate;
                        break;
                    }
                }
            }
        }

        PlayerInput input;
        if (alignedStandStill) {
            // Sin input de movimiento: Tank::Update no intenta moverse (asi
            // que tampoco puede activar el asistente de deslizamiento de
            // esquinas, que en un choque frontal contra un tanque la podia
            // correr medio casillero al costado), solo tiquea su animacion.
            enemy.tank.SetFacing(enemy.moveDir);
        } else {
            switch (enemy.moveDir) {
                case Direction::Up:    input.moveUp = true;    break;
                case Direction::Down:  input.moveDown = true;  break;
                case Direction::Left:  input.moveLeft = true;  break;
                case Direction::Right: input.moveRight = true; break;
            }
        }

        const float prevX = enemy.tank.X();
        const float prevY = enemy.tank.Y();
        enemy.tank.Update(dt, input, map, others);

        // Fiel a la fila/columna: si avanza en horizontal, se mantiene
        // exactamente en su fila (Y entero); si avanza en vertical, se
        // mantiene exactamente en su columna (X entero). Nunca se le deja
        // quedar a mitad de camino en el eje perpendicular al que se mueve
        // (eso era lo que la desalineaba y la trababa sin sentido).
        if (enemy.moveDir == Direction::Left || enemy.moveDir == Direction::Right) {
            enemy.tank.SetPosition(enemy.tank.X(), std::round(enemy.tank.Y()));
        } else {
            enemy.tank.SetPosition(std::round(enemy.tank.X()), enemy.tank.Y());
        }

        const float movedDist = std::fabs(enemy.tank.X() - prevX) + std::fabs(enemy.tank.Y() - prevY);
        const bool moved = movedDist > 0.0005f;
        enemy.stuckFrames = moved ? 0 : (enemy.stuckFrames + 1);

        // --- Disparo ---
        // Solo 1 bala propia a la vez (ver BulletSystem::TryShoot). El
        // temporizador del proximo disparo arranca justo cuando la bala
        // anterior impacta/desaparece (bulletWasAlive detecta esa
        // transicion), no en paralelo mientras sigue en pantalla.
        const bool bulletAliveNow = bullets.HasAliveBullet(enemy.ownerId);
        const float curX = enemy.tank.X() + 0.5f;
        const float curY = enemy.tank.Y() + 0.5f;
        const float baseCx = baseX + 0.5f;
        const float baseCy = baseY + 0.5f;
        const float toBaseDx = baseCx - curX;
        const float toBaseDy = baseCy - curY;
        const float distToBase = std::sqrt(toBaseDx * toBaseDx + toBaseDy * toBaseDy);
        const bool nearBase = distToBase <= kNearBaseRadius;

        if (enemy.bulletWasAlive && !bulletAliveNow) {
            const bool stuckALot = enemy.stuckFrames > kDodgeStuckFrames; // incluye "plantada rompiendo": a proposito, se quiere que dispare seguido
            if (stuckALot) {
                enemy.shootTimer = RandomInRange(kShootIntervalStuckMinByLevel[lvl], kShootIntervalStuckMaxByLevel[lvl]);
            } else if (nearBase) {
                enemy.shootTimer = RandomInRange(kShootIntervalNearBaseMinByLevel[lvl], kShootIntervalNearBaseMaxByLevel[lvl]);
            } else {
                enemy.shootTimer = RandomInRange(kShootIntervalMinByLevel[lvl], kShootIntervalMaxByLevel[lvl]);
            }
        }
        enemy.bulletWasAlive = bulletAliveNow;

        if (!bulletAliveNow && enemy.tank.CanShoot()) {
            // Prioridad de disparo (de mas a menos justificado): jugador que
            // la esta tocando ya encarado (nivel 5, ver touchingPlayer) >
            // plantada rompiendo un ladrillo del camino (ya esta
            // encarandolo, ver breachingNow) > el aguila ya encarada y
            // cerca > el temporizador de rutina.
            const bool playerAimed = touchingPlayer != nullptr &&
                IsFacingTarget(enemy.tank, touchingPlayer->X() + 0.5f, touchingPlayer->Y() + 0.5f, kPlayerAlignTolerance);
            const bool breachShot = breachingNow;
            const bool baseAimed = nearBase && aggressivenessLevel_ >= kBaseAimedMinLevel &&
                IsFacingTarget(enemy.tank, baseCx, baseCy, kBaseAlignTolerance);

            enemy.shootTimer -= dt;
            const bool timerShot = enemy.shootTimer <= 0.0;

            if (playerAimed || breachShot || baseAimed || timerShot) {
                // Disparo de rutina (ninguno de los casos de arriba, solo el
                // temporizador) y a 2 casillas o mas del aguila: a veces
                // gira a apuntarle antes de tirar en vez de disparar para
                // donde este mirando.
                if (!playerAimed && !breachShot && !baseAimed && distToBase >= kBaseSnapShotMinRadius && RollChance(kBaseSnapShotChanceByLevel[lvl])) {
                    const Direction snapDir = (std::fabs(toBaseDx) >= std::fabs(toBaseDy))
                        ? (toBaseDx >= 0.0f ? Direction::Right : Direction::Left)
                        : (toBaseDy >= 0.0f ? Direction::Down : Direction::Up);
                    enemy.tank.SetFacing(snapDir);
                }
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed(), 1, 1);
            }
        }
    }
}

} // namespace bc
