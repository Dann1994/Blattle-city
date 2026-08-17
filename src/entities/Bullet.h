#pragma once

#include "Tank.h"

namespace bc {

// Los jugadores usan ownerId 0-3 (ver Game::kPlayer1Id..kPlayer4Id); los
// tanques enemigo arrancan aca en adelante (uno distinto por tanque), para
// poder distinguir el origen de una bala/impacto de un vistazo sin
// necesitar una lista aparte de "que IDs son enemigos".
constexpr int kEnemyOwnerIdBase = 1000;

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
