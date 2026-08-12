#include "Tank.h"

#include <algorithm>
#include <cmath>

namespace bc {

namespace {
constexpr double kAnimFrameDuration = 0.1; // segundos por frame de animacion
constexpr float kTankSize = 1.0f;          // el tanque ocupa 1 celda, como en el original
constexpr float kAlignAssistMaxOffset = 0.25f; // celdas (~4px con tiles de 16px)
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

    // Si el movimiento recto choca pero el tanque esta desalineado por pocos
    // pixeles respecto de la celda (por ejemplo entrando a un hueco justo),
    // se lo empuja de a poco hacia la celda mas cercana en el eje perpendicular
    // y se reintenta. TryMove sigue validando colision real, asi que esto no
    // atraviesa paredes: solo ayuda cuando el hueco realmente existe.
    const float assistDistance = static_cast<float>(speed_ * dt);

    if (dx != 0.0f) {
        const float offset = std::round(y_) - y_;
        if (std::fabs(offset) > 0.001f && std::fabs(offset) <= kAlignAssistMaxOffset) {
            const float nudgeY = std::clamp(offset, -assistDistance, assistDistance);
            if (TryMove(dx, nudgeY, map)) {
                return true;
            }
        }
    } else if (dy != 0.0f) {
        const float offset = std::round(x_) - x_;
        if (std::fabs(offset) > 0.001f && std::fabs(offset) <= kAlignAssistMaxOffset) {
            const float nudgeX = std::clamp(offset, -assistDistance, assistDistance);
            if (TryMove(nudgeX, dy, map)) {
                return true;
            }
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
