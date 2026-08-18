#include "FastEnemySystem.h"

#include <array>
#include <cmath>
#include <limits>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

// Tanque "Rapido": a diferencia del "Basico" (que persigue al aguila con
// pathfinding, ver EnemySystem.cpp), este PATRULLA el escenario al azar sin
// ningun objetivo fijo. Si un jugador entra en su radio de deteccion (ver
// kDetectionRadiusByLevel, nivel 1 = 1 sola casilla), entra en "modo matar":
// lo persigue derecho y le dispara mas seguido, hasta que se aleja o muere.
// Reusa varias piezas de razonamiento de EnemySystem.cpp (que hay adelante,
// esquive por atascamiento, romper un ladrillo en el medio) pero con un
// objetivo bien distinto.

namespace bc {

namespace {
// Cuantos frames seguidos sin moverse antes de considerarla realmente
// atascada (un tanque en el medio, o un rincon sin salida durante la
// patrulla): rota entre las 4 direcciones hasta encontrar una libre.
constexpr int kDodgeStuckFrames = 20;  // ~0.33s a 60fps
constexpr int kDodgeRotateFrames = 15; // ~0.25s a 60fps: cuanto prueba cada candidata antes de pasar a la siguiente
constexpr int kDodgeCandidateCount = 4;

// En "modo matar" (ver kDetectionRadiusByLevel), si el camino directo hacia
// el jugador pasa por un ladrillo, se planta a romperlo en vez de rodearlo
// (patrullando, en cambio, no se molesta: rodea cualquier bloqueo, no hay
// apuro). Limite de paciencia antes de rendirse igual.
constexpr int kBreachPatienceFrames = 300; // ~5s a 60fps

// Si lleva ~15s seguidos sin poder avanzar, nada de lo de arriba lo
// resolvio solo: se reposiciona a la fuerza (una direccion al azar que no
// este bloqueada, si encuentra alguna) para garantizar que nunca quede
// trabada para siempre.
constexpr int kHardUnstuckFrames = 900; // ~15s a 60fps

// Que tan lejos por delante del tanque se chequea que hay en el mapa antes
// de comprometerse a una direccion.
constexpr float kLookaheadStep = 0.2f;

// Patrulla: cada tramo recto dura entre estas 2 celdas (al azar), antes de
// girar hacia otra direccion tambien al azar.
constexpr int kPatrolMinCells = 2;
constexpr int kPatrolMaxCells = 6;

// Radio de deteccion de jugadores (celdas, centro a centro): 2 casillas,
// igual en los 5 niveles de agresividad por ahora (queda la tabla lista
// por si despues se pide diferenciarlos, como con el "Basico").
constexpr float kDetectionRadiusByLevel[5] = {2.0f, 2.0f, 2.0f, 2.0f, 2.0f};

// "Modo matar": una vez que detecta a un jugador dentro del radio de
// arriba, persigue y dispara durante esta cantidad de segundos, se renueva
// cada vez que vuelve a detectar a alguien (aunque se haya alejado del
// radio mientras tanto), y recien cuando pasan 3s sin ninguna deteccion
// nueva vuelve a patrullar.
constexpr double kKillModeDuration = 3.0;

// En "modo matar", a esta distancia deja de perseguir y se planta quieta
// encarando al jugador para dispararle. Un tanque mide 1 celda de lado, asi
// que 1.0 es justo cuando los bordes se tocan — pero el chequeo de "hay
// alguien adelante" (ver IsTankAhead) ya mira un poco mas alla de la
// posicion actual (kLookaheadStep=0.2), asi que entre 1.0 y ~1.2 el propio
// jugador empieza a "bloquear" la persecucion antes de llegar a este radio,
// dejandola sin poder avanzar un rato y activando el esquive de emergencia
// (que se veia como si se moviera al azar). Por eso el radio real es mas
// grande que 1.0, para plantarse ANTES de que ese conflicto aparezca.
constexpr float kAdjacentSnapRadius = 1.3f;

// Cada cuanto dispara: fijo, sin variacion, no depende del nivel de
// agresividad (a diferencia del "Basico").
constexpr double kPatrolShootInterval = 5.0;
constexpr double kKillShootInterval = 2.0;

// En "modo matar", apenas queda encarando al jugador sobre su misma fila o
// columna (perseguiendolo o ya plantada, ver alignedStandStill), dispara de
// inmediato sin esperar kKillShootInterval (ver IsFacingTarget). No hace
// falta estar perfilada del todo, alcanza con estar cerca del eje.
constexpr float kPlayerAlignTolerance = 0.4f;

// Apenas dispara en modo matar (ver el bloque de disparo), corta la
// persecucion ahi mismo: apaga la deteccion (ver detectionLockoutTimer en
// Enemy.h) por esta cantidad de segundos y vuelve a patrullar (con un rumbo
// nuevo al azar), en vez de quedarse plantada tiroteando sin parar.
constexpr double kDetectionLockoutDuration = 2.0;

// Diferencia del "Rapido" respecto al "Basico": +0.5 tambien a la velocidad
// de la bala (ver kFastEnemySpeedMultiplier en el .h para el movimiento).
// Tank::BulletSpeed() devuelve el valor base (8.0 en nivel 1, sin
// multiplicador propio); esto lo escala 1.5x = "+0.5" en la misma logica
// que el multiplicador de movimiento (1.0 = sin cambio).
constexpr float kFastBulletSpeedMultiplier = 1.5f;

// Nivel de agresividad valido: 1 (mas pasivo, detecta de mas cerca) a 5
// (mas agresivo, detecta de mas lejos).
constexpr int kMinAggressivenessLevel = 1;
constexpr int kMaxAggressivenessLevel = 5;

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

int RandomPatrolLength() {
    std::uniform_int_distribution<int> dist(kPatrolMinCells, kPatrolMaxCells);
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

bool IsBrickAhead(const Tank& tank, Direction dir, const TileMap& map) {
    return TileAhead(tank, dir, map) == TileType::Brick;
}

// Acero, agua o fuera de mapa: intransitable de verdad, ni a tiros. El
// ladrillo NO cuenta aca a proposito (se puede volar, ver IsBrickAhead).
bool IsStaticImpassable(const Tank& tank, Direction dir, const TileMap& map) {
    const TileType t = TileAhead(tank, dir, map);
    return t == TileType::Steel || t == TileType::Water;
}

// Hay otro tanque pegado al frente en esa direccion (jugador o enemigo).
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

// Si el tanque, mirando hacia dir, esta alineado con (targetX, targetY) en
// su misma fila o columna y el objetivo esta adelante (no atras). alignTolerance
// es cuanto puede estar corrida la otra coordenada y seguir contando como
// "encarada".
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

// Igual que IsStaticImpassable + IsTankAhead combinados: se usa solo para
// el esquive reactivo por atascamiento, donde SI conviene tratar al
// ladrillo como bloqueo (hay que probar otra cosa, no insistir).
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
} // namespace

void FastEnemySystem::SetAggressivenessLevel(int level) {
    if (level < kMinAggressivenessLevel) {
        level = kMinAggressivenessLevel;
    } else if (level > kMaxAggressivenessLevel) {
        level = kMaxAggressivenessLevel;
    }
    aggressivenessLevel_ = level;
}

void FastEnemySystem::SpawnAt(float x, float y) {
    Enemy enemy;
    enemy.tank.SetPosition(x, y);
    enemy.tank.SetFacing(Direction::Down); // entra mirando hacia adentro del mapa
    enemy.tank.SetSpeedMultiplier(kFastEnemySpeedMultiplier);
    enemy.ownerId = nextOwnerId_++;
    enemy.alive = true;
    enemy.shootTimer = kPatrolShootInterval; // el primer disparo espera el ciclo completo de patrulla
    enemy.spawn.Start(x, y); // destello de aparicion, igual que los jugadores
    enemies_.push_back(enemy);
}

void FastEnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, const std::vector<Tank*>& otherEnemyTanks, float /*baseX*/, float /*baseY*/) {
    const int lvl = aggressivenessLevel_ - 1; // indice 0..4

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

        // Deteccion: el jugador activo mas cercano dentro del radio de este
        // nivel de agresividad. Si detecta a alguien, (re)arranca los 3s de
        // "modo matar" (ver kKillModeDuration); mientras el contador siga
        // arriba de 0, sigue persiguiendo/disparando aunque el jugador se
        // haya alejado del radio mientras tanto (usa el mas cercano de
        // cualquier distancia como blanco durante ese margen). Mientras
        // detectionLockoutTimer siga arriba de 0 (recien disparo en modo
        // matar, ver el bloque de disparo mas abajo), la deteccion queda
        // apagada del todo: ni se escanea, para que vuelva a patrullar en
        // paz un rato en vez de re-engancharse al toque.
        enemy.detectionLockoutTimer -= dt;
        const float ex = enemy.tank.X() + 0.5f;
        const float ey = enemy.tank.Y() + 0.5f;
        Tank* detected = nullptr;
        Tank* nearestAny = nullptr;
        if (enemy.detectionLockoutTimer <= 0.0) {
            float bestDetected = kDetectionRadiusByLevel[lvl];
            float bestAny = std::numeric_limits<float>::infinity();
            for (Tank* p : playerTanks) {
                if (p == nullptr) {
                    continue;
                }
                const float px = p->X() + 0.5f;
                const float py = p->Y() + 0.5f;
                const float d = std::sqrt((px - ex) * (px - ex) + (py - ey) * (py - ey));
                if (d <= kDetectionRadiusByLevel[lvl] && d < bestDetected) {
                    bestDetected = d;
                    detected = p;
                }
                if (d < bestAny) {
                    bestAny = d;
                    nearestAny = p;
                }
            }
        }

        enemy.killModeTimer -= dt;
        if (detected != nullptr) {
            enemy.killModeTimer = kKillModeDuration;
        }
        const bool killMode = enemy.killModeTimer > 0.0 && nearestAny != nullptr;
        Tank* target = detected != nullptr ? detected : nearestAny;

        // Ultimo recurso: si lleva ~15s seguidos sin poder avanzar, nada de
        // lo de abajo lo resolvio solo. Se reposiciona a la fuerza en una
        // direccion que no este bloqueada ahora mismo (si encuentra alguna),
        // para garantizar que nunca quede trabada para siempre.
        bool hardUnstuckTriggered = false;
        if (enemy.stuckFrames > kHardUnstuckFrames) {
            Direction bestDir = enemy.moveDir;
            bool foundOpen = false;
            for (int tries = 0; tries < 4; ++tries) {
                const Direction candidate = RandomDirection();
                if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                    bestDir = candidate;
                    foundOpen = true;
                    break;
                }
            }
            if (!foundOpen) {
                bestDir = RandomDirection();
            }
            float ddx = 0.0f, ddy = 0.0f;
            DirectionVector(bestDir, ddx, ddy);
            enemy.tank.SetPosition(std::round(enemy.tank.X()) + ddx, std::round(enemy.tank.Y()) + ddy);
            enemy.tank.SetFacing(bestDir);
            enemy.moveDir = bestDir;
            enemy.stuckFrames = 0;
            enemy.debugMode = 'U';
            hardUnstuckTriggered = true;
        }

        bool breachingNow = false;
        bool alignedStandStill = false;
        if (hardUnstuckTriggered) {
            // Ya se repuso a la fuerza este frame: no reconsiderar de nuevo.
        } else if (killMode) {
            const float pdx = target->X() - enemy.tank.X();
            const float pdy = target->Y() - enemy.tank.Y();
            const float distToTarget = std::sqrt(pdx * pdx + pdy * pdy);

            if (distToTarget <= kAdjacentSnapRadius) {
                // Ya esta pegada: se queda quieta encarandolo para
                // dispararle (avanzar chocaria de lleno contra el; sin
                // input de movimiento, Tank::Update no puede activar el
                // asistente de deslizamiento de esquinas que la corria de
                // costado en un choque frontal contra un tanque).
                constexpr float kAlignEpsilon = 0.05f;
                enemy.debugMode = 'K';
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
                // Persigue derecho: eje de mayor diferencia primero, el otro
                // como alternativa si el principal esta bloqueado.
                const bool horizontalIsBigger = std::fabs(pdx) >= std::fabs(pdy);
                const Direction primary = horizontalIsBigger
                    ? (pdx >= 0.0f ? Direction::Right : Direction::Left)
                    : (pdy >= 0.0f ? Direction::Down : Direction::Up);
                const Direction secondary = horizontalIsBigger
                    ? (pdy >= 0.0f ? Direction::Down : Direction::Up)
                    : (pdx >= 0.0f ? Direction::Right : Direction::Left);

                Direction chosenDir = primary;
                bool found = false;
                if (!IsStaticImpassable(enemy.tank, primary, map) && !IsTankAhead(enemy.tank, primary, others)) {
                    chosenDir = primary;
                    found = true;
                } else if (!IsStaticImpassable(enemy.tank, secondary, map) && !IsTankAhead(enemy.tank, secondary, others)) {
                    chosenDir = secondary;
                    found = true;
                }

                // El camino directo pasa por un ladrillo: se planta a
                // romperlo en vez de rodearlo (persiguiendo, si hay apuro).
                const bool wantsBreach = found && IsBrickAhead(enemy.tank, chosenDir, map);
                breachingNow = wantsBreach && enemy.stuckFrames <= kBreachPatienceFrames;
                const bool stuck = !breachingNow && enemy.stuckFrames > kDodgeStuckFrames;

                if (breachingNow) {
                    enemy.moveDir = chosenDir;
                    enemy.debugMode = 'B';
                } else if (stuck) {
                    enemy.debugMode = 'D';
                    constexpr std::array<Direction, kDodgeCandidateCount> kAllDirs{Direction::Up, Direction::Down, Direction::Left, Direction::Right};
                    const int dodgeStage = (enemy.stuckFrames - kDodgeStuckFrames) / kDodgeRotateFrames;
                    Direction dodgeChosen = kAllDirs[dodgeStage % kDodgeCandidateCount];
                    bool foundClear = false;
                    for (int tries = 0; tries < kDodgeCandidateCount; ++tries) {
                        const Direction candidate = kAllDirs[(dodgeStage + tries) % kDodgeCandidateCount];
                        if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                            dodgeChosen = candidate;
                            foundClear = true;
                            break;
                        }
                    }
                    if (!foundClear && found && IsBrickAhead(enemy.tank, chosenDir, map)) {
                        dodgeChosen = chosenDir;
                        enemy.debugMode = 'B';
                    }
                    enemy.moveDir = dodgeChosen;
                } else if (found) {
                    enemy.debugMode = 'K';
                    enemy.moveDir = chosenDir;
                } else {
                    // Los 2 ejes bloqueados de frente (pared, bloque u otro
                    // tanque, el jugador perseguido incluido, si todavia no
                    // llego al radio de plantarse): en vez de esperar el
                    // esquive por atascamiento (kDodgeStuckFrames), prueba
                    // ya una direccion distinta al azar que si este libre,
                    // igual que la patrulla y el esquive del "Basico".
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
        } else {
            // Patrulla: caminata al azar por todo el escenario, sin ningun
            // objetivo. Reusa deviationDir/deviationCellsRemaining como el
            // tramo recto actual (mismos campos que el "Basico" usa para su
            // desvio ocasional, aca es el comportamiento principal).
            const int cellX = static_cast<int>(std::floor(enemy.tank.X() + 0.5f));
            const int cellY = static_cast<int>(std::floor(enemy.tank.Y() + 0.5f));
            if (cellX != enemy.decisionCellX || cellY != enemy.decisionCellY) {
                enemy.decisionCellX = cellX;
                enemy.decisionCellY = cellY;
                if (enemy.deviationCellsRemaining > 0) {
                    --enemy.deviationCellsRemaining;
                }
            }

            // Si intenta avanzar y algo de frente no la deja (ladrillo,
            // acero, agua u otro tanque: ver WouldBeBlocked, que a
            // diferencia de IsStaticImpassable SI cuenta el ladrillo como
            // bloqueo, porque patrullando no rompe nada), se desvia YA a
            // una direccion distinta al azar, sin esperar a terminar el
            // tramo ni a quedar "atascada" un rato primero.
            bool needNewDir = enemy.deviationCellsRemaining <= 0;
            if (!needNewDir && WouldBeBlocked(enemy.tank, enemy.deviationDir, map, others)) {
                needNewDir = true;
            }
            if (needNewDir) {
                Direction candidate = enemy.deviationDir;
                bool ok = false;
                for (int tries = 0; tries < 4; ++tries) {
                    candidate = RandomDirection();
                    if (!WouldBeBlocked(enemy.tank, candidate, map, others)) {
                        ok = true;
                        break;
                    }
                }
                enemy.deviationDir = candidate; // si ninguna dio libre (rincon sin salida), se usa igual: el proximo frame se vuelve a intentar
                enemy.deviationCellsRemaining = RandomPatrolLength();
                (void)ok;
            }

            enemy.moveDir = enemy.deviationDir;
            enemy.debugMode = 'P';
        }

        PlayerInput input;
        if (alignedStandStill) {
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
        // mantiene exactamente en su columna (X entero).
        if (enemy.moveDir == Direction::Left || enemy.moveDir == Direction::Right) {
            enemy.tank.SetPosition(enemy.tank.X(), std::round(enemy.tank.Y()));
        } else {
            enemy.tank.SetPosition(std::round(enemy.tank.X()), enemy.tank.Y());
        }

        const float movedDist = std::fabs(enemy.tank.X() - prevX) + std::fabs(enemy.tank.Y() - prevY);
        const bool moved = movedDist > 0.0005f;
        enemy.stuckFrames = moved ? 0 : (enemy.stuckFrames + 1);

        // --- Disparo: cada 5s patrullando, cada 2s en modo matar (fijo, ver
        // kPatrolShootInterval/kKillShootInterval), siempre para donde este
        // mirando en ese momento. Solo 1 bala propia a la vez (ver
        // BulletSystem::TryShoot); el temporizador arranca justo cuando la
        // bala anterior impacta/desaparece, no en paralelo.
        const bool bulletAliveNow = bullets.HasAliveBullet(enemy.ownerId);
        if (enemy.bulletWasAlive && !bulletAliveNow) {
            enemy.shootTimer = killMode ? kKillShootInterval : kPatrolShootInterval;
        }
        enemy.bulletWasAlive = bulletAliveNow;

        if (!bulletAliveNow && enemy.tank.CanShoot()) {
            // En modo matar, apenas queda encarando al jugador sobre su
            // misma fila/columna (persiguiendolo o ya plantada), dispara ya
            // mismo sin esperar el temporizador de kKillShootInterval.
            const bool killAimed = killMode && target != nullptr &&
                IsFacingTarget(enemy.tank, target->X() + 0.5f, target->Y() + 0.5f, kPlayerAlignTolerance);

            enemy.shootTimer -= dt;
            const bool timerShot = enemy.shootTimer <= 0.0;

            if (killAimed || timerShot) {
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed() * kFastBulletSpeedMultiplier, 1, 1);

                if (killMode) {
                    // Ya disparo en modo matar: corta el enganche aca mismo
                    // en vez de seguir persiguiendo/disparando sin parar.
                    enemy.killModeTimer = 0.0;
                    enemy.detectionLockoutTimer = kDetectionLockoutDuration;
                    enemy.deviationCellsRemaining = 0; // fuerza un rumbo nuevo al volver a patrullar
                }
            }
        }
    }
}

} // namespace bc
