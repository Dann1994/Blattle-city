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

    const int minCellX = static_cast<int>(std::floor(left));
    const int maxCellX = static_cast<int>(std::ceil(right)) - 1;
    const int minCellY = static_cast<int>(std::floor(top));
    const int maxCellY = static_cast<int>(std::ceil(bottom)) - 1;

    for (int cy = minCellY; cy <= maxCellY; ++cy) {
        for (int cx = minCellX; cx <= maxCellX; ++cx) {
            if (!map.InBounds(cx, cy) || TileBlocksMovement(map.At(cx, cy).type)) {
                return false;
            }
        }
    }

    x_ = newX;
    y_ = newY;
    return true;
}

bool Tank::TryMoveWithAssist(float dx, float dy, double dt, const TileMap& map) {
    if (TryMove(dx, dy, map)) {
        return true;
    }

    // El movimiento recto choca. Si el tanque tiene menos de la mitad de sus
    // pixeles metidos en la fila/columna que esta chocando, se lo desliza de a
    // poco hacia el lado libre (la fila/columna donde esta la mayoria de su
    // cuerpo) y se reintenta. TryMove sigue validando la colision real del
    // destino, asi que esto nunca atraviesa una pared genuina.
    const float assist = static_cast<float>(speed_ * dt);

    if (dx != 0.0f) {
        return TrySlidePerpendicularY(dx, assist, map);
    }
    if (dy != 0.0f) {
        return TrySlidePerpendicularX(dy, assist, map);
    }
    return false;
}

bool Tank::TrySlidePerpendicularY(float dx, float assist, const TileMap& map) {
    const float rowTop = std::floor(y_);
    const float fracBottom = y_ - rowTop; // fraccion del sprite metida en la fila de abajo

    if (fracBottom > 0.001f && fracBottom < 0.5f) {
        // Menos de la mitad choca contra la fila de abajo: deslizar hacia arriba.
        if (TryMove(dx, std::max(-fracBottom, -assist), map)) {
            return true;
        }
    }

    const float fracTop = 1.0f - fracBottom; // fraccion metida en la fila de arriba
    if (fracTop > 0.001f && fracTop < 0.5f) {
        // Menos de la mitad choca contra la fila de arriba: deslizar hacia abajo.
        if (TryMove(dx, std::min(fracTop, assist), map)) {
            return true;
        }
    }

    return false;
}

bool Tank::TrySlidePerpendicularX(float dy, float assist, const TileMap& map) {
    const float colLeft = std::floor(x_);
    const float fracRight = x_ - colLeft; // fraccion del sprite metida en la columna de la derecha

    if (fracRight > 0.001f && fracRight < 0.5f) {
        if (TryMove(std::max(-fracRight, -assist), dy, map)) {
            return true;
        }
    }

    const float fracLeft = 1.0f - fracRight; // fraccion metida en la columna de la izquierda
    if (fracLeft > 0.001f && fracLeft < 0.5f) {
        if (TryMove(std::min(fracLeft, assist), dy, map)) {
            return true;
        }
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
        moved = TryMoveWithAssist(0.0f, -distance, dt, map);
    } else if (input.moveDown) {
        facing_ = Direction::Down;
        moved = TryMoveWithAssist(0.0f, distance, dt, map);
    } else if (input.moveLeft) {
        facing_ = Direction::Left;
        moved = TryMoveWithAssist(-distance, 0.0f, dt, map);
    } else if (input.moveRight) {
        facing_ = Direction::Right;
        moved = TryMoveWithAssist(distance, 0.0f, dt, map);
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
