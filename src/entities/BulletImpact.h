#pragma once

namespace bc {

// Destello de impacto: frames crecientes, una sola vez (sin rebote), en el
// punto donde una bala choca contra algo (terreno u otra bala). 2 frames
// normalmente, 3 (mas grande) para las balas de nivel de arma 4.
struct BulletImpact {
    float x = 0.0f;
    float y = 0.0f;
    double frameTimer = 0.0;
    int frameIndex = 0;
    int frameCount = 2;
    bool alive = false;
};

} // namespace bc
