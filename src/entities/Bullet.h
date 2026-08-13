#pragma once

#include "Tank.h"

namespace bc {

struct Bullet {
    float x = 0.0f; // celdas, centro de la bala
    float y = 0.0f;
    Direction direction = Direction::Up;
    int ownerId = -1;
    bool alive = false;
};

} // namespace bc
