#pragma once

#include "Tank.h"

namespace bc {

struct Bullet {
    float x = 0.0f; // celdas, centro de la bala
    float y = 0.0f;
    Direction direction = Direction::Up;
    int ownerId = -1;
    bool alive = false;
    float speed = 8.0f;           // celdas por segundo, depende del nivel de arma de quien dispara
    bool canDestroySteel = false; // nivel 3+ (seccion 4.3)
};

} // namespace bc
