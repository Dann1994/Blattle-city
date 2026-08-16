#include "Tank.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
constexpr double kAnimFrameDuration = 0.1; // segundos por frame de animacion
constexpr float kTankSize = 1.0f;          // el tanque ocupa 1 celda, como en el original

// Indexado por nivel-1 (nivel 1..4). Ver seccion 4.3.
constexpr float kBulletSpeedByLevel[4] = {8.0f, 10.0f, 10.0f, 13.0f};
constexpr int kMaxBulletsByLevel[4] = {1, 1, 2, 2};
constexpr int kMaxWeaponLevel = 4;
}

void Tank::UpgradeWeapon() {
    if (weaponLevel_ < kMaxWeaponLevel) {
        ++weaponLevel_;
    }
}

float Tank::BulletSpeed() const {
    return kBulletSpeedByLevel[weaponLevel_ - 1];
}

int Tank::MaxBullets() const {
    return kMaxBulletsByLevel[weaponLevel_ - 1];
}

bool Tank::CanDestroySteel() const {
    return weaponLevel_ >= 3;
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

void Tank::MuzzlePosition(float& outX, float& outY) const {
    float dx = 0.0f, dy = 0.0f;
    DirectionVector(facing_, dx, dy);
    outX = x_ + 0.5f + dx * 0.5f;
    outY = y_ + 0.5f + dy * 0.5f;
}

bool Tank::TryMove(float dx, float dy, const TileMap& map) {
    const float newX = x_ + dx;
    const float newY = y_ + dy;

    const float left = newX;
    const float right = newX + kTankSize;
    const float top = newY;
    const float bottom = newY + kTankSize;

    if (left < 0.0f || top < 0.0f || right > static_cast<float>(map.Width()) || bottom > static_cast<float>(map.Height())) {
        return false;
    }

    if (map.IsBoxBlocked(left, right, top, bottom)) {
        return false;
    }

    x_ = newX;
    y_ = newY;
    return true;
}

bool Tank::TryMoveWithAssist(float dx, float dy, const TileMap& map) {
    if (TryMove(dx, dy, map)) {
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
        return TrySlidePerpendicularY(dx, map);
    }
    if (dy != 0.0f) {
        return TrySlidePerpendicularX(dy, map);
    }
    return false;
}

bool Tank::TrySlidePerpendicularY(float dx, const TileMap& map) {
    const float rowTop = std::floor(y_);
    const float fracBottom = y_ - rowTop;    // fraccion del sprite metida en la fila de abajo
    const float fracTop = 1.0f - fracBottom; // fraccion metida en la fila de arriba

    // Probar alinear del todo a la fila de arriba: si funciona, la fila de
    // abajo era la que chocaba, y solo se acepta si esa porcion era minoria.
    if (fracBottom > 0.001f && fracBottom < 0.5f && TryMove(dx, -fracBottom, map)) {
        return true;
    }
    // Simetrico: alinear a la fila de abajo si la de arriba era la que chocaba.
    if (fracTop > 0.001f && fracTop < 0.5f && TryMove(dx, fracTop, map)) {
        return true;
    }
    return false;
}

bool Tank::TrySlidePerpendicularX(float dy, const TileMap& map) {
    const float colLeft = std::floor(x_);
    const float fracRight = x_ - colLeft;    // fraccion del sprite metida en la columna de la derecha
    const float fracLeft = 1.0f - fracRight; // fraccion metida en la columna de la izquierda

    if (fracRight > 0.001f && fracRight < 0.5f && TryMove(-fracRight, dy, map)) {
        return true;
    }
    if (fracLeft > 0.001f && fracLeft < 0.5f && TryMove(fracLeft, dy, map)) {
        return true;
    }
    return false;
}

void Tank::Update(double dt, const PlayerInput& input, const TileMap& map) {
    const float distance = static_cast<float>(speed_ * dt);
    bool moved = false;

    // Sin diagonales (seccion 4.3): el tanque siempre rota hacia la ultima
    // direccion presionada, incluso si el movimiento queda bloqueado.
    if (input.moveUp) {
        facing_ = Direction::Up;
        moved = TryMoveWithAssist(0.0f, -distance, map);
    } else if (input.moveDown) {
        facing_ = Direction::Down;
        moved = TryMoveWithAssist(0.0f, distance, map);
    } else if (input.moveLeft) {
        facing_ = Direction::Left;
        moved = TryMoveWithAssist(-distance, 0.0f, map);
    } else if (input.moveRight) {
        facing_ = Direction::Right;
        moved = TryMoveWithAssist(distance, 0.0f, map);
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
