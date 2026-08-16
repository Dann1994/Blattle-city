#pragma once

#include "Tank.h"

namespace bc {

struct Bullet {
    float x = 0.0f; // celdas, centro de la bala
    float y = 0.0f;
    Direction direction = Direction::Up;
    int ownerId = -1;
    bool alive = false;
    float speed = 8.0f; // celdas por segundo, depende del nivel de arma de quien dispara
    int weaponLevel = 1; // nivel de quien dispara (1-4): decide dano a hierro, y en nivel 4 explosion grande + doble capa de ladrillo
    bool isSpecial = false; // disparo especial: veloz, explota en radio y destruye todo lo que toca
};

} // namespace bc
