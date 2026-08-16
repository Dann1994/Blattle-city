#pragma once

namespace bc {

// Por ahora solo Estrella (ver seccion 6); el resto de los power-ups llega
// mas adelante junto con lo que los necesita (enemigos, vidas, base).
struct PowerUp {
    float x = 0.0f; // celda, esquina superior-izquierda (ocupa 1 celda)
    float y = 0.0f;
    bool alive = false;
};

} // namespace bc
