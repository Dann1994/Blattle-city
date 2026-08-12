#pragma once

#include "TileMap.h"

namespace bc {

enum class Direction { Up, Down, Left, Right };

// Ver seccion 12.4: capa de input unica, sin distinguir origen local/remoto.
struct PlayerInput {
    bool moveUp = false;
    bool moveDown = false;
    bool moveLeft = false;
    bool moveRight = false;
    bool shoot = false;
};

class Tank {
public:
    void SetPosition(float cellX, float cellY) { x_ = cellX; y_ = cellY; }
    void Update(double dt, const PlayerInput& input, const TileMap& map);

    float X() const { return x_; }
    float Y() const { return y_; }
    Direction Facing() const { return facing_; }
    int AnimFrame() const { return animFrame_; }

private:
    bool TryMove(float dx, float dy, const TileMap& map);

    float x_ = 0.0f;
    float y_ = 0.0f;
    Direction facing_ = Direction::Up;
    float speed_ = 3.5f; // celdas por segundo
    double animTimer_ = 0.0;
    int animFrame_ = 0;
};

} // namespace bc
