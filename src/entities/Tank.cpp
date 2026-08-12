#include "Tank.h"

#include <cmath>

namespace bc {

namespace {
constexpr double kAnimFrameDuration = 0.1; // segundos por frame de animacion
constexpr float kTankSize = 1.0f;          // el tanque ocupa 1 celda, como en el original
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

void Tank::Update(double dt, const PlayerInput& input, const TileMap& map) {
    const float distance = static_cast<float>(speed_ * dt);
    bool moved = false;

    // Sin diagonales (seccion 4.3): el tanque siempre rota hacia la ultima
    // direccion presionada, incluso si el movimiento queda bloqueado.
    if (input.moveUp) {
        facing_ = Direction::Up;
        moved = TryMove(0.0f, -distance, map);
    } else if (input.moveDown) {
        facing_ = Direction::Down;
        moved = TryMove(0.0f, distance, map);
    } else if (input.moveLeft) {
        facing_ = Direction::Left;
        moved = TryMove(-distance, 0.0f, map);
    } else if (input.moveRight) {
        facing_ = Direction::Right;
        moved = TryMove(distance, 0.0f, map);
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
