#include "EnemySystem.h"

#include <array>
#include <cmath>
#include <random>

#include "BulletImpactSystem.h"
#include "BulletSystem.h"

namespace bc {

namespace {
// Cuantos frames seguidos sin moverse antes de probar el eje perpendicular
// (esquivar) y, si sigue sin poder, disparar al ladrillo que tenga enfrente.
constexpr int kDodgeStuckFrames = 20;  // ~0.33s a 60fps
constexpr int kShootStuckFrames = 45;  // ~0.75s a 60fps: le da tiempo a esquivar antes de tirotear
// Si sigue trabada despues de probar una direccion de esquive, cuanto espera
// antes de probar la siguiente (rota entre las 4, ver kDodgeCandidateCount).
constexpr int kDodgeRotateFrames = 15;  // ~0.25s a 60fps
constexpr int kDodgeCandidateCount = 4;

// Cuanto tiempo se queda comprometida con una direccion antes de poder
// re-elegir hacia el objetivo (ver Enemy::redecideTimer). Un rango, no un
// valor fijo, para que no giren todas sincronizadas. Bastante mas amplio que
// antes: para que a veces se note una excursion larga hacia un costado (o
// hacia arriba) antes de volver a orientarse hacia el objetivo.
constexpr double kRedecideMinInterval = 0.4;
constexpr double kRedecideMaxInterval = 2.0;
// Cerca del aguila (ver kBaseAggroRadius) se re-orienta mas seguido, para que
// la persecucion se sienta mas insistente en vez de seguir deambulando.
constexpr double kRedecideMaxIntervalAggro = 0.9;

// Distancia (celdas, centro a centro) a la que el aguila pasa a ser una
// prioridad activa (ver PickAggressiveWanderDirection): sigue sin ser un
// camino 100% directo, pero mucho mas insistente que el deambular normal.
constexpr float kBaseAggroRadius = 4.0f;
// Al esquivar (ver kDodgeStuckFrames) se compromete un rato mas corto: solo
// lo suficiente para despegarse del obstaculo, no para alejarse del objetivo.
constexpr double kRedecideDodgeInterval = 0.35;

// Cada cuanto intenta un disparo "porque si", sin necesitar estar trabada ni
// tener a un jugador enfilado (asi se siente mas vivo, como el original).
constexpr double kRandomShootMinInterval = 1.2;
constexpr double kRandomShootMaxInterval = 3.5;

std::mt19937& Rng() {
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

double RandomRedecideInterval(bool aggro) {
    std::uniform_real_distribution<double> dist(kRedecideMinInterval, aggro ? kRedecideMaxIntervalAggro : kRedecideMaxInterval);
    return dist(Rng());
}

double RandomShootInterval() {
    std::uniform_real_distribution<double> dist(kRandomShootMinInterval, kRandomShootMaxInterval);
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

// Elige una de las 4 direcciones con tendencia hacia el objetivo, pero sin
// ser tan directa: preferred/fallback (los 2 ejes que acercan) pesan mas,
// pero tambien puede alejarse un poco (mas seguido en el eje secundario, que
// es lo que se nota como "a veces sube" o "se va mas largo para el costado"
// cuando el objetivo esta abajo/al costado).
// aggro=true: cerca del aguila (ver kBaseAggroRadius), mucho mas insistente
// en cerrar distancia (85% de las veces por uno de los 2 ejes que acercan),
// pero sin llegar a ser un camino 100% directo como al atacar a un jugador.
Direction PickWanderDirection(Direction preferred, Direction fallback, bool aggro) {
    const Direction awayFromFallback = Opposite(fallback);
    const Direction awayFromPreferred = Opposite(preferred);
    const std::array<Direction, 4> options{preferred, fallback, awayFromFallback, awayFromPreferred};
    if (aggro) {
        std::discrete_distribution<int> dist({60.0, 25.0, 12.0, 3.0});
        return options[dist(Rng())];
    }
    std::discrete_distribution<int> dist({45.0, 27.0, 20.0, 8.0});
    return options[dist(Rng())];
}

// Hay un ladrillo justo pegado al frente del tanque, en la direccion que mira.
bool IsFacingBrick(const TileMap& map, const Tank& tank) {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    tank.GetBounds(left, right, top, bottom);
    float checkX = (left + right) * 0.5f;
    float checkY = (top + bottom) * 0.5f;
    switch (tank.Facing()) {
        case Direction::Up:    checkY = top - 0.1f; break;
        case Direction::Down:  checkY = bottom + 0.1f; break;
        case Direction::Left:  checkX = left - 0.1f; break;
        case Direction::Right: checkX = right + 0.1f; break;
    }
    const int cx = static_cast<int>(std::floor(checkX));
    const int cy = static_cast<int>(std::floor(checkY));
    if (!map.InBounds(cx, cy)) {
        return false;
    }
    return map.At(cx, cy).type == TileType::Brick;
}

// El tanque esta mirando hacia el objetivo (mismo pasillo, en la direccion
// correcta): suficiente para animo de disparo simple, sin trazar linea de
// vision real contra obstaculos (la IA no necesita puntaria perfecta).
bool IsFacingTarget(const Tank& tank, float targetX, float targetY) {
    constexpr float kAlignTolerance = 0.4f;
    const float cx = tank.X() + 0.5f;
    const float cy = tank.Y() + 0.5f;
    switch (tank.Facing()) {
        case Direction::Up:    return std::fabs(cx - targetX) < kAlignTolerance && targetY < cy;
        case Direction::Down:  return std::fabs(cx - targetX) < kAlignTolerance && targetY > cy;
        case Direction::Left:  return std::fabs(cy - targetY) < kAlignTolerance && targetX < cx;
        case Direction::Right: return std::fabs(cy - targetY) < kAlignTolerance && targetX > cx;
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
    enemy.randomShootTimer = RandomShootInterval();
    enemies_.push_back(enemy);
}

void EnemySystem::Update(double dt, TileMap& map, BulletSystem& bullets, BulletImpactSystem& impacts, const std::vector<Tank*>& playerTanks, float baseX, float baseY) {
    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& enemy = enemies_[i];
        if (!enemy.alive) {
            continue;
        }

        enemy.tank.TickShootCooldown(dt);

        // Muere de un solo impacto de bala de jugador, sin importar el nivel.
        float eLeft = 0.0f, eRight = 0.0f, eTop = 0.0f, eBottom = 0.0f;
        enemy.tank.GetBounds(eLeft, eRight, eTop, eBottom);
        std::vector<int> hitLevels;
        if (bullets.KillBulletsHittingBox(enemy.ownerId, eLeft, eRight, eTop, eBottom, impacts, hitLevels)) {
            enemy.alive = false;
            continue;
        }

        // Deteccion: el jugador activo mas cercano dentro del radio.
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

        const float targetX = (enemy.state == EnemyState::AttackPlayer) ? (nearestPlayer->X() + 0.5f) : (baseX + 0.5f);
        const float targetY = (enemy.state == EnemyState::AttackPlayer) ? (nearestPlayer->Y() + 0.5f) : (baseY + 0.5f);

        // Eje preferido: el de mayor diferencia hacia el objetivo.
        const float dx = targetX - ex;
        const float dy = targetY - ey;
        const bool preferHorizontal = std::fabs(dx) >= std::fabs(dy);
        const Direction horizDir = (dx >= 0.0f) ? Direction::Right : Direction::Left;
        const Direction vertDir = (dy >= 0.0f) ? Direction::Down : Direction::Up;
        const Direction preferred = preferHorizontal ? horizDir : vertDir;
        const Direction fallback = preferHorizontal ? vertDir : horizDir;

        // Cerca del aguila (buscandola, no atacando a un jugador): prioridad
        // mas activa a destruirla, aunque sin ser un camino perfectamente
        // directo (ver PickWanderDirection).
        const float distToTarget = std::sqrt(dx * dx + dy * dy);
        const bool baseAggro = (enemy.state == EnemyState::SeekBase) && (distToTarget <= kBaseAggroRadius);

        // Fiel al original: se mueve en linea recta un rato en vez de
        // re-apuntar al objetivo cada frame (eso, con pasos tan chicos,
        // termina viendose como si caminara en diagonal). Solo re-elige
        // direccion cuando se le acaba el compromiso, cuando cambia de
        // prioridad (SeekBase <-> AttackPlayer) o cuando esta atascada
        // (ver stuckFrames), momento en que prueba una direccion distinta a
        // la bloqueada para esquivar el obstaculo.
        const bool stuck = enemy.stuckFrames > kDodgeStuckFrames;
        const bool stateChanged = enemy.state != enemy.previousState;
        enemy.previousState = enemy.state;
        enemy.redecideTimer -= dt;
        if (stuck) {
            // Si la primera direccion de esquive tampoco funciona, prueba la
            // siguiente (rota entre las 4 posibles) en vez de insistir para
            // siempre con la misma direccion bloqueada.
            const int dodgeStage = (enemy.stuckFrames - kDodgeStuckFrames) / kDodgeRotateFrames;
            const std::array<Direction, kDodgeCandidateCount> dodgeCandidates{fallback, Opposite(fallback), Opposite(preferred), preferred};
            enemy.moveDir = dodgeCandidates[dodgeStage % kDodgeCandidateCount];
            enemy.redecideTimer = kRedecideDodgeInterval;
        } else if (enemy.redecideTimer <= 0.0 || stateChanged) {
            // Al atacar a un jugador va derecho a el (sin vueltas); buscando
            // el aguila, deambula con tendencia hacia ella, mas insistente
            // si ya la tiene cerca (ver baseAggro).
            enemy.moveDir = (enemy.state == EnemyState::AttackPlayer) ? preferred : PickWanderDirection(preferred, fallback, baseAggro);
            enemy.redecideTimer = RandomRedecideInterval(baseAggro);
        }

        PlayerInput input;
        switch (enemy.moveDir) {
            case Direction::Up:    input.moveUp = true;    break;
            case Direction::Down:  input.moveDown = true;  break;
            case Direction::Left:  input.moveLeft = true;  break;
            case Direction::Right: input.moveRight = true; break;
        }

        std::vector<Tank*> others = playerTanks;
        for (size_t j = 0; j < enemies_.size(); ++j) {
            if (j == i || !enemies_[j].alive) {
                continue;
            }
            others.push_back(&enemies_[j].tank);
        }

        const float prevX = enemy.tank.X();
        const float prevY = enemy.tank.Y();
        enemy.tank.Update(dt, input, map, others);
        const bool moved = std::fabs(enemy.tank.X() - prevX) > 0.0005f || std::fabs(enemy.tank.Y() - prevY) > 0.0005f;
        enemy.stuckFrames = moved ? 0 : (enemy.stuckFrames + 1);

        // Dispara para abrirse paso si esta atascada hace rato contra un
        // ladrillo, oportunamente si esta atacando a un jugador (o tiene al
        // aguila cerca y encarada, ver baseAggro) y lo tiene enfilado, o de
        // vez en cuando porque si (ver randomShootTimer), sin importar hacia
        // donde este mirando.
        enemy.randomShootTimer -= dt;
        const bool wantsRandomShot = enemy.randomShootTimer <= 0.0;
        const bool facingPriorityTarget =
            (enemy.state == EnemyState::AttackPlayer || baseAggro) && IsFacingTarget(enemy.tank, targetX, targetY);
        const bool shouldShoot =
            wantsRandomShot ||
            (enemy.stuckFrames > kShootStuckFrames && IsFacingBrick(map, enemy.tank)) ||
            facingPriorityTarget;

        if (shouldShoot && enemy.tank.CanShoot()) {
            float muzzleX = 0.0f, muzzleY = 0.0f;
            enemy.tank.MuzzlePosition(muzzleX, muzzleY);
            if (bullets.TryShoot(enemy.ownerId, muzzleX, muzzleY, enemy.tank.Facing(), enemy.tank.BulletSpeed(), 1, 1)) {
                if (enemy.stuckFrames > kShootStuckFrames) {
                    enemy.stuckFrames = 0; // le dio una oportunidad de abrirse paso
                }
                enemy.randomShootTimer = RandomShootInterval();
            }
        }
    }
}

} // namespace bc
