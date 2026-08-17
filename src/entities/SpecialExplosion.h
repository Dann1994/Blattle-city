#pragma once

namespace bc {

// Explosion del disparo especial (nivel 4 + carga de estrella extra): 5
// frames crecientes, mas grande y mas lenta que el destello de impacto
// normal (ver BulletImpact.h). Ver Documentaciones/Exploción.png.
struct SpecialExplosion {
    float x = 0.0f;
    float y = 0.0f;
    double frameTimer = 0.0;
    int frameIndex = 0;
    bool alive = false;
    // false (disparo especial): tamano fijo, segun el radio de la explosion.
    // true (tanque destruido): cada frame se dibuja a su tamano nativo en
    // pixeles, escalado igual que los sprites de los tanques (mas chica).
    bool nativeScale = false;
};

} // namespace bc
