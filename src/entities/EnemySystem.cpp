#include "EnemySystem.h"

#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"
#include "SpecialExplosionSystem.h"

namespace bc {

namespace {
// Cuantos frames seguidos sin moverse antes de considerarla atascada: prueba
// una direccion distinta a la bloqueada (rota entre las 4) y dispara mas
// seguido mientras dure (ver kShootIntervalStuck*).
constexpr int kDodgeStuckFrames = 20;  // ~0.33s a 60fps
// Si la direccion de esquive elegida sigue trabada despues de un rato, prueba
// la siguiente (siempre que kDodgeCandidateCount candidatas).
constexpr int kDodgeRotateFrames = 15;  // ~0.25s a 60fps
constexpr int kDodgeCandidateCount = 4;

// Tramo recto: cuantas celdas recorre en una direccion antes de re-elegir.
// "focused" (ver kBaseAggroRadius) re-elige mucho mas seguido, para que la
// persecucion se note mas insistente en vez de seguir de largo un buen rato.
constexpr int kMinCellsPerLeg = 1;
constexpr int kMaxCellsPerLeg = 5;
constexpr int kMinCellsPerLegFocused = 1;
constexpr int kMaxCellsPerLegFocused = 2;
// Pegada al aguila: re-elige cada celda, sin comprometerse a un tramo largo.
constexpr int kCellsPerLegBaseAggro = 1;

// Cerca del aguila (buscandola, no atacando a un jugador) su atencion se
// concentra mas en llegar a ella (ver ChooseDirection, "focused").
constexpr float kBaseAggroRadius = 5.0f;

// Pesos [principal, secundario, aleja-secundario, aleja-principal] segun que
// tan enfocada esta la IA. Lejos: tendencia pareja hacia el objetivo, con
// harto margen para deambular. Enfocada (atacando a un jugador): bastante
// mas insistente. Con el aguila encima (ver kBaseAggroRadius): casi sin
// margen para deambular, muy decidida.
constexpr std::array<double, 4> kWanderWeights{45.0, 45.0, 5.0, 5.0};
constexpr std::array<double, 4> kFocusedWeights{60.0, 35.0, 3.0, 2.0};
constexpr std::array<double, 4> kBaseAggroWeights{88.0, 10.0, 1.0, 1.0};

// Que tan lejos por delante del tanque se chequea si una direccion esta
// bloqueada antes de comprometerse a ella (ver WouldBeBlocked).
constexpr float kLookaheadStep = 0.2f;

// "Reflejo" de cuerpo a cuerpo: si un jugador entra a 1 casilla de distancia,
// 50% de chance de girar a encararlo y dispararle de una para matarlo, sin
// esperar el temporizador normal (ver Update). Se sortea una sola vez por
// acercamiento (ver Enemy::playerWasAdjacent), no en cada frame que se queda
// pegado.
constexpr float kAdjacentSnapRadius = 1.0f;
constexpr double kAdjacentSnapChance = 0.50;

// Buscando el aguila (no atacando a un jugador) y a 2 casillas o mas: en
// cada disparo "de rutina" (no uno ya encarado, ver aimedShot), 40% de
// chance de girar a apuntarle antes de tirar, en vez de disparar para
// donde este mirando.
constexpr float kBaseSnapShotMinRadius = 2.0f;
constexpr double kBaseSnapShotChance = 0.40;

// Cuanto tarda el proximo disparo despues de que el anterior impacto o
// desaparecio (ver Enemy::shootTimer/bulletWasAlive): normal, mas seguido
// atacando a un jugador, y mas seguido todavia si esta atascada.
constexpr double kShootIntervalMin = 3.0;
constexpr double kShootIntervalMax = 5.0;
constexpr double kShootIntervalAttackMin = 1.0;
constexpr double kShootIntervalAttackMax = 2.0;
constexpr double kShootIntervalStuckMin = 0.4;
constexpr double kShootIntervalStuckMax = 1.0;
// Cerca del aguila (ver kBaseAggroRadius), como fallback para cuando todavia
// no la tiene encarada (ver aimedShot en Update: si la tiene encarada, ni
// siquiera espera este intervalo).
constexpr double kShootIntervalBaseAggroMin = 0.8;
constexpr double kShootIntervalBaseAggroMax = 1.5;

// Tolerancia de alineacion para el disparo "ya encarado" (ver aimedShot):
// contra un jugador (blanco chico y movil) se pide bastante precision;
// contra el aguila, mucho mas laxo — no hace falta estar perfilado, alcanza
// con estar en una posicion desde la que la bala pueda llegar a pegarle.
constexpr float kPlayerAlignTolerance = 0.4f;
constexpr float kBaseAlignTolerance = 0.9f;

// Si el camino directo hacia el aguila esta bloqueado por LADRILLO (no
// acero, agua u otro tanque), se queda plantada encarandolo y lo tirotea en
// vez de rodearlo, hasta este limite de frames atascada (despues, se rinde
// y vuelve al esquive normal por las dudas).
constexpr int kBrickBreachPatienceFrames = 240;  // ~4s a 60fps

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

int RandomCellsPerLeg() {
    std::uniform_int_distribution<int> dist(kMinCellsPerLeg, kMaxCellsPerLeg);
    return dist(Rng());
}

int RandomCellsPerLegFocused() {
    std::uniform_int_distribution<int> dist(kMinCellsPerLegFocused, kMaxCellsPerLegFocused);
    return dist(Rng());
}

double RandomInRange(double lo, double hi) {
    std::uniform_real_distribution<double> dist(lo, hi);
    return dist(Rng());
}

bool RollChance(double probability) {
    std::bernoulli_distribution dist(probability);
    return dist(Rng());
}

Direction Opposite(Direction dir) {
    switch (dir) {
        case Direction::Up:    return Direction::Down;
        case Direction::Down:  return Direction::Up;
        case Direction::Left:  return Direction::Right;
        case Direction::Right: return Direction::Left;
    }
    return dir;
}

// Un paso chico en esa direccion desde donde esta ahora chocaria contra el
// mapa o contra otro tanque: razona sobre el obstaculo antes de comprometerse
// a una direccion, en vez de solo reaccionar despues de quedar atascada.
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

// Hay un ladrillo (destructible, a diferencia del acero) justo pegado al
// frente en esa direccion: a diferencia de WouldBeBlocked, distingue el
// material porque a un ladrillo conviene plantarse y tirotearlo, mientras
// que a un bloqueo de acero/agua/tanque conviene rodearlo (ver Update).
bool IsBrickAhead(const Tank& tank, Direction dir, const TileMap& map) {
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
        return false;
    }
    return map.At(cx, cy).type == TileType::Brick;
}

// Elige una de las 4 direcciones con tendencia hacia el objetivo segun los
// pesos [principal, secundario, aleja-secundario, aleja-principal] que le
// pasen (ver kWanderWeights/kFocusedWeights/kBaseAggroWeights). Antes de
// sortear, descarta las direcciones que chocarian de una (ver
// WouldBeBlocked) repartiendo su peso entre las que quedan libres.
Direction ChooseDirection(const Tank& tank, const TileMap& map, const std::vector<Tank*>& others, Direction primary, Direction secondary, const std::array<double, 4>& baseWeights) {
    const Direction awaySecondary = Opposite(secondary);
    const Direction awayPrimary = Opposite(primary);
    const std::array<Direction, 4> options{primary, secondary, awaySecondary, awayPrimary};

    std::array<double, 4> weights{0.0, 0.0, 0.0, 0.0};
    double total = 0.0;
    for (int i = 0; i < 4; ++i) {
        if (!WouldBeBlocked(tank, options[i], map, others)) {
            weights[i] = baseWeights[i];
            total += weights[i];
        }
    }
    if (total <= 0.0) {
        // Las 4 direcciones chocan (rincon/callejon sin salida): elige igual
        // la de mas peso nominal, el esquive por atascamiento se hace cargo
        // apenas quede trabada de verdad.
        int best = 0;
        for (int i = 1; i < 4; ++i) {
            if (baseWeights[i] > baseWeights[best]) {
                best = i;
            }
        }
        return options[best];
    }
    std::discrete_distribution<int> dist(weights.begin(), weights.end());
    return options[dist(Rng())];
}

// El tanque esta mirando hacia el objetivo (mismo pasillo, en la direccion
// correcta): suficiente para animo de disparo simple, sin trazar linea de
// vision real contra obstaculos (la IA no necesita puntaria perfecta).
// alignTolerance es cuanto puede estar corrida la otra coordenada y seguir
// contando como "encarado" (celdas): con el jugador se pide bastante
// precision (kPlayerAlignTolerance); con el aguila es mucho mas laxo (ver
// kBaseAlignTolerance) porque no hace falta estar perfilado de una, solo
// parado en una posicion desde la que la bala pueda llegar a pegarle.
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

void EnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, SpecialExplosionSystem& specialExplosions, const std::vector<Tank*>& playerTanks, float baseX, float baseY) {
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

        // Deteccion: el jugador activo mas cercano dentro del radio. Fuera de
        // ese radio (se alejo), vuelve a su mision principal: el aguila.
        const float ex = enemy.tank.X() + 0.5f;
        const float ey = enemy.tank.Y() + 0.5f;
        Tank* nearestPlayer = nullptr;
        float nearestDist = kEnemyDetectionRadius;
        for (Tank* p : playerTanks) {
            if (p == nullptr) {
                continue;
            }
            const float px = p->X() + 0.5f;
            const float py = p->Y() + 0.5f;
            const float d = std::sqrt((px - ex) * (px - ex) + (py - ey) * (py - ey));
            if (d <= kEnemyDetectionRadius && d < nearestDist) {
                nearestDist = d;
                nearestPlayer = p;
            }
        }
        enemy.state = (nearestPlayer != nullptr) ? EnemyState::AttackPlayer : EnemyState::SeekBase;
        const bool stateChanged = enemy.state != enemy.previousState;
        enemy.previousState = enemy.state;

        // Reflejo de cuerpo a cuerpo: un jugador acaba de entrar a 1 casilla
        // (ver kAdjacentSnapRadius). Se sortea una sola vez por acercamiento
        // (playerWasAdjacent detecta el flanco de entrada), no en cada frame.
        const bool playerAdjacentNow = (nearestPlayer != nullptr) && (nearestDist <= kAdjacentSnapRadius);
        if (playerAdjacentNow && !enemy.playerWasAdjacent && RollChance(kAdjacentSnapChance)) {
            const float pdx = nearestPlayer->X() - enemy.tank.X();
            const float pdy = nearestPlayer->Y() - enemy.tank.Y();
            const Direction snapDir = (std::fabs(pdx) >= std::fabs(pdy))
                ? (pdx >= 0.0f ? Direction::Right : Direction::Left)
                : (pdy >= 0.0f ? Direction::Down : Direction::Up);
            enemy.tank.SetFacing(snapDir);
            if (!bullets.HasAliveBullet(enemy.ownerId) && enemy.tank.CanShoot()) {
                float muzzleX = 0.0f, muzzleY = 0.0f;
                enemy.tank.MuzzlePosition(muzzleX, muzzleY);
                bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, snapDir, enemy.tank.BulletSpeed(), 1, 1);
            }
        }
        enemy.playerWasAdjacent = playerAdjacentNow;

        const float targetX = (enemy.state == EnemyState::AttackPlayer) ? (nearestPlayer->X() + 0.5f) : (baseX + 0.5f);
        const float targetY = (enemy.state == EnemyState::AttackPlayer) ? (nearestPlayer->Y() + 0.5f) : (baseY + 0.5f);
        const float dx = targetX - ex;
        const float dy = targetY - ey;
        const float distToTarget = std::sqrt(dx * dx + dy * dy);

        // Lejos, el eje horizontal siempre es el principal (tendencia fija).
        // Pegada al aguila (ver kBaseAggroRadius), en cambio, el principal
        // pasa a ser el que tenga mayor diferencia real: si ya esta a la
        // misma altura pero lejos de costado, ataca por el costado en vez de
        // insistir en bajar (antes solo la atacaba por arriba).
        const Direction fixedPrimary = (dx >= 0.0f) ? Direction::Right : Direction::Left;
        const Direction fixedSecondary = (dy >= 0.0f) ? Direction::Down : Direction::Up;
        const bool baseAggro = (enemy.state == EnemyState::SeekBase) && (distToTarget <= kBaseAggroRadius);
        const bool focused = (enemy.state == EnemyState::AttackPlayer) || baseAggro;
        const bool horizontalIsBigger = std::fabs(dx) >= std::fabs(dy);
        const Direction primary = baseAggro ? (horizontalIsBigger ? fixedPrimary : fixedSecondary) : fixedPrimary;
        const Direction secondary = baseAggro ? (horizontalIsBigger ? fixedSecondary : fixedPrimary) : fixedSecondary;
        const std::array<double, 4>& weights = baseAggro ? kBaseAggroWeights : (focused ? kFocusedWeights : kWanderWeights);

        std::vector<Tank*> others = playerTanks;
        for (size_t j = 0; j < enemies_.size(); ++j) {
            if (j == i || !enemies_[j].alive) {
                continue;
            }
            others.push_back(&enemies_[j].tank);
        }

        const bool stuck = enemy.stuckFrames > kDodgeStuckFrames;
        // Cerca del aguila, con el camino directo tapado por un ladrillo
        // (se puede volar a tiros): se queda plantada encarandolo y
        // disparando en vez de rodearlo, mientras no se pase de paciencia
        // (por si algo raro pasa y nunca se abre paso, ver kBrickBreachPatienceFrames).
        const bool breachingBrick = baseAggro && enemy.stuckFrames <= kBrickBreachPatienceFrames && IsBrickAhead(enemy.tank, primary, map);
        if (breachingBrick) {
            enemy.moveDir = primary;
            enemy.tank.SetFacing(primary);
            enemy.cellsRemaining = static_cast<float>(kCellsPerLegBaseAggro);
        } else if (stuck) {
            // Si la primera direccion de esquive tambien choca de una, salta
            // a la siguiente candidata (hasta las 4) en vez de comprometerse
            // a algo que ya se sabe bloqueado.
            const std::array<Direction, kDodgeCandidateCount> dodgeCandidates{secondary, Opposite(secondary), Opposite(primary), primary};
            int dodgeStage = (enemy.stuckFrames - kDodgeStuckFrames) / kDodgeRotateFrames;
            Direction chosen = dodgeCandidates[dodgeStage % kDodgeCandidateCount];
            for (int tries = 0; tries < kDodgeCandidateCount && WouldBeBlocked(enemy.tank, chosen, map, others); ++tries) {
                ++dodgeStage;
                chosen = dodgeCandidates[dodgeStage % kDodgeCandidateCount];
            }
            enemy.moveDir = chosen;
            enemy.cellsRemaining = static_cast<float>(baseAggro ? kCellsPerLegBaseAggro : RandomCellsPerLeg());
        } else if (enemy.cellsRemaining <= 0.0f || stateChanged) {
            enemy.moveDir = ChooseDirection(enemy.tank, map, others, primary, secondary, weights);
            enemy.cellsRemaining = static_cast<float>(baseAggro ? kCellsPerLegBaseAggro : (focused ? RandomCellsPerLegFocused() : RandomCellsPerLeg()));
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
        const float movedDist = std::fabs(enemy.tank.X() - prevX) + std::fabs(enemy.tank.Y() - prevY);
        const bool moved = movedDist > 0.0005f;
        enemy.stuckFrames = moved ? 0 : (enemy.stuckFrames + 1);
        enemy.cellsRemaining -= movedDist;

        // Solo 1 bala propia a la vez (ver BulletSystem::TryShoot). El
        // temporizador del proximo disparo arranca justo cuando la bala
        // anterior impacta/desaparece (bulletWasAlive detecta esa
        // transicion), no en paralelo mientras sigue en pantalla.
        const bool bulletAliveNow = bullets.HasAliveBullet(enemy.ownerId);
        if (enemy.bulletWasAlive && !bulletAliveNow) {
            const bool stuckALot = enemy.stuckFrames > kDodgeStuckFrames;
            if (stuckALot) {
                enemy.shootTimer = RandomInRange(kShootIntervalStuckMin, kShootIntervalStuckMax);
            } else if (enemy.state == EnemyState::AttackPlayer) {
                enemy.shootTimer = RandomInRange(kShootIntervalAttackMin, kShootIntervalAttackMax);
            } else if (baseAggro) {
                enemy.shootTimer = RandomInRange(kShootIntervalBaseAggroMin, kShootIntervalBaseAggroMax);
            } else {
                enemy.shootTimer = RandomInRange(kShootIntervalMin, kShootIntervalMax);
            }
        }
        enemy.bulletWasAlive = bulletAliveNow;

        if (!bulletAliveNow && enemy.tank.CanShoot()) {
            // Enfocada (atacando a un jugador o con el aguila cerca) y ya la
            // tiene encarada: dispara ya, sin esperar el resto del intervalo
            // (que sigue sirviendo de todos modos mientras no este encarada).
            const bool aimedShot = focused && IsFacingTarget(enemy.tank, targetX, targetY, baseAggro ? kBaseAlignTolerance : kPlayerAlignTolerance);
            enemy.shootTimer -= dt;
            const bool timerShot = enemy.shootTimer <= 0.0;
            if (aimedShot || timerShot) {
                // Disparo de rutina (no uno ya encarado) buscando el aguila y
                // a 2 casillas o mas: a veces gira a apuntarle antes de tirar
                // en vez de disparar para donde este mirando (dx/dy ya
                // apuntan al aguila en este estado, ver targetX/targetY).
                if (!aimedShot && enemy.state == EnemyState::SeekBase && distToTarget >= kBaseSnapShotMinRadius && RollChance(kBaseSnapShotChance)) {
                    const Direction snapDir = (std::fabs(dx) >= std::fabs(dy))
                        ? (dx >= 0.0f ? Direction::Right : Direction::Left)
                        : (dy >= 0.0f ? Direction::Down : Direction::Up);
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
