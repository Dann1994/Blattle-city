#pragma once

namespace bc {

// Estrella, Casco, Pistola y Vida por ahora (ver seccion 6); el resto llega
// mas adelante junto con lo que los necesita (enemigos, base).
enum class PowerUpType { Star, Helmet, Gun, Life };

struct PowerUp {
    float x = 0.0f; // celda, esquina superior-izquierda (ocupa 1 celda)
    float y = 0.0f;
    PowerUpType type = PowerUpType::Star;
    bool alive = false;
};

} // namespace bc
