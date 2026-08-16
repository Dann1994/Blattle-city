#pragma once

namespace bc {

// Destello de impacto: 3 frames crecientes, una sola vez (sin rebote), en el
// punto donde una bala choca contra algo (terreno u otra bala).
struct BulletImpact {
    float x = 0.0f;
    float y = 0.0f;
    double frameTimer = 0.0;
    int frameIndex = 0;
    bool alive = false;
};

} // namespace bc
