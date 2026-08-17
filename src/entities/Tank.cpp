#include "Tank.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
constexpr double kAnimFrameDuration = 0.1; // segundos por frame de animacion
constexpr float kTankSize = 1.0f;          // el tanque ocupa 1 celda, como en el original

// Indexado por nivel-1 (nivel 1..4). Ver seccion 4.3.
// Nivel 2: velocidad de movimiento x1.5 y de disparo x2 respecto al nivel 1.
// Nivel 3: velocidad de movimiento x0.75 y de disparo x1.5 respecto al nivel 1.
// Nivel 4: velocidad de movimiento x0.5 y de disparo x1.75 respecto al nivel 1.
constexpr float kMoveSpeedByLevel[4] = {3.5f, 5.25f, 2.625f, 1.75f};
constexpr float kBulletSpeedByLevel[4] = {8.0f, 16.0f, 12.0f, 14.0f};
constexpr int kMaxBulletsByLevel[4] = {1, 1, 2, 2};
constexpr int kMaxWeaponLevel = 4;
constexpr double kOverheatLockSeconds = 5.0;
constexpr float kHeatPerShotByLevel[4] = {5.0f, 5.0f, 5.0f, 15.0f}; // nivel 4 calienta mas
constexpr float kHeatMax = 100.0f;
constexpr double kHeatDecayInterval = 0.5; // segundos entre cada paso de enfriado
constexpr float kHeatDecayStep = 5.0f;
constexpr int kChipHitsPerLevel = 2; // fuego amigo nivel 2 disparado por un rival nivel 2
constexpr float kRecoilSpeed = 5.0f; // celdas por segundo a las que se anima el retroceso

// Hielo (seccion custom): patina un poco al soltar el movimiento (sigue
// deslizando por inercia) y se enfria el doble de rapido.
constexpr float kIceCoastDecay = 0.94f;         // por frame: cuanto le queda del impulso patinando sin apretar nada
constexpr float kIceCoastStopEpsilon = 0.0008f; // celdas: por debajo de esto, se corta del todo
constexpr float kIceHeatDecayMultiplier = 2.0f;
}

void Tank::PickupStar() {
    if (weaponLevel_ < kMaxWeaponLevel) {
        ++weaponLevel_;
    } else {
        specialShotCharges_ = 1; // maximo 1 carga, no se acumula
    }
}

void Tank::PickupGun() {
    weaponLevel_ = kMaxWeaponLevel;
    specialShotCharges_ = 1;
}

bool Tank::HasSpecialShotReady() const {
    return weaponLevel_ == kMaxWeaponLevel && specialShotCharges_ > 0;
}

void Tank::ApplyWeaponLevelPenalty(int levels) {
    weaponLevel_ = std::max(1, weaponLevel_ - levels);
    if (weaponLevel_ < kMaxWeaponLevel) {
        specialShotCharges_ = 0;
    }
}

bool Tank::RegisterChipHit() {
    ++chipHitCount_;
    if (chipHitCount_ >= kChipHitsPerLevel) {
        chipHitCount_ = 0;
        ApplyWeaponLevelPenalty(1);
        return true;
    }
    return false;
}

void Tank::RegisterNormalShotHeat() {
    heatPercent_ = std::min(kHeatMax, heatPercent_ + kHeatPerShotByLevel[weaponLevel_ - 1]);
    if (heatPercent_ >= kHeatMax) {
        shootCooldownTimer_ = kOverheatLockSeconds; // se paso de calor: bloqueado, fijo en 100%
    }
}

void Tank::RegisterSpecialShotHeat() {
    heatPercent_ = kHeatMax; // el especial llena el contador de una
    shootCooldownTimer_ = kOverheatLockSeconds;
}

void Tank::TickHeatDecay(double dt) {
    if (shootCooldownTimer_ > 0.0 || heatPercent_ <= 0.0f) {
        return; // bloqueado por sobrecalentamiento (se mantiene fijo) o ya en 0
    }
    // onIce_ es el hielo del Update anterior (este se llama antes en
    // Game::UpdatePlayer): un frame de atraso, imperceptible a 60fps.
    const float step = onIce_ ? kHeatDecayStep * kIceHeatDecayMultiplier : kHeatDecayStep;
    heatDecayAccumulator_ += dt;
    while (heatDecayAccumulator_ >= kHeatDecayInterval && heatPercent_ > 0.0f) {
        heatDecayAccumulator_ -= kHeatDecayInterval;
        heatPercent_ = std::max(0.0f, heatPercent_ - step);
    }
}

void Tank::TickShootCooldown(double dt) {
    if (shootCooldownTimer_ > 0.0) {
        shootCooldownTimer_ = std::max(0.0, shootCooldownTimer_ - dt);
        if (shootCooldownTimer_ <= 0.0) {
            heatPercent_ = 0.0f; // termino el bloqueo: el contador vuelve a 0%
            heatDecayAccumulator_ = 0.0;
        }
    }
}

void Tank::TickFreeze(double dt) {
    if (freezeTimer_ > 0.0) {
        freezeTimer_ = std::max(0.0, freezeTimer_ - dt);
    }
}

float Tank::BulletSpeed() const {
    return kBulletSpeedByLevel[weaponLevel_ - 1];
}

int Tank::MaxBullets() const {
    return kMaxBulletsByLevel[weaponLevel_ - 1];
}

void Tank::TickShield(double dt) {
    if (shieldTimer_ > 0.0) {
        shieldTimer_ = std::max(0.0, shieldTimer_ - dt);
    }
}

bool Tank::ConsumeShootTrigger(const PlayerInput& input) {
    bool shouldFire = false;
    if (fireMode_ == FireMode::HoldToFire) {
        shouldFire = input.shoot;
    } else {
        shouldFire = input.shoot && !shootHeldLastFrame_;
    }
    shootHeldLastFrame_ = input.shoot;
    return shouldFire;
}

bool Tank::ConsumeSpecialShotTrigger(const PlayerInput& input) {
    const bool shouldFire = input.specialShoot && !specialShootHeldLastFrame_;
    specialShootHeldLastFrame_ = input.specialShoot;
    return shouldFire;
}

void Tank::MuzzlePosition(float& outX, float& outY) const {
    float dx = 0.0f, dy = 0.0f;
    DirectionVector(facing_, dx, dy);
    outX = x_ + 0.5f + dx * 0.5f;
    outY = y_ + 0.5f + dy * 0.5f;
}

void Tank::GetBounds(float& outLeft, float& outRight, float& outTop, float& outBottom) const {
    outLeft = x_;
    outRight = x_ + kTankSize;
    outTop = y_;
    outBottom = y_ + kTankSize;
}

void Tank::StartRecoil(float distanceCells) {
    // Direccion opuesta a donde mira EN ESTE MOMENTO (se dispara); se guarda
    // fija para toda la animacion, aunque el jugador gire despues.
    float dx = 0.0f, dy = 0.0f;
    DirectionVector(facing_, dx, dy);
    recoilDx_ = -dx;
    recoilDy_ = -dy;
    recoilRemaining_ = distanceCells;
}

void Tank::TickRecoil(double dt, const TileMap& map, const std::vector<Tank*>& others) {
    if (recoilRemaining_ <= 0.0f) {
        return;
    }
    const float step = std::min(recoilRemaining_, static_cast<float>(kRecoilSpeed * dt));
    if (TryMove(recoilDx_ * step, recoilDy_ * step, map, others)) {
        recoilRemaining_ -= step;
    } else {
        recoilRemaining_ = 0.0f; // choco contra algo (pared, bloque u otro tanque): se corta ahi
    }
}

bool Tank::IsPositionBlocked(float newX, float newY, const TileMap& map, const std::vector<Tank*>& others) const {
    const float left = newX;
    const float right = newX + kTankSize;
    const float top = newY;
    const float bottom = newY + kTankSize;

    if (left < 0.0f || top < 0.0f || right > static_cast<float>(map.Width()) || bottom > static_cast<float>(map.Height())) {
        return true;
    }

    if (map.IsBoxBlocked(left, right, top, bottom)) {
        return true;
    }

    // No se pueden atravesar entre tanques: si la posicion nueva se solapa
    // con alguno de los otros, queda bloqueado (sin empuje, ver feedback: traia problemas).
    for (const Tank* other : others) {
        if (other == nullptr) {
            continue;
        }
        float oLeft = 0.0f, oRight = 0.0f, oTop = 0.0f, oBottom = 0.0f;
        other->GetBounds(oLeft, oRight, oTop, oBottom);
        const bool overlaps = left < oRight && right > oLeft && top < oBottom && bottom > oTop;
        if (overlaps) {
            return true;
        }
    }

    return false;
}

bool Tank::TryMove(float dx, float dy, const TileMap& map, const std::vector<Tank*>& others) {
    const float newX = x_ + dx;
    const float newY = y_ + dy;

    if (IsPositionBlocked(newX, newY, map, others)) {
        return false;
    }

    x_ = newX;
    y_ = newY;
    return true;
}

bool Tank::TryMoveWithAssist(float dx, float dy, const TileMap& map, const std::vector<Tank*>& others) {
    if (TryMove(dx, dy, map, others)) {
        return true;
    }

    // El movimiento recto choca contra una esquina/borde de bloque. Se prueba
    // alinear del todo contra cada una de las dos filas/columnas que ocupa el
    // tanque; si alguna de las dos hace que el movimiento funcione, esa era
    // la que chocaba. Solo se acepta la correccion si lo que chocaba contra
    // el bloque era menos de la mitad del sprite (si es mas de la mitad, no
    // hay ajuste posible sin atravesar la pared: el jugador tiene que mover
    // el tanque el mismo). Ver TryMove: sigue siendo la unica fuente de
    // verdad sobre colision real, asi que esto nunca atraviesa una pared.
    if (dx != 0.0f) {
        return TrySlidePerpendicularY(dx, map, others);
    }
    if (dy != 0.0f) {
        return TrySlidePerpendicularX(dy, map, others);
    }
    return false;
}

bool Tank::TrySlidePerpendicularY(float dx, const TileMap& map, const std::vector<Tank*>& others) {
    const float rowTop = std::floor(y_);
    const float fracBottom = y_ - rowTop;    // fraccion del sprite metida en la fila de abajo
    const float fracTop = 1.0f - fracBottom; // fraccion metida en la fila de arriba

    // Probar alinear del todo a la fila de arriba: si funciona, la fila de
    // abajo era la que chocaba, y solo se acepta si esa porcion era minoria.
    if (fracBottom > 0.001f && fracBottom < 0.5f && TryMove(dx, -fracBottom, map, others)) {
        return true;
    }
    // Simetrico: alinear a la fila de abajo si la de arriba era la que chocaba.
    if (fracTop > 0.001f && fracTop < 0.5f && TryMove(dx, fracTop, map, others)) {
        return true;
    }
    // Huecos a caballo entre 2 celdas (p.ej. rebajados por igual de dos
    // bloques pegados, ver graze de costura): ni el alineado de arriba ni el
    // de abajo entran, pero centrado justo en el borde entre filas si puede
    // haber lugar. Se prueba al final, solo si las dos alineaciones enteras
    // ya fallaron.
    const float deltaToCenter = 0.5f - fracBottom;
    if (std::fabs(deltaToCenter) > 0.001f && TryMove(dx, deltaToCenter, map, others)) {
        return true;
    }
    return false;
}

bool Tank::TrySlidePerpendicularX(float dy, const TileMap& map, const std::vector<Tank*>& others) {
    const float colLeft = std::floor(x_);
    const float fracRight = x_ - colLeft;    // fraccion del sprite metida en la columna de la derecha
    const float fracLeft = 1.0f - fracRight; // fraccion metida en la columna de la izquierda

    if (fracRight > 0.001f && fracRight < 0.5f && TryMove(-fracRight, dy, map, others)) {
        return true;
    }
    if (fracLeft > 0.001f && fracLeft < 0.5f && TryMove(fracLeft, dy, map, others)) {
        return true;
    }
    // Simetrico al caso de TrySlidePerpendicularY: hueco a caballo entre 2
    // columnas, centrado en el borde entre ellas.
    const float deltaToCenter = 0.5f - fracRight;
    if (std::fabs(deltaToCenter) > 0.001f && TryMove(deltaToCenter, dy, map, others)) {
        return true;
    }
    return false;
}

bool Tank::IsOnIce(const TileMap& map) const {
    const int cellX = static_cast<int>(std::floor(x_ + 0.5f));
    const int cellY = static_cast<int>(std::floor(y_ + 0.5f));
    return map.InBounds(cellX, cellY) && map.At(cellX, cellY).type == TileType::Ice;
}

void Tank::Update(double dt, const PlayerInput& input, const TileMap& map, const std::vector<Tank*>& others) {
    onIce_ = IsOnIce(map);

    if (freezeTimer_ > 0.0) {
        animTimer_ = 0.0; // paralizado: no se mueve, no anima, mantiene la ultima direccion
        return;
    }

    const float distance = static_cast<float>(kMoveSpeedByLevel[weaponLevel_ - 1] * dt) * speedMultiplier_;
    float moveDx = 0.0f, moveDy = 0.0f;
    bool hasInput = true;

    // Sin diagonales (seccion 4.3): el tanque siempre rota hacia la ultima
    // direccion presionada, incluso si el movimiento queda bloqueado. Girar
    // y arrancar a moverse es instantaneo incluso sobre hielo; lo unico que
    // patina es seguir deslizando un instante al soltar el movimiento (ver
    // mas abajo).
    if (input.moveUp) {
        facing_ = Direction::Up;
        moveDy = -distance;
    } else if (input.moveDown) {
        facing_ = Direction::Down;
        moveDy = distance;
    } else if (input.moveLeft) {
        facing_ = Direction::Left;
        moveDx = -distance;
    } else if (input.moveRight) {
        facing_ = Direction::Right;
        moveDx = distance;
    } else {
        hasInput = false;
    }

    if (hasInput) {
        iceCoastDx_ = moveDx;
        iceCoastDy_ = moveDy;
    } else if (onIce_ && (iceCoastDx_ != 0.0f || iceCoastDy_ != 0.0f)) {
        // No se aprieta nada, pero sigue deslizando por inercia sobre el
        // hielo: un solo eje a la vez (el mismo que traia), nunca diagonal.
        iceCoastDx_ *= kIceCoastDecay;
        iceCoastDy_ *= kIceCoastDecay;
        if (std::fabs(iceCoastDx_) < kIceCoastStopEpsilon) iceCoastDx_ = 0.0f;
        if (std::fabs(iceCoastDy_) < kIceCoastStopEpsilon) iceCoastDy_ = 0.0f;
        moveDx = iceCoastDx_;
        moveDy = iceCoastDy_;
    } else {
        iceCoastDx_ = 0.0f;
        iceCoastDy_ = 0.0f;
    }

    bool moved = false;
    if (moveDx != 0.0f || moveDy != 0.0f) {
        moved = TryMoveWithAssist(moveDx, moveDy, map, others);
        if (!moved) {
            // Choco patinando (o caminando): se corta el resto del impulso.
            iceCoastDx_ = 0.0f;
            iceCoastDy_ = 0.0f;
        }
    }

    if (moved) {
        animTimer_ += dt;
        if (animTimer_ >= kAnimFrameDuration) {
            animTimer_ -= kAnimFrameDuration;
            animFrame_ = 1 - animFrame_;
        }
    } else {
        animTimer_ = 0.0;
    }
}

} // namespace bc
