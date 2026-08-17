#pragma once

namespace bc {

// Ver seccion 6. Granada y Reloj estan pensados para pegarle a los enemigos
// (Fase 3, todavia no existen): mientras tanto se pueden agarrar pero no
// hacen nada. Pala si tiene efecto ya (fortifica la base con hierro
// temporal, ver Game::ApplyShovelFortification).
enum class PowerUpType { Star, Helmet, Gun, Life, Grenade, Shovel, Clock };

struct PowerUp {
    float x = 0.0f; // celda, esquina superior-izquierda (ocupa 1 celda)
    float y = 0.0f;
    PowerUpType type = PowerUpType::Star;
    bool alive = false;
};

} // namespace bc
